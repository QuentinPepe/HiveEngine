#pragma once

#include <hive/core/assert.h>

#include <comb/allocator_concepts.h>

#include <queen/command/commands.h>
#include <queen/query/query.h>
#include <queen/query/query_term.h>
#include <queen/system/resource_param.h>
#include <queen/system/system.h>
#include <queen/system/system_storage.h>

namespace queen
{
    class World;

    template <comb::Allocator Allocator> class SystemStorage;

    // Fluent builder returned by World::System. The Terms pack lets the builder
    // derive component access automatically; resources/exclusivity must be
    // declared explicitly so the scheduler has complete conflict information.
    template <comb::Allocator Allocator, typename... Terms> class SystemBuilder
    {
    public:
        SystemBuilder(World& world, Allocator& allocator, SystemStorage<Allocator>& storage,
                      SystemDescriptor<Allocator>* descriptor)
            : m_world{&world}
            , m_allocator{&allocator}
            , m_storage{&storage}
            , m_descriptor{descriptor}
        {
            InitializeFromTerms();
        }

        SystemBuilder& After(SystemId id)
        {
            m_descriptor->AddAfter(id);
            return *this;
        }

        // Name-based overload asserts: the target must already be registered
        // because we resolve the SystemId eagerly here.
        SystemBuilder& After(const char* name)
        {
            auto* other = m_storage->GetSystemByName(name);
            hive::Assert(other != nullptr, "After(): system not found");
            m_descriptor->AddAfter(other->Id());
            return *this;
        }

        SystemBuilder& Before(SystemId id)
        {
            m_descriptor->AddBefore(id);
            return *this;
        }

        SystemBuilder& Before(const char* name)
        {
            auto* other = m_storage->GetSystemByName(name);
            hive::Assert(other != nullptr, "Before(): system not found");
            m_descriptor->AddBefore(other->Id());
            return *this;
        }

        // Forces serial execution; the scheduler will not run any other system
        // concurrently with an exclusive one.
        SystemBuilder& Exclusive()
        {
            m_descriptor->SetExecutorMode(SystemExecutor::EXCLUSIVE);
            return *this;
        }

        template <typename T> SystemBuilder& WithResource()
        {
            m_descriptor->Access().template AddResourceRead<T>();
            return *this;
        }

        template <typename T> SystemBuilder& WithResourceMut()
        {
            m_descriptor->Access().template AddResourceWrite<T>();
            return *this;
        }

        template <typename F> SystemId Each(F&& func); // Implementation in system_builder_impl.h

        template <typename F> SystemId EachWithEntity(F&& func); // Implementation in system_builder_impl.h

        // Per-entity callback that also receives the World's Commands collection
        // so handlers can queue deferred mutations without breaking iteration.
        template <typename F> SystemId EachWithCommands(F&& func); // Implementation in system_builder_impl.h

        // Resource-only execution path. Caller must have declared resource access
        // via WithResource/WithResourceMut for the scheduler to honor it.
        template <typename F> SystemId Run(F&& func)
        {
            using FuncType = std::decay_t<F>;

            void* userData = m_allocator->Allocate(sizeof(FuncType), alignof(FuncType));
            new (userData) FuncType{std::forward<F>(func)};

            auto executor = [](World& world, void* data) {
                FuncType* fn = static_cast<FuncType*>(data);
                (*fn)(world);
            };

            auto destructor = [](void* data) {
                FuncType* fn = static_cast<FuncType*>(data);
                fn->~FuncType();
            };

            m_descriptor->SetExecutor(executor, userData, destructor);
            return m_descriptor->Id();
        }

        // Implicitly registers read access on R so the scheduler sees the resource dep.
        template <typename R, typename F> SystemId EachWithRes(F&& func); // Implementation in system_builder_impl.h

        // Implicitly registers write access on R so the scheduler sees the resource dep.
        template <typename R, typename F> SystemId EachWithResMut(F&& func); // Implementation in system_builder_impl.h

        [[nodiscard]] SystemId Id() const noexcept
        {
            return m_descriptor->Id();
        }

    private:
        void InitializeFromTerms()
        {
            if constexpr (sizeof...(Terms) > 0)
            {
                (AddTermToAccess<Terms>(), ...);
                (AddTermToQuery<Terms>(), ...);
            }
        }

        template <typename T> void AddTermToAccess()
        {
            if constexpr (T::access == TermAccess::READ)
            {
                m_descriptor->Access().AddComponentRead(T::typeId);
            }
            else if constexpr (T::access == TermAccess::WRITE)
            {
                m_descriptor->Access().AddComponentWrite(T::typeId);
            }
        }

        template <typename T> void AddTermToQuery()
        {
            m_descriptor->Query().AddTerm(T::ToTerm());
        }

        World* m_world;
        Allocator* m_allocator;
        SystemStorage<Allocator>* m_storage;
        SystemDescriptor<Allocator>* m_descriptor;
    };

