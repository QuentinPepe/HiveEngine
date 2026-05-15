#pragma once

#include <comb/allocator_concepts.h>

#include <queen/core/entity.h>
#include <queen/observer/observer.h>
#include <queen/observer/observer_event.h>

#include <type_traits>
#include <utility>

namespace queen
{
    class World;

    template <comb::Allocator Allocator> class ObserverStorage;

    // Fluent builder that finalises an Observer slot already registered with ObserverStorage.
    // Callbacks are type-erased into a stable thunk + heap-allocated closure so the storage
    // does not need to know the lambda's concrete type.
    template <ObserverTrigger TriggerEvent, comb::Allocator Allocator> class ObserverBuilder
    {
    public:
        using ComponentType = typename TriggerEvent::ComponentType;

        ObserverBuilder(World& world, Allocator& allocator, ObserverStorage<Allocator>& storage,
                        Observer<Allocator>* observer)
            : m_world{&world}
            , m_allocator{&allocator}
            , m_storage{&storage}
            , m_observer{observer}
        {
        }

        // Restricts firing to entities that also carry component T.
        template <typename T> ObserverBuilder& With()
        {
            m_observer->AddFilter(TypeIdOf<T>());
            return *this;
        }

        // Attaches a callback of signature void(Entity, const Component&). The component
        // pointer can be null on OnRemove after destruction; the thunk skips the call in that case.
        template <typename F> ObserverId Each(F&& func)
        {
            using FuncType = std::decay_t<F>;

            void* userData = m_allocator->Allocate(sizeof(FuncType), alignof(FuncType));
            new (userData) FuncType{std::forward<F>(func)};

            auto callback = [](World& world, Entity entity, const void* component, void* data) {
                (void)world;
                FuncType* fn = static_cast<FuncType*>(data);
                const ComponentType* comp = static_cast<const ComponentType*>(component);
                if (comp != nullptr)
                {
                    (*fn)(entity, *comp);
                }
            };

            auto destructor = [](void* data) {
                FuncType* fn = static_cast<FuncType*>(data);
                fn->~FuncType();
            };

            m_observer->SetCallback(callback, userData, destructor);
            return m_observer->Id();
        }

        // Entity-only variant for OnRemove and other cases where the component has already
        // been destroyed by the time the callback fires.
        template <typename F> ObserverId EachEntity(F&& func)
        {
            using FuncType = std::decay_t<F>;

            void* userData = m_allocator->Allocate(sizeof(FuncType), alignof(FuncType));
            new (userData) FuncType{std::forward<F>(func)};

            auto callback = [](World& world, Entity entity, const void* component, void* data) {
                (void)world;
                (void)component;
                FuncType* fn = static_cast<FuncType*>(data);
                (*fn)(entity);
            };

            auto destructor = [](void* data) {
                FuncType* fn = static_cast<FuncType*>(data);
                fn->~FuncType();
            };

            m_observer->SetCallback(callback, userData, destructor);
            return m_observer->Id();
        }

        // World-aware variant for callbacks that need to query other components reactively.
        template <typename F> ObserverId EachWithWorld(F&& func)
        {
            using FuncType = std::decay_t<F>;

            void* userData = m_allocator->Allocate(sizeof(FuncType), alignof(FuncType));
            new (userData) FuncType{std::forward<F>(func)};

            auto callback = [](World& world, Entity entity, const void* component, void* data) {
                FuncType* fn = static_cast<FuncType*>(data);
                const ComponentType* comp = static_cast<const ComponentType*>(component);
                if (comp != nullptr)
                {
                    (*fn)(world, entity, *comp);
                }
            };

            auto destructor = [](void* data) {
                FuncType* fn = static_cast<FuncType*>(data);
                fn->~FuncType();
            };

            m_observer->SetCallback(callback, userData, destructor);
            return m_observer->Id();
        }

        [[nodiscard]] ObserverId Id() const noexcept
        {
            return m_observer->Id();
        }

    private:
        World* m_world;
        Allocator* m_allocator;
        ObserverStorage<Allocator>* m_storage;
        Observer<Allocator>* m_observer;
    };
} // namespace queen
