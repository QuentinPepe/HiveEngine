#pragma once

#include <hive/core/assert.h>

#include <comb/allocator_concepts.h>

#include <queen/core/component_info.h>
#include <queen/core/tick.h>
#include <queen/core/type_id.h>

#include <cstddef>
#include <cstring>

namespace queen
{
    // Type-erased contiguous array of one component type, paired with a parallel
    // ComponentTicks array for change detection. Lifecycle ops route through ComponentMeta.
    // Memory layout:
    // ┌────────────────────────────────────────────────────────────┐
    // │ data_:  aligned bytes at stride meta.m_size                │
    // │   [Component0, Component1, Component2, ...]                │
    // │                                                            │
    // │ ticks_: parallel ComponentTicks array                      │
    // │   [Ticks0, Ticks1, Ticks2, ...]                            │
    // └────────────────────────────────────────────────────────────┘
    template <comb::Allocator Allocator> class Column
    {
    public:
        Column(Allocator& allocator, ComponentMeta meta, size_t initialCapacity = 64)
            : m_allocator{&allocator}
            , m_meta{meta}
            , m_data{nullptr}
            , m_ticks{nullptr}
            , m_size{0}
            , m_capacity{0}
        {
            hive::Assert(meta.IsValid(), "Column requires valid ComponentMeta");
            Reserve(initialCapacity);
        }

        ~Column()
        {
            Clear();
            if (m_data != nullptr)
            {
                m_allocator->Deallocate(m_data);
                m_data = nullptr;
            }
            if (m_ticks != nullptr)
            {
                m_allocator->Deallocate(m_ticks);
                m_ticks = nullptr;
            }
        }

        Column(const Column&) = delete;
        Column& operator=(const Column&) = delete;

        Column(Column&& other) noexcept
            : m_allocator{other.m_allocator}
            , m_meta{other.m_meta}
            , m_data{other.m_data}
            , m_ticks{other.m_ticks}
            , m_size{other.m_size}
            , m_capacity{other.m_capacity}
        {
            other.m_data = nullptr;
            other.m_ticks = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }

        Column& operator=(Column&& other) noexcept
        {
            if (this != &other)
            {
                Clear();
                if (m_data != nullptr)
                {
                    m_allocator->Deallocate(m_data);
                }
                if (m_ticks != nullptr)
                {
                    m_allocator->Deallocate(m_ticks);
                }

                m_allocator = other.m_allocator;
                m_meta = other.m_meta;
                m_data = other.m_data;
                m_ticks = other.m_ticks;
                m_size = other.m_size;
                m_capacity = other.m_capacity;

                other.m_data = nullptr;
                other.m_ticks = nullptr;
                other.m_size = 0;
                other.m_capacity = 0;
            }
            return *this;
        }

        void PushDefault(Tick currentTick = Tick{0})
        {
            EnsureCapacity(m_size + 1);

            void* dst = GetRaw(m_size);
            if (m_meta.m_construct != nullptr)
            {
                m_meta.m_construct(dst);
            }
            else
            {
                std::memset(dst, 0, m_meta.m_size);
            }
            m_ticks[m_size].SetAdded(currentTick);
            ++m_size;
        }

        void PushCopy(const void* src, Tick currentTick = Tick{0})
        {
            hive::Assert(src != nullptr, "Cannot push null source");
            EnsureCapacity(m_size + 1);

            void* dst = GetRaw(m_size);
            if (m_meta.m_copy != nullptr)
            {
                m_meta.m_copy(dst, src);
            }
            else
            {
                std::memcpy(dst, src, m_meta.m_size);
            }
            m_ticks[m_size].SetAdded(currentTick);
            ++m_size;
        }

        void PushMove(void* src, Tick currentTick = Tick{0})
        {
            hive::Assert(src != nullptr, "Cannot push null source");
            EnsureCapacity(m_size + 1);

            void* dst = GetRaw(m_size);
            if (m_meta.m_move != nullptr)
            {
                m_meta.m_move(dst, src);
            }
            else
            {
                std::memcpy(dst, src, m_meta.m_size);
            }
            m_ticks[m_size].SetAdded(currentTick);
            ++m_size;
        }

        void Pop()
        {
            hive::Assert(m_size > 0, "Cannot pop from empty column");

            --m_size;
            void* ptr = GetRaw(m_size);
            if (m_meta.m_destruct != nullptr)
            {
                m_meta.m_destruct(ptr);
            }
        }

        void SwapRemove(size_t index)
        {
            hive::Assert(index < m_size, "Index out of bounds");

            if (index != m_size - 1)
            {
                void* dst = GetRaw(index);
                void* src = GetRaw(m_size - 1);

                if (m_meta.m_destruct != nullptr)
                {
                    m_meta.m_destruct(dst);
                }

                if (m_meta.m_move != nullptr)
                {
                    m_meta.m_move(dst, src);
                }
                else
                {
                    std::memcpy(dst, src, m_meta.m_size);
                }

                if (m_meta.m_destruct != nullptr)
                {
                    m_meta.m_destruct(src);
                }

                m_ticks[index] = m_ticks[m_size - 1];
            }
            else
            {
                if (m_meta.m_destruct != nullptr)
                {
                    m_meta.m_destruct(GetRaw(index));
                }
            }

            --m_size;
        }

        [[nodiscard]] void* GetRaw(size_t index) noexcept
        {
            hive::Assert(index < m_capacity, "Index out of bounds");
            return static_cast<std::byte*>(m_data) + (index * m_meta.m_size);
        }

        [[nodiscard]] const void* GetRaw(size_t index) const noexcept
        {
            hive::Assert(index < m_capacity, "Index out of bounds");
            return static_cast<const std::byte*>(m_data) + (index * m_meta.m_size);
        }

        template <typename T> [[nodiscard]] T* Get(size_t index) noexcept
        {
            hive::Assert(TypeIdOf<T>() == m_meta.m_typeId, "Type mismatch");
            hive::Assert(index < m_size, "Index out of bounds");
            return static_cast<T*>(GetRaw(index));
        }

        template <typename T> [[nodiscard]] const T* Get(size_t index) const noexcept
        {
            hive::Assert(TypeIdOf<T>() == m_meta.m_typeId, "Type mismatch");
            hive::Assert(index < m_size, "Index out of bounds");
            return static_cast<const T*>(GetRaw(index));
        }

        template <typename T> [[nodiscard]] T* Data() noexcept
        {
            hive::Assert(TypeIdOf<T>() == m_meta.m_typeId, "Type mismatch");
            return static_cast<T*>(m_data);
        }

        template <typename T> [[nodiscard]] const T* Data() const noexcept
        {
            hive::Assert(TypeIdOf<T>() == m_meta.m_typeId, "Type mismatch");
            return static_cast<const T*>(m_data);
        }

        void Clear()
        {
            if (m_meta.m_destruct != nullptr)
            {
                for (size_t i = 0; i < m_size; ++i)
                {
                    m_meta.m_destruct(GetRaw(i));
                }
            }
            m_size = 0;
        }

        void Reserve(size_t newCapacity)
        {
            if (newCapacity <= m_capacity)
            {
                return;
            }

            void* newData = m_allocator->Allocate(newCapacity * m_meta.m_size, m_meta.m_alignment);
            hive::Assert(newData != nullptr, "Column data allocation failed");

            ComponentTicks* newTicks = static_cast<ComponentTicks*>(
                m_allocator->Allocate(newCapacity * sizeof(ComponentTicks), alignof(ComponentTicks)));
            hive::Assert(newTicks != nullptr, "Column ticks allocation failed");

            if (m_data != nullptr)
            {
                for (size_t i = 0; i < m_size; ++i)
                {
                    void* dst = static_cast<std::byte*>(newData) + (i * m_meta.m_size);
                    void* src = GetRaw(i);

                    if (m_meta.m_move != nullptr)
                    {
                        m_meta.m_move(dst, src);
                    }
                    else
                    {
                        std::memcpy(dst, src, m_meta.m_size);
                    }

                    if (m_meta.m_destruct != nullptr)
                    {
                        m_meta.m_destruct(src);
                    }

                    newTicks[i] = m_ticks[i];
                }

                m_allocator->Deallocate(m_data);
            }

            if (m_ticks != nullptr)
            {
                m_allocator->Deallocate(m_ticks);
            }

            m_data = newData;
            m_ticks = newTicks;
            m_capacity = newCapacity;
        }

        [[nodiscard]] size_t Size() const noexcept
        {
            return m_size;
        }
        [[nodiscard]] size_t Capacity() const noexcept
        {
            return m_capacity;
        }
        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_size == 0;
        }
        [[nodiscard]] TypeId GetTypeId() const noexcept
        {
            return m_meta.m_typeId;
        }
        [[nodiscard]] const ComponentMeta& GetMeta() const noexcept
        {
            return m_meta;
        }

