#include <comb/system_allocator.h>

#include <hive/core/assert.h>

#include <comb/platform.h>

#include <cstdint>

namespace comb
{
    namespace
    {
        struct AllocHeader
        {
            void* m_base;
            size_t m_totalSize;
        };

        constexpr size_t kHeaderSize = sizeof(AllocHeader);

        inline size_t AlignUp(size_t value, size_t alignment) noexcept
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        inline bool IsPowerOfTwo(size_t value) noexcept
        {
            return value != 0 && (value & (value - 1)) == 0;
        }
    } // namespace

    void* SystemAllocator::Allocate(size_t size, size_t alignment, const char* /*tag*/)
    {
        if (size == 0)
            return nullptr;

        hive::Assert(IsPowerOfTwo(alignment), "SystemAllocator: alignment must be power of two");

        const size_t headerAlign = alignof(AllocHeader);
        const size_t userAlignment = alignment < headerAlign ? headerAlign : alignment;
        const size_t userOffset = AlignUp(kHeaderSize, userAlignment);

        const size_t pageSize = GetPageSize();
        const size_t totalSize = AlignUp(userOffset + size, pageSize);

        void* base = AllocatePages(totalSize);
        if (!base)
            return nullptr;

        auto* userPtr = static_cast<std::uint8_t*>(base) + userOffset;
        auto* header = reinterpret_cast<AllocHeader*>(userPtr - kHeaderSize);
        header->m_base = base;
        header->m_totalSize = totalSize;

        m_usedBytes.fetch_add(totalSize, std::memory_order_relaxed);
        m_liveCount.fetch_add(1, std::memory_order_relaxed);
        return userPtr;
    }

    void SystemAllocator::Deallocate(void* ptr)
    {
        if (!ptr)
            return;

        auto* header = reinterpret_cast<AllocHeader*>(static_cast<std::uint8_t*>(ptr) - kHeaderSize);
        void* base = header->m_base;
        const size_t totalSize = header->m_totalSize;

        FreePages(base, totalSize);

        m_usedBytes.fetch_sub(totalSize, std::memory_order_relaxed);
        m_liveCount.fetch_sub(1, std::memory_order_relaxed);
    }
} // namespace comb
