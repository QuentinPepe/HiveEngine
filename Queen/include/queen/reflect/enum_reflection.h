#pragma once

#include <hive/core/assert.h>

#include <queen/core/type_id.h>
#include <queen/reflect/field_info.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace queen
{
    struct EnumEntry
    {
        const char* m_name = nullptr;
        int64_t m_value = 0;
    };

    // Type-erased name<->value mapping for reflected enums. Referenced from
    // FieldInfo for FieldType::ENUM fields.
    struct EnumReflectionBase
    {
        const char* m_typeName = nullptr;
        TypeId m_typeId = 0;
        size_t m_underlyingSize = 0;
        const EnumEntry* m_entries = nullptr;
        size_t m_entryCount = 0;

        // Returns nullptr if value is not registered.
        [[nodiscard]] const char* NameOf(int64_t value) const noexcept
        {
            for (size_t i = 0; i < m_entryCount; ++i)
            {
                if (m_entries[i].m_value == value)
                {
                    return m_entries[i].m_name;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool ValueOf(const char* name, int64_t& out) const noexcept
        {
            if (name == nullptr)
                return false;

            for (size_t i = 0; i < m_entryCount; ++i)
            {
                if (detail::StringsEqual(m_entries[i].m_name, name))
                {
                    out = m_entries[i].m_value;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_entries != nullptr && m_entryCount > 0;
        }
    };

    // Fixed-size builder for enum reflection. Use inside an EnumInfo<E>::Get()
    // specialization to register name<->value pairs.
    template <size_t MaxEntries = 32> class EnumReflector
    {
    public:
        constexpr EnumReflector() noexcept = default;

        template <typename E> void Value(const char* name, E value) noexcept
        {
            static_assert(std::is_enum_v<E>, "Value() requires an enum type");
            hive::Assert(m_count < MaxEntries, "Too many enum entries, increase MaxEntries");

            m_entries[m_count].m_name = name;
            m_entries[m_count].m_value = static_cast<int64_t>(value);
            ++m_count;

            // Update base on each insertion (so Base() is always valid)
            m_base.m_entries = m_entries;
            m_base.m_entryCount = m_count;
            m_base.m_typeId = TypeIdOf<E>();
            m_base.m_typeName = TypeNameOf<E>().data();
            m_base.m_underlyingSize = sizeof(std::underlying_type_t<E>);
        }

        [[nodiscard]] const EnumReflectionBase& Base() const noexcept
        {
            return m_base;
        }

        [[nodiscard]] size_t Count() const noexcept
        {
            return m_count;
        }

    private:
        EnumEntry m_entries[MaxEntries]{};
        size_t m_count = 0;
        EnumReflectionBase m_base{};
    };

    // Extension point: specialize EnumInfo<E> with a static Get() returning
    // an EnumReflectionBase. Will be obsoleted by C++26 reflection.
    template <typename E> struct EnumInfo; // No default definition — must be specialized

    template <typename E>
    concept ReflectableEnum = std::is_enum_v<E> && requires {
        { EnumInfo<E>::Get() } -> std::convertible_to<const EnumReflectionBase&>;
    };
} // namespace queen
