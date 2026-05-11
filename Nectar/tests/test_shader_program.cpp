#include <comb/default_allocator.h>

#include <nectar/core/asset_id.h>
#include <nectar/database/asset_database.h>
#include <nectar/database/asset_record.h>
#include <nectar/hive/hive_document.h>
#include <nectar/pipeline/import_context.h>
#include <nectar/shader_program/shader_program_asset.h>
#include <nectar/shader_program/shader_program_data.h>
#include <nectar/shader_program/shader_program_importer.h>

#include <larvae/larvae.h>

#include <cstring>

namespace
{
    comb::DefaultAllocator& GetAlloc()
    {
        static comb::ModuleAllocator allocator{"TestShaderProgram", 4 * 1024 * 1024};
        return allocator.Get();
    }

    nectar::AssetId MakeId(uint64_t v)
    {
        uint8_t bytes[16] = {};
        std::memcpy(bytes, &v, sizeof(v));
        return nectar::AssetId::FromBytes(bytes);
    }

    void InsertAssetRecord(nectar::AssetDatabase& db, nectar::AssetId id, const char* path,
                           const char* type)
    {
        nectar::AssetRecord record;
        record.m_uuid = id;
        record.m_path = wax::String{GetAlloc(), path};
        record.m_type = wax::String{GetAlloc(), type};
        db.Insert(std::move(record));
    }

