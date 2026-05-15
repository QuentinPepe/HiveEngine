#pragma once

#include <hive/core/assert.h>

#include <comb/allocator_concepts.h>

#include <wax/containers/vector.h>

#include <queen/command/command_buffer.h>

#include <drone/worker_context.h>

namespace queen
{
    // Per-worker command-buffer pool used to defer structural mutations during parallel
    // system execution. Each worker writes into its own slot to avoid contention, and
    // FlushAll() applies them in deterministic slot order from the main thread.
    //
    // Slot 0 is reserved for the main thread; slots 1..N map to worker indices reported by
    // drone::WorkerContext, giving O(1) dispatch instead of a thread-id lookup.
    template <comb::Allocator Allocator> class Commands
    {
    public:
        explicit Commands(Allocator& allocator, size_t workerCount = 0)
            : m_allocator{&allocator}
            , m_buffers{allocator}
        {
            // Slot 0 = main thread, slots 1..N = workers
            size_t slotCount = 1 + workerCount;
            m_buffers.Reserve(slotCount);
            for (size_t i = 0; i < slotCount; ++i)
            {
                m_buffers.EmplaceBack(allocator);
            }
        }

        ~Commands() = default;

        Commands(const Commands&) = delete;
        Commands& operator=(const Commands&) = delete;
        Commands(Commands&&) = default;
        Commands& operator=(Commands&&) = default;

        // Returns the command buffer assigned to the calling worker (or slot 0 from the
        // main thread). Indexing through WorkerContext keeps this allocation-free and lock-free.
        [[nodiscard]] CommandBuffer<Allocator>& Get()
        {
            return m_buffers[SlotIndex()];
        }

        [[nodiscard]] const CommandBuffer<Allocator>& Get() const
        {
            return m_buffers[SlotIndex()];
        }

        // Applies every worker's queued commands to the World. Must run on a single thread
        // outside parallel execution; iteration order is deterministic for replay stability.
        void FlushAll(World& world)
        {
            for (size_t i = 0; i < m_buffers.Size(); ++i)
            {
                if (!m_buffers[i].IsEmpty())
                {
                    m_buffers[i].Flush(world);
                }
            }
        }

        void ClearAll()
        {
            for (size_t i = 0; i < m_buffers.Size(); ++i)
            {
                m_buffers[i].Clear();
            }
        }

        [[nodiscard]] size_t BufferCount() const noexcept
        {
            return m_buffers.Size();
        }

        [[nodiscard]] size_t TotalCommandCount() const noexcept
        {
            size_t total = 0;
            for (size_t i = 0; i < m_buffers.Size(); ++i)
            {
                total += m_buffers[i].CommandCount();
            }
            return total;
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            for (size_t i = 0; i < m_buffers.Size(); ++i)
            {
                if (!m_buffers[i].IsEmpty())
                {
                    return false;
                }
            }
            return true;
        }

        template <typename Func> void ForEach(Func&& func)
        {
            for (size_t i = 0; i < m_buffers.Size(); ++i)
            {
                func(m_buffers[i]);
            }
        }

    private:
        [[nodiscard]] static size_t SlotIndex()
        {
            size_t idx = drone::WorkerContext::CurrentWorkerIndex();
            return (idx == drone::WorkerContext::kMainThread) ? 0 : idx + 1;
        }

        Allocator* m_allocator;
        wax::Vector<CommandBuffer<Allocator>> m_buffers;
    };
} // namespace queen
