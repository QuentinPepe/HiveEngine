#include <nectar/texture/texture_importer.h>

#include <nectar/texture/texture_data.h>

#include <wax/containers/vector.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <cstring>
#include <stb_image.h>

#include <bc7enc.h>

namespace nectar
{
    namespace
    {
        // bc7enc_compress_block_init() builds static lookup tables; guard with a one-shot flag.
        // Thread-safe after init (the block compressor only reads from the tables).
        void EnsureBc7EncInitialized()
        {
            static const bool init = []() {
                bc7enc_compress_block_init();
                return true;
            }();
            (void)init;
        }

        // Encode an RGBA8 mip (tightly packed) into BC7 blocks. Pads sub-block edges (mips <4)
        // by replicating the last available row/column so each 4x4 block always has 16 valid pixels.
        // Returns the number of bytes written to dst.
        size_t EncodeMipBc7(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst,
                            const bc7enc_compress_block_params& params)
        {
            const uint32_t blocksW = (width + 3) / 4;
            const uint32_t blocksH = (height + 3) / 4;
            uint8_t blockPixels[16 * 4];

            uint8_t* writeCursor = dst;
            for (uint32_t by = 0; by < blocksH; ++by)
            {
                for (uint32_t bx = 0; bx < blocksW; ++bx)
                {
                    for (uint32_t py = 0; py < 4; ++py)
                    {
                        const uint32_t srcY = by * 4 + py;
                        const uint32_t clampedY = srcY < height ? srcY : height - 1;
                        for (uint32_t px = 0; px < 4; ++px)
                        {
                            const uint32_t srcX = bx * 4 + px;
                            const uint32_t clampedX = srcX < width ? srcX : width - 1;
                            const size_t srcIndex = (static_cast<size_t>(clampedY) * width + clampedX) * 4;
                            const size_t dstIndex = (py * 4 + px) * 4;
                            blockPixels[dstIndex + 0] = src[srcIndex + 0];
                            blockPixels[dstIndex + 1] = src[srcIndex + 1];
                            blockPixels[dstIndex + 2] = src[srcIndex + 2];
                            blockPixels[dstIndex + 3] = src[srcIndex + 3];
                        }
                    }
                    bc7enc_compress_block(writeCursor, blockPixels, &params);
                    writeCursor += 16;
                }
            }
            return static_cast<size_t>(writeCursor - dst);
        }

        size_t Bc7BlockBytes(uint32_t width, uint32_t height) noexcept
        {
            const uint32_t blocksW = (width + 3) / 4;
            const uint32_t blocksH = (height + 3) / 4;
            return static_cast<size_t>(blocksW) * blocksH * 16;
        }

        void DownscaleHalf(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* dst, uint32_t channels)
        {
            const uint32_t dstW = srcW / 2;
            const uint32_t dstH = srcH / 2;
            for (uint32_t y = 0; y < dstH; ++y)
            {
                for (uint32_t x = 0; x < dstW; ++x)
                {
                    for (uint32_t c = 0; c < channels; ++c)
                    {
                        uint32_t sum = 0;
                        sum += src[((y * 2) * srcW + (x * 2)) * channels + c];
                        sum += src[((y * 2) * srcW + (x * 2 + 1)) * channels + c];
                        sum += src[((y * 2 + 1) * srcW + (x * 2)) * channels + c];
                        sum += src[((y * 2 + 1) * srcW + (x * 2 + 1)) * channels + c];
                        dst[(y * dstW + x) * channels + c] = static_cast<uint8_t>(sum / 4);
                    }
                }
            }
        }

        void FlipVertical(uint8_t* data, uint32_t width, uint32_t height, uint32_t channels)
        {
            const uint32_t rowBytes = width * channels;
            for (uint32_t y = 0; y < height / 2; ++y)
            {
                uint8_t* rowA = data + y * rowBytes;
                uint8_t* rowB = data + (height - 1 - y) * rowBytes;
                for (uint32_t i = 0; i < rowBytes; ++i)
                {
                    const uint8_t tmp = rowA[i];
                    rowA[i] = rowB[i];
                    rowB[i] = tmp;
                }
            }
        }
    } // namespace

    wax::Span<const char* const> TextureImporter::SourceExtensions() const
    {
        static const char* const kExtensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr"};
        return {kExtensions, 6};
    }

    uint32_t TextureImporter::Version() const
    {
        return 3;
    }