        [[nodiscard]] ComponentTicks& GetTicks(size_t index) noexcept
        {
            hive::Assert(index < m_size, "Index out of bounds");
            return m_ticks[index];
        }

        [[nodiscard]] const ComponentTicks& GetTicks(size_t index) const noexcept
        {
            hive::Assert(index < m_size, "Index out of bounds");
            return m_ticks[index];
        }

        [[nodiscard]] ComponentTicks* TicksData() noexcept
        {
            return m_ticks;
        }
        [[nodiscard]] const ComponentTicks* TicksData() const noexcept
        {
            return m_ticks;
        }

        void MarkChanged(size_t index, Tick currentTick) noexcept
        {
            hive::Assert(index < m_size, "Index out of bounds");
            m_ticks[index].MarkChanged(currentTick);
        }

    private:
        void EnsureCapacity(size_t required)
        {
            if (required > m_capacity)
            {
                size_t newCapacity = m_capacity == 0 ? 8 : m_capacity * 2;
                while (newCapacity < required)
                {
                    newCapacity *= 2;
                }
                Reserve(newCapacity);
            }
        }

        Allocator* m_allocator;
        ComponentMeta m_meta;
        void* m_data;
        ComponentTicks* m_ticks;
        size_t m_size;
        size_t m_capacity;
    };
} // namespace queen
