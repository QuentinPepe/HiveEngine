#include <hive/core/log.h>
#include <hive/platform/file_mapping.h>

#include <comb/default_allocator.h>
#include <comb/system_allocator.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>

#include <nectar/core/content_hash.h>
#include <nectar/database/asset_database.h>
#include <nectar/database/asset_record.h>
#include <nectar/import/gltf_import_job.h>
#include <nectar/material/material_data.h>
#include <nectar/material/material_serializer.h>
#include <nectar/mesh/gltf_importer.h>
#include <nectar/mesh/gltf_material.h>
#include <nectar/pipeline/import_pipeline.h>
#include <nectar/registry/hiveid_file.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace nectar
{

    static const hive::LogCategory LOG_IMPORT{"Nectar.GltfImport"};

    static bool IsTextureExtension(wax::StringView ext)
    {
        return ext.Equals(".jpg") || ext.Equals(".jpeg") || ext.Equals(".png") || ext.Equals(".tga") ||
               ext.Equals(".bmp") || ext.Equals(".hdr");
    }

    static wax::String ToLowerStem(const std::filesystem::path& p)
    {
        auto stem = p.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return std::tolower(c); });
        return wax::String{stem.c_str()};
    }

    static nectar::AssetId MakeAssetIdFromPath(wax::StringView relativePath)
    {
        auto hash = ContentHash::FromData(relativePath.Data(), relativePath.Size());
        return AssetId{hash.High(), hash.Low()};
    }

    static AssetId LookupTextureGuid(const wax::String& textureName, const std::filesystem::path& modelDir,
                                     const std::filesystem::path& assetsDir, comb::DefaultAllocator& alloc)
    {
        if (textureName.IsEmpty())
            return AssetId::Invalid();

        std::filesystem::path texPath = modelDir / textureName.CStr();
        if (!std::filesystem::exists(texPath))
            return AssetId::Invalid();

        HiveIdData hid{};
        if (ReadHiveId(texPath.string().c_str(), hid, alloc))
            return hid.m_guid;

        std::error_code ec;
        auto rel = std::filesystem::relative(texPath, assetsDir, ec).generic_string();
        return MakeAssetIdFromPath(wax::StringView{rel.c_str()});
    }

    GltfImportResult ExecuteGltfImport(const GltfImportDesc& desc, AssetDatabase& db, comb::DefaultAllocator& alloc,
                                       ImportPipeline* pipeline, GltfProgressFn progress, void* progressUserData)
    {
        auto report = [&](const char* step, uint32_t cur, uint32_t tot) {
            if (progress)
                progress(step, cur, tot, progressUserData);
        };
        GltfImportResult result{};
        std::error_code ec;

        std::string gltfStr{desc.m_gltfPath.Data(), desc.m_gltfPath.Size()};
        std::string assetsStr{desc.m_assetsDir.Data(), desc.m_assetsDir.Size()};

        hive::LogInfo(LOG_IMPORT, "Starting glTF import: {}", gltfStr);

        std::filesystem::path gltfPath{gltfStr};
        std::filesystem::path assetsDir{assetsStr};

        if (!std::filesystem::exists(gltfPath, ec))
        {
            result.m_error = wax::String{"glTF file not found"};
            hive::LogError(LOG_IMPORT, "glTF file not found: {}", gltfStr);
            return result;
        }

        wax::String modelName = ToLowerStem(gltfPath);
        std::filesystem::path srcDir = gltfPath.parent_path();
        std::filesystem::path modelDir = assetsDir / modelName.CStr();

        std::filesystem::create_directories(modelDir, ec);

        for (const auto& entry : std::filesystem::directory_iterator(srcDir, ec))
        {
            if (!entry.is_regular_file())
                continue;

            auto extStd = entry.path().extension().string();
            std::transform(extStd.begin(), extStd.end(), extStd.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            if (!IsTextureExtension(wax::StringView{extStd.c_str()}))
                continue;

            std::filesystem::path dstTexture = modelDir / entry.path().filename();
            std::filesystem::copy_file(entry.path(), dstTexture, std::filesystem::copy_options::skip_existing, ec);
            if (ec)
            {
                hive::LogWarning(LOG_IMPORT, "Failed to copy texture: {}", entry.path().filename().string());
                ec.clear();
                continue;
            }

            auto relativePath = std::filesystem::relative(dstTexture, assetsDir, ec).generic_string();
            auto assetId = MakeAssetIdFromPath(wax::StringView{relativePath.c_str()});

            HiveIdData texHid{assetId, wax::String{"Texture"}};
            WriteHiveId(dstTexture.string().c_str(), texHid);

            if (!db.Contains(assetId))
            {
                AssetRecord rec{};
                rec.m_uuid = assetId;
                rec.m_path = wax::String{relativePath.c_str()};
                rec.m_type = wax::String{"Texture"};
                rec.m_name = wax::String{entry.path().stem().string().c_str()};
                db.Insert(static_cast<AssetRecord&&>(rec));
            }

            if (pipeline)
            {
                ImportRequest req;
                req.m_sourcePath = wax::StringView{relativePath.c_str()};
                req.m_assetId = assetId;
                (void)pipeline->ImportAsset(req);
            }

            ++result.m_textureCount;
            report("Copying textures", result.m_textureCount, 0);
        }

        hive::LogInfo(LOG_IMPORT, "Textures copied: {}", result.m_textureCount);

        hive::FileMapping gltfFile{};
        if (!gltfFile.Open(gltfPath.string().c_str()) || gltfFile.Size() == 0)
        {
            result.m_error = wax::String{"Failed to read glTF file"};
            return result;
        }

        hive::LogInfo(LOG_IMPORT, "Mapped glTF: {} bytes", gltfFile.Size());

        // Extract embedded textures (buffer-view images / data URIs) into modelDir
        // and register them as assets. External-URI images are already handled by
        // the sibling-copy pass above.
        {
            wax::ByteSpan extractSpan{gltfFile.Data(), gltfFile.Size()};
            auto modelDirStr = modelDir.generic_string();
            auto extracted = ExtractEmbeddedTextures(extractSpan, modelDirStr.c_str(), alloc);

            for (size_t i = 0; i < extracted.Size(); ++i)
            {
                const auto& entry = extracted[i];
                if (entry.m_fileName.IsEmpty())
                    continue;

                std::filesystem::path dstTexture = modelDir / entry.m_fileName.CStr();
                auto relativePath = std::filesystem::relative(dstTexture, assetsDir, ec).generic_string();
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                auto assetId = MakeAssetIdFromPath(wax::StringView{relativePath.c_str()});

                HiveIdData texHid{assetId, wax::String{"Texture"}};
                WriteHiveId(dstTexture.string().c_str(), texHid);

                if (!db.Contains(assetId))
                {
                    AssetRecord rec{};
                    rec.m_uuid = assetId;
                    rec.m_path = wax::String{relativePath.c_str()};
                    rec.m_type = wax::String{"Texture"};
                    rec.m_name = wax::String{dstTexture.stem().string().c_str()};
                    db.Insert(static_cast<AssetRecord&&>(rec));
                }

                if (pipeline)
                {
                    ImportRequest req;
                    req.m_sourcePath = wax::StringView{relativePath.c_str()};
                    req.m_assetId = assetId;
                    (void)pipeline->ImportAsset(req);
                }

                ++result.m_textureCount;
                report("Extracting embedded textures", result.m_textureCount, 0);
            }
            hive::LogInfo(LOG_IMPORT, "Embedded textures extracted; total textures: {}", result.m_textureCount);
        }

        {
            GltfImporter importer{};
            HiveDocument settings{alloc};
            auto gltfPathStr = gltfPath.generic_string();
            settings.SetValue("import", "base_path",
                              HiveValue::MakeString(alloc, wax::StringView{gltfPathStr.c_str()}));

            wax::ByteSpan meshSpan{gltfFile.Data(), gltfFile.Size()};
            auto meshRelName = std::string{modelName.CStr()} + ".nmsh";
            auto meshAssetId = MakeAssetIdFromPath(wax::StringView{meshRelName.c_str()});

            comb::ModuleAllocator importAlloc{"GltfMeshImport", 16 * 1024 * 1024};
            comb::SystemAllocator importBigAlloc{"GltfMeshImport.Big"};
            ImportContext ctx{importAlloc.Get(), db, meshAssetId};
            ctx.SetLargeBufferAllocator(comb::MemoryResource{importBigAlloc});

            report("Importing mesh", 0, 1);
            hive::LogInfo(LOG_IMPORT, "Importing mesh...");
            auto meshResult = importer.Import(meshSpan, settings, ctx);
            hive::LogInfo(LOG_IMPORT, "Mesh import done: success={}", meshResult.m_success);

            if (meshResult.m_success && meshResult.m_intermediateData.Size() > 0)
            {
                std::filesystem::path nmshPath = modelDir / (std::string{modelName.CStr()} + ".nmsh");

                FILE* nmshFile = nullptr;
#ifdef _MSC_VER
                fopen_s(&nmshFile, nmshPath.string().c_str(), "wb");
#else
                nmshFile = fopen(nmshPath.string().c_str(), "wb");
#endif
                if (nmshFile)
                {
                    fwrite(meshResult.m_intermediateData.Data(), 1, meshResult.m_intermediateData.Size(), nmshFile);
                    fclose(nmshFile);

                    auto meshRelative = std::filesystem::relative(nmshPath, assetsDir, ec).generic_string();
                    auto meshAssetId = MakeAssetIdFromPath(wax::StringView{meshRelative.c_str()});

                    HiveIdData meshHid{meshAssetId, wax::String{"Mesh"}};
                    WriteHiveId(nmshPath.string().c_str(), meshHid);

                    if (!db.Contains(meshAssetId))
                    {
                        AssetRecord rec{};
                        rec.m_uuid = meshAssetId;
                        rec.m_path = wax::String{meshRelative.c_str()};
                        rec.m_type = wax::String{"Mesh"};
                        rec.m_name = modelName;
                        db.Insert(static_cast<AssetRecord&&>(rec));
                    }

                    if (pipeline)
                    {
                        ImportRequest req;
                        req.m_sourcePath = wax::StringView{meshRelative.c_str()};
                        req.m_assetId = meshAssetId;
                        (void)pipeline->ImportAsset(req);
                    }

                    hive::LogInfo(LOG_IMPORT, "Wrote mesh: {}", nmshPath.generic_string());
                }
                else
                {
                    hive::LogWarning(LOG_IMPORT, "Failed to write .nmsh: {}", nmshPath.generic_string());
                }
            }
            else
            {
                hive::LogWarning(LOG_IMPORT, "Mesh import produced no geometry");
            }
        }

        wax::ByteSpan gltfSpan{gltfFile.Data(), gltfFile.Size()};
        auto materials = ParseGltfMaterials(gltfSpan, alloc);

        for (size_t mi = 0; mi < materials.Size(); ++mi)
        {
            const auto& src = materials[mi];

            MaterialData matData{alloc};
            matData.m_shaderPath = wax::String{alloc, wax::StringView{"engine/standard.hshader"}};

            HiveValue baseColor = HiveValue::MakeFloatArray(alloc);
            for (size_t i = 0; i < 4; ++i)
                baseColor.PushFloat(static_cast<double>(src.m_baseColorFactor[i]));
            matData.m_paramOverrides.Insert(wax::String{alloc, wax::StringView{"base_color_factor"}},
                                            static_cast<HiveValue&&>(baseColor));
            matData.m_paramOverrides.Insert(wax::String{alloc, wax::StringView{"metallic_factor"}},
                                            HiveValue::MakeFloat(static_cast<double>(src.m_metallicFactor)));
            matData.m_paramOverrides.Insert(wax::String{alloc, wax::StringView{"roughness_factor"}},
                                            HiveValue::MakeFloat(static_cast<double>(src.m_roughnessFactor)));

            const AssetId albedoId = LookupTextureGuid(src.m_baseColorTexture, modelDir, assetsDir, alloc);
            if (albedoId.IsValid())
                matData.m_textureBindings.Insert(wax::String{alloc, wax::StringView{"albedo_map"}}, albedoId);
            const AssetId normalId = LookupTextureGuid(src.m_normalTexture, modelDir, assetsDir, alloc);
            if (normalId.IsValid())
                matData.m_textureBindings.Insert(wax::String{alloc, wax::StringView{"normal_map"}}, normalId);
            const AssetId mrId = LookupTextureGuid(src.m_metallicRoughnessTexture, modelDir, assetsDir, alloc);
            if (mrId.IsValid())
                matData.m_textureBindings.Insert(wax::String{alloc, wax::StringView{"metallic_roughness_map"}}, mrId);

            if (src.m_doubleSided)
                matData.m_featureOverrides.Insert(wax::String{alloc, wax::StringView{"double_sided"}}, true);
            if (src.m_alphaCutoff > 0.f)
            {
                matData.m_featureOverrides.Insert(wax::String{alloc, wax::StringView{"alpha_test"}}, true);
                matData.m_paramOverrides.Insert(wax::String{alloc, wax::StringView{"alpha_cutoff"}},
                                                HiveValue::MakeFloat(static_cast<double>(src.m_alphaCutoff)));
            }

            wax::String matName = src.m_name.IsEmpty() ? [&] {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Material_%zu", mi);
                return wax::String{buf};
            }()
                                                       : wax::String{src.m_name.View()};

            std::filesystem::path matFilePath = modelDir / (std::string{matName.CStr()} + ".hmat");
            wax::String matPathStr{matFilePath.generic_string().c_str()};

            if (!std::filesystem::exists(matFilePath, ec))
            {
                if (SaveMaterial(matData, matPathStr.View(), alloc))
                {
                    hive::LogInfo(LOG_IMPORT, "Saved material: {}", matPathStr.CStr());
                }
                else
                {
                    hive::LogWarning(LOG_IMPORT, "Failed to save material: {}", matName.CStr());
                }
            }

            auto relativePath = std::filesystem::relative(matFilePath, assetsDir, ec).generic_string();
            auto assetId = MakeAssetIdFromPath(wax::StringView{relativePath.c_str()});

            HiveIdData matHid{assetId, wax::String{"Material"}};
            WriteHiveId(matFilePath.string().c_str(), matHid);

            if (!db.Contains(assetId))
            {
                AssetRecord rec{};
                rec.m_uuid = assetId;
                rec.m_path = wax::String{relativePath.c_str()};
                rec.m_type = wax::String{"Material"};
                rec.m_name = matName;
                db.Insert(static_cast<AssetRecord&&>(rec));
            }

            if (pipeline)
            {
                ImportRequest req;
                req.m_sourcePath = wax::StringView{relativePath.c_str()};
                req.m_assetId = assetId;
                (void)pipeline->ImportAsset(req);
            }

            ++result.m_materialCount;
            report("Saving materials", result.m_materialCount, static_cast<uint32_t>(materials.Size()));
        }

        result.m_success = true;
        hive::LogInfo(LOG_IMPORT, "Import complete: {} textures, {} materials, {} meshes", result.m_textureCount,
                      result.m_materialCount, result.m_meshCount);
        return result;
    }

} // namespace nectar
