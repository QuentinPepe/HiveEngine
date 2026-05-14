#pragma once

#include <queen/reflect/component_reflector.h>

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

        // m_state is intentionally NOT reflected: it is allocated by Propolis.Init at runtime
        // from the script's nameHash and must not be serialized.
        static void Reflect(queen::ComponentReflector<>& r)
        {
            // Parameterized: the editor exposes this via a dedicated Scripts picker
            // (a bare instance with nameHash=0 is meaningless).
            r.Category(queen::ComponentCategory::PARAMETERIZED);
            r.Field("name_hash", &PropolisScript::m_nameHash).Flag(queen::FieldFlag::READ_ONLY);
        }
    };

    struct ScriptTime
    {
        float m_dt{0.0f};
    };
} // namespace propolis
