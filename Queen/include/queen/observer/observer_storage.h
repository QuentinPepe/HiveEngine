#pragma once

#include <comb/allocator_concepts.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/vector.h>

#include <queen/core/entity.h>
#include <queen/observer/observer.h>
#include <queen/observer/observer_builder.h>
#include <queen/observer/observer_event.h>

#include <cstring>

namespace queen
{
    class World;

    // World-owned registry that indexes observers by (TriggerType, component TypeId) so
    // structural changes can fan out in O(1) average. Observers are stored contiguously and
    // referenced from the lookup map by index, never by pointer, to survive vector growth.
    //
    // lookup_  Key{Add, Health}    -> [0, 3]   indices into observers_
    //          Key{Remove, Health} -> [1]
    //          Key{Set, Position}  -> [2, 4]
    template <comb::Allocator Allocator> class ObserverStorage
    {
    public:
        explicit ObserverStorage(Allocator& allocator)
            : m_allocator{&allocator}
            , m_observers{allocator}
            , m_lookup{allocator, 32}
        {
        }

        ~ObserverStorage() = default;

        ObserverStorage(const ObserverStorage&) = delete;
        ObserverStorage& operator=(const ObserverStorage&) = delete;
        ObserverStorage(ObserverStorage&&) = default;
        ObserverStorage& operator=(ObserverStorage&&) = default;

        // Registration

        // Registers an observer slot and returns a builder to attach a callback. The slot is
        // already inserted into the lookup map so Trigger() can find it before SetCallback runs.
        template <ObserverTrigger TriggerEvent>
        ObserverBuilder<TriggerEvent, Allocator> Register(World& world, const char* name)
        {
            // Start IDs at 1 so 0 can be used as invalid sentinel
            ObserverId id{static_cast<uint32_t>(m_observers.Size()) + 1};

            TriggerType trigger = GetTriggerType<TriggerEvent>();
            TypeId componentId = GetTriggerComponentId<TriggerEvent>();

            uint32_t index = static_cast<uint32_t>(m_observers.Size());
            m_observers.EmplaceBack(*m_allocator, id, name, trigger, componentId);

            // Add to lookup table using vector index (not ID)
            ObserverKey key = ObserverKey::Of<TriggerEvent>();
            AddToLookup(key, index);

            return ObserverBuilder<TriggerEvent, Allocator>{world, *m_allocator, *this,
                                                            &m_observers[m_observers.Size() - 1]};
        }

        // Lookup

        [[nodiscard]] Observer<Allocator>* GetObserver(ObserverId id)
        {
            // IDs start at 1, so index = id - 1
            if (!id.IsValid() || id.Value() > m_observers.Size())
            {
                return nullptr;
            }
            return &m_observers[id.Value() - 1];
        }

        [[nodiscard]] const Observer<Allocator>* GetObserver(ObserverId id) const
        {
            // IDs start at 1, so index = id - 1
            if (!id.IsValid() || id.Value() > m_observers.Size())
            {
                return nullptr;
            }
            return &m_observers[id.Value() - 1];
        }

        // Linear search by name; intended for debug/tooling, not hot paths.
        [[nodiscard]] Observer<Allocator>* GetObserverByName(const char* name)
        {
            for (size_t i = 0; i < m_observers.Size(); ++i)
            {
                if (std::strcmp(m_observers[i].Name(), name) == 0)
                {
                    return &m_observers[i];
                }
            }
            return nullptr;
        }

        // Triggering

        // Runtime fan-out used by World plumbing; component may be nullptr for OnRemove
        // after the destructor has already run. Implementation lives in observer_storage_impl.h
        // to break the dependency cycle with World.
        void Trigger(TriggerType trigger, TypeId componentId, World& world, Entity entity, const void* component);

        // Compile-time-typed wrapper around Trigger() for call sites that know the event type.
        template <ObserverTrigger TriggerEvent>
        void Trigger(World& world, Entity entity, const typename TriggerEvent::ComponentType* component)
        {
            Trigger(GetTriggerType<TriggerEvent>(), GetTriggerComponentId<TriggerEvent>(), world, entity,
                    static_cast<const void*>(component));
        }

        // State management

        void SetEnabled(ObserverId id, bool enabled)
        {
            Observer<Allocator>* obs = GetObserver(id);
            if (obs != nullptr)
            {
                obs->SetEnabled(enabled);
            }
        }

        [[nodiscard]] bool IsEnabled(ObserverId id) const
        {
            const Observer<Allocator>* obs = GetObserver(id);
            if (obs != nullptr)
            {
                return obs->IsEnabled();
            }
            return false;
        }

        [[nodiscard]] size_t ObserverCount() const noexcept
        {
            return m_observers.Size();
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_observers.IsEmpty();
        }

        [[nodiscard]] bool HasObservers(TriggerType trigger, TypeId componentId) const
        {
            ObserverKey key{trigger, componentId};
            auto* indices = m_lookup.Find(key);
            return indices != nullptr && !indices->IsEmpty();
        }

        template <ObserverTrigger TriggerEvent> [[nodiscard]] bool HasObservers() const
        {
            return HasObservers(GetTriggerType<TriggerEvent>(), GetTriggerComponentId<TriggerEvent>());
        }

    private:
        void AddToLookup(ObserverKey key, uint32_t observerIndex)
        {
            auto* indices = m_lookup.Find(key);
            if (indices == nullptr)
            {
                wax::Vector<uint32_t> newIndices{*m_allocator};
                newIndices.PushBack(observerIndex);
                m_lookup.Insert(key, std::move(newIndices));
            }
            else
            {
                indices->PushBack(observerIndex);
            }
        }

        Allocator* m_allocator;
        wax::Vector<Observer<Allocator>> m_observers;
        wax::HashMap<ObserverKey, wax::Vector<uint32_t>, ObserverKeyHash> m_lookup;
    };
} // namespace queen
