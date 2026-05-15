#pragma once

#include <hive/core/assert.h>

#include <queen/core/entity.h>
#include <queen/core/type_id.h>
#include <queen/reflect/component_category.h>
#include <queen/reflect/enum_reflection.h>
#include <queen/reflect/field_attributes.h>
#include <queen/reflect/field_info.h>

#include <cstddef>
#include <type_traits>

namespace queen
{
    // Forward declaration
    template <size_t MaxFields> class ComponentReflector;

    namespace detail
    {
        // Detect wax::FixedString without including the header.
        // Struct layout: char buffer_[MaxCapacity + 1] + uint8_t m_size.
        template <typename T>
        concept IsFixedString = requires(const T& s) {
            { T::MaxCapacity } -> std::convertible_to<size_t>;
            { s.CStr() } -> std::same_as<const char*>;
            { s.Size() } -> std::same_as<size_t>;
        } && (sizeof(T) == T::MaxCapacity + 2) && std::is_trivially_copyable_v<T>;
    } // namespace detail

    // Chaining builder for optional editor attributes. Return value is
    // safely discardable so callers that don't annotate pay nothing.
    class FieldBuilder
    {
    public:
        constexpr FieldBuilder(FieldInfo& info, FieldAttributes& attrs) noexcept
            : m_info{info}
            , m_attrs{attrs}
        {
        }

        FieldBuilder& Range(float min, float max, float step = 0.f) noexcept
        {
            EnsureAttributes();
            m_attrs.m_min = min;
            m_attrs.m_max = max;
            m_attrs.m_step = step;
            return *this;
        }

        FieldBuilder& Tooltip(const char* text) noexcept
        {
            EnsureAttributes();
            m_attrs.m_tooltip = text;
            return *this;
        }

        FieldBuilder& Category(const char* cat) noexcept
        {
            EnsureAttributes();
            m_attrs.m_category = cat;
            return *this;
        }

        FieldBuilder& DisplayName(const char* name) noexcept
        {
            EnsureAttributes();
            m_attrs.m_displayName = name;
            return *this;
        }

        FieldBuilder& Flag(FieldFlag flag) noexcept
        {
            EnsureAttributes();
            m_attrs.m_flags |= static_cast<uint32_t>(flag);
            return *this;
        }

    private:
        void EnsureAttributes() noexcept
        {
            if (m_info.m_attributes == nullptr)
            {
                m_info.m_attributes = &m_attrs;
            }
        }

        FieldInfo& m_info;
        FieldAttributes& m_attrs;
    };

