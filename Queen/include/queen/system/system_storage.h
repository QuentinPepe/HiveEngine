#pragma once

#include <comb/allocator_concepts.h>

#include <wax/containers/vector.h>

#include <queen/system/system.h>
#include <queen/system/system_builder.h>

namespace queen
{
    class World;

    // Owns the registered system descriptors. Indices double as SystemId, so
    // removal is logical (executor cleared) rather than physical to keep ids stable.
    template <comb::Allocator Allocator> class SystemStorage
    {
    public:
        explicit SystemStorage(Allocator& allocator)
            : m_allocator{&allocator}
            , m_systems{allocator}
        {
        }

        ~SystemStorage() = default;

        SystemStorage(const SystemStorage&) = delete;
        SystemStorage& operator=(const SystemStorage&) = delete;
        SystemStorage(SystemStorage&&) = default;
        SystemStorage& operator=(SystemStorage&&) = default;

        template <typename... Terms> SystemBuilder<Allocator, Terms...> Register(World& world, const char* name)
        {
            SystemId id{static_cast<uint32_t>(m_systems.Size())};

            m_systems.EmplaceBack(*m_allocator, id, name);

            return SystemBuilder<Allocator, Terms...>{world, *m_allocator, *this, &m_systems[m_systems.Size() - 1]};
        }

        // Direct-callback overload used by tests and trivial systems. Caller supplies
        // the access descriptor explicitly since there is no query to infer it from.
        template <typename F> SystemId Register(const char* name, F&& func, AccessDescriptor<Allocator>&& access)
        {
            SystemId id{static_cast<uint32_t>(m_systems.Size())};

            m_systems.EmplaceBack(*m_allocator, id, name);

            auto& desc = m_systems[m_systems.Size() - 1];
            desc.Access() = std::move(access);

            using FuncType = std::decay_t<F>;
            void* userData = m_allocator->Allocate(sizeof(FuncType), alignof(FuncType));
            new (userData) FuncType{std::forward<F>(func)};

            desc.SetExecutor(
                [](World& world, void* data) {
                    auto* fn = static_cast<FuncType*>(data);
                    (*fn)(world);
                },
                userData, [](void* data) { static_cast<FuncType*>(data)->~FuncType(); });

            return id;
        }

        [[nodiscard]] SystemDescriptor<Allocator>* GetSystem(SystemId id)
        {
            if (!id.IsValid() || id.Index() >= m_systems.Size())
            {
                return nullptr;
            }
            return &m_systems[id.Index()];
        }

        [[nodiscard]] const SystemDescriptor<Allocator>* GetSystem(SystemId id) const
        {
            if (!id.IsValid() || id.Index() >= m_systems.Size())
            {
                return nullptr;
            }
            return &m_systems[id.Index()];
        }

        [[nodiscard]] SystemDescriptor<Allocator>* GetSystemByIndex(size_t index)
        {
            if (index >= m_systems.Size())
            {
                return nullptr;
            }
            return &m_systems[index];
        }

        [[nodiscard]] const SystemDescriptor<Allocator>* GetSystemByIndex(size_t index) const
        {
            if (index >= m_systems.Size())
            {
                return nullptr;
            }
            return &m_systems[index];
        }

        [[nodiscard]] SystemDescriptor<Allocator>* GetSystemByName(const char* name)
        {
            for (size_t i = 0; i < m_systems.Size(); ++i)
            {
                if (std::strcmp(m_systems[i].Name(), name) == 0)
                {
                    return &m_systems[i];
                }
            }
            return nullptr;
        }

        void RunSystem(World& world, SystemId id, Tick currentTick)
        {
            SystemDescriptor<Allocator>* system = GetSystem(id);
            if (system != nullptr)
            {
                system->Execute(world, currentTick);
            }
        }

        void RunAll(World& world, Tick currentTick)
        {
            for (size_t i = 0; i < m_systems.Size(); ++i)
            {
                m_systems[i].Execute(world, currentTick);
            }
        }

        [[nodiscard]] size_t SystemCount() const noexcept
        {
            return m_systems.Size();
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_systems.IsEmpty();
        }

        void SetSystemEnabled(SystemId id, bool enabled)
        {
            SystemDescriptor<Allocator>* system = GetSystem(id);
            if (system != nullptr)
            {
                system->SetEnabled(enabled);
            }
        }

        [[nodiscard]] bool IsSystemEnabled(SystemId id) const
        {
            const SystemDescriptor<Allocator>* system = GetSystem(id);
            if (system != nullptr)
            {
                return system->IsEnabled();
            }
            return false;
        }

        void RemoveSystem(SystemId id)
        {
            SystemDescriptor<Allocator>* system = GetSystem(id);
            if (system != nullptr)
            {
                system->SetExecutor(nullptr, nullptr, nullptr);
                system->SetEnabled(false);
            }
        }

        bool RemoveSystemByName(const char* name)
        {
            for (size_t i = 0; i < m_systems.Size(); ++i)
            {
                if (std::strcmp(m_systems[i].Name(), name) == 0)
                {
                    m_systems[i].SetExecutor(nullptr, nullptr, nullptr);
                    m_systems[i].SetEnabled(false);
                    return true;
                }
            }
            return false;
        }

        void Clear()
        {
            m_systems.Clear();
        }

    private:
        Allocator* m_allocator;
        wax::Vector<SystemDescriptor<Allocator>> m_systems;
    };
} // namespace queen
