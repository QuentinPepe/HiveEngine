#pragma once

#include <hive/core/assert.h>

#include <cstdint>
#include <functional>

namespace nectar
{
    /**
     * 128-bit unique asset identifier
     *
     * Implemented as two 64-bit integers for efficient storage and comparison.
     *
     * Performance characteristics:
     * - Storage: 16 bytes (two uint64)
     * - Comparison: O(1) - two 64-bit compares
     * - Hash: O(1) - XOR of high and low
     */
    class AssetId
    {
    public:
        static constexpr size_t kByteSize = 16;

        constexpr AssetId() noexcept
            : m_high{0}
            , m_low{0}
        {
        }

        constexpr AssetId(uint64_t high, uint64_t low) noexcept
            : m_high{high}
            , m_low{low}
        {
        }

        [[nodiscard]] static constexpr AssetId Invalid() noexcept
        {
            return AssetId{0, 0};
        }

        [[nodiscard]] static AssetId FromBytes(const uint8_t* bytes) noexcept
        {
            hive::Assert(bytes != nullptr, "Bytes cannot be null");

            uint64_t high = 0;
            uint64_t low = 0;

            for (size_t i = 0; i < 8; ++i)
            {
                high |= static_cast<uint64_t>(bytes[i]) << (56 - i * 8);
            }
            for (size_t i = 0; i < 8; ++i)
            {
                low |= static_cast<uint64_t>(bytes[8 + i]) << (56 - i * 8);
            }

            return AssetId{high, low};
        }

        // Parse a "{hexhexhex...}" 34-char guid string. Returns Invalid() on malformed input.
        [[nodiscard]] static AssetId FromGuidString(const char* str, size_t size) noexcept
        {
            if (str == nullptr || size < 34 || str[0] != '{' || str[33] != '}')
                return AssetId::Invalid();

            auto hexDigit = [](char c) -> uint64_t {
                if (c >= '0' && c <= '9')
                    return static_cast<uint64_t>(c - '0');
                if (c >= 'a' && c <= 'f')
                    return static_cast<uint64_t>(10 + c - 'a');
                if (c >= 'A' && c <= 'F')
                    return static_cast<uint64_t>(10 + c - 'A');
                return 0;
            };

            uint64_t high = 0;
            uint64_t low = 0;
            for (size_t i = 0; i < 16; ++i)
                high = (high << 4) | hexDigit(str[1 + i]);
            for (size_t i = 0; i < 16; ++i)
                low = (low << 4) | hexDigit(str[17 + i]);
            return AssetId{high, low};
        }

        // Serialize as "{...}" 34-char guid string into a 35-byte buffer (incl. NUL terminator).
        void ToGuidString(char (&out)[35]) const noexcept
        {
            static constexpr char kHex[] = "0123456789abcdef";
            out[0] = '{';
            for (size_t i = 0; i < 8; ++i)
            {
                const uint8_t byte = static_cast<uint8_t>(m_high >> (56 - i * 8));
                out[1 + i * 2] = kHex[byte >> 4];
                out[1 + i * 2 + 1] = kHex[byte & 0x0F];
            }
            for (size_t i = 0; i < 8; ++i)
            {
                const uint8_t byte = static_cast<uint8_t>(m_low >> (56 - i * 8));
                out[17 + i * 2] = kHex[byte >> 4];
                out[17 + i * 2 + 1] = kHex[byte & 0x0F];
            }
            out[33] = '}';
            out[34] = '\0';
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_high != 0 || m_low != 0;
        }

        [[nodiscard]] constexpr uint64_t High() const noexcept
        {
            return m_high;
        }

        [[nodiscard]] constexpr uint64_t Low() const noexcept
        {
            return m_low;
        }

        [[nodiscard]] constexpr size_t Hash() const noexcept
        {
            return static_cast<size_t>(m_high ^ m_low);
        }

        [[nodiscard]] constexpr bool operator==(const AssetId& other) const noexcept
        {
            return m_high == other.m_high && m_low == other.m_low;
        }

        [[nodiscard]] constexpr bool operator!=(const AssetId& other) const noexcept
        {
            return !(*this == other);
        }

        [[nodiscard]] constexpr bool operator<(const AssetId& other) const noexcept
        {
            if (m_high != other.m_high)
            {
                return m_high < other.m_high;
            }
            return m_low < other.m_low;
        }

        [[nodiscard]] constexpr bool operator<=(const AssetId& other) const noexcept
        {
            return !(other < *this);
        }

        [[nodiscard]] constexpr bool operator>(const AssetId& other) const noexcept
        {
            return other < *this;
        }

        [[nodiscard]] constexpr bool operator>=(const AssetId& other) const noexcept
        {
            return !(*this < other);
        }

    private:
        uint64_t m_high;
        uint64_t m_low;
    };

} // namespace nectar

// Hash specialization for wax::HashMap/HashSet
template <> struct std::hash<nectar::AssetId>
{
    size_t operator()(const nectar::AssetId& id) const noexcept
    {
        return id.Hash();
    }
};
