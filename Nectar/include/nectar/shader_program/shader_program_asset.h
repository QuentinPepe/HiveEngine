#pragma once

#include <comb/default_allocator.h>
#include <comb/new.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/string.h>
#include <wax/containers/vector.h>

#include <nectar/server/asset_loader.h>
#include <nectar/shader_program/shader_program_data.h>

namespace nectar
{
    enum class ParamUiHint : uint8_t
    {
        Default = 0,
        Color = 1,
        Range = 2,
    };

    struct ShaderParamAnnotation
    {
        wax::String m_name;
        ParamUiHint m_uiHint{ParamUiHint::Default};
        float m_min{0.f};
        float m_max{1.f};
        wax::Vector<float> m_default;
    };

    struct ShaderTextureAnnotation
    {
        wax::String m_name;
        wax::String m_defaultPath;
        wax::String m_sampler;
    };

    struct ShaderFeatureToggle
    {
        wax::String m_name;
        bool m_defaultValue{false};
    };

    struct ShaderRenderState
    {
        wax::String m_cull{"Back"};
        wax::String m_blend{"Opaque"};
        wax::String m_fill{"Solid"};
        bool m_depthTest{true};
        bool m_depthWrite{true};
        bool m_frontCcw{true};
    };

    struct ShaderProgramAsset
    {
        wax::String m_name;
        wax::String m_vertexPath;
        wax::String m_pixelPath;
        VertexDomain m_domain{VertexDomain::StaticMesh};
        ShadingModel m_shadingModel{ShadingModel::Unlit};
        ShaderRenderState m_renderState;
        wax::Vector<ShaderFeatureToggle> m_features;
        wax::Vector<ShaderParamAnnotation> m_paramAnnotations;
        wax::Vector<ShaderTextureAnnotation> m_textureAnnotations;
    };

    class HIVE_API ShaderProgramAssetLoader final : public AssetLoader<ShaderProgramAsset>
    {
    public:
        [[nodiscard]] ShaderProgramAsset* Load(wax::ByteSpan data, comb::DefaultAllocator& alloc) override;
        void Unload(ShaderProgramAsset* asset, comb::DefaultAllocator& alloc) override;
        [[nodiscard]] size_t SizeOf(const ShaderProgramAsset* asset) const override;
    };
} // namespace nectar
