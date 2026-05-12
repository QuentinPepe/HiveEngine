#pragma once

#include <hive/math/types.h>

#include <swarm/platform/diligent_swarm.h>
#include <swarm/swarm.h>

#include <Buffer.h>
#include <PipelineState.h>
#include <Sampler.h>
#include <ShaderResourceBinding.h>
#include <Texture.h>
#include <TextureView.h>

namespace swarm
{
    struct Mesh
    {
        Diligent::IBuffer* m_vertexBuffer{nullptr};
        Diligent::IBuffer* m_indexBuffer{nullptr};
        uint32_t m_indexCount{0};

        Submesh* m_submeshes{nullptr};
        uint32_t m_submeshCount{0};
    };

    struct Material
    {
        Diligent::IPipelineState* m_pipelineState{nullptr};
        Diligent::IShaderResourceBinding* m_resourceBinding{nullptr};
        VertexDomain m_domain{VertexDomain::STATIC_MESH};
        uint32_t m_materialIndex{0};
    };

    struct Texture
    {
        Diligent::ITexture* m_texture{nullptr};
        Diligent::ITextureView* m_shaderView{nullptr};
    };

    // GPU-side layouts. Must match the HLSL cbuffers in diligent_material.cpp.
    // Float3 is padded up to 16 bytes to satisfy std140-style alignment.
    struct ViewConstantsGpu
    {
        hive::math::Mat4 m_view;
        hive::math::Mat4 m_proj;
        hive::math::Float4 m_eyeWorld;     // xyz = eye, w = 0
        hive::math::Float4 m_sunDirection; // xyz = direction the light travels, w = 0
        hive::math::Float4 m_sunColor;     // rgb = color * intensity, a = 0
        hive::math::Float4 m_ambient;      // rgb = ambient, a = 0
    };
    static_assert(sizeof(ViewConstantsGpu) == 192, "ViewConstantsGpu must be 192 bytes");

    struct ObjectConstantsGpu
    {
        hive::math::Mat4 m_world;
        hive::math::Mat4 m_worldInvTranspose;
        uint32_t m_materialIndex;
        uint32_t m_pad[3];
    };
    static_assert(sizeof(ObjectConstantsGpu) == 144, "ObjectConstantsGpu must be 144 bytes");

    struct TimeConstantsGpu
    {
        float m_timeSeconds{0.f};
        float m_deltaSeconds{0.f};
        float m_padding[2]{};
    };
    static_assert(sizeof(TimeConstantsGpu) == 16, "TimeConstantsGpu must be 16 bytes");

    bool InitSceneConstants(RenderContext* context);
    void ShutdownSceneConstants(RenderContext* context);
} // namespace swarm
