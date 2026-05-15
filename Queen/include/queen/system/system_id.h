#pragma once

#include <cstdint>

namespace queen
{
    // Type-safe handle for a registered system. Backed by a 32-bit index that
    // matches the system's slot in SystemStorage, so lookups stay O(1).
    class SystemId
    {
    public:
        constexpr SystemId() noexcept
            : m_index{kInvalidIndex}
        {
        }

        constexpr explicit SystemId(uint32_t index) noexcept
            : m_index{index}
        {
        }

        [[nodiscard]] constexpr uint32_t Index() const noexcept
        {
            return m_index;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_index != kInvalidIndex;
        }

        [[nodiscard]] static constexpr SystemId Invalid() noexcept
        {
            return SystemId{};
        }

        constexpr bool operator==(const SystemId& other) const noexcept = default;

        constexpr bool operator<(const SystemId& other) const noexcept
        {
            return m_index < other.m_index;
        }

    private:
        static constexpr uint32_t kInvalidIndex = ~uint32_t{0};
        uint32_t m_index;
    };
} // namespace queen
