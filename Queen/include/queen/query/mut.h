#pragma once

#include <queen/core/tick.h>

#include <type_traits>

namespace queen
{
    // Mutable component handle that defers change-tick marking until the value is actually
    // touched through the mutating accessors. Avoids marking unread components dirty so
    // Changed<T> filters stay precise. GetReadOnly() bypasses marking for read-after-write.
    template <typename T> class Mut
    {
    public:
        using ComponentType = T;

        constexpr Mut() noexcept
            : m_ptr{nullptr}
            , m_ticks{nullptr}
            , m_currentTick{0}
        {
        }

        constexpr Mut(T* ptr, ComponentTicks* ticks, Tick current_tick) noexcept
            : m_ptr{ptr}
            , m_ticks{ticks}
            , m_currentTick{current_tick}
        {
        }

        // Mutating accessor: stamps the current tick before returning the pointer.
        [[nodiscard]] T* operator->() noexcept
        {
            MarkChanged();
            return m_ptr;
        }

        [[nodiscard]] const T* operator->() const noexcept
        {
            return m_ptr;
        }

        // Mutating dereference: stamps the current tick before returning the reference.
        [[nodiscard]] T& operator*() noexcept
        {
            MarkChanged();
            return *m_ptr;
        }

        [[nodiscard]] const T& operator*() const noexcept
        {
            return *m_ptr;
        }

        // Returns the raw pointer and marks the component changed.
        [[nodiscard]] T* Get() noexcept
        {
            MarkChanged();
            return m_ptr;
        }

        [[nodiscard]] const T* Get() const noexcept
        {
            return m_ptr;
        }

        // Escape hatch for reading without triggering change detection.
        [[nodiscard]] const T* GetReadOnly() const noexcept
        {
            return m_ptr;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return m_ptr != nullptr;
        }

        // Manual override when the mutation happens through an aliased pointer/reference.
        void MarkChanged() noexcept
        {
            if (m_ticks != nullptr)
            {
                m_ticks->MarkChanged(m_currentTick);
            }
        }

        [[nodiscard]] bool WasAdded(Tick last_run) const noexcept
        {
            return m_ticks != nullptr && m_ticks->WasAdded(last_run);
        }

        [[nodiscard]] bool WasChanged(Tick last_run) const noexcept
        {
            return m_ticks != nullptr && m_ticks->WasChanged(last_run);
        }

        [[nodiscard]] const ComponentTicks* Ticks() const noexcept
        {
            return m_ticks;
        }

    private:
        T* m_ptr;
        ComponentTicks* m_ticks;
        Tick m_currentTick;
    };

    // Type traits to detect Mut<T>
    namespace detail
    {
        template <typename T> struct IsMut : std::false_type
        {
        };

        template <typename T> struct IsMut<Mut<T>> : std::true_type
        {
        };

        template <typename T> constexpr bool IsMutV = IsMut<T>::value;

        template <typename T> struct UnwrapMut
        {
            using type = T;
        };

        template <typename T> struct UnwrapMut<Mut<T>>
        {
            using type = T;
        };

        template <typename T> using UnwrapMutT = typename UnwrapMut<T>::type;
    } // namespace detail
} // namespace queen
