#pragma once

#include <cstdint>

namespace propolis
{
    [[nodiscard]] constexpr uint32_t ScriptNameHash(const char* str) noexcept
    {
        uint32_t hash = 2166136261u;
        while (*str)
        {
            hash ^= static_cast<uint32_t>(*str++);
            hash *= 16777619u;
        }
        return hash;
    }

    struct PropolisScript
    {
        uint32_t m_nameHash{0};
        void* m_state{nullptr};
    };

    struct ScriptTime
    {
        float m_dt{0.0f};
    };
} // namespace propolis
