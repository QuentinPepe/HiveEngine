#pragma once

#include <comb/new.h>

#include <nectar/server/asset_loader.h>
#include <nectar/shader/shader_data.h>

#include <cstring>

namespace nectar
{
    struct ShaderAsset
    {
        NsdrHeader m_header{};
        uint8_t* m_data{nullptr}; // owns the full NSDR blob
        size_t m_dataSize{0};

        [[nodiscard]] const char* EntryPoint() const noexcept
        {
            return reinterpret_cast<const char*>(m_data + m_header.m_entryOffset);
        }
        [[nodiscard]] uint32_t EntryLength() const noexcept
        {
            return m_header.m_entryLength;
        }
        [[nodiscard]] const uint8_t* Payload() const noexcept
        {
            return m_data + m_header.m_payloadOffset;
        }
        [[nodiscard]] uint32_t PayloadLength() const noexcept
        {
            return m_header.m_payloadLength;
        }
    };

    class ShaderAssetLoader final : public AssetLoader<ShaderAsset>
    {
    public:
        [[nodiscard]] ShaderAsset* Load(wax::ByteSpan data, comb::DefaultAllocator& alloc) override
        {
            if (data.Size() < sizeof(NsdrHeader))
            {
                return nullptr;
            }

            NsdrHeader hdr{};
            std::memcpy(&hdr, data.Data(), sizeof(NsdrHeader));
            if (hdr.m_magic != kNsdrMagic)
            {
                return nullptr;
            }

            const size_t entryEnd = static_cast<size_t>(hdr.m_entryOffset) + hdr.m_entryLength;
            const size_t payloadEnd = static_cast<size_t>(hdr.m_payloadOffset) + hdr.m_payloadLength;
            if (entryEnd > data.Size() || payloadEnd > data.Size())
            {
                return nullptr;
            }

            auto* blob = static_cast<uint8_t*>(alloc.Allocate(data.Size(), alignof(std::max_align_t)));
            if (blob == nullptr)
            {
                return nullptr;
            }
            std::memcpy(blob, data.Data(), data.Size());

            auto* asset = comb::New<ShaderAsset>(alloc);
            asset->m_header = hdr;
            asset->m_data = blob;
            asset->m_dataSize = data.Size();
            return asset;
        }

        void Unload(ShaderAsset* asset, comb::DefaultAllocator& alloc) override
        {
            if (asset == nullptr)
            {
                return;
            }
            if (asset->m_data != nullptr)
            {
                alloc.Deallocate(asset->m_data);
            }
            comb::Delete(alloc, asset);
        }

        [[nodiscard]] size_t SizeOf(const ShaderAsset* asset) const override
        {
            return sizeof(ShaderAsset) + (asset != nullptr ? asset->m_dataSize : 0);
        }
    };
} // namespace nectar