    // Component field registration builder used inside a component's static
    // Reflect() function. Fixed-size storage avoids heap allocation. Intermediate
    // solution until C++26 reflection lands.
    template <size_t MaxFields = 32> class ComponentReflector
    {
    public:
        constexpr ComponentReflector() noexcept = default;

        // Tag the whole component with a category. User by default.
        // System: engine-managed, hidden from "Add Component" picker.
        // Internal: never shown in the inspector at all.
        constexpr void Category(ComponentCategory category) noexcept
        {
            m_category = category;
        }

        [[nodiscard]] constexpr ComponentCategory GetCategory() const noexcept
        {
            return m_category;
        }

        // Name must be a string literal or have static storage — stored by pointer.
        template <typename T, typename C> FieldBuilder Field(const char* name, T C::* memberPtr) noexcept
        {
            hive::Assert(m_count < MaxFields, "Too many fields, increase MaxFields");

            size_t idx = m_count++;
            FieldInfo& info = m_fields[idx];
            info.m_name = name;
            info.m_offset = GetMemberOffset(memberPtr);
            info.m_size = sizeof(T);

            // Type deduction
            if constexpr (std::is_same_v<T, Entity>)
            {
                info.m_type = FieldType::ENTITY;
            }
            else if constexpr (std::is_enum_v<T>)
            {
                info.m_type = FieldType::ENUM;

                // Auto-lookup EnumInfo<T> if specialized
                if constexpr (ReflectableEnum<T>)
                {
                    info.m_enumInfo = &EnumInfo<T>::Get();
                }
            }
            else if constexpr (detail::IsFixedString<T>)
            {
                info.m_type = FieldType::STRING;
            }
            else if constexpr (std::is_array_v<T>)
            {
                info.m_type = FieldType::FIXED_ARRAY;
                info.m_elementCount = std::extent_v<T>;
                info.m_elementType = detail::GetFieldType<std::remove_extent_t<T>>();
            }
            else
            {
                info.m_type = detail::GetFieldType<T>();
            }

            // For struct types, store the nested type ID and reflection data if available
            if (info.m_type == FieldType::STRUCT)
            {
                info.m_nestedTypeId = TypeIdOf<T>();

                // If the nested type is Reflectable, capture its field layout
                if constexpr (requires(ComponentReflector<MaxFields>& r) { T::Reflect(r); })
                {
                    // Holder avoids copying the reflector (which would leave
                    // FieldInfo::attributes pointers dangling).
                    struct NestedHolder
                    {
                        ComponentReflector<MaxFields> m_reflector;
                        NestedHolder()
                        {
                            T::Reflect(m_reflector);
                        }
                    };
                    static NestedHolder s_nested;
                    info.m_nestedFields = s_nested.m_reflector.Data();
                    info.m_nestedFieldCount = s_nested.m_reflector.Count();
                }
            }

            return FieldBuilder{info, m_attributes[idx]};
        }

        [[nodiscard]] constexpr size_t Count() const noexcept
        {
            return m_count;
        }

        [[nodiscard]] constexpr const FieldInfo& operator[](size_t index) const noexcept
        {
            return m_fields[index];
        }

        // Linear search; returns nullptr if not found.
        [[nodiscard]] constexpr const FieldInfo* FindField(const char* name) const noexcept
        {
            for (size_t i = 0; i < m_count; ++i)
            {
                if (detail::StringsEqual(m_fields[i].m_name, name))
                {
                    return &m_fields[i];
                }
            }
            return nullptr;
        }

        [[nodiscard]] constexpr const FieldInfo* Data() const noexcept
        {
            return m_fields;
        }

        [[nodiscard]] constexpr const FieldInfo* Begin() const noexcept
        {
            return m_fields;
        }

        [[nodiscard]] constexpr const FieldInfo* End() const noexcept
        {
            return m_fields + m_count;
        }

    private:
        template <typename T, typename C> static size_t GetMemberOffset(T C::* memberPtr) noexcept
        {
            // Use stack-allocated storage to avoid nullptr dereference UB
            alignas(C) unsigned char storage[sizeof(C)]{};
            return static_cast<size_t>(
                reinterpret_cast<const unsigned char*>(&(reinterpret_cast<C*>(storage)->*memberPtr)) - storage);
        }

        FieldInfo m_fields[MaxFields]{};
        FieldAttributes m_attributes[MaxFields]{};
        size_t m_count = 0;
        ComponentCategory m_category = ComponentCategory::USER;
    };

    // Non-template view of reflection data, used by ComponentRegistry and
    // serialization so consumers don't need to know MaxFields.
    struct ComponentReflection
    {
        const FieldInfo* m_fields = nullptr;
        size_t m_fieldCount = 0;
        TypeId m_typeId = 0;
        const char* m_name = nullptr;
        ComponentCategory m_category = ComponentCategory::USER;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_typeId != 0 && m_fields != nullptr;
        }

        [[nodiscard]] constexpr const FieldInfo* FindField(const char* fieldName) const noexcept
        {
            for (size_t i = 0; i < m_fieldCount; ++i)
            {
                if (detail::StringsEqual(m_fields[i].m_name, fieldName))
                {
                    return &m_fields[i];
                }
            }
            return nullptr;
        }

        [[nodiscard]] constexpr const FieldInfo* Begin() const noexcept
        {
            return m_fields;
        }

        [[nodiscard]] constexpr const FieldInfo* End() const noexcept
        {
            return m_fields + m_fieldCount;
        }
    };
} // namespace queen
