#pragma once

#include <comb/allocator_concepts.h>

#include <queen/core/tick.h>
#include <queen/scheduler/dependency_graph.h>
#include <queen/system/system_storage.h>

namespace queen
{
    class World;

    // Single-threaded scheduler that walks the dependency graph in topological
    // order. Used as the deterministic fallback for tests, debugging, and the
    // default World::Update path when parallelism is not requested.
    template <comb::Allocator Allocator> class Scheduler
    {
    public:
        explicit Scheduler(Allocator& allocator)
            : m_graph{allocator}
        {
        }

        Scheduler(const Scheduler&) = delete;
        Scheduler& operator=(const Scheduler&) = delete;
        Scheduler(Scheduler&&) noexcept = default;
        Scheduler& operator=(Scheduler&&) noexcept = default;

        void Build(const SystemStorage<Allocator>& storage)
        {
            m_graph.Build(storage);
        }

        void Invalidate() noexcept
        {
            m_graph.MarkDirty();
        }

        [[nodiscard]] bool NeedsRebuild() const noexcept
        {
            return m_graph.IsDirty();
        }

        // Flushes deferred commands and updates each system's last-run tick after
        // the topological walk completes.
        void RunAll(World& world, SystemStorage<Allocator>& storage); // Implementation in scheduler_impl.h

        [[nodiscard]] const DependencyGraph<Allocator>& Graph() const noexcept
        {
            return m_graph;
        }

        [[nodiscard]] DependencyGraph<Allocator>& Graph() noexcept
        {
            return m_graph;
        }

        [[nodiscard]] const wax::Vector<uint32_t>& ExecutionOrder() const noexcept
        {
            return m_graph.ExecutionOrder();
        }

        [[nodiscard]] bool HasCycle() const noexcept
        {
            return m_graph.HasCycle();
        }

    private:
        DependencyGraph<Allocator> m_graph;
    };
} // namespace queen
