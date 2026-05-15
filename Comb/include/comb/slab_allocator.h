#pragma once

#include <hive/core/assert.h>
#include <hive/profiling/profiler.h>

#include <comb/allocator_concepts.h>
#include <comb/platform.h>
#include <comb/utils.h>

#include <array>
#include <cstddef>
#include <memory>
#include <utility>

// Memory debugging (zero overhead when disabled)
#if COMB_MEM_DEBUG
#include <hive/core/log.h>

#include <comb/combmodule.h>
#include <comb/debug/allocation_history.h>
#include <comb/debug/allocation_registry.h>
#include <comb/debug/global_memory_tracker.h>
#include <comb/debug/mem_debug_config.h>
#include <comb/debug/platform_utils.h>

#include <cstring> // For memset
#endif

namespace comb
{
    // Slab allocator — multiple PoolAllocators behind different size classes.
    // Allocations routed to the smallest slab that fits. O(1) alloc/dealloc.
    template <size_t ObjectsPerSlab, size_t... SizeClasses> class SlabAllocator
    {
        static_assert(sizeof...(SizeClasses) > 0, "Must provide at least one size class");
        static_assert(ObjectsPerSlab > 0, "Must allocate at least one object per slab");

    private:
        // Size classes rounded to powers of 2 and sorted
        static constexpr auto sizes_ = MakeArray(NextPowerOfTwo(SizeClasses)...);
        static_assert(IsSorted(sizes_), "Size classes must be sorted");

        static constexpr size_t NumSlabs = sizeof...(SizeClasses);

        // Each slab: memory block + free-list head + usage counter
        struct Slab
        {
            void* memory_block{nullptr};
            void* free_list_head{nullptr};
            size_t used_count{0};
            size_t slot_size{0};
            size_t total_size{0};
            size_t m_freeListOffset{0};
            size_t m_userSize{0};

            void Initialize(size_t size, size_t free_list_offset = 0, size_t user_size = 0)
            {
                slot_size = size;
                total_size = ObjectsPerSlab * slot_size;
                m_freeListOffset = free_list_offset;

                // user_size = raw size class without debug overhead.
                m_userSize = (user_size > 0) ? user_size : size;

                memory_block = AllocatePages(total_size);
                hive::Assert(memory_block != nullptr, "Failed to allocate slab memory");

                RebuildFreeList(free_list_offset);
            }

            void Destroy()
            {
                if (memory_block != nullptr)
                {
                    FreePages(memory_block, total_size);
                    memory_block = nullptr;
                    free_list_head = nullptr;
                }
            }

            void RebuildFreeList(size_t free_list_offset)
            {
                // free_list_offset skips the guard bytes at the start of each slot in debug mode.
                char* current = static_cast<char*>(memory_block) + free_list_offset;
                free_list_head = current;

                for (size_t i = 0; i < ObjectsPerSlab - 1; ++i)
                {
                    char* next = current + slot_size;
                    *reinterpret_cast<void**>(current) = next;
                    current = next;
                }

                *reinterpret_cast<void**>(current) = nullptr;
                used_count = 0;
            }

            void RebuildFreeList()
            {
                RebuildFreeList(m_freeListOffset);
            }

            void* Allocate()
            {
                if (free_list_head == nullptr)
                {
                    return nullptr;
                }

                void* ptr = free_list_head;
                free_list_head = *static_cast<void**>(free_list_head);
                ++used_count;
                return ptr;
            }

            void Deallocate(void* ptr)
            {
                if (ptr == nullptr)
                {
                    return;
                }

                hive::Assert(used_count > 0, "Deallocate called more than Allocate");

                *static_cast<void**>(ptr) = free_list_head;
                free_list_head = ptr;
                --used_count;
            }

            bool Contains(void* ptr) const
            {
                if (ptr == nullptr || memory_block == nullptr)
                {
                    return false;
                }

                const char* start = static_cast<const char*>(memory_block);
                const char* end = start + (ObjectsPerSlab * slot_size);
                const char* p = static_cast<const char*>(ptr);

                return p >= start && p < end;
            }

            size_t GetUsedMemory() const
            {
                // User-visible memory excludes guard bytes.
                return used_count * m_userSize;
            }

            size_t GetTotalMemory() const
            {
                // User-visible capacity excludes guard bytes.
                return ObjectsPerSlab * m_userSize;
            }

            size_t GetFreeCount() const
            {
                return ObjectsPerSlab - used_count;
            }
        };

        std::array<Slab, NumSlabs> slabs_{};

        // Find slab index for given size
        constexpr size_t FindSlabIndex(size_t size) const
        {
            for (size_t i = 0; i < sizes_.size(); ++i)
            {
                if (size <= sizes_[i])
                {
                    return i;
                }
            }
            return NumSlabs;
        }

    public:
        SlabAllocator(const SlabAllocator&) = delete;
        SlabAllocator& operator=(const SlabAllocator&) = delete;

        SlabAllocator()
        {
            for (size_t i = 0; i < NumSlabs; ++i)
            {
#if COMB_MEM_DEBUG
                // Front guard padded for max_align_t so user data is properly aligned
                constexpr size_t min_align = alignof(std::max_align_t);
                constexpr size_t guard_front_padded = (debug::guardSize + min_align - 1) & ~(min_align - 1);
                size_t raw_slot = sizes_[i] + guard_front_padded + debug::guardSize;
                size_t aligned_slot = (raw_slot + min_align - 1) & ~(min_align - 1);
                slabs_[i].Initialize(aligned_slot, guard_front_padded, sizes_[i]);
#else
                slabs_[i].Initialize(sizes_[i]);
#endif
            }

#if COMB_MEM_DEBUG
            m_registry = std::make_unique<debug::AllocationRegistry>();
            m_history = std::make_unique<debug::AllocationHistory>();
            debug::GlobalMemoryTracker::GetInstance().RegisterAllocator(GetName(), m_registry.get());
#endif
        }

        ~SlabAllocator()
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

            for (auto& slab : slabs_)
            {
                slab.Destroy();
            }
        }

