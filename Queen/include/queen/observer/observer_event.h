#pragma once

#include <queen/core/type_id.h>

#include <type_traits>

namespace queen
{
    // Compile-time tag identifying an observer trigger event. The trio OnAdd/OnRemove/OnSet
    // is empty (zero runtime cost) and only carries the component type plus pre-computed
    // TypeIds used by ObserverStorage for O(1) hash lookup.

    // Fires after component T is added and initialised on an entity.
    template <typename T> struct OnAdd
    {
        using ComponentType = T;
        static constexpr TypeId triggerId = TypeIdOf<OnAdd<T>>();
        static constexpr TypeId componentId = TypeIdOf<T>();
    };

    // Fires before component T is destroyed on an entity (also during full despawn).
    template <typename T> struct OnRemove
    {
        using ComponentType = T;
        static constexpr TypeId triggerId = TypeIdOf<OnRemove<T>>();
        static constexpr TypeId componentId = TypeIdOf<T>();
    };

    // Fires when T is mutated through Mut<T> or replaced via Commands::Set. Requires explicit
    // change notification: raw pointer writes won't trigger this observer.
    template <typename T> struct OnSet
    {
        using ComponentType = T;
        static constexpr TypeId triggerId = TypeIdOf<OnSet<T>>();
        static constexpr TypeId componentId = TypeIdOf<T>();
    };

    // Trigger type detection concepts

    namespace detail
    {
        template <typename T> struct IsOnAdd : std::false_type
        {
        };

        template <typename T> struct IsOnAdd<OnAdd<T>> : std::true_type
        {
        };

        template <typename T> struct IsOnRemove : std::false_type
        {
        };

        template <typename T> struct IsOnRemove<OnRemove<T>> : std::true_type
        {
        };

        template <typename T> struct IsOnSet : std::false_type
        {
        };

        template <typename T> struct IsOnSet<OnSet<T>> : std::true_type
        {
        };
    } // namespace detail

    template <typename T>
    concept IsOnAddTrigger = detail::IsOnAdd<T>::value;

    template <typename T>
    concept IsOnRemoveTrigger = detail::IsOnRemove<T>::value;

    template <typename T>
    concept IsOnSetTrigger = detail::IsOnSet<T>::value;

    template <typename T>
    concept ObserverTrigger = IsOnAddTrigger<T> || IsOnRemoveTrigger<T> || IsOnSetTrigger<T>;

    // Type-erased runtime trigger; mirrors the OnAdd/OnRemove/OnSet template family so
    // ObserverStorage can dispatch without instantiating per-type code paths.
    enum class TriggerType : uint8_t
    {
        ADD,
        REMOVE,
        SET
    };

    template <ObserverTrigger T> [[nodiscard]] constexpr TriggerType GetTriggerType() noexcept
    {
        if constexpr (IsOnAddTrigger<T>)
        {
            return TriggerType::ADD;
        }
        else if constexpr (IsOnRemoveTrigger<T>)
        {
            return TriggerType::REMOVE;
        }
        else
        {
            return TriggerType::SET;
        }
    }

    template <ObserverTrigger T> [[nodiscard]] constexpr TypeId GetTriggerComponentId() noexcept
    {
        return T::componentId;
    }

    // Composite (trigger, component) key used by ObserverStorage's lookup HashMap.
    struct ObserverKey
    {
        TriggerType m_trigger;
        TypeId m_componentId;

        [[nodiscard]] constexpr bool operator==(const ObserverKey& other) const noexcept
        {
            return m_trigger == other.m_trigger && m_componentId == other.m_componentId;
        }

        template <ObserverTrigger T> [[nodiscard]] static constexpr ObserverKey Of() noexcept
        {
            return ObserverKey{GetTriggerType<T>(), GetTriggerComponentId<T>()};
        }

        [[nodiscard]] static constexpr ObserverKey From(TriggerType trigger, TypeId componentId) noexcept
        {
            return ObserverKey{trigger, componentId};
        }
    };

    // FNV-1a-style hash; the multiplication after the XOR is what diffuses the trigger byte
    // across the TypeId bits so similar keys don't collide on a single bucket.
    struct ObserverKeyHash
    {
        [[nodiscard]] constexpr uint64_t operator()(const ObserverKey& key) const noexcept
        {
            // Combine trigger type and component_id using FNV-1a style mixing
            uint64_t hash = static_cast<uint64_t>(key.m_trigger);
            hash ^= key.m_componentId;
            hash *= 0x100000001b3ULL; // FNV prime
            return hash;
        }
    };
} // namespace queen
