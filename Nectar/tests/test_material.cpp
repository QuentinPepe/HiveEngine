#include <comb/default_allocator.h>

#include <nectar/assets/material_asset.h>
#include <nectar/core/asset_id.h>
#include <nectar/database/asset_database.h>
#include <nectar/database/asset_record.h>
#include <nectar/hive/hive_document.h>
#include <nectar/material/material_importer.h>
#include <nectar/pipeline/import_context.h>

#include <larvae/larvae.h>

#include <cstring>

namespace
{
    comb::DefaultAllocator& GetAlloc()
    {
        static comb::ModuleAllocator allocator{"TestMaterial", 4 * 1024 * 1024};
        return allocator.Get();
    }

    nectar::AssetId MakeId(uint64_t v)
    {
        uint8_t bytes[16] = {};
        std::memcpy(bytes, &v, sizeof(v));
        return nectar::AssetId::FromBytes(bytes);
    }

    void InsertAssetRecord(nectar::AssetDatabase& db, nectar::AssetId id, const char* path, const char* type)
    {
        nectar::AssetRecord record;
        record.m_uuid = id;
        record.m_path = wax::String{GetAlloc(), path};
        record.m_type = wax::String{GetAlloc(), type};
        db.Insert(std::move(record));
    }

    constexpr const char* kMaterialSource = R"([material]
shader = "shaders/wave.hshader"

[parameters]
albedo_tint = [0.2, 0.8, 1.0]
pulse_speed = 3.0

[textures]
albedo_map = "{0fab62552bb7db60e7ed12e1f1195c71}"

[features]
alpha_test = true
)";

    static const auto kImportDeclaresDeps = larvae::RegisterTest(
        "NectarMaterial", "ImportDeclaresShaderAndTextureDeps", []() {
            nectar::AssetDatabase db{GetAlloc()};
            const auto matId = MakeId(0x401);
            InsertAssetRecord(db, matId, "materials/wave.hmat", "Material");

            const auto shaderId = MakeId(0x402);
            InsertAssetRecord(db, shaderId, "shaders/wave.hshader", "ShaderProgram");

            const auto albedoId =
                nectar::AssetId::FromGuidString("{0fab62552bb7db60e7ed12e1f1195c71}", 34);
            InsertAssetRecord(db, albedoId, "textures/sponza/albedo.png", "Texture");

            wax::ByteSpan source{reinterpret_cast<const uint8_t*>(kMaterialSource),
                                  std::strlen(kMaterialSource)};
            nectar::HiveDocument settings{GetAlloc()};
            nectar::ImportContext ctx{GetAlloc(), db, matId};

            nectar::MaterialImporter importer;
            const auto result = importer.Import(source, settings, ctx);
            larvae::AssertTrue(result.m_success);

            const auto& deps = ctx.GetDeclaredDeps();
            larvae::AssertEqual(deps.Size(), size_t{2});
        });

    static const auto kLoaderRoundtrip = larvae::RegisterTest(
        "NectarMaterial", "LoaderParsesParamsTexturesFeatures", []() {
            nectar::MaterialAssetLoader loader;
            wax::ByteSpan source{reinterpret_cast<const uint8_t*>(kMaterialSource),
                                  std::strlen(kMaterialSource)};
            auto* asset = loader.Load(source, GetAlloc());
            larvae::AssertTrue(asset != nullptr);

            larvae::AssertTrue(asset->m_data.m_shaderPath.View() ==
                               wax::StringView{"shaders/wave.hshader"});

            // Parameters preserved untyped.
            wax::String tintKey{GetAlloc(), wax::StringView{"albedo_tint"}};
            const auto* tint = asset->m_data.m_paramOverrides.Find(tintKey);
            larvae::AssertTrue(tint != nullptr);
            larvae::AssertTrue(tint->m_type == nectar::HiveValue::Type::FLOAT_ARRAY);
            larvae::AssertEqual(tint->AsFloatArray().Size(), size_t{3});

            wax::String speedKey{GetAlloc(), wax::StringView{"pulse_speed"}};
            const auto* speed = asset->m_data.m_paramOverrides.Find(speedKey);
            larvae::AssertTrue(speed != nullptr);
            larvae::AssertTrue(speed->m_type == nectar::HiveValue::Type::FLOAT);

            // Textures resolved to AssetIds.
            wax::String albedoKey{GetAlloc(), wax::StringView{"albedo_map"}};
            const auto* albedoId = asset->m_data.m_textureBindings.Find(albedoKey);
            larvae::AssertTrue(albedoId != nullptr);
            larvae::AssertTrue(albedoId->IsValid());

            // Feature toggles preserved.
            wax::String alphaKey{GetAlloc(), wax::StringView{"alpha_test"}};
            const auto* alpha = asset->m_data.m_featureOverrides.Find(alphaKey);
            larvae::AssertTrue(alpha != nullptr);
            larvae::AssertTrue(*alpha);

            loader.Unload(asset, GetAlloc());
        });

    static const auto kAssetIdGuidRoundtrip = larvae::RegisterTest(
        "NectarAssetId", "GuidStringRoundtrip", []() {
            const nectar::AssetId original{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
            char buf[35];
            original.ToGuidString(buf);
            const nectar::AssetId parsed = nectar::AssetId::FromGuidString(buf, 34);
            larvae::AssertTrue(parsed == original);
        });
} // namespace
