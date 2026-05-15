#pragma once

#include <hive/core/assert.h>
#include <hive/profiling/profiler.h>

#include <comb/allocator_concepts.h>

#include <drone/counter.h>
#include <drone/job_submitter.h>
#include <drone/job_types.h>

#include <queen/core/tick.h>
#include <queen/scheduler/dependency_graph.h>
#include <queen/scheduler/scheduler_scratch.h>
#include <queen/system/system_storage.h>

#include <atomic>
#include <cstring>

namespace queen
{
    class World;

    // Submits ready systems to a Drone job pool, decrementing per-node atomic
    // counters as dependencies complete. Roots fan out first; the frame ends when
    // every node's counter reaches zero. Command buffers flush once per frame.
    template <comb::Allocator Allocator> class ParallelScheduler
    {
    public:
        explicit ParallelScheduler(Allocator& allocator, drone::JobSubmitter jobs)
            : m_graph{allocator}
            , m_jobs{jobs}
            , m_remaining{nullptr}
            , m_remainingCount{0}
            , m_allocator{&allocator}
        {
        }

        ~ParallelScheduler()
        {
            if (m_remaining != nullptr)
            {
                for (size_t i = 0; i < m_remainingCount; ++i)
                {
                    m_remaining[i].~atomic<uint16_t>();
                }
                m_allocator->Deallocate(m_remaining);
            }
        }

        ParallelScheduler(const ParallelScheduler&) = delete;
        ParallelScheduler& operator=(const ParallelScheduler&) = delete;
        ParallelScheduler(ParallelScheduler&&) = delete;
        ParallelScheduler& operator=(ParallelScheduler&&) = delete;

        void Build(const SystemStorage<Allocator>& storage)
        {
            m_graph.Build(storage);
            ReallocateRemaining(m_graph.NodeCount());
        }

        void Invalidate() noexcept
        {
            m_graph.MarkDirty();
        }

        [[nodiscard]] bool NeedsRebuild() const noexcept
        {
            return m_graph.IsDirty();
        }

        void RunAll(World& world, SystemStorage<Allocator>& storage); // Implementation in parallel_scheduler_impl.h

        [[nodiscard]] const DependencyGraph<Allocator>& Graph() const noexcept
        {
            return m_graph;
        }

        [[nodiscard]] DependencyGraph<Allocator>& Graph() noexcept
        {
            return m_graph;
        }

        [[nodiscard]] drone::JobSubmitter& Jobs() noexcept
        {
            return m_jobs;
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
        void ReallocateRemaining(size_t count)
        {
            // Clean up old array
            if (m_remaining != nullptr)
            {
                for (size_t i = 0; i < m_remainingCount; ++i)
                {
                    m_remaining[i].~atomic<uint16_t>();
                }
                m_allocator->Deallocate(m_remaining);
                m_remaining = nullptr;
            }

            m_remainingCount = count;
            if (count == 0)
            {
                return;
            }

            void* mem = m_allocator->Allocate(sizeof(std::atomic<uint16_t>) * count, alignof(std::atomic<uint16_t>));
            m_remaining = static_cast<std::atomic<uint16_t>*>(mem);

            for (size_t i = 0; i < count; ++i)
            {
                new (&m_remaining[i]) std::atomic<uint16_t>{0};
            }
        }

        void ResetRemainingCounts()
        {
            for (size_t i = 0; i < m_remainingCount; ++i)
            {
                const SystemNode* node = m_graph.GetNode(static_cast<uint32_t>(i));
                if (node != nullptr)
                {
                    m_remaining[i].store(node->DependencyCount(), std::memory_order_relaxed);
                }
            }
        }

        void SubmitSystemTask(uint32_t nodeIndex, World& world, SystemStorage<Allocator>& storage, Tick tick,
                              drone::Counter& counter)
        {
            struct TaskData
            {
                ParallelScheduler* m_scheduler;
                World* m_world;
                SystemStorage<Allocator>* m_storage;
                uint32_t m_nodeIndex;
                Tick m_tick;
                drone::Counter* m_counter;
            };
            static_assert(sizeof(TaskData) <= sizeof(SchedulerTaskSlot));

            size_t slot = AllocateSchedulerTaskSlot();
            auto& td = *reinterpret_cast<TaskData*>(&GetSchedulerTaskBuffer()[slot]);

            td.m_scheduler = this;
            td.m_world = &world;
            td.m_storage = &storage;
            td.m_nodeIndex = nodeIndex;
            td.m_tick = tick;
            td.m_counter = &counter;

            drone::JobDecl job;
            job.m_func = [](void* data) {
                auto* td = static_cast<TaskData*>(data);
                td->m_scheduler->ExecuteSystem(td->m_nodeIndex, *td->m_world, *td->m_storage, td->m_tick,
                                               *td->m_counter);
            };
            job.m_userData = &td;
            job.m_priority = drone::Priority::HIGH;

            m_jobs.SubmitDetached(job);
        }

        void ExecuteSystem(uint32_t nodeIndex, World& world, SystemStorage<Allocator>& storage, Tick tick,
                           drone::Counter& counter)
        {
            SystemNode* node = m_graph.GetNode(nodeIndex);
            if (node == nullptr)
            {
                counter.Decrement();
                return;
            }

            node->SetState(SystemState::RUNNING);

            SystemDescriptor<Allocator>* system = storage.GetSystemByIndex(nodeIndex);
            if (system != nullptr)
            {
                HIVE_PROFILE_SCOPE_N("ExecuteSystem");
                HIVE_PROFILE_ZONE_NAME(system->Name(), std::strlen(system->Name()));
                system->Execute(world, tick);
            }

            node->SetState(SystemState::COMPLETE);

            const auto* dependents = m_graph.GetDependents(nodeIndex);
            if (dependents != nullptr)
            {
                for (size_t i = 0; i < dependents->Size(); ++i)
                {
                    uint32_t depIdx = (*dependents)[i];
                    uint16_t prev = m_remaining[depIdx].fetch_sub(1, std::memory_order_acq_rel);
                    if (prev == 1)
                    {
                        SubmitSystemTask(depIdx, world, storage, tick, counter);
                    }
                }
            }

            counter.Decrement();
        }

        DependencyGraph<Allocator> m_graph;
        drone::JobSubmitter m_jobs;
        std::atomic<uint16_t>* m_remaining;
        size_t m_remainingCount;
        Allocator* m_allocator;
    };
} // namespace queen
