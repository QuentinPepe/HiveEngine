#include <nectar/hive/hive_document.h>
#include <nectar/hive/hive_parser.h>
#include <nectar/shader_program/shader_program_asset.h>

#include <cstring>

namespace nectar
{
    namespace
    {
        VertexDomain ParseDomain(wax::StringView s) noexcept
        {
            if (s == wax::StringView{"SkinnedMesh"})
                return VertexDomain::SkinnedMesh;
            if (s == wax::StringView{"UI"})
                return VertexDomain::UI;
            return VertexDomain::StaticMesh;
        }

        ShadingModel ParseShadingModel(wax::StringView s) noexcept
        {
            if (s == wax::StringView{"Standard"})
                return ShadingModel::Standard;
            return ShadingModel::Unlit;
        }

        ParamUiHint ParseUiHint(wax::StringView s) noexcept
        {
            if (s == wax::StringView{"color"})
                return ParamUiHint::Color;
            if (s == wax::StringView{"range"})
                return ParamUiHint::Range;
            return ParamUiHint::Default;
        }

        wax::String StripPrefix(wax::StringView section, wax::StringView prefix, comb::DefaultAllocator& alloc)
        {
            if (section.Size() <= prefix.Size())
                return wax::String{alloc};
            if (std::memcmp(section.Data(), prefix.Data(), prefix.Size()) != 0)
                return wax::String{alloc};
            const size_t start = prefix.Size();
            return wax::String{alloc, wax::StringView{section.Data() + start, section.Size() - start}};
        }
    } // namespace

