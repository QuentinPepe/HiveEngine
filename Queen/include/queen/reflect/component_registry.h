#pragma once

#include <hive/core/assert.h>

#include <queen/core/component_info.h>
#include <queen/core/type_id.h>
#include <queen/reflect/component_serializer.h>
#include <queen/reflect/reflectable.h>

#include <cstddef>
#include <cstring>

namespace queen
{
    // Bundles ComponentMeta (lifecycle) and ComponentReflection (field layout)
    // plus a snapshot of T{} used to diff prefab overrides against defaults.
    struct RegisteredComponent
    {
        ComponentMeta m_meta;
        ComponentReflection m_reflection;
        const void* m_defaultValue = nullptr; // Snapshot of T{} for prefab diff

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_meta.IsValid();
        }

        [[nodiscard]] constexpr bool HasReflection() const noexcept
        {
            return m_reflection.IsValid();
        }

        [[nodiscard]] constexpr bool HasDefault() const noexcept
        {
            return m_defaultValue != nullptr;
        }
    };

    // Runtime registry of component types. Entries stay sorted by TypeId so
    // Find() can binary-search. Not thread-safe: register at startup only.
    // TypeId collisions assert.
    template <size_t MaxComponents = 256> class ComponentRegistry
    {
    public:
        constexpr ComponentRegistry() noexcept = default;

        template <Reflectable T> void Register() noexcept
        {
            hive::Assert(m_count < MaxComponents, "ComponentRegistry full");
            hive::Assert(Find(TypeIdOf<T>()) == nullptr, "Component already registered");

            RegisteredComponent entry;
            entry.m_meta = ComponentMeta::Of<T>();
            entry.m_reflection = GetReflectionData<T>();

            // Capture default-constructed instance for prefab diff
            if constexpr (std::is_default_constructible_v<T>)
            {
                static const T DEFAULT_INSTANCE{};
                entry.m_defaultValue = &DEFAULT_INSTANCE;
            }

            // Insert in sorted order by type_id for binary search
            size_t insertPos = m_count;
            for (size_t i = 0; i < m_count; ++i)
            {
                if (m_entries[i].m_meta.m_typeId > entry.m_meta.m_typeId)
                {
                    insertPos = i;
                    break;
                }
            }

            // Shift elements to make room
            for (size_t i = m_count; i > insertPos; --i)
            {
                m_entries[i] = m_entries[i - 1];
            }

            m_entries[insertPos] = entry;
            ++m_count;
        }

        // For tag/runtime-only components that don't need serialization.
        template <typename T> void RegisterWithoutReflection() noexcept
        {
            hive::Assert(m_count < MaxComponents, "ComponentRegistry full");
            hive::Assert(Find(TypeIdOf<T>()) == nullptr, "Component already registered");

            RegisteredComponent entry;
            entry.m_meta = ComponentMeta::Of<T>();
            // reflection stays default (invalid)

            // Capture default-constructed instance
            if constexpr (std::is_default_constructible_v<T>)
            {
                static const T DEFAULT_INSTANCE{};
                entry.m_defaultValue = &DEFAULT_INSTANCE;
            }

            // Insert in sorted order
            size_t insertPos = m_count;
            for (size_t i = 0; i < m_count; ++i)
            {
                if (m_entries[i].m_meta.m_typeId > entry.m_meta.m_typeId)
                {
                    insertPos = i;
                    break;
                }
            }

            for (size_t i = m_count; i > insertPos; --i)
            {
                m_entries[i] = m_entries[i - 1];
            }

            m_entries[insertPos] = entry;
            ++m_count;
        }

        // Binary search; returns nullptr if not found.
        [[nodiscard]] const RegisteredComponent* Find(TypeId typeId) const noexcept
        {
            if (m_count == 0)
                return nullptr;

            // Binary search
            size_t left = 0;
            size_t right = m_count;

            while (left < right)
            {
                size_t mid = left + (right - left) / 2;
                TypeId midId = m_entries[mid].m_meta.m_typeId;

                if (midId == typeId)
                {
                    return &m_entries[mid];
                }
                else if (midId < typeId)
                {
                    left = mid + 1;
                }
                else
                {
                    right = mid;
                }
            }

            return nullptr;
        }

        // Linear search; returns nullptr if not found.
        [[nodiscard]] const RegisteredComponent* FindByName(const char* name) const noexcept
        {
            if (name == nullptr)
                return nullptr;

            for (size_t i = 0; i < m_count; ++i)
            {
                if (detail::StringsEqual(m_entries[i].m_reflection.m_name, name))
                {
                    return &m_entries[i];
                }
            }

            return nullptr;
        }

        [[nodiscard]] const RegisteredComponent& operator[](size_t index) const noexcept
        {
            hive::Assert(index < m_count, "Index out of bounds");
            return m_entries[index];
        }

        [[nodiscard]] size_t Count() const noexcept
        {
            return m_count;
        }

        [[nodiscard]] bool Contains(TypeId typeId) const noexcept
        {
            return Find(typeId) != nullptr;
        }

        template <typename T> [[nodiscard]] bool Contains() const noexcept
        {
            return Contains(TypeIdOf<T>());
        }

        [[nodiscard]] const RegisteredComponent* Begin() const noexcept
        {
            return m_entries;
        }

        [[nodiscard]] const RegisteredComponent* End() const noexcept
        {
            return m_entries + m_count;
        }

        // dst must be at least meta.size bytes, properly aligned. Returns
        // false if type is not registered.
        [[nodiscard]] bool Construct(TypeId typeId, void* dst) const noexcept
        {
            const RegisteredComponent* comp = Find(typeId);
            if (comp == nullptr || comp->m_meta.m_construct == nullptr)
                return false;
            comp->m_meta.m_construct(dst);
            return true;
        }

        [[nodiscard]] bool Clone(TypeId typeId, void* dst, const void* src) const noexcept
        {
            const RegisteredComponent* comp = Find(typeId);
            if (comp == nullptr || comp->m_meta.m_copy == nullptr)
                return false;
            comp->m_meta.m_copy(dst, src);
            return true;
        }

        // Bitmask of fields differing from the default. Caps at 64 fields.
        // Returns ~0 when no default snapshot is available.
        [[nodiscard]] uint64_t DiffWithDefault(TypeId typeId, const void* instance) const noexcept
        {
            const RegisteredComponent* comp = Find(typeId);
            if (comp == nullptr || comp->m_defaultValue == nullptr || !comp->HasReflection())
            {
                return ~uint64_t{0};
            }

            uint64_t mask = 0;
            const auto& refl = comp->m_reflection;
            for (size_t i = 0; i < refl.m_fieldCount && i < 64; ++i)
            {
                const FieldInfo& field = refl.m_fields[i];
                const auto* a = static_cast<const std::byte*>(instance) + field.m_offset;
                const auto* b = static_cast<const std::byte*>(comp->m_defaultValue) + field.m_offset;

                if (std::memcmp(a, b, field.m_size) != 0)
                {
                    mask |= (uint64_t{1} << i);
                }
            }
            return mask;
        }

        [[nodiscard]] const void* GetDefault(TypeId typeId) const noexcept
        {
            const RegisteredComponent* comp = Find(typeId);
            if (comp == nullptr)
                return nullptr;
            return comp->m_defaultValue;
        }

        void Clear() noexcept
        {
            m_count = 0;
        }

    private:
        RegisteredComponent m_entries[MaxComponents]{};
        size_t m_count = 0;
    };
} // namespace queen