    constexpr const char* kManifest = R"([shader]
name = "Wave"
vertex = "shaders/wave.vs.hlsl"
pixel = "shaders/wave.ps.hlsl"
domain = "StaticMesh"
shading_model = "Unlit"

[render_state]
cull = "Back"
blend = "Opaque"
depth_test = true
depth_write = true

[features]
use_normal_map = false
alpha_test = true

[parameters.albedo_tint]
ui = "color"
default = [0.2, 0.8, 1.0]

[parameters.pulse_speed]
ui = "range"
min = 0.0
max = 10.0
default = 3.0

[textures.albedo_map]
default = "textures/white.png"
sampler = "LinearWrap"
)";

    static const auto kImport = larvae::RegisterTest(
        "NectarShaderProgram", "ImportProducesNshpBlob", []() {
            nectar::AssetDatabase db{GetAlloc()};
            const auto progId = MakeId(0x201);
            InsertAssetRecord(db, progId, "shaders/wave.hshader", "ShaderProgram");

            const auto vsId = MakeId(0x202);
            InsertAssetRecord(db, vsId, "shaders/wave.vs.hlsl", "Shader");
            const auto psId = MakeId(0x203);
            InsertAssetRecord(db, psId, "shaders/wave.ps.hlsl", "Shader");

            wax::ByteSpan source{reinterpret_cast<const uint8_t*>(kManifest), std::strlen(kManifest)};
            nectar::HiveDocument settings{GetAlloc()};
            nectar::ImportContext ctx{GetAlloc(), db, progId};

            nectar::ShaderProgramImporter importer;
            const auto result = importer.Import(source, settings, ctx);
            larvae::AssertTrue(result.m_success);
            larvae::AssertTrue(result.m_intermediateData.Size() >= sizeof(nectar::NshpHeader));

            nectar::NshpHeader header{};
            std::memcpy(&header, result.m_intermediateData.Data(), sizeof(header));
            larvae::AssertEqual(header.m_magic, nectar::kNshpMagic);
            larvae::AssertEqual(header.m_version, 1u);
            larvae::AssertEqual(header.m_manifestLength, static_cast<uint32_t>(std::strlen(kManifest)));

            // Importer declares hard deps on VS and PS sources.
            const auto& deps = ctx.GetDeclaredDeps();
            larvae::AssertEqual(deps.Size(), size_t{2});
        });

    static const auto kRoundtrip = larvae::RegisterTest(
        "NectarShaderProgram", "LoaderRoundtrip", []() {
            nectar::AssetDatabase db{GetAlloc()};
            const auto progId = MakeId(0x301);
            InsertAssetRecord(db, progId, "shaders/wave.hshader", "ShaderProgram");

            wax::ByteSpan source{reinterpret_cast<const uint8_t*>(kManifest), std::strlen(kManifest)};
            nectar::HiveDocument settings{GetAlloc()};
            nectar::ImportContext ctx{GetAlloc(), db, progId};

            nectar::ShaderProgramImporter importer;
            const auto result = importer.Import(source, settings, ctx);
            larvae::AssertTrue(result.m_success);

            nectar::ShaderProgramAssetLoader loader;
            wax::ByteSpan cooked{result.m_intermediateData.Data(), result.m_intermediateData.Size()};
            auto* asset = loader.Load(cooked, GetAlloc());
            larvae::AssertTrue(asset != nullptr);

            larvae::AssertTrue(asset->m_name.View() == wax::StringView{"Wave"});
            larvae::AssertTrue(asset->m_vertexPath.View() == wax::StringView{"shaders/wave.vs.hlsl"});
            larvae::AssertTrue(asset->m_pixelPath.View() == wax::StringView{"shaders/wave.ps.hlsl"});
            larvae::AssertTrue(asset->m_domain == nectar::VertexDomain::StaticMesh);
            larvae::AssertTrue(asset->m_shadingModel == nectar::ShadingModel::Unlit);
            larvae::AssertTrue(asset->m_renderState.m_cull.View() == wax::StringView{"Back"});
            larvae::AssertTrue(asset->m_renderState.m_depthTest);

            larvae::AssertEqual(asset->m_features.Size(), size_t{2});
            larvae::AssertEqual(asset->m_paramAnnotations.Size(), size_t{2});
            larvae::AssertEqual(asset->m_textureAnnotations.Size(), size_t{1});

            // Find albedo_tint param and check it parsed as a 3-element float array.
            const nectar::ShaderParamAnnotation* tint = nullptr;
            const nectar::ShaderParamAnnotation* speed = nullptr;
            for (size_t i = 0; i < asset->m_paramAnnotations.Size(); ++i)
            {
                if (asset->m_paramAnnotations[i].m_name.View() == wax::StringView{"albedo_tint"})
                    tint = &asset->m_paramAnnotations[i];
                else if (asset->m_paramAnnotations[i].m_name.View() == wax::StringView{"pulse_speed"})
                    speed = &asset->m_paramAnnotations[i];
            }
            larvae::AssertTrue(tint != nullptr);
            larvae::AssertTrue(tint->m_uiHint == nectar::ParamUiHint::Color);
            larvae::AssertEqual(tint->m_default.Size(), size_t{3});
            larvae::AssertTrue(tint->m_default[0] > 0.19f && tint->m_default[0] < 0.21f);
            larvae::AssertTrue(tint->m_default[2] > 0.99f && tint->m_default[2] < 1.01f);

            larvae::AssertTrue(speed != nullptr);
            larvae::AssertTrue(speed->m_uiHint == nectar::ParamUiHint::Range);
            larvae::AssertEqual(speed->m_default.Size(), size_t{1});
            larvae::AssertTrue(speed->m_default[0] > 2.99f && speed->m_default[0] < 3.01f);
            larvae::AssertTrue(speed->m_min < 0.001f);
            larvae::AssertTrue(speed->m_max > 9.99f);

            loader.Unload(asset, GetAlloc());
        });

    static const auto kBadMagic = larvae::RegisterTest(
        "NectarShaderProgram", "LoaderRejectsBadMagic", []() {
            uint8_t junk[sizeof(nectar::NshpHeader) + 4] = {};
            nectar::ShaderProgramAssetLoader loader;
            wax::ByteSpan span{junk, sizeof(junk)};
            larvae::AssertTrue(loader.Load(span, GetAlloc()) == nullptr);
        });
} // namespace
