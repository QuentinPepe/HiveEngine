#pragma once

#include <hive/core/assert.h>

#include <comb/allocator_concepts.h>

#include <wax/containers/vector.h>

#include <queen/core/component_info.h>
#include <queen/core/entity.h>
#include <queen/core/type_id.h>

namespace queen
{
    class World;

    enum class CommandType : uint8_t
    {
        SPAWN,            // Create a new entity
        DESPAWN,          // Destroy an entity
        ADD_COMPONENT,    // Add a component to an entity
        REMOVE_COMPONENT, // Remove a component from an entity
        SET_COMPONENT,    // Set/update a component on an entity
    };

    // Single-threaded buffer of deferred structural commands. Component payloads live in a
    // linked list of fixed-size blocks so individual Add/Set arguments don't fragment the
    // allocator. Spawned entities use placeholder values until Flush() resolves them.
    //
    // Block storage:
    //   Block 0 (4KB) -> Block 1 (4KB) -> ... -> Block N (4KB)
    //   each block packs component data sequentially with per-type alignment.
    template <comb::Allocator Allocator> class CommandBuffer;

    template <comb::Allocator Allocator> class SpawnCommandBuilder;

    namespace detail
    {
        static constexpr size_t kCommandBlockSize = 4096;

        struct CommandDataBlock
        {
            alignas(alignof(std::max_align_t)) uint8_t m_data[kCommandBlockSize];
            size_t m_used = 0;
            CommandDataBlock* m_next = nullptr;
        };

        struct Command
        {
            CommandType m_type;
            Entity m_entity;        // Target entity or pending entity index
            TypeId m_componentType; // Component type for add/remove/set
            void* m_data;           // Component data (for add/set)
            size_t m_dataSize;      // Size of component data
            ComponentMeta m_meta;   // Lifecycle info for component
        };
    } // namespace detail

    // Fluent helper paired with CommandBuffer::Spawn(); accumulates components against the
    // spawn placeholder so the eventual Flush() can build the entity in a single archetype move.
    template <comb::Allocator Allocator> class SpawnCommandBuilder
    {
    public:
        SpawnCommandBuilder(CommandBuffer<Allocator>& buffer, uint32_t spawnIndex)
            : m_buffer{&buffer}
            , m_spawnIndex{spawnIndex}
        {
        }

        template <typename T> SpawnCommandBuilder& With(T&& component);

        uint32_t GetSpawnIndex() const noexcept
        {
            return m_spawnIndex;
        }

    private:
        CommandBuffer<Allocator>* m_buffer;
        uint32_t m_spawnIndex;
    };

