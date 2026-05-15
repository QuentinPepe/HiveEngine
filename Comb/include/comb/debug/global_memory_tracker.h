// Engine-wide aggregation of per-allocator registries. Compiled out when
// COMB_MEM_DEBUG=0 so release builds pay nothing.

#pragma once

#include <comb/debug/mem_debug_config.h>

#if COMB_MEM_DEBUG

#include <hive/core/log.h>

#include <comb/combmodule.h>
#include <comb/debug/allocation_info.h>
#include <comb/debug/allocation_registry.h>
#include <comb/debug/platform_utils.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace comb::debug
{

    struct AllocatorInfo
    {
        const char* m_name;             // Allocator name (e.g., "LinearAllocator")
        AllocationRegistry* m_registry; // Pointer to allocator's registry
        uint64_t m_registrationTime;    // When allocator was registered (nanoseconds)
        bool m_includeInLeakReport;     // Include in explicit live leak reporting
    };

    // Thread-safe Meyer's singleton aggregating allocator registries.
    class GlobalMemoryTracker
    {
    public:
        HIVE_API static GlobalMemoryTracker& GetInstance();

        // Non-copyable, non-movable (singleton)
        GlobalMemoryTracker(const GlobalMemoryTracker&) = delete;
        GlobalMemoryTracker& operator=(const GlobalMemoryTracker&) = delete;
        GlobalMemoryTracker(GlobalMemoryTracker&&) = delete;
        GlobalMemoryTracker& operator=(GlobalMemoryTracker&&) = delete;

        // Allocators call this in their constructor. name must outlive the
        // allocator (string literal expected, not copied).
        void RegisterAllocator(const char* name, AllocationRegistry* registry, bool includeInLeakReport = true)
        {
            hive::Assert(name != nullptr, "Allocator name cannot be null");
            hive::Assert(registry != nullptr, "Registry cannot be null");

            std::lock_guard<std::mutex> lock(m_mutex);

            // Create unique key: name + address (allows multiple allocators with same name)
            std::string key = std::string(name) + "_" + std::to_string(reinterpret_cast<uintptr_t>(registry));

            AllocatorInfo info{};
            info.m_name = name;
            info.m_registry = registry;
            info.m_registrationTime = comb::debug::GetTimestamp();
            info.m_includeInLeakReport = includeInLeakReport;

            m_allocators[key] = info;

            hive::LogTrace(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Registered allocator: {} ({})", name,
                           reinterpret_cast<void*>(registry));
        }

        void UnregisterAllocator(AllocationRegistry* registry)
        {
            hive::Assert(registry != nullptr, "Registry cannot be null");

            std::lock_guard<std::mutex> lock(m_mutex);

            for (auto it = m_allocators.begin(); it != m_allocators.end(); ++it)
            {
                if (it->second.m_registry == registry)
                {
                    hive::LogTrace(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Unregistered allocator: {}", it->second.m_name);
                    m_allocators.erase(it);
                    return;
                }
            }

            hive::LogWarning(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Attempted to unregister unknown allocator: {}",
                             reinterpret_cast<void*>(registry));
        }

        [[nodiscard]] AllocationStats GetGlobalStats() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            AllocationStats globalStats{};

            for (const auto& [key, info] : m_allocators)
            {
                AllocationStats allocStats = info.m_registry->GetStats();

                globalStats.m_totalAllocations += allocStats.m_totalAllocations;
                globalStats.m_totalDeallocations += allocStats.m_totalDeallocations;
                globalStats.m_currentAllocations += allocStats.m_currentAllocations;
                globalStats.m_currentBytesUsed += allocStats.m_currentBytesUsed;
                globalStats.m_totalBytesAllocated += allocStats.m_totalBytesAllocated;
                globalStats.m_overheadBytes += allocStats.m_overheadBytes;

                // Sum peaks across allocators (upper bound — individual peaks may not coincide)
                globalStats.m_peakBytesUsed += allocStats.m_peakBytesUsed;
            }

            return globalStats;
        }

        [[nodiscard]] size_t GetAllocatorCount() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_allocators.size();
        }

        void PrintAllAllocators() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_allocators.empty())
            {
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] No allocators registered");
                return;
            }

            hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] ========== Global Memory Report ==========");
            hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Registered allocators: {}", m_allocators.size());

            size_t totalCurrent = 0;
            size_t totalPeak = 0;

            for (const auto& [key, info] : m_allocators)
            {
                AllocationStats stats = info.m_registry->GetStats();

                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] ---");
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Allocator: {}", info.m_name);
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG]   Current: {} bytes ({} allocations)",
                              stats.m_currentBytesUsed, stats.m_currentAllocations);
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG]   Peak: {} bytes", stats.m_peakBytesUsed);
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG]   Total allocs/deallocs: {} / {}",
                              stats.m_totalAllocations, stats.m_totalDeallocations);

                totalCurrent += stats.m_currentBytesUsed;
                totalPeak = (std::max)(totalPeak, stats.m_peakBytesUsed);
            }

            hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] ---");
            hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] TOTAL Current: {} bytes ({} MB)", totalCurrent,
                          totalCurrent / (1024 * 1024));
            hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] TOTAL Peak: {} bytes ({} MB)", totalPeak,
                          totalPeak / (1024 * 1024));
            hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] ============================================");
        }

        void PrintLeakReport() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            size_t totalLeaks = 0;
            size_t totalLeakedBytes = 0;
            std::vector<const char*> leakyAllocators;

            for (const auto& [key, info] : m_allocators)
            {
                size_t leakCount = info.m_registry->GetAllocationCount();
                if (leakCount > 0)
                {
                    AllocationStats stats = info.m_registry->GetStats();
                    totalLeaks += leakCount;
                    totalLeakedBytes += stats.m_currentBytesUsed;
                    leakyAllocators.push_back(info.m_name);
                }
            }

            if (totalLeaks == 0)
            {
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] ========== Global Leak Report ==========");
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] NO MEMORY LEAKS DETECTED ✓");
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] =========================================");
            }
            else
            {
                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] ========== Global Leak Report ==========");
                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] MEMORY LEAKS DETECTED!");
                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Total leaks: {} allocations", totalLeaks);
                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Total leaked: {} bytes ({} MB)", totalLeakedBytes,
                               totalLeakedBytes / (1024 * 1024));
                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Leaky allocators:");

                for (const char* name : leakyAllocators)
                {
                    hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG]   - {}", name);
                }

                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] =========================================");
            }
        }

        // Call before shutdown while the logging system is still alive.
        void ReportLiveAllocatorLeaks() const
        {
            std::vector<AllocatorInfo> allocators;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                allocators.reserve(m_allocators.size());
                for (const auto& [key, info] : m_allocators)
                {
                    if (info.m_includeInLeakReport)
                    {
                        allocators.push_back(info);
                    }
                }
            }

            size_t totalLeaks = 0;
            size_t totalLeakedBytes = 0;

            for (const AllocatorInfo& info : allocators)
            {
                const size_t leakCount = info.m_registry->GetAllocationCount();
                if (leakCount == 0)
                {
                    continue;
                }

                const AllocationStats stats = info.m_registry->GetStats();
                totalLeaks += leakCount;
                totalLeakedBytes += stats.m_currentBytesUsed;
                info.m_registry->ReportLeaks(info.m_name);
            }

            if (totalLeaks == 0)
            {
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] No live allocator leaks detected");
                return;
            }

            hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Live allocator leaks: {} allocations, {} bytes",
                           totalLeaks, totalLeakedBytes);
        }

        // Exports per-allocator stats plus aggregated totals for external
        // visualization tools.
        [[nodiscard]] std::string ExportToJSON() const;

    private:
        GlobalMemoryTracker() = default;
        ~GlobalMemoryTracker() = default;

        // Caller must already hold m_mutex.
        [[nodiscard]] AllocationStats GetGlobalStatsLocked() const;

        std::unordered_map<std::string, AllocatorInfo> m_allocators;
        mutable std::mutex m_mutex;
    };

    inline void ReportLiveAllocatorLeaks()
    {
        GlobalMemoryTracker::GetInstance().ReportLiveAllocatorLeaks();
    }

} // namespace comb::debug

#endif // COMB_MEM_DEBUG
