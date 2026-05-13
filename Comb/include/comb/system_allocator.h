#pragma once

#include <hive/hive_config.h>

#include <comb/allocator_concepts.h>

#include <atomic>
#include <cstddef>

namespace comb
{
    /**
     * Allocator backed directly by VirtualAlloc/mmap.
     *
     * No pre-allocation, no power-of-2 rounding, no per-block limit — each
     * Allocate call reserves pages from the OS, each Deallocate returns them.
     * Intended for transient, very large buffers (asset import, decompression)
     * where the overhead of a buddy/pool allocator is prohibitive.
     *
     * Trade-offs vs. ModuleAllocator:
     *   + Allocation size limited only by available RAM/address space.
     *   + No memory reserved when idle.
     *   - Per-allocation syscall cost (irrelevant for huge buffers).
     *   - No fine-grained placement control.
     *
     * Thread-safe: stats use atomics, OS allocators are inherently thread-safe.
     */
    class HIVE_API SystemAllocator
    {
    public:
        explicit SystemAllocator(const char* debugName = "SystemAllocator") noexcept
            : m_debugName{debugName}
        {
        }

        ~SystemAllocator() = default;

        SystemAllocator(const SystemAllocator&) = delete;
        SystemAllocator& operator=(const SystemAllocator&) = delete;
        SystemAllocator(SystemAllocator&&) = delete;
        SystemAllocator& operator=(SystemAllocator&&) = delete;

        [[nodiscard]] void* Allocate(size_t size, size_t alignment, const char* tag = nullptr);
        void Deallocate(void* ptr);

        [[nodiscard]] size_t GetUsedMemory() const noexcept
        {
            return m_usedBytes.load(std::memory_order_relaxed);
        }

        [[nodiscard]] size_t GetTotalMemory() const noexcept
        {
            return m_usedBytes.load(std::memory_order_relaxed);
        }

        [[nodiscard]] const char* GetName() const noexcept
        {
            return m_debugName;
        }

        [[nodiscard]] size_t GetLiveAllocationCount() const noexcept
        {
            return m_liveCount.load(std::memory_order_relaxed);
        }

    private:
        const char* m_debugName;
        std::atomic<size_t> m_usedBytes{0};
        std::atomic<size_t> m_liveCount{0};
    };

    static_assert(Allocator<SystemAllocator>, "SystemAllocator must satisfy Allocator concept");
} // namespace comb
