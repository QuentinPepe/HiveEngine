#include <hive/core/log.h>

#include <wax/containers/vector.h>

#include <swarm/platform/diligent_swarm.h>
#include <swarm/precomp.h>
#include <swarm/swarm.h>
#include <swarm/swarm_log.h>
#include <swarm/swarmmodule.h>

#include <GraphicsTypes.h>
#include <RefCntAutoPtr.hpp>
#include <diligent_internal.h>

namespace swarm
{
    namespace
    {
        using namespace Diligent;

        TEXTURE_FORMAT TranslateFormat(TextureFormat fmt)
        {
            switch (fmt)
            {
                case TextureFormat::RGBA8_UNORM:
                    return TEX_FORMAT_RGBA8_UNORM;
                case TextureFormat::R8_UNORM:
                    return TEX_FORMAT_R8_UNORM;
                case TextureFormat::RGBA8_SRGB:
                default:
                    return TEX_FORMAT_RGBA8_UNORM_SRGB;
            }
        }
    } // namespace

    Texture* CreateTexture(RenderContext* context, const TextureDesc& desc)
    {
        using namespace Diligent;

        if (context == nullptr || context->m_device == nullptr)
            return nullptr;
        if (desc.m_width == 0 || desc.m_height == 0 || desc.m_mipCount == 0)
            return nullptr;

        Diligent::TextureDesc td;
        td.Name = desc.m_debugName != nullptr ? desc.m_debugName : "Swarm Texture";
        td.Type = RESOURCE_DIM_TEX_2D;
        td.Width = desc.m_width;
        td.Height = desc.m_height;
        td.MipLevels = desc.m_mipCount;
        td.Format = TranslateFormat(desc.m_format);
        td.Usage = USAGE_IMMUTABLE;
        td.BindFlags = BIND_SHADER_RESOURCE;

        wax::Vector<TextureSubResData> subres{SwarmModule::GetInstance().GetAllocator()};
        subres.Reserve(desc.m_mipCount);
        for (uint32_t i = 0; i < desc.m_mipCount; ++i)
        {
            TextureSubResData s;
            s.pData = desc.m_mips[i].m_pixels;
            const uint32_t bpp = (desc.m_format == TextureFormat::R8_UNORM) ? 1u : 4u;
            s.Stride = static_cast<Uint64>(desc.m_mips[i].m_width) * bpp;
            subres.PushBack(s);
        }
        TextureData data;
        data.pSubResources = subres.Data();
        data.NumSubresources = static_cast<Uint32>(subres.Size());

        RefCntAutoPtr<ITexture> tex;
        context->m_device->CreateTexture(td, &data, &tex);
        if (!tex)
        {
            hive::LogError(LOG_SWARM, "CreateTexture: device->CreateTexture failed");
            return nullptr;
        }

        auto& allocator = SwarmModule::GetInstance().GetAllocator();
        auto* texture = comb::New<Texture>(allocator);
        texture->m_texture = tex.Detach();
        texture->m_shaderView = texture->m_texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        if (texture->m_shaderView != nullptr)
            texture->m_shaderView->AddRef();
        return texture;
    }

    void DestroyTexture(Texture* texture)
    {
        if (texture == nullptr)
            return;
        if (texture->m_shaderView != nullptr)
            texture->m_shaderView->Release();
        if (texture->m_texture != nullptr)
            texture->m_texture->Release();
        auto& allocator = SwarmModule::GetInstance().GetAllocator();
        comb::Delete(allocator, texture);
    }
} // namespace swarm