    wax::StringView TextureImporter::TypeName() const
    {
        return "Texture";
    }

    ImportResult TextureImporter::Import(wax::ByteSpan sourceData, const HiveDocument& settings,
                                         ImportContext& context)
    {
        ImportResult result{};

        int widthInt = 0;
        int heightInt = 0;
        int originalChannels = 0;
        uint8_t* pixels = stbi_load_from_memory(sourceData.Data(), static_cast<int>(sourceData.Size()), &widthInt,
                                                &heightInt, &originalChannels, 4);

        if (pixels == nullptr || widthInt <= 0 || heightInt <= 0)
        {
            if (pixels != nullptr)
            {
                stbi_image_free(pixels);
            }
            result.m_errorMessage = wax::String{"Failed to decode image"};
            return result;
        }

        uint32_t width = static_cast<uint32_t>(widthInt);
        uint32_t height = static_cast<uint32_t>(heightInt);
        constexpr uint32_t kChannels = 4;

        const bool srgb = settings.GetBool("import", "srgb", true);
        const bool genMipmaps = settings.GetBool("import", "generate_mipmaps", true);
        const bool flipY = settings.GetBool("import", "flip_y", false);
        const int64_t maxSize = settings.GetInt("import", "max_size", 0);
        const wax::StringView compressionView = settings.GetString("import", "compression", "bc7");
        const bool compressBc7 = compressionView == wax::StringView{"bc7"};

        if (flipY)
        {
            FlipVertical(pixels, width, height, kChannels);
        }

        uint8_t* current = pixels;
        uint32_t currentWidth = width;
        uint32_t currentHeight = height;
        uint8_t* downscaled = nullptr;

        if (maxSize > 0)
        {
            while (currentWidth > static_cast<uint32_t>(maxSize) || currentHeight > static_cast<uint32_t>(maxSize))
            {
                if (currentWidth < 2 || currentHeight < 2)
                {
                    break;
                }

                const uint32_t newWidth = currentWidth / 2;
                const uint32_t newHeight = currentHeight / 2;
                const size_t dstSize = static_cast<size_t>(newWidth) * newHeight * kChannels;
                auto* dst = static_cast<uint8_t*>(context.GetAllocator().Allocate(dstSize, 1));
                if (dst == nullptr)
                {
                    stbi_image_free(pixels);
                    if (downscaled != nullptr)
                    {
                        context.GetAllocator().Deallocate(downscaled);
                    }
                    result.m_errorMessage = wax::String{"Failed to allocate downscale buffer"};
                    return result;
                }

                DownscaleHalf(current, currentWidth, currentHeight, dst, kChannels);

                if (downscaled != nullptr)
                {
                    context.GetAllocator().Deallocate(downscaled);
                }
                downscaled = dst;
                current = dst;
                currentWidth = newWidth;
                currentHeight = newHeight;
            }
        }

        width = currentWidth;
        height = currentHeight;

        uint8_t mipCount = 1;
        if (genMipmaps)
        {
            uint32_t mipWidth = width;
            uint32_t mipHeight = height;
            while (mipWidth > 1 || mipHeight > 1)
            {
                mipWidth = (mipWidth > 1) ? mipWidth / 2 : 1;
                mipHeight = (mipHeight > 1) ? mipHeight / 2 : 1;
                ++mipCount;
            }
        }

        size_t rgbaPyramidBytes = 0;
        {
            uint32_t mipWidth = width;
            uint32_t mipHeight = height;
            for (uint8_t i = 0; i < mipCount; ++i)
            {
                rgbaPyramidBytes += static_cast<size_t>(mipWidth) * mipHeight * kChannels;
                mipWidth = (mipWidth > 1) ? mipWidth / 2 : 1;
                mipHeight = (mipHeight > 1) ? mipHeight / 2 : 1;
            }
        }

        // Build the full RGBA8 mip pyramid in a scratch buffer first. If BC7 compression is
        // enabled we transcode each level into the output below; otherwise we copy RGBA8 directly.
        auto* rgbaPyramid = static_cast<uint8_t*>(context.GetAllocator().Allocate(rgbaPyramidBytes, 4));
        if (rgbaPyramid == nullptr)
        {
            stbi_image_free(pixels);
            if (downscaled != nullptr)
            {
                context.GetAllocator().Deallocate(downscaled);
            }
            result.m_errorMessage = wax::String{"Failed to allocate mip pyramid"};
            return result;
        }

        wax::Vector<TextureMipLevel> rgbaMipLevels{context.GetAllocator()};
        rgbaMipLevels.Reserve(mipCount);
        {
            const size_t baseBytes = static_cast<size_t>(width) * height * kChannels;
            uint32_t cursor = 0;
            std::memcpy(rgbaPyramid + cursor, current, baseBytes);
            rgbaMipLevels.PushBack(TextureMipLevel{width, height, cursor, static_cast<uint32_t>(baseBytes)});
            cursor += static_cast<uint32_t>(baseBytes);

            const uint8_t* prev = rgbaPyramid;
            uint32_t prevWidth = width;
            uint32_t prevHeight = height;
            for (uint8_t mipIndex = 1; mipIndex < mipCount; ++mipIndex)
            {
                const uint32_t newWidth = (prevWidth > 1) ? prevWidth / 2 : 1;
                const uint32_t newHeight = (prevHeight > 1) ? prevHeight / 2 : 1;
                const size_t mipBytes = static_cast<size_t>(newWidth) * newHeight * kChannels;
                uint8_t* mipDst = rgbaPyramid + cursor;
                DownscaleHalf(prev, prevWidth, prevHeight, mipDst, kChannels);
                rgbaMipLevels.PushBack(
                    TextureMipLevel{newWidth, newHeight, cursor, static_cast<uint32_t>(mipBytes)});
                cursor += static_cast<uint32_t>(mipBytes);
                prev = mipDst;
                prevWidth = newWidth;
                prevHeight = newHeight;
            }
        }

        const PixelFormat outFormat = compressBc7 ? PixelFormat::BC7 : PixelFormat::RGBA8;
        size_t outPixelBytes = 0;
        if (compressBc7)
        {
            for (uint8_t i = 0; i < mipCount; ++i)
            {
                outPixelBytes += Bc7BlockBytes(rgbaMipLevels[i].m_width, rgbaMipLevels[i].m_height);
            }
        }
        else
        {
            outPixelBytes = rgbaPyramidBytes;
        }

        NtexHeader header{};
        header.m_width = width;
        header.m_height = height;
        header.m_channels = kChannels;
        header.m_format = outFormat;
        header.m_srgb = srgb;
        header.m_mipCount = mipCount;

        const size_t headerSize = sizeof(NtexHeader);
        const size_t mipTableSize = sizeof(TextureMipLevel) * mipCount;
        const size_t totalSize = headerSize + mipTableSize + outPixelBytes;

        result.m_intermediateData.Resize(totalSize);
        uint8_t* blob = result.m_intermediateData.Data();

        std::memcpy(blob, &header, headerSize);
        size_t offset = headerSize;

        auto* mipTable = reinterpret_cast<TextureMipLevel*>(blob + offset);
        offset += mipTableSize;

        if (compressBc7)
        {
            EnsureBc7EncInitialized();
            bc7enc_compress_block_params params{};
            bc7enc_compress_block_params_init(&params);
            // Linear (non-perceptual) weights so we don't bias colour fidelity by Y/Cb/Cr — the
            // perceptual mode is tuned for "view as image" output, not engine-bound textures
            // where albedo, masks and packed channels all share the same code path.
            bc7enc_compress_block_params_init_linear_weights(&params);

            uint32_t writeCursor = 0;
            for (uint8_t i = 0; i < mipCount; ++i)
            {
                const TextureMipLevel& source = rgbaMipLevels[i];
                const size_t blockBytes = Bc7BlockBytes(source.m_width, source.m_height);
                uint8_t* mipDst = blob + offset + writeCursor;
                EncodeMipBc7(rgbaPyramid + source.m_offset, source.m_width, source.m_height, mipDst, params);
                mipTable[i] = TextureMipLevel{source.m_width, source.m_height, writeCursor,
                                              static_cast<uint32_t>(blockBytes)};
                writeCursor += static_cast<uint32_t>(blockBytes);
            }
        }
        else
        {
            std::memcpy(blob + offset, rgbaPyramid, rgbaPyramidBytes);
            for (uint8_t i = 0; i < mipCount; ++i)
            {
                mipTable[i] = rgbaMipLevels[i];
            }
        }

        context.GetAllocator().Deallocate(rgbaPyramid);
        stbi_image_free(pixels);
        if (downscaled != nullptr)
        {
            context.GetAllocator().Deallocate(downscaled);
        }

        result.m_success = true;
        return result;
    }
} // namespace nectar
