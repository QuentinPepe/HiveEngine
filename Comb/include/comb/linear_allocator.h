#pragma once

#include <hive/hive_config.h>

#include <comb/allocator_concepts.h>

#include <cstddef>
#include <memory>

// Memory debugging (zero overhead when disabled)
#if COMB_MEM_DEBUG
#include <comb/debug/allocation_history.h>
#include <comb/debug/allocation_registry.h>
#include <comb/debug/global_memory_tracker.h>
#include <comb/debug/mem_debug_config.h>
#include <comb/debug/platform_utils.h>
#endif

namespace comb
{
    // Linear (bump/arena) allocator — O(1) alloc, no individual deallocation.
    // Reset() frees everything at once. Supports markers for scoped rollback.
    class HIVE_API LinearAllocator
    {
    public:
        explicit LinearAllocator(size_t capacity);

        ~LinearAllocator();

        LinearAllocator(const LinearAllocator&) = delete;
        LinearAllocator& operator=(const LinearAllocator&) = delete;

        // Pre-move allocations remain valid on the moved-to allocator.
        LinearAllocator(LinearAllocator&& other) noexcept;
        LinearAllocator& operator=(LinearAllocator&& other) noexcept;

        // tag is zero-cost when COMB_MEM_DEBUG=0.
        [[nodiscard]] void* Allocate(size_t size, size_t alignment, const char* tag = nullptr);

        // No-op in release. LinearAllocator never frees individually.
        void Deallocate(void* ptr);

        void Reset();

        [[nodiscard]] void* GetMarker() const noexcept;

        void ResetToMarker(void* marker);

        [[nodiscard]] size_t GetUsedMemory() const noexcept;

        [[nodiscard]] size_t GetTotalMemory() const noexcept;

        [[nodiscard]] const char* GetName() const noexcept;

    private:
        void* m_base{nullptr};
        void* m_current{nullptr};
        size_t m_capacity{0};

#if COMB_MEM_DEBUG
        // Debug tracking (zero overhead when COMB_MEM_DEBUG=0)
        void* AllocateDebug(size_t size, size_t alignment, const char* tag);
        void DeallocateDebug(void* ptr);

        // Use unique_ptr to enable move semantics (AllocationRegistry contains non-movable mutex)
        std::unique_ptr<debug::AllocationRegistry> m_registry;
        std::unique_ptr<debug::AllocationHistory> m_history;

        // Tracks current_ without guard bytes — keeps GetUsedMemory() consistent across debug/release
        void* m_releaseCurrent{nullptr};
#endif
    };

    static_assert(Allocator<LinearAllocator>, "LinearAllocator must satisfy Allocator concept");
} // namespace comb
