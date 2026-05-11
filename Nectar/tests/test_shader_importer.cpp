#include <comb/default_allocator.h>

#include <nectar/assets/shader_asset.h>
#include <nectar/core/asset_id.h>
#include <nectar/database/asset_database.h>
#include <nectar/database/asset_record.h>
#include <nectar/hive/hive_document.h>
#include <nectar/pipeline/import_context.h>
#include <nectar/shader/shader_data.h>
#include <nectar/shader/shader_importer.h>

#include <larvae/larvae.h>

#include <cstring>

namespace
{
    comb::DefaultAllocator& GetAlloc()
    {
        static comb::ModuleAllocator allocator{"TestShader", 4 * 1024 * 1024};
        return allocator.Get();
    }

    nectar::AssetId MakeId(uint64_t v)
    {
        uint8_t bytes[16] = {};
        std::memcpy(bytes, &v, sizeof(v));
        return nectar::AssetId::FromBytes(bytes);
    }

    nectar::HiveDocument MakeEmptyDoc()
    {
        return nectar::HiveDocument{GetAlloc()};
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

    static const auto kImportVertexShader = larvae::RegisterTest(
        "NectarShaderImporter", "ImportClassifiesVertexFromFilename", []() {
            nectar::AssetDatabase db{GetAlloc()};
            const auto id = MakeId(0x101);
            InsertAssetRecord(db, id, "shaders/test.vs.hlsl", "Shader");

            const char kSource[] = "void main() {}";
            wax::ByteSpan source{reinterpret_cast<const uint8_t*>(kSource), sizeof(kSource) - 1};
            const auto settings = MakeEmptyDoc();
            nectar::ImportContext ctx{GetAlloc(), db, id};

            nectar::ShaderImporter importer;
            const auto result = importer.Import(source, settings, ctx);
            larvae::AssertTrue(result.m_success);
            larvae::AssertTrue(result.m_intermediateData.Size() >= sizeof(nectar::NsdrHeader));

            nectar::NsdrHeader header{};
            std::memcpy(&header, result.m_intermediateData.Data(), sizeof(header));
            larvae::AssertEqual(header.m_magic, nectar::kNsdrMagic);
            larvae::AssertEqual(header.m_version, 1u);
            larvae::AssertTrue(header.m_stage == nectar::ShaderStage::Vertex);
            larvae::AssertTrue(header.m_payloadKind == nectar::ShaderPayloadKind::HlslSource);
            larvae::AssertEqual(header.m_payloadLength, static_cast<uint32_t>(sizeof(kSource) - 1));
        });

    static const auto kImportPixelShader = larvae::RegisterTest(
        "NectarShaderImporter", "ImportClassifiesPixelFromFilename", []() {
            nectar::AssetDatabase db{GetAlloc()};
            const auto id = MakeId(0x102);
            InsertAssetRecord(db, id, "shaders/test.ps.hlsl", "Shader");

            const char kSource[] = "float4 main():SV_TARGET{return 1;}";
            wax::ByteSpan source{reinterpret_cast<const uint8_t*>(kSource), sizeof(kSource) - 1};
            const auto settings = MakeEmptyDoc();
            nectar::ImportContext ctx{GetAlloc(), db, id};

            nectar::ShaderImporter importer;
            const auto result = importer.Import(source, settings, ctx);
            larvae::AssertTrue(result.m_success);

            nectar::NsdrHeader header{};
            std::memcpy(&header, result.m_intermediateData.Data(), sizeof(header));
            larvae::AssertTrue(header.m_stage == nectar::ShaderStage::Pixel);
        });

    static const auto kRoundTrip = larvae::RegisterTest(
        "NectarShaderImporter", "AssetLoaderRoundTrip", []() {
            nectar::AssetDatabase db{GetAlloc()};
            const auto id = MakeId(0x103);
            InsertAssetRecord(db, id, "shaders/round.cs.hlsl", "Shader");

            const char kSource[] = "[numthreads(1,1,1)] void main() {}";
            wax::ByteSpan source{reinterpret_cast<const uint8_t*>(kSource), sizeof(kSource) - 1};
            const auto settings = MakeEmptyDoc();
            nectar::ImportContext ctx{GetAlloc(), db, id};

            nectar::ShaderImporter importer;
            const auto result = importer.Import(source, settings, ctx);
            larvae::AssertTrue(result.m_success);

            nectar::ShaderAssetLoader loader;
            wax::ByteSpan blob{result.m_intermediateData.Data(), result.m_intermediateData.Size()};
            nectar::ShaderAsset* asset = loader.Load(blob, GetAlloc());
            larvae::AssertTrue(asset != nullptr);
            larvae::AssertTrue(asset->m_header.m_stage == nectar::ShaderStage::Compute);
            larvae::AssertEqual(asset->m_header.m_payloadLength,
                                static_cast<uint32_t>(sizeof(kSource) - 1));
            larvae::AssertEqual(std::memcmp(asset->Payload(), kSource, sizeof(kSource) - 1), 0);

            larvae::AssertTrue(std::strcmp(asset->EntryPoint(), "main") == 0);
            loader.Unload(asset, GetAlloc());
        });

    static const auto kRejectsBadMagic = larvae::RegisterTest(
        "NectarShaderImporter", "AssetLoaderRejectsBadMagic", []() {
            uint8_t bogus[sizeof(nectar::NsdrHeader)] = {};
            nectar::ShaderAssetLoader loader;
            nectar::ShaderAsset* asset = loader.Load({bogus, sizeof(bogus)}, GetAlloc());
            larvae::AssertTrue(asset == nullptr);
        });
} // namespace