        // Allocations made before the move stay valid on the moved-to allocator.
        SlabAllocator(SlabAllocator&& other) noexcept
            : slabs_{std::move(other.slabs_)}
#if COMB_MEM_DEBUG
            , m_registry{std::move(other.m_registry)}
            , m_history{std::move(other.m_history)}
#endif
        {
            // unique_ptr move keeps registry identity, so the global tracker stays valid.
            for (auto& slab : other.slabs_)
            {
                slab.memory_block = nullptr;
                slab.free_list_head = nullptr;
                slab.used_count = 0;
            }
        }

        SlabAllocator& operator=(SlabAllocator&& other) noexcept
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

                for (auto& slab : slabs_)
                {
                    slab.Destroy();
                }

                slabs_ = std::move(other.slabs_);

#if COMB_MEM_DEBUG
                m_registry = std::move(other.m_registry);
                m_history = std::move(other.m_history);
#endif

                for (auto& slab : other.slabs_)
                {
                    slab.memory_block = nullptr;
                    slab.free_list_head = nullptr;
                    slab.used_count = 0;
                }
            }
            return *this;
        }

        // Returns nullptr if no slab fits or the matching slab is exhausted — no operator new fallback.
        // tag is zero-cost when COMB_MEM_DEBUG=0.
        [[nodiscard]] void* Allocate(size_t size, size_t alignment, const char* tag = nullptr)
        {
#if COMB_MEM_DEBUG
            void* ptr = AllocateDebug(size, alignment, tag);
#else
            (void)tag;

            hive::Assert(alignment <= alignof(std::max_align_t), "SlabAllocator alignment limited to max_align_t");

            const size_t slab_index = FindSlabIndex(size);

            if (slab_index >= NumSlabs)
            {
                return nullptr;
            }

            void* ptr = slabs_[slab_index].Allocate();
#endif

            if (ptr != nullptr)
            {
                HIVE_PROFILE_ALLOC(ptr, size, GetName());
            }
            return ptr;
        }

        // Pointer must come from this allocator. Locates the owning slab and returns to its free-list.
        void Deallocate(void* ptr)
        {
            if (ptr == nullptr)
            {
                return;
            }

            HIVE_PROFILE_FREE(ptr, GetName());

#if COMB_MEM_DEBUG
            DeallocateDebug(ptr);
#else
            for (auto& slab : slabs_)
            {
                if (slab.Contains(ptr))
                {
                    slab.Deallocate(ptr);
                    return;
                }
            }

            hive::Assert(false, "Pointer not allocated from this SlabAllocator");
#endif
        }

        void Reset()
        {
            for (auto& slab : slabs_)
            {
                slab.RebuildFreeList();
            }

#if COMB_MEM_DEBUG
            if (m_registry != nullptr)
            {
                m_registry->Clear();
            }
#endif
        }

        [[nodiscard]] size_t GetUsedMemory() const noexcept
        {
            size_t total = 0;
            for (const auto& slab : slabs_)
            {
                total += slab.GetUsedMemory();
            }
            return total;
        }

        [[nodiscard]] size_t GetTotalMemory() const noexcept
        {
            size_t total = 0;
            for (const auto& slab : slabs_)
            {
                total += slab.GetTotalMemory();
            }
            return total;
        }

        [[nodiscard]] const char* GetName() const noexcept
        {
            return "SlabAllocator";
        }

        [[nodiscard]] constexpr size_t GetSlabCount() const noexcept
        {
            return NumSlabs;
        }

        [[nodiscard]] constexpr auto GetSizeClasses() const noexcept
        {
            return sizes_;
        }

        [[nodiscard]] size_t GetSlabUsedCount(size_t slab_index) const noexcept
        {
            hive::Assert(slab_index < NumSlabs, "Slab index out of range");
            return slabs_[slab_index].used_count;
        }

        [[nodiscard]] size_t GetSlabFreeCount(size_t slab_index) const noexcept
        {
            hive::Assert(slab_index < NumSlabs, "Slab index out of range");
            return slabs_[slab_index].GetFreeCount();
        }

