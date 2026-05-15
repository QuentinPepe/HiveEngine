#include <comb/linear_allocator.h>

#include <hive/core/assert.h>
#include <hive/core/log.h>

#include <comb/combmodule.h>
#include <comb/platform.h>
#include <comb/precomp.h>
#include <comb/utils.h>

#include <cstring> // For memset

namespace comb
{
    LinearAllocator::LinearAllocator(size_t capacity)
        : m_base{AllocatePages(capacity)}
        , m_current{m_base}
        , m_capacity{capacity}
    {
        hive::Assert(m_base != nullptr, "Failed to allocate memory for LinearAllocator");

#if COMB_MEM_DEBUG
        m_registry = std::make_unique<debug::AllocationRegistry>();
        m_history = std::make_unique<debug::AllocationHistory>();
        m_releaseCurrent = m_base;

        debug::GlobalMemoryTracker::GetInstance().RegisterAllocator(GetName(), m_registry.get(), false);
#endif
    }

    LinearAllocator::~LinearAllocator()
    {
#if COMB_MEM_DEBUG
        if (m_registry != nullptr)
        {
            // No leak report: LinearAllocator never frees individually, so live allocations at dtor are expected.
            debug::GlobalMemoryTracker::GetInstance().UnregisterAllocator(m_registry.get());
        }
#endif

        if (m_base != nullptr)
        {
            FreePages(m_base, m_capacity);
            m_base = nullptr;
            m_current = nullptr;
        }
    }

    LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
        : m_base{other.m_base}
        , m_current{other.m_current}
        , m_capacity{other.m_capacity}
#if COMB_MEM_DEBUG
        , m_registry{std::move(other.m_registry)}
        , m_history{std::move(other.m_history)}
        , m_releaseCurrent{other.m_releaseCurrent}
#endif
    {
        // unique_ptr move keeps registry identity, so the global tracker stays valid.
        other.m_base = nullptr;
        other.m_current = nullptr;
        other.m_capacity = 0;
#if COMB_MEM_DEBUG
        other.m_releaseCurrent = nullptr;
#endif
    }

    LinearAllocator& LinearAllocator::operator=(LinearAllocator&& other) noexcept
    {
        if (this != &other)
        {
#if COMB_MEM_DEBUG
            if (m_registry != nullptr)
            {
                if constexpr (debug::kLeakDetectionEnabled)
                {
                    m_registry->ReportLeaks(GetName());
                }
                debug::GlobalMemoryTracker::GetInstance().UnregisterAllocator(m_registry.get());
            }
#endif

            if (m_base != nullptr)
            {
                FreePages(m_base, m_capacity);
            }

            m_base = other.m_base;
            m_current = other.m_current;
            m_capacity = other.m_capacity;

#if COMB_MEM_DEBUG
            m_registry = std::move(other.m_registry);
            m_history = std::move(other.m_history);
            m_releaseCurrent = other.m_releaseCurrent;
#endif

            other.m_base = nullptr;
            other.m_current = nullptr;
            other.m_capacity = 0;
#if COMB_MEM_DEBUG
            other.m_releaseCurrent = nullptr;
#endif
        }

        return *this;
    }

    void* LinearAllocator::Allocate(size_t size, size_t alignment, const char* tag)
    {
#if COMB_MEM_DEBUG
        void* result = AllocateDebug(size, alignment, tag);
#else
        (void)tag;

        hive::Assert(IsPowerOfTwo(alignment), "Alignment must be a power of 2");
        hive::Assert(size > 0, "Cannot allocate 0 bytes");

        const uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_current);
        const uintptr_t aligned_addr = AlignUp(currentAddr, alignment);
        const size_t padding = aligned_addr - currentAddr;
        const size_t required = padding + size;
        const size_t used = currentAddr - reinterpret_cast<uintptr_t>(m_base);
        const size_t remaining = m_capacity - used;

        if (required > remaining)
        {
            return nullptr;
        }

        void* result = reinterpret_cast<void*>(aligned_addr);
        m_current = reinterpret_cast<void*>(aligned_addr + size);
#endif

        return result;
    }

    void LinearAllocator::Deallocate(void* ptr)
    {
#if COMB_MEM_DEBUG
        DeallocateDebug(ptr);
#else
        (void)ptr;
#endif
    }

    void LinearAllocator::Reset()
    {
        m_current = m_base;

#if COMB_MEM_DEBUG
        m_releaseCurrent = m_base;

        if (m_registry != nullptr)
        {
            m_registry->Clear();
        }
#endif
    }

    void* LinearAllocator::GetMarker() const noexcept
    {
        return m_current;
    }

    void LinearAllocator::ResetToMarker(void* marker)
    {
        const uintptr_t markerAddr = reinterpret_cast<uintptr_t>(marker);
        const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_base);
        const uintptr_t endAddr = baseAddr + m_capacity;

        hive::Assert(markerAddr >= baseAddr && markerAddr <= endAddr, "Marker is outside allocator memory range");

        m_current = marker;

#if COMB_MEM_DEBUG
        // Rebuild m_releaseCurrent from registered allocations so GetUsedMemory() reports
        // the guard-free offset the marker would have had in release mode.
        if (m_registry != nullptr)
        {
            size_t releaseOffset = m_registry->CalculateBytesUsedUpTo(marker);
            m_releaseCurrent = static_cast<std::byte*>(m_base) + releaseOffset;

            m_registry->ClearAllocationsFrom(marker);
        }
