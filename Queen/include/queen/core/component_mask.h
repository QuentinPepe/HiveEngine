#pragma once

#include <hive/core/assert.h>

#include <comb/allocator_concepts.h>

#include <wax/containers/vector.h>

#include <cstdint>

namespace queen
{
    // Dynamic bitset over uint64_t blocks. Underlies AccessDescriptor conflict detection
    // and archetype matching; intersection/contains are the hot paths.
    // Memory layout:
    // ┌────────────────────────────────────────────────────────────┐
    // │ m_blocks: Vector<uint64_t>                                 │
    // │   [block0: bits 0-63] [block1: bits 64-127] ...            │
    // └────────────────────────────────────────────────────────────┘
    template <comb::Allocator Allocator> class ComponentMask
    {
    public:
        static constexpr size_t BitsPerBlock = 64;

        explicit ComponentMask(Allocator& allocator)
            : m_allocator{&allocator}
            , m_blocks{allocator}
        {
        }

        // Copy ctor reuses other's allocator (masks cannot rebind their allocator).
        ComponentMask(const ComponentMask& other)
            : m_allocator{other.m_allocator}
            , m_blocks{*other.m_allocator}
        {
            m_blocks.Reserve(other.m_blocks.Size());
            for (size_t i = 0; i < other.m_blocks.Size(); ++i)
            {
                m_blocks.PushBack(other.m_blocks[i]);
            }
        }

        // Copy assignment keeps this instance's allocator; only block contents are copied.
        ComponentMask& operator=(const ComponentMask& other)
        {
            if (this != &other)
            {
                m_blocks.Clear();
                m_blocks.Reserve(other.m_blocks.Size());
                for (size_t i = 0; i < other.m_blocks.Size(); ++i)
                {
                    m_blocks.PushBack(other.m_blocks[i]);
                }
            }
            return *this;
        }

        ComponentMask(ComponentMask&& other) noexcept
            : m_allocator{other.m_allocator}
            , m_blocks{std::move(other.m_blocks)}
        {
        }

        ComponentMask& operator=(ComponentMask&& other) noexcept
        {
            if (this != &other)
            {
                m_allocator = other.m_allocator;
                m_blocks = std::move(other.m_blocks);
            }
            return *this;
        }

        void Set(size_t index)
        {
            size_t block_index = index / BitsPerBlock;
            size_t bit_index = index % BitsPerBlock;

            EnsureCapacity(block_index + 1);
            m_blocks[block_index] |= (uint64_t{1} << bit_index);
        }

        void Clear(size_t index)
        {
            size_t block_index = index / BitsPerBlock;
            if (block_index >= m_blocks.Size())
            {
                return;
            }

            size_t bit_index = index % BitsPerBlock;
            m_blocks[block_index] &= ~(uint64_t{1} << bit_index);
        }

        [[nodiscard]] bool Test(size_t index) const noexcept
        {
            size_t block_index = index / BitsPerBlock;
            if (block_index >= m_blocks.Size())
            {
                return false;
            }

            size_t bit_index = index % BitsPerBlock;
            return (m_blocks[block_index] & (uint64_t{1} << bit_index)) != 0;
        }

        void Toggle(size_t index)
        {
            size_t block_index = index / BitsPerBlock;
            size_t bit_index = index % BitsPerBlock;

            EnsureCapacity(block_index + 1);
            m_blocks[block_index] ^= (uint64_t{1} << bit_index);
        }

        void ClearAll()
        {
            for (size_t i = 0; i < m_blocks.Size(); ++i)
            {
                m_blocks[i] = 0;
            }
        }

        // Set bits 0..count-1, leaving higher bits untouched.
        void SetAll(size_t count)
        {
            size_t block_count = (count + BitsPerBlock - 1) / BitsPerBlock;
            EnsureCapacity(block_count);

            for (size_t i = 0; i < block_count; ++i)
            {
                if (i < block_count - 1)
                {
                    m_blocks[i] = ~uint64_t{0};
                }
                else
                {
                    size_t remaining_bits = count % BitsPerBlock;
                    if (remaining_bits == 0)
                    {
                        m_blocks[i] = ~uint64_t{0};
                    }
                    else
                    {
                        m_blocks[i] = (uint64_t{1} << remaining_bits) - 1;
                    }
                }
            }
        }

        [[nodiscard]] bool Any() const noexcept
        {
            for (size_t i = 0; i < m_blocks.Size(); ++i)
            {
                if (m_blocks[i] != 0)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool None() const noexcept
        {
            return !Any();
        }

        [[nodiscard]] size_t Count() const noexcept
        {
            size_t count = 0;
            for (size_t i = 0; i < m_blocks.Size(); ++i)
            {
                count += PopCount(m_blocks[i]);
            }
            return count;
        }

        [[nodiscard]] bool Intersects(const ComponentMask& other) const noexcept
        {
            size_t min_size = m_blocks.Size() < other.m_blocks.Size() ? m_blocks.Size() : other.m_blocks.Size();

            for (size_t i = 0; i < min_size; ++i)
            {
                if ((m_blocks[i] & other.m_blocks[i]) != 0)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool ContainsAll(const ComponentMask& other) const noexcept
        {
            for (size_t i = 0; i < other.m_blocks.Size(); ++i)
            {
                uint64_t our_block = (i < m_blocks.Size()) ? m_blocks[i] : 0;
                if ((our_block & other.m_blocks[i]) != other.m_blocks[i])
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool Disjoint(const ComponentMask& other) const noexcept
        {
            return !Intersects(other);
        }

        ComponentMask& operator&=(const ComponentMask& other)
        {
            size_t min_size = m_blocks.Size() < other.m_blocks.Size() ? m_blocks.Size() : other.m_blocks.Size();

            for (size_t i = 0; i < min_size; ++i)
            {
                m_blocks[i] &= other.m_blocks[i];
            }

            // Clear bits beyond other's size
            for (size_t i = min_size; i < m_blocks.Size(); ++i)
            {
                m_blocks[i] = 0;
            }

            return *this;
        }

        ComponentMask& operator|=(const ComponentMask& other)
        {
            EnsureCapacity(other.m_blocks.Size());

            for (size_t i = 0; i < other.m_blocks.Size(); ++i)
            {
                m_blocks[i] |= other.m_blocks[i];
            }

            return *this;
        }

        ComponentMask& operator^=(const ComponentMask& other)
        {
            EnsureCapacity(other.m_blocks.Size());

            for (size_t i = 0; i < other.m_blocks.Size(); ++i)
            {
                m_blocks[i] ^= other.m_blocks[i];
            }

            return *this;
        }

        void Invert()
        {
            for (size_t i = 0; i < m_blocks.Size(); ++i)
            {
                m_blocks[i] = ~m_blocks[i];
            }
        }

        // Different block counts compare equal when the extra blocks are all zero.
        [[nodiscard]] bool operator==(const ComponentMask& other) const noexcept
        {
            size_t max_size = m_blocks.Size() > other.m_blocks.Size() ? m_blocks.Size() : other.m_blocks.Size();

            for (size_t i = 0; i < max_size; ++i)
            {
                uint64_t our_block = (i < m_blocks.Size()) ? m_blocks[i] : 0;
                uint64_t their_block = (i < other.m_blocks.Size()) ? other.m_blocks[i] : 0;

                if (our_block != their_block)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool operator!=(const ComponentMask& other) const noexcept
        {
            return !(*this == other);
        }

        // Returns size_t(-1) if no bit is set.
        [[nodiscard]] size_t FirstSetBit() const noexcept
        {
            for (size_t i = 0; i < m_blocks.Size(); ++i)
            {
                if (m_blocks[i] != 0)
                {
                    return i * BitsPerBlock + CountTrailingZeros(m_blocks[i]);
                }
            }
            return static_cast<size_t>(-1);
        }

        // Returns size_t(-1) if no bit is set.
        [[nodiscard]] size_t LastSetBit() const noexcept
        {
            for (size_t i = m_blocks.Size(); i > 0; --i)
            {
                if (m_blocks[i - 1] != 0)
                {
                    return (i - 1) * BitsPerBlock + (BitsPerBlock - 1 - CountLeadingZeros(m_blocks[i - 1]));
                }
            }
            return static_cast<size_t>(-1);
        }

        [[nodiscard]] size_t BlockCount() const noexcept
        {
            return m_blocks.Size();
        }

        // Highest bit index storable without reallocation.
        [[nodiscard]] size_t Capacity() const noexcept
        {
            return m_blocks.Size() * BitsPerBlock;
        }

        void Reserve(size_t bit_count)
        {
            size_t block_count = (bit_count + BitsPerBlock - 1) / BitsPerBlock;
            m_blocks.Reserve(block_count);
        }

    private:
        void EnsureCapacity(size_t block_count)
        {
            while (m_blocks.Size() < block_count)
            {
                m_blocks.PushBack(0);
            }
        }

        static size_t PopCount(uint64_t x) noexcept
        {
#if defined(__GNUC__) || defined(__clang__)
            return static_cast<size_t>(__builtin_popcountll(x));
#elif defined(_MSC_VER)
            return static_cast<size_t>(__popcnt64(x));
#else
            // Fallback: Brian Kernighan's algorithm
            size_t count = 0;
            while (x)
            {
                x &= (x - 1);
                ++count;
            }
            return count;
#endif
        }

        static size_t CountTrailingZeros(uint64_t x) noexcept
        {
            if (x == 0)
                return 64;
#if defined(__GNUC__) || defined(__clang__)
            return static_cast<size_t>(__builtin_ctzll(x));
#elif defined(_MSC_VER)
            unsigned long index;
            _BitScanForward64(&index, x);
            return static_cast<size_t>(index);
#else
            size_t count = 0;
            while ((x & 1) == 0)
            {
                x >>= 1;
                ++count;
            }
            return count;
#endif
        }

        static size_t CountLeadingZeros(uint64_t x) noexcept
        {
            if (x == 0)
                return 64;
#if defined(__GNUC__) || defined(__clang__)
            return static_cast<size_t>(__builtin_clzll(x));
#elif defined(_MSC_VER)
            unsigned long index;
            _BitScanReverse64(&index, x);
            return static_cast<size_t>(63 - index);
#else
            size_t count = 0;
            uint64_t mask = uint64_t{1} << 63;
            while ((x & mask) == 0)
            {
                mask >>= 1;
                ++count;
            }
            return count;
#endif
        }

        Allocator* m_allocator;
        wax::Vector<uint64_t> m_blocks;
    };

    template <comb::Allocator Allocator>
    [[nodiscard]] ComponentMask<Allocator> operator&(ComponentMask<Allocator> lhs, const ComponentMask<Allocator>& rhs)
    {
        lhs &= rhs;
        return lhs;
    }

    template <comb::Allocator Allocator>
    [[nodiscard]] ComponentMask<Allocator> operator|(ComponentMask<Allocator> lhs, const ComponentMask<Allocator>& rhs)
    {
        lhs |= rhs;
        return lhs;
    }

    template <comb::Allocator Allocator>
    [[nodiscard]] ComponentMask<Allocator> operator^(ComponentMask<Allocator> lhs, const ComponentMask<Allocator>& rhs)
    {
        lhs ^= rhs;
        return lhs;
    }
} // namespace queen
