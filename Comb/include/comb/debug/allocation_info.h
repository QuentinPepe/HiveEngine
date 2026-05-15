// Per-allocation metadata used by the debug registry. Only compiled when
// COMB_MEM_DEBUG=1.
//
// Memory layout with guards:
// ┌──────────────┬─────────────────────┬──────────────┐
// │ GUARD_FRONT  │   User Data (size)  │  GUARD_BACK  │
// │  (4 bytes)   │                     │   (4 bytes)  │
// │ 0xDEADBEEF   │                     │  0xDEADBEEF  │
// └──────────────┴─────────────────────┴──────────────┘

#pragma once

#include <comb/debug/mem_debug_config.h>

#if COMB_MEM_DEBUG

#include <cstddef>
#include <cstdint>

namespace comb::debug
{

    struct AllocationInfo
    {
        // Core Information (Always Present)

        void* m_address{nullptr};
        size_t m_size{0};
        size_t m_alignment{0};

        // Monotonic ns timestamp from GetTimestamp(); orders allocations.
        uint64_t m_timestamp{0};

        // Assumed to be a string literal; not owned.
        const char* m_tag{nullptr};

        uint32_t m_allocationId{0};
        uint32_t m_threadId{0};

        // Optional: Callstack (Platform-Specific)

#if COMB_MEM_DEBUG_CALLSTACKS
        void* callstack[maxCallstackDepth]{};
        uint32_t callstackDepth{0};
#endif

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_address != nullptr && m_size > 0;
        }

        // Raw allocator pointer (before the front guard).
        [[nodiscard]] void* GetRawPointer() const noexcept
        {
            return static_cast<std::byte*>(m_address) - guardSize;
        }

        // Includes guard bytes.
        [[nodiscard]] constexpr size_t GetTotalSize() const noexcept
        {
            return m_size + totalGuardSize;
        }

        // memcpy-based so misaligned guard addresses are safe.
        [[nodiscard]] uint32_t ReadGuardFront() const noexcept
        {
            return ReadGuard(static_cast<std::byte*>(m_address) - guardSize);
        }

        [[nodiscard]] uint32_t ReadGuardBack() const noexcept
        {
            return ReadGuard(static_cast<std::byte*>(m_address) + m_size);
        }

        [[nodiscard]] bool CheckGuards() const noexcept
        {
            return ReadGuardFront() == guardMagic && ReadGuardBack() == guardMagic;
        }

        [[nodiscard]] const char* GetTagOrDefault(const char* defaultTag = "<no tag>") const noexcept
        {
            return m_tag ? m_tag : defaultTag;
        }
    };

    struct AllocationStats
    {
        size_t m_totalAllocations{0};   // Total number of Allocate() calls
        size_t m_totalDeallocations{0}; // Total number of Deallocate() calls
        size_t m_currentAllocations{0}; // Active allocations (allocs - deallocs)

        size_t m_currentBytesUsed{0};    // Current memory used (user bytes)
        size_t m_peakBytesUsed{0};       // Peak memory used (high water mark)
        size_t m_totalBytesAllocated{0}; // Lifetime total bytes allocated

        size_t m_overheadBytes{0}; // Debug overhead (guards, metadata)

        [[nodiscard]] constexpr size_t GetLeakCount() const noexcept
        {
            return m_totalAllocations - m_totalDeallocations;
        }

        [[nodiscard]] constexpr float GetOverheadPercentage() const noexcept
        {
            if (m_currentBytesUsed == 0)
                return 0.0f;
            return (static_cast<float>(m_overheadBytes) / static_cast<float>(m_currentBytesUsed + m_overheadBytes)) *
                   100.0f;
        }

        // Heuristic in [0,1]; not a real fragmentation measure, just the
        // dealloc/alloc ratio used as a rough liveness indicator.
        [[nodiscard]] constexpr float GetFragmentationRatio() const noexcept
        {
            if (m_totalAllocations == 0)
                return 0.0f;

            // Simple heuristic: more deallocations relative to allocations = more fragmentation
            float deallocRatio = static_cast<float>(m_totalDeallocations) / static_cast<float>(m_totalAllocations);

            // Clamp to [0, 1]
            return deallocRatio > 1.0f ? 1.0f : deallocRatio;
        }
    };

} // namespace comb::debug

#endif // COMB_MEM_DEBUG
