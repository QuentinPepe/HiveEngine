#pragma once

#include <cstddef>
#include <cstdint>

namespace nectar
{
    enum class PixelFormat : uint8_t
    {
        RGBA8 = 0,
        RGB8 = 1,
        GREY8 = 2,
        BC7 = 3,
    };

    struct TextureMipLevel
    {
        uint32_t m_width{0};
        uint32_t m_height{0};
        uint32_t m_offset{0}; // byte offset into pixel data blob
        uint32_t m_size{0};   // byte size of this mip level
    };

    // NTEX intermediate format (Nectar Texture)
    // Layout in memory/file:
    //   NtexHeader
    //   TextureMipLevel[mip_count]
    //   pixel data bytes

    static constexpr uint32_t kNtexMagic = 0x5845544E; // "NTEX" in little-endian

    static constexpr uint32_t kNtexVersion = 2;

    struct NtexHeader
    {
        uint32_t m_magic{kNtexMagic};
        uint32_t m_version{kNtexVersion};
        uint32_t m_width{0};
        uint32_t m_height{0};
        uint32_t m_channels{0};
        PixelFormat m_format{PixelFormat::RGBA8};
        bool m_srgb{true};
        uint8_t m_mipCount{1};
        uint8_t m_padding[1]{};
    };

    [[nodiscard]] constexpr size_t BytesPerPixel(PixelFormat fmt) noexcept
    {
        switch (fmt)
        {
            case PixelFormat::RGBA8:
                return 4;
            case PixelFormat::RGB8:
                return 3;
            case PixelFormat::GREY8:
                return 1;
            case PixelFormat::BC7:
                return 0;
        }
        return 4;
    }
} // namespace nectar
