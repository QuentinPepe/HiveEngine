#pragma once

#include <queen/reflect/component_reflector.h>

#include <concepts>
#include <type_traits>

namespace queen
{
    // Required constraint for serializable components. Intermediate solution
    // until C++26 native reflection lands.
    template <typename T>
    concept Reflectable = requires(ComponentReflector<32>& reflector) {
        { T::Reflect(reflector) } -> std::same_as<void>;
    };

    template <Reflectable T, size_t MaxFields = 32> constexpr ComponentReflector<MaxFields> GetReflection() noexcept
    {
        ComponentReflector<MaxFields> reflector;
        T::Reflect(reflector);
        return reflector;
    }

    // Returns a non-template view backed by static storage so it can live
    // in a registry without template parameters.
    template <Reflectable T> ComponentReflection GetReflectionData() noexcept
    {
        // Holder struct ensures the reflector is constructed in-place (no copy).
        // A copy would leave FieldInfo::attributes pointers dangling because
        // they point into the reflector's internal attributes_ array.
        // The name buffer stores a null-terminated copy because TypeNameOf()
        // returns a string_view trimmed via remove_suffix — .data() would
        // include trailing characters from the original __PRETTY_FUNCTION__.
        struct Holder
        {
            ComponentReflector<32> m_reflector;
            char m_name[128]{};
            Holder()
            {
                T::Reflect(m_reflector);
                auto sv = TypeNameOf<T>();
                auto len = sv.size() < 127 ? sv.size() : size_t{127};
                for (size_t i = 0; i < len; ++i)
                    m_name[i] = sv[i];
                m_name[len] = '\0';
            }
        };
        static Holder s_holder;

        ComponentReflection result;
        result.m_fields = s_holder.m_reflector.Data();
        result.m_fieldCount = s_holder.m_reflector.Count();
        result.m_typeId = TypeIdOf<T>();
        result.m_name = s_holder.m_name;
        result.m_category = s_holder.m_reflector.GetCategory();
        return result;
    }

    template <typename T> struct IsReflectable : std::bool_constant<Reflectable<T>>
    {
    };

    template <typename T> inline constexpr bool isReflectableV = IsReflectable<T>::value;
} // namespace queen