    // Empty-Terms specialization: no query, so resource access must be declared
    // manually via WithResource/WithResourceMut.
    template <comb::Allocator Allocator> class SystemBuilder<Allocator>
    {
    public:
        SystemBuilder(World& world, Allocator& allocator, SystemStorage<Allocator>& storage,
                      SystemDescriptor<Allocator>* descriptor)
            : m_world{&world}
            , m_allocator{&allocator}
            , m_storage{&storage}
            , m_descriptor{descriptor}
        {
        }

        SystemBuilder& After(SystemId id)
        {
            m_descriptor->AddAfter(id);
            return *this;
        }

        SystemBuilder& After(const char* name)
        {
            auto* other = m_storage->GetSystemByName(name);
            hive::Assert(other != nullptr, "After(): system not found");
            m_descriptor->AddAfter(other->Id());
            return *this;
        }

        SystemBuilder& Before(SystemId id)
        {
            m_descriptor->AddBefore(id);
            return *this;
        }

        SystemBuilder& Before(const char* name)
        {
            auto* other = m_storage->GetSystemByName(name);
            hive::Assert(other != nullptr, "Before(): system not found");
            m_descriptor->AddBefore(other->Id());
            return *this;
        }

        SystemBuilder& Exclusive()
        {
            m_descriptor->SetExecutorMode(SystemExecutor::EXCLUSIVE);
            return *this;
        }

        template <typename T> SystemBuilder& WithResource()
        {
            m_descriptor->Access().template AddResourceRead<T>();
            return *this;
        }

        template <typename T> SystemBuilder& WithResourceMut()
        {
            m_descriptor->Access().template AddResourceWrite<T>();
            return *this;
        }

        template <typename F> SystemId Run(F&& func)
        {
            using FuncType = std::decay_t<F>;

            void* userData = m_allocator->Allocate(sizeof(FuncType), alignof(FuncType));
            new (userData) FuncType{std::forward<F>(func)};

            auto executor = [](World& world, void* data) {
                FuncType* fn = static_cast<FuncType*>(data);
                (*fn)(world);
            };

            auto destructor = [](void* data) {
                FuncType* fn = static_cast<FuncType*>(data);
                fn->~FuncType();
            };

            m_descriptor->SetExecutor(executor, userData, destructor);
            return m_descriptor->Id();
        }

        // Implicitly registers read access on R so the scheduler sees the resource dep.
        template <typename R, typename F> SystemId RunWithRes(F&& func); // Implementation in system_builder_impl.h

        // Implicitly registers write access on R so the scheduler sees the resource dep.
        template <typename R, typename F> SystemId RunWithResMut(F&& func); // Implementation in system_builder_impl.h

        [[nodiscard]] SystemId Id() const noexcept
        {
            return m_descriptor->Id();
        }

    private:
        World* m_world;
        Allocator* m_allocator;
        SystemStorage<Allocator>* m_storage;
        SystemDescriptor<Allocator>* m_descriptor;
    };
} // namespace queen