    template <comb::Allocator Allocator> class CommandBuffer
    {
    public:
        explicit CommandBuffer(Allocator& allocator)
            : m_allocator{&allocator}
            , m_commands{allocator}
            , m_spawnedEntities{allocator}
            , m_headBlock{nullptr}
            , m_currentBlock{nullptr}
            , m_spawnCount{0}
        {
        }

        ~CommandBuffer()
        {
            ClearBlocks();
        }

        CommandBuffer(const CommandBuffer&) = delete;
        CommandBuffer& operator=(const CommandBuffer&) = delete;

        CommandBuffer(CommandBuffer&& other) noexcept
            : m_allocator{other.m_allocator}
            , m_commands{static_cast<wax::Vector<detail::Command>&&>(other.m_commands)}
            , m_spawnedEntities{static_cast<wax::Vector<Entity>&&>(other.m_spawnedEntities)}
            , m_headBlock{other.m_headBlock}
            , m_currentBlock{other.m_currentBlock}
            , m_spawnCount{other.m_spawnCount}
        {
            other.m_headBlock = nullptr;
            other.m_currentBlock = nullptr;
            other.m_spawnCount = 0;
        }

        CommandBuffer& operator=(CommandBuffer&& other) noexcept
        {
            if (this != &other)
            {
                ClearBlocks();
                m_allocator = other.m_allocator;
                m_commands = static_cast<wax::Vector<detail::Command>&&>(other.m_commands);
                m_spawnedEntities = static_cast<wax::Vector<Entity>&&>(other.m_spawnedEntities);
                m_headBlock = other.m_headBlock;
                m_currentBlock = other.m_currentBlock;
                m_spawnCount = other.m_spawnCount;
                other.m_headBlock = nullptr;
                other.m_currentBlock = nullptr;
                other.m_spawnCount = 0;
            }
            return *this;
        }

        // Queues a spawn and returns a builder whose placeholder entity is resolved at Flush time.
        // The placeholder carries kPendingDelete so any code path that leaks it before flush is
        // detectable instead of silently aliasing a real entity slot.
        [[nodiscard]] SpawnCommandBuilder<Allocator> Spawn()
        {
            uint32_t spawnIndex = m_spawnCount++;

            detail::Command cmd{};
            cmd.m_type = CommandType::SPAWN;
            cmd.m_entity = Entity{spawnIndex, 0, Entity::Flags::kPendingDelete};
            cmd.m_componentType = 0;
            cmd.m_data = nullptr;
            cmd.m_dataSize = 0;

            m_commands.PushBack(cmd);

            return SpawnCommandBuilder<Allocator>{*this, spawnIndex};
        }

        void Despawn(Entity entity)
        {
            detail::Command cmd{};
            cmd.m_type = CommandType::DESPAWN;
            cmd.m_entity = entity;
            cmd.m_componentType = 0;
            cmd.m_data = nullptr;
            cmd.m_dataSize = 0;

            m_commands.PushBack(cmd);
        }

        // Queues an Add; collapses to Set semantics at flush time if the entity already has T.
        template <typename T> void Add(Entity entity, T&& component)
        {
            using DecayedT = std::decay_t<T>;

            void* data = AllocateData(sizeof(DecayedT), alignof(DecayedT));
            new (data) DecayedT{std::forward<T>(component)};

            detail::Command cmd{};
            cmd.m_type = CommandType::ADD_COMPONENT;
            cmd.m_entity = entity;
            cmd.m_componentType = TypeIdOf<DecayedT>();
            cmd.m_data = data;
            cmd.m_dataSize = sizeof(DecayedT);
            cmd.m_meta = ComponentMeta::Of<DecayedT>();

            m_commands.PushBack(cmd);
        }

        template <typename T> void Remove(Entity entity)
        {
            detail::Command cmd{};
            cmd.m_type = CommandType::REMOVE_COMPONENT;
            cmd.m_entity = entity;
            cmd.m_componentType = TypeIdOf<T>();
            cmd.m_data = nullptr;
            cmd.m_dataSize = 0;

            m_commands.PushBack(cmd);
        }

        // Upsert variant: replaces T if present, otherwise adds it.
        template <typename T> void Set(Entity entity, T&& component)
        {
            using DecayedT = std::decay_t<T>;

            void* data = AllocateData(sizeof(DecayedT), alignof(DecayedT));
            new (data) DecayedT{std::forward<T>(component)};

            detail::Command cmd{};
            cmd.m_type = CommandType::SET_COMPONENT;
            cmd.m_entity = entity;
            cmd.m_componentType = TypeIdOf<DecayedT>();
            cmd.m_data = data;
            cmd.m_dataSize = sizeof(DecayedT);
            cmd.m_meta = ComponentMeta::Of<DecayedT>();

            m_commands.PushBack(cmd);
        }

        // Drains the buffer into the World in insertion order, then resets it for reuse.
        void Flush(World& world);

        // Drops queued commands without touching the World; still runs component destructors
        // for any payload that was already constructed in a block.
        void Clear()
        {
            for (size_t i = 0; i < m_commands.Size(); ++i)
            {
                const detail::Command& cmd = m_commands[i];
                if (cmd.m_data != nullptr && cmd.m_meta.m_destruct != nullptr)
                {
                    cmd.m_meta.m_destruct(cmd.m_data);
                }
            }

            m_commands.Clear();
            m_spawnedEntities.Clear();
            m_spawnCount = 0;

            ClearBlocks();
        }

        [[nodiscard]] size_t CommandCount() const noexcept
        {
            return m_commands.Size();
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_commands.IsEmpty();
        }

        // Resolves a Spawn placeholder to its real Entity after Flush(); returns Invalid()
        // before flush or for out-of-range indices.
        [[nodiscard]] Entity GetSpawnedEntity(uint32_t spawnIndex) const noexcept
        {
            if (spawnIndex < m_spawnedEntities.Size())
            {
                return m_spawnedEntities[spawnIndex];
            }
            return Entity::Invalid();
        }

    private:
        friend class SpawnCommandBuilder<Allocator>;

        void AddComponentToSpawn(uint32_t spawnIndex, const ComponentMeta& meta, void* data)
        {
            Entity pending{spawnIndex, 0, Entity::Flags::kPendingDelete};

            detail::Command cmd{};
            cmd.m_type = CommandType::ADD_COMPONENT;
            cmd.m_entity = pending;
            cmd.m_componentType = meta.m_typeId;
            cmd.m_data = data;
            cmd.m_dataSize = meta.m_size;
            cmd.m_meta = meta;

            m_commands.PushBack(cmd);
        }

        void* AllocateData(size_t size, size_t alignment)
        {
            if (m_currentBlock == nullptr)
            {
                AllocateNewBlock();
            }

            size_t alignedOffset = (m_currentBlock->m_used + alignment - 1) & ~(alignment - 1);

            if (alignedOffset + size > detail::kCommandBlockSize)
            {
                AllocateNewBlock();
                alignedOffset = 0;
            }

            void* ptr = m_currentBlock->m_data + alignedOffset;
            m_currentBlock->m_used = alignedOffset + size;
            return ptr;
        }

        void AllocateNewBlock()
        {
            void* memory = m_allocator->Allocate(sizeof(detail::CommandDataBlock), alignof(detail::CommandDataBlock));
            hive::Assert(memory != nullptr, "Failed to allocate command data block");

            auto* block = new (memory) detail::CommandDataBlock{};

            if (m_currentBlock != nullptr)
            {
                m_currentBlock->m_next = block;
            }
            else
            {
                m_headBlock = block;
            }

            m_currentBlock = block;
        }

        void ClearBlocks()
        {
            detail::CommandDataBlock* block = m_headBlock;
            while (block != nullptr)
            {
                detail::CommandDataBlock* next = block->m_next;
                m_allocator->Deallocate(block);
                block = next;
            }

            m_headBlock = nullptr;
            m_currentBlock = nullptr;
        }

        bool IsPendingEntity(Entity entity) const noexcept
        {
            return entity.HasFlag(Entity::Flags::kPendingDelete);
        }

        Entity ResolveEntity(Entity entity) const noexcept
        {
            if (IsPendingEntity(entity))
            {
                uint32_t spawnIndex = entity.Index();
                if (spawnIndex < m_spawnedEntities.Size())
                {
                    return m_spawnedEntities[spawnIndex];
                }
                return Entity::Invalid();
            }
            return entity;
        }

        Allocator* m_allocator;
        wax::Vector<detail::Command> m_commands;
        wax::Vector<Entity> m_spawnedEntities;
        detail::CommandDataBlock* m_headBlock;
        detail::CommandDataBlock* m_currentBlock;
        uint32_t m_spawnCount;
    };

    template <comb::Allocator Allocator>
    template <typename T>
    SpawnCommandBuilder<Allocator>& SpawnCommandBuilder<Allocator>::With(T&& component)
    {
        using DecayedT = std::decay_t<T>;

        void* data = m_buffer->AllocateData(sizeof(DecayedT), alignof(DecayedT));
        new (data) DecayedT{std::forward<T>(component)};

        m_buffer->AddComponentToSpawn(m_spawnIndex, ComponentMeta::Of<DecayedT>(), data);

        return *this;
    }
} // namespace queen

// Note: CommandBuffer::Flush implementation is in world.h to avoid circular dependency
