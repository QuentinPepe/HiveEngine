#pragma once

#include <cstdint>

namespace queen
{
    // Monotonic counter for change detection. Wraps at UINT32_MAX, so comparisons go through
    // signed-difference helpers (correct as long as ticks differ by less than 2^31).
    struct Tick
    {
        uint32_t m_value{0};

        constexpr Tick() noexcept = default;
        explicit constexpr Tick(uint32_t v) noexcept
            : m_value{v}
        {
        }

        [[nodiscard]] constexpr bool IsNewerThan(Tick other) const noexcept
        {
            // Cast to signed to handle wraparound correctly
            return static_cast<int32_t>(m_value - other.m_value) > 0;
        }

        [[nodiscard]] constexpr bool IsAtLeast(Tick other) const noexcept
        {
            return static_cast<int32_t>(m_value - other.m_value) >= 0;
        }

        constexpr Tick& operator++() noexcept
        {
            ++m_value;
            return *this;
        }

        constexpr Tick operator++(int) noexcept
        {
            Tick tmp = *this;
            ++m_value;
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(Tick other) const noexcept
        {
            return m_value == other.m_value;
        }

        [[nodiscard]] constexpr bool operator!=(Tick other) const noexcept
        {
            return m_value != other.m_value;
        }
    };

    // Per-component (added, changed) tick pair powering Added<T> and Changed<T> query filters.
    // 8 bytes per component per entity.
    struct ComponentTicks
    {
        Tick m_added{0};
        Tick m_changed{0};

        constexpr ComponentTicks() noexcept = default;

        explicit constexpr ComponentTicks(Tick currentTick) noexcept
            : m_added{currentTick}
            , m_changed{currentTick}
        {
        }

        constexpr ComponentTicks(Tick addedTick, Tick changedTick) noexcept
            : m_added{addedTick}
            , m_changed{changedTick}
        {
        }

        [[nodiscard]] constexpr bool WasAdded(Tick lastRun) const noexcept
        {
            return m_added.IsNewerThan(lastRun);
        }

        [[nodiscard]] constexpr bool WasChanged(Tick lastRun) const noexcept
        {
            return m_changed.IsNewerThan(lastRun);
        }

        [[nodiscard]] constexpr bool WasAddedOrChanged(Tick lastRun) const noexcept
        {
            return WasAdded(lastRun) || WasChanged(lastRun);
        }

        constexpr void MarkChanged(Tick currentTick) noexcept
        {
            m_changed = currentTick;
        }

        // Stamp both added and changed; call when constructing a new component.
        constexpr void SetAdded(Tick currentTick) noexcept
        {
            m_added = currentTick;
            m_changed = currentTick;
        }
    };
} // namespace queen