#endif
    }

    size_t LinearAllocator::GetUsedMemory() const noexcept
    {
#if COMB_MEM_DEBUG
        // Virtual release offset excludes guard bytes — keeps stats consistent across debug/release.
        return reinterpret_cast<uintptr_t>(m_releaseCurrent) - reinterpret_cast<uintptr_t>(m_base);
#else
        return reinterpret_cast<uintptr_t>(m_current) - reinterpret_cast<uintptr_t>(m_base);
#endif
    }

    size_t LinearAllocator::GetTotalMemory() const noexcept
    {
        return m_capacity;
    }

    const char* LinearAllocator::GetName() const noexcept
    {
        return "LinearAllocator";
    }

#if COMB_MEM_DEBUG
    // Debug Implementation (Only compiled when COMB_MEM_DEBUG=1)

    void* LinearAllocator::AllocateDebug(size_t size, size_t alignment, const char* tag)
    {
        hive::Assert(IsPowerOfTwo(alignment), "Alignment must be a power of 2");
        hive::Assert(size > 0, "Cannot allocate 0 bytes");

        const size_t guardSize = sizeof(uint32_t);
        const size_t totalSize = size + 2 * guardSize;

        // Align the USER pointer (after front guard), not the raw pointer — alignment contract is on user data.
        // Layout: [GUARD_FRONT (4B)][user data (aligned)][GUARD_BACK (4B)]
        const uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_current);
        const uintptr_t userAddrUnaligned = currentAddr + guardSize;
        const uintptr_t userAddrAligned = AlignUp(userAddrUnaligned, alignment);
        const uintptr_t rawAddr = userAddrAligned - guardSize;

        const size_t padding = rawAddr - currentAddr;
        const size_t required = padding + totalSize;
        const size_t used = currentAddr - reinterpret_cast<uintptr_t>(m_base);
        const size_t remaining = m_capacity - used;

        if (required > remaining)
        {
            hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] Allocation failed: size={}, alignment={}, tag={}",
                           GetName(), size, alignment, (tag != nullptr) ? tag : "<no tag>");
            return nullptr;
        }

        void* rawPtr = reinterpret_cast<void*>(rawAddr);
        m_current = reinterpret_cast<void*>(rawAddr + totalSize);

        // Advance virtual release pointer as if we were in release mode (no guard bytes).
        const uintptr_t releaseAddr = reinterpret_cast<uintptr_t>(m_releaseCurrent);
        const uintptr_t releaseAligned = AlignUp(releaseAddr, alignment);
        m_releaseCurrent = reinterpret_cast<void*>(releaseAligned + size);

        debug::WriteGuard(rawPtr);

        void* userPtr = reinterpret_cast<void*>(userAddrAligned);

        debug::WriteGuard(static_cast<std::byte*>(userPtr) + size);

        if constexpr (debug::kMemDebugEnabled)
        {
            std::memset(userPtr, debug::allocatedMemoryPattern, size);
        }

        debug::AllocationInfo info{};
        info.m_address = userPtr;
        info.m_size = size;
        info.m_alignment = alignment;
        info.m_timestamp = debug::GetTimestamp();
        info.m_tag = tag;
        info.m_allocationId = m_registry->GetNextAllocationId();
        info.m_threadId = debug::GetThreadId();

#if COMB_MEM_DEBUG_CALLSTACKS
        debug::CaptureCallstack(info.callstack, info.callstackDepth);
#endif

        m_registry->RegisterAllocation(info);

#if COMB_MEM_DEBUG_HISTORY
        m_history->RecordAllocation(info);
#endif

        return userPtr;
    }

    void LinearAllocator::DeallocateDebug(void* ptr)
    {
        // LinearAllocator never frees individually — this path only updates debug tracking.
        if (ptr == nullptr)
        {
            return;
        }

        auto infoOpt = m_registry->FindAllocation(ptr);
        if (!infoOpt)
        {
            hive::LogWarning(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] Deallocate called on unknown pointer: {}",
                             GetName(), ptr);
            return;
        }
        auto& info = *infoOpt;

        if constexpr (debug::kMemDebugEnabled)
        {
            if (!info.CheckGuards())
            {
                if (info.ReadGuardFront() != debug::guardMagic)
                {
                    hive::LogError(comb::LOG_COMB_ROOT,
                                   "[MEM_DEBUG] [{}] Buffer UNDERRUN detected! Address: {}, Size: {}, Tag: {}",
                                   GetName(), ptr, info.m_size, info.GetTagOrDefault());
                    hive::Assert(false, "Buffer underrun detected");
                }

                if (info.ReadGuardBack() != debug::guardMagic)
                {
                    hive::LogError(comb::LOG_COMB_ROOT,
                                   "[MEM_DEBUG] [{}] Buffer OVERRUN detected! Address: {}, Size: {}, Tag: {}",
                                   GetName(), ptr, info.m_size, info.GetTagOrDefault());
                    hive::Assert(false, "Buffer overrun detected");
                }
            }
        }

#if COMB_MEM_DEBUG_USE_AFTER_FREE
        std::memset(ptr, debug::freedMemoryPattern, info.m_size);
#endif

#if COMB_MEM_DEBUG_HISTORY
        m_history->RecordDeallocation(ptr, info.m_size);
#endif

        m_registry->UnregisterAllocation(ptr);
    }

#endif // COMB_MEM_DEBUG
} // namespace comb
