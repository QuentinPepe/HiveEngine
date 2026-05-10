#pragma once

#include <cstdint>

namespace propolis
{
    struct NodeId
    {
        uint32_t m_value{0};

        [[nodiscard]] bool operator==(NodeId other) const noexcept { return m_value == other.m_value; }
        [[nodiscard]] bool operator!=(NodeId other) const noexcept { return m_value != other.m_value; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_value != 0; }
    };

    struct PinId
    {
        uint32_t m_value{0};

        [[nodiscard]] bool operator==(PinId other) const noexcept { return m_value == other.m_value; }
        [[nodiscard]] bool operator!=(PinId other) const noexcept { return m_value != other.m_value; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_value != 0; }
    };

    struct EdgeId
    {
        uint32_t m_value{0};

        [[nodiscard]] bool operator==(EdgeId other) const noexcept { return m_value == other.m_value; }
        [[nodiscard]] bool operator!=(EdgeId other) const noexcept { return m_value != other.m_value; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_value != 0; }
    };

    inline constexpr NodeId kInvalidNode{0};
    inline constexpr PinId kInvalidPin{0};
    inline constexpr EdgeId kInvalidEdge{0};

    enum class PinDirection : uint8_t
    {
        INPUT,
        OUTPUT
    };
} // namespace propolis