#if COMB_MEM_DEBUG
    private:
        // Debug tracking (zero overhead when COMB_MEM_DEBUG=0)
        void* AllocateDebug(size_t size, size_t alignment, const char* tag);
        void DeallocateDebug(void* ptr);

        // Use unique_ptr to enable move semantics (AllocationRegistry contains non-movable mutex)
        std::unique_ptr<debug::AllocationRegistry> m_registry;
        std::unique_ptr<debug::AllocationHistory> m_history;
#endif
    };

    template <size_t N, size_t... Sizes>
    concept ValidSlabAllocator = Allocator<SlabAllocator<N, Sizes...>>;

#if COMB_MEM_DEBUG
    // Debug Implementation (Template methods - must be in header)

    template <size_t ObjectsPerSlab, size_t... SizeClasses>
    void* SlabAllocator<ObjectsPerSlab, SizeClasses...>::AllocateDebug(size_t size, size_t alignment, const char* tag)
    {
        hive::Assert(alignment <= alignof(std::max_align_t), "SlabAllocator alignment limited to max_align_t");

        // Check fit BEFORE adding guard bytes — user size is what callers care about.
        const size_t slab_index = FindSlabIndex(size);

        if (slab_index >= NumSlabs)
        {
            hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] No slab can fit size={}, max_size={}, tag={}",
                           GetName(), size, sizes_[NumSlabs - 1], (tag != nullptr) ? tag : "<no tag>");
            return nullptr;
        }

        const size_t slotSize = sizes_[slab_index];

        // In debug mode, Allocate() returns a pointer positioned after the guard front.
        void* userPtr = slabs_[slab_index].Allocate();
        if (userPtr == nullptr)
        {
            hive::LogError(comb::LOG_COMB_ROOT,
                           "[MEM_DEBUG] [{}] Slab {} (size={}) exhausted: requested size={}, tag={}", GetName(),
                           slab_index, slotSize, size, (tag != nullptr) ? tag : "<no tag>");
            return nullptr;
        }

        // Guard bytes live in-place within the slab slot.
        // Layout: [GUARD_FRONT (4B)][user data (size)][GUARD_BACK (4B)]
        const size_t guardSize = sizeof(uint32_t);
        void* rawPtr = static_cast<std::byte*>(userPtr) - guardSize;

        debug::WriteGuard(rawPtr);
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

    template <size_t ObjectsPerSlab, size_t... SizeClasses>
    void SlabAllocator<ObjectsPerSlab, SizeClasses...>::DeallocateDebug(void* ptr)
    {
        if (ptr == nullptr)
        {
            return;
        }

        auto infoOpt = m_registry->FindAllocation(ptr);
        if (!infoOpt)
        {
            hive::LogError(comb::LOG_COMB_ROOT, "[MEM_DEBUG] [{}] Double-free or invalid pointer detected! Address: {}",
                           GetName(), ptr);
            hive::Assert(false, "Double-free or invalid pointer (not found in registry)");
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

        // Debug free-list stores pointers in user data area (after guard), so deallocate using ptr (= userPtr).
        for (auto& slab : slabs_)
        {
            if (slab.Contains(ptr))
            {
                slab.Deallocate(ptr);
                return;
            }
        }

        hive::Assert(false, "Internal error: ptr not found in any slab");
    }

#endif // COMB_MEM_DEBUG
} // namespace comb
