// Fixed-size ring buffer of recent allocation events, used for post-mortem
// analysis after crashes. Size is set by COMB_MEM_DEBUG_HISTORY_SIZE.

#pragma once

#include <comb/debug/mem_debug_config.h>

#if COMB_MEM_DEBUG && COMB_MEM_DEBUG_HISTORY

#include <hive/core/log.h>

#include <comb/combmodule.h>
#include <comb/debug/allocation_info.h>
#include <comb/debug/platform_utils.h>

#include <array>
#include <fstream>
#include <mutex>

namespace comb::debug
{

    enum class HistoryEventType : uint8_t
    {
        ALLOCATION,
        DEALLOCATION
    };

    struct HistoryEntry
    {
        HistoryEventType m_type{HistoryEventType::ALLOCATION};
        void* m_address{nullptr};
        size_t m_size{0};
        uint64_t m_timestamp{0};
        const char* m_tag{nullptr};
        uint32_t m_threadId{0};
        uint32_t m_allocationId{0};
    };

    // Thread-safe circular buffer of recent allocation/deallocation events.
    class AllocationHistory
    {
    public:
        AllocationHistory() = default;
        ~AllocationHistory() = default;

        // Non-copyable, non-movable (contains std::mutex)
        AllocationHistory(const AllocationHistory&) = delete;
        AllocationHistory& operator=(const AllocationHistory&) = delete;
        AllocationHistory(AllocationHistory&&) noexcept = delete;
        AllocationHistory& operator=(AllocationHistory&&) noexcept = delete;

        // Overwrites oldest entry when full.
        void RecordAllocation(const AllocationInfo& info)
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            HistoryEntry entry{};
            entry.m_type = HistoryEventType::ALLOCATION;
            entry.m_address = info.m_address;
            entry.m_size = info.m_size;
            entry.m_timestamp = info.m_timestamp;
            entry.m_tag = info.m_tag;
            entry.m_threadId = info.m_threadId;
            entry.m_allocationId = info.m_allocationId;

            AddEntry(entry);
        }

        void RecordDeallocation(void* address, size_t size)
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            HistoryEntry entry{};
            entry.m_type = HistoryEventType::DEALLOCATION;
            entry.m_address = address;
            entry.m_size = size;
            entry.m_timestamp = GetTimestamp();
            entry.m_threadId = GetThreadId();

            AddEntry(entry);
        }

        [[nodiscard]] size_t GetEntryCount() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_count;
        }

        [[nodiscard]] bool IsEmpty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_count == 0;
        }

        [[nodiscard]] bool IsFull() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_count >= COMB_MEM_DEBUG_HISTORY_SIZE;
        }

        void Clear()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_count = 0;
            m_writeIndex = 0;
        }

        // maxEntries == 0 prints them all.
        void DumpToLog(const char* allocatorName, size_t maxEntries = 100) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_count == 0)
            {
                hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] Allocation history is empty", allocatorName);
                return;
            }

            size_t entriesToPrint = (maxEntries == 0 || maxEntries > m_count) ? m_count : maxEntries;

            hive::LogInfo(comb::LOG_COMB_ROOT,
                          "[MEM_DEBUG] [{}] Recent allocation history ({} / {} entries):", allocatorName,
                          entriesToPrint, m_count);

            // Print in chronological order (oldest first)
            size_t startIndex = m_count < COMB_MEM_DEBUG_HISTORY_SIZE ? 0 : m_writeIndex;
            size_t printed = 0;

            for (size_t i = 0; i < m_count && printed < entriesToPrint; ++i)
            {
                size_t index = (startIndex + i) % COMB_MEM_DEBUG_HISTORY_SIZE;
                const HistoryEntry& entry = m_history[index];

                const char* eventType = (entry.m_type == HistoryEventType::ALLOCATION) ? "ALLOC" : "FREE ";

                hive::LogInfo(comb::LOG_COMB_ROOT, "  [{}] #{}: Address={}, Size={} bytes, Tag={}, Thread={}",
                              eventType, entry.m_allocationId, entry.m_address, entry.m_size,
                              entry.m_tag ? entry.m_tag : "<no tag>", entry.m_threadId);

                printed++;
            }
        }

        bool DumpToFile(const char* filename) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            std::ofstream file(filename);
            if (!file.is_open())
            {
                hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] Failed to open file for history dump: {}", filename);
                return false;
            }

            file << "=== Comb Allocation History Dump ===\n";
            file << "Entries: " << m_count << " / " << COMB_MEM_DEBUG_HISTORY_SIZE << "\n";
            file << "Timestamp: " << GetTimestamp() << " ns\n";
            file << "=====================================\n\n";

            if (m_count == 0)
            {
                file << "(no entries)\n";
                file.close();
                return true;
            }

            // Dump in chronological order
            size_t startIndex = m_count < COMB_MEM_DEBUG_HISTORY_SIZE ? 0 : m_writeIndex;

            for (size_t i = 0; i < m_count; ++i)
            {
                size_t index = (startIndex + i) % COMB_MEM_DEBUG_HISTORY_SIZE;
                const HistoryEntry& entry = m_history[index];

                const char* eventType = (entry.m_type == HistoryEventType::ALLOCATION) ? "ALLOC" : "FREE ";

                file << "[" << eventType << "] "
                     << "#" << entry.m_allocationId << ": "
                     << "Address=" << entry.m_address << ", "
                     << "Size=" << entry.m_size << " bytes, "
                     << "Tag=" << (entry.m_tag ? entry.m_tag : "<no tag>") << ", "
                     << "Thread=" << entry.m_threadId << ", "
                     << "Timestamp=" << entry.m_timestamp << " ns\n";
            }

            file.close();

            hive::LogInfo(comb::LOG_COMB_ROOT, "[MEM_DEBUG] History dumped to file: {}", filename);

            return true;
        }

    private:
        // Assumes mutex held
        void AddEntry(const HistoryEntry& entry)
        {
            m_history[m_writeIndex] = entry;
            m_writeIndex = (m_writeIndex + 1) % COMB_MEM_DEBUG_HISTORY_SIZE;

            if (m_count < COMB_MEM_DEBUG_HISTORY_SIZE)
            {
                m_count++;
            }
        }

        std::array<HistoryEntry, COMB_MEM_DEBUG_HISTORY_SIZE> m_history{};
        size_t m_count{0};
        size_t m_writeIndex{0};
        mutable std::mutex m_mutex;
    };

} // namespace comb::debug

#endif // COMB_MEM_DEBUG && COMB_MEM_DEBUG_HISTORY