    ShaderProgramAsset* ShaderProgramAssetLoader::Load(wax::ByteSpan data, comb::DefaultAllocator& alloc)
    {
        if (data.IsEmpty())
            return nullptr;

        wax::StringView manifestText;

        if (data.Size() >= sizeof(NshpHeader))
        {
            NshpHeader hdr{};
            std::memcpy(&hdr, data.Data(), sizeof(NshpHeader));
            if (hdr.m_magic == kNshpMagic)
            {
                const size_t manifestEnd = static_cast<size_t>(hdr.m_manifestOffset) + hdr.m_manifestLength;
                if (manifestEnd > data.Size())
                    return nullptr;
                manifestText = wax::StringView{reinterpret_cast<const char*>(data.Data() + hdr.m_manifestOffset),
                                               hdr.m_manifestLength};
            }
        }
        if (manifestText.IsEmpty())
        {
            // Raw .hshader file from VFS — no NSHP wrapper yet (e.g. dev mode before cook).
            manifestText = wax::StringView{reinterpret_cast<const char*>(data.Data()), data.Size()};
        }
        auto parsed = HiveParser::Parse(manifestText, alloc);
        if (!parsed.m_errors.IsEmpty())
            return nullptr;

        auto* asset = comb::New<ShaderProgramAsset>(alloc);
        asset->m_name = wax::String{alloc, parsed.m_document.GetString("shader", "name")};
        asset->m_vertexPath = wax::String{alloc, parsed.m_document.GetString("shader", "vertex")};
        asset->m_pixelPath = wax::String{alloc, parsed.m_document.GetString("shader", "pixel")};
        asset->m_domain = ParseDomain(parsed.m_document.GetString("shader", "domain"));
        asset->m_shadingModel = ParseShadingModel(parsed.m_document.GetString("shader", "shading_model"));

        if (parsed.m_document.HasSection("render_state"))
        {
            ShaderRenderState rs;
            rs.m_cull =
                wax::String{alloc, parsed.m_document.GetString("render_state", "cull", wax::StringView{"Back"})};
            rs.m_blend =
                wax::String{alloc, parsed.m_document.GetString("render_state", "blend", wax::StringView{"Opaque"})};
            rs.m_fill =
                wax::String{alloc, parsed.m_document.GetString("render_state", "fill", wax::StringView{"Solid"})};
            rs.m_depthTest = parsed.m_document.GetBool("render_state", "depth_test", true);
            rs.m_depthWrite = parsed.m_document.GetBool("render_state", "depth_write", true);
            rs.m_frontCcw = parsed.m_document.GetBool("render_state", "front_ccw", true);
            asset->m_renderState = static_cast<ShaderRenderState&&>(rs);
        }

        asset->m_features = wax::Vector<ShaderFeatureToggle>{alloc};
        asset->m_paramAnnotations = wax::Vector<ShaderParamAnnotation>{alloc};
        asset->m_textureAnnotations = wax::Vector<ShaderTextureAnnotation>{alloc};

        constexpr wax::StringView kParamsPrefix{"parameters."};
        constexpr wax::StringView kTexturesPrefix{"textures."};

        for (auto it = parsed.m_document.Sections().Begin(); it != parsed.m_document.Sections().End(); ++it)
        {
            const wax::StringView sectionName = it.Key().View();

            if (sectionName == wax::StringView{"features"})
            {
                for (auto kv = it.Value().Begin(); kv != it.Value().End(); ++kv)
                {
                    if (kv.Value().m_type != HiveValue::Type::BOOL)
                        continue;
                    ShaderFeatureToggle f;
                    f.m_name = wax::String{alloc, kv.Key().View()};
                    f.m_defaultValue = kv.Value().AsBool();
                    asset->m_features.PushBack(static_cast<ShaderFeatureToggle&&>(f));
                }
                continue;
            }

            if (sectionName.Size() > kParamsPrefix.Size() &&
                std::memcmp(sectionName.Data(), kParamsPrefix.Data(), kParamsPrefix.Size()) == 0)
            {
                ShaderParamAnnotation p;
                p.m_name = StripPrefix(sectionName, kParamsPrefix, alloc);
                p.m_uiHint = ParseUiHint(parsed.m_document.GetString(sectionName, "ui"));
                p.m_min = static_cast<float>(parsed.m_document.GetFloat(sectionName, "min", 0.0));
                p.m_max = static_cast<float>(parsed.m_document.GetFloat(sectionName, "max", 1.0));
                p.m_default = wax::Vector<float>{alloc};

                if (const auto* arr = parsed.m_document.GetFloatArray(sectionName, "default"))
                {
                    for (size_t i = 0; i < arr->Size(); ++i)
                        p.m_default.PushBack(static_cast<float>((*arr)[i]));
                }
                else if (const auto* raw = parsed.m_document.GetValue(sectionName, "default");
                         raw != nullptr &&
                         (raw->m_type == HiveValue::Type::FLOAT || raw->m_type == HiveValue::Type::INT))
                {
                    const double v =
                        raw->m_type == HiveValue::Type::FLOAT ? raw->AsFloat() : static_cast<double>(raw->AsInt());
                    p.m_default.PushBack(static_cast<float>(v));
                }

                asset->m_paramAnnotations.PushBack(static_cast<ShaderParamAnnotation&&>(p));
                continue;
            }

            if (sectionName.Size() > kTexturesPrefix.Size() &&
                std::memcmp(sectionName.Data(), kTexturesPrefix.Data(), kTexturesPrefix.Size()) == 0)
            {
                ShaderTextureAnnotation t;
                t.m_name = StripPrefix(sectionName, kTexturesPrefix, alloc);
                t.m_defaultPath = wax::String{alloc, parsed.m_document.GetString(sectionName, "default")};
                t.m_sampler = wax::String{
                    alloc, parsed.m_document.GetString(sectionName, "sampler", wax::StringView{"LinearWrap"})};
                asset->m_textureAnnotations.PushBack(static_cast<ShaderTextureAnnotation&&>(t));
                continue;
            }
        }

        return asset;
    }

    void ShaderProgramAssetLoader::Unload(ShaderProgramAsset* asset, comb::DefaultAllocator& alloc)
    {
        if (asset == nullptr)
            return;
        comb::Delete(alloc, asset);
    }

    size_t ShaderProgramAssetLoader::SizeOf(const ShaderProgramAsset* asset) const
    {
        if (asset == nullptr)
            return 0;
        size_t size = sizeof(ShaderProgramAsset);
        size += asset->m_features.Size() * sizeof(ShaderFeatureToggle);
        size += asset->m_paramAnnotations.Size() * sizeof(ShaderParamAnnotation);
        size += asset->m_textureAnnotations.Size() * sizeof(ShaderTextureAnnotation);
        return size;
    }
} // namespace nectar
