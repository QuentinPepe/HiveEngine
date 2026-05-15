// Per-allocator hash table tracking active allocations for leak/double-free
// detection. std::unordered_map is acceptable because this only compiles in
// debug builds (COMB_MEM_DEBUG=1).

#pragma once

#include <comb/debug/mem_debug_config.h>

#if COMB_MEM_DEBUG

#include <hive/core/log.h>

#include <comb/combmodule.h>
#include <comb/debug/allocation_info.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace comb::debug
{

    // Thread-safe hash table tracking active allocations for a single allocator.
    class AllocationRegistry
    {
    public:
        AllocationRegistry() = default;
        ~AllocationRegistry() = default;

        // Non-copyable, non-movable (contains std::atomic)
        AllocationRegistry(const AllocationRegistry&) = delete;
        AllocationRegistry& operator=(const AllocationRegistry&) = delete;
        AllocationRegistry(AllocationRegistry&&) noexcept = delete;
        AllocationRegistry& operator=(AllocationRegistry&&) noexcept = delete;

        // Asserts on duplicate address (allocator returned the same pointer twice).
        void RegisterAllocation(const AllocationInfo& info)
        {
            hive::Assert(info.IsValid(), "Invalid AllocationInfo");

            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_allocations.find(info.m_address) != m_allocations.end())
            {
                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Double allocation detected! Address: {}, Size: {}",
                               info.m_address, info.m_size);
                hive::Assert(false, "Double allocation detected (same address allocated twice)");
                return;
            }

            m_allocations[info.m_address] = info;

            m_stats.m_totalAllocations++;
            m_stats.m_currentAllocations++;
            m_stats.m_currentBytesUsed += info.m_size;
            m_stats.m_totalBytesAllocated += info.m_size;
            m_stats.m_overheadBytes += info.GetTotalSize() - info.m_size;

            if (m_stats.m_currentBytesUsed > m_stats.m_peakBytesUsed)
            {
                m_stats.m_peakBytesUsed = m_stats.m_currentBytesUsed;
            }
        }

        // Asserts on double-free / invalid pointer (not present in registry).
        AllocationInfo* UnregisterAllocation(void* address)
        {
            hive::Assert(address != nullptr, "Cannot unregister nullptr");

            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_allocations.find(address);
            if (it == m_allocations.end())
            {
                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Double-free or invalid pointer detected! Address: {}",
                               address);
                hive::Assert(false, "Double-free or invalid pointer (not found in registry)");
                return nullptr;
            }

            m_stats.m_totalDeallocations++;
            m_stats.m_currentAllocations--;
            m_stats.m_currentBytesUsed -= it->second.m_size;
            m_stats.m_overheadBytes -= (it->second.GetTotalSize() - it->second.m_size);

            m_allocations.erase(it);

            return nullptr; // Allocation no longer valid
        }

        std::optional<AllocationInfo> FindAllocation(void* address) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_allocations.find(address);
            if (it != m_allocations.end())
                return it->second;

            return std::nullopt;
        }

        [[nodiscard]] AllocationStats GetStats() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_stats;
        }

        [[nodiscard]] size_t GetAllocationCount() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_allocations.size();
        }

        [[nodiscard]] bool IsEmpty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_allocations.empty();
        }

        [[nodiscard]] uint32_t GetNextAllocationId()
        {
            return m_nextAllocationId.fetch_add(1, std::memory_order_relaxed);
        }

        // Call from allocator destructor to dump unfreed allocations.
        void ReportLeaks(const char* allocatorName) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_allocations.empty())
            {
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] No memory leaks detected ✓", allocatorName);
                return;
            }

            hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] MEMORY LEAKS DETECTED: {} allocations not freed",
                           allocatorName, m_allocations.size());

            size_t totalLeaked = 0;
            for (const auto& [addr, info] : m_allocations)
            {
                totalLeaked += info.m_size;

                hive::LogError(comb::LOG_COMB_ROOT, "  LEAK #{}: Address={}, Size={} bytes, Tag={}, Thread={}",
                               info.m_allocationId, addr, info.m_size, info.GetTagOrDefault(), info.m_threadId);

#if COMB_MEM_DEBUG_CALLSTACKS
                if (info.callstackDepth > 0)
                {
                    hive::LogError(comb::LOG_COMB_ROOT, "    Callstack:");
                    PrintCallstack(info.callstack, info.callstackDepth);
                }
#endif
            }

            hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] Total leaked: {} bytes in {} allocations",
                           allocatorName, totalLeaked, m_allocations.size());
        }

        void PrintStats(const char* allocatorName) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] Statistics:", allocatorName);
            hive::LogInfo(comb::LOG_COMB_ROOT, "  Total allocations:   {}", m_stats.m_totalAllocations);
            hive::LogInfo(comb::LOG_COMB_ROOT, "  Total deallocations: {}", m_stats.m_totalDeallocations);
            hive::LogInfo(comb::LOG_COMB_ROOT, "  Active allocations:  {}", m_stats.m_currentAllocations);
            hive::LogInfo(comb::LOG_COMB_ROOT, "  Current memory used: {} bytes", m_stats.m_currentBytesUsed);
            hive::LogInfo(comb::LOG_COMB_ROOT, "  Peak memory used:    {} bytes", m_stats.m_peakBytesUsed);
            hive::LogInfo(comb::LOG_COMB_ROOT, "  Debug overhead:      {} bytes ({:.1f}%)", m_stats.m_overheadBytes,
                          m_stats.GetOverheadPercentage());
            hive::LogInfo(comb::LOG_COMB_ROOT, "  Fragmentation ratio: {:.2f}", m_stats.GetFragmentationRatio());
        }

        // Does NOT free memory — only clears tracking. Tests/teardown only,
        // when memory is freed externally.
        void Clear()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_allocations.clear();
            m_stats = AllocationStats{};
            m_nextAllocationId.store(1, std::memory_order_relaxed);
        }

        // Used by marker-based allocators (Linear, Stack) when rolling back
        // to a marker: drops all entries with address >= startAddress.
        void ClearAllocationsFrom(void* startAddress)
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            const uintptr_t start = reinterpret_cast<uintptr_t>(startAddress);

            for (auto it = m_allocations.begin(); it != m_allocations.end();)
            {
                const uintptr_t addr = reinterpret_cast<uintptr_t>(it->first);

                if (addr >= start)
                {
                    m_stats.m_currentAllocations--;
                    m_stats.m_currentBytesUsed -= it->second.m_size;
                    m_stats.m_overheadBytes -= (it->second.GetTotalSize() - it->second.m_size);

                    it = m_allocations.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // Used by marker-based allocators to recompute GetUsedMemory() after
        // rolling back to a marker. Excludes debug overhead.
        [[nodiscard]] size_t CalculateBytesUsedUpTo(void* endAddress) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            const uintptr_t end = reinterpret_cast<uintptr_t>(endAddress);
            size_t total = 0;

            for (const auto& [addr, info] : m_allocations)
            {
                if (reinterpret_cast<uintptr_t>(addr) < end)
                {
                    total += info.m_size;
                }
            }

            return total;
        }

        // Used by marker-based allocators to recompute guard-byte overhead
        // after rolling back to a marker.
        [[nodiscard]] size_t CountAllocationsUpTo(void* endAddress) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            const uintptr_t end = reinterpret_cast<uintptr_t>(endAddress);
            size_t count = 0;

            for (const auto& [addr, info] : m_allocations)
            {
                if (reinterpret_cast<uintptr_t>(addr) < end)
                {
                    ++count;
                }
            }

            return count;
        }

    private:
        std::unordered_map<void*, AllocationInfo> m_allocations;
        AllocationStats m_stats{};
        std::atomic<uint32_t> m_nextAllocationId{1};
        mutable std::mutex m_mutex;
    };

} // namespace comb::debug

#endif // COMB_MEM_DEBUG
