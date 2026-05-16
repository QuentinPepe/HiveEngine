#include <waggle/project/ship_pak_writer.h>

#include <hive/core/log.h>

#include <wax/containers/string.h>
#include <wax/serialization/byte_buffer.h>

#include <nectar/core/asset_id.h>
#include <nectar/core/content_hash.h>
#include <nectar/pak/asset_manifest.h>
#include <nectar/pak/pak_builder.h>
#include <nectar/vfs/path.h>
#include <nectar/vfs/virtual_filesystem.h>

#include <waggle/project/project_manager.h>
#include <waggle/scene/asset_reachability.h>

#include <cstdio>

namespace waggle
{
    namespace
    {
        const hive::LogCategory LOG_SHIP_PAK{"Waggle.ShipPak"};

        void FormatCookedKey(nectar::AssetId id, char (&out)[48])
        {
            std::snprintf(out, sizeof(out), "__cooked/%016llx%016llx", static_cast<unsigned long long>(id.High()),
                          static_cast<unsigned long long>(id.Low()));
        }
    } // namespace

    bool WriteShipPak(const ShipPakInput& input, ShipPakStats* stats, ShipPakProgressFn progress, void* userdata,
                      comb::DefaultAllocator& alloc)
    {
        if (input.m_assets == nullptr || input.m_project == nullptr || input.m_outputPath.IsEmpty())
        {
            hive::LogError(LOG_SHIP_PAK, "Invalid input");
            return false;
        }

        ShipPakStats local{};
        nectar::PakBuilder builder{alloc};
        nectar::AssetManifest manifest{alloc};

        const uint32_t total = static_cast<uint32_t>(input.m_assets->m_vfsPaths.Size() +
                                                     input.m_assets->m_cookedIds.Size());
        uint32_t current = 0;

        nectar::VirtualFilesystem& vfs = input.m_project->VFS();

        for (size_t i = 0; i < input.m_assets->m_vfsPaths.Size(); ++i)
        {
            const wax::StringView path = input.m_assets->m_vfsPaths[i].View();
            ++current;
            if (progress != nullptr)
            {
                progress(current, total, input.m_assets->m_vfsPaths[i].CStr(), userdata);
            }

            wax::ByteBuffer blob = vfs.ReadSync(path);
            if (blob.IsEmpty())
            {
                hive::LogWarning(LOG_SHIP_PAK, "Skip empty VFS blob: {}", wax::String{alloc, path}.CStr());
                ++local.m_skippedCount;
                continue;
            }

            const nectar::ContentHash hash = nectar::ContentHash::FromData(blob.Data(), blob.Size());
            builder.AddBlob(hash, wax::ByteSpan{blob.Data(), blob.Size()}, input.m_compression);
            const wax::String normalized = nectar::NormalizePath(path, alloc);
            manifest.Add(normalized.View(), hash);
            ++local.m_blobCount;
            local.m_uncompressedBytes += blob.Size();
        }

        for (size_t i = 0; i < input.m_assets->m_cookedIds.Size(); ++i)
        {
            const nectar::AssetId id = input.m_assets->m_cookedIds[i];
            char key[48];
            FormatCookedKey(id, key);
            ++current;
            if (progress != nullptr)
            {
                progress(current, total, key, userdata);
            }

            wax::ByteBuffer blob = input.m_project->ReadCookedBlob(id);
            if (blob.IsEmpty())
            {
                hive::LogWarning(LOG_SHIP_PAK, "Skip empty cooked blob: {}", key);
                ++local.m_skippedCount;
                continue;
            }

            const nectar::ContentHash hash = nectar::ContentHash::FromData(blob.Data(), blob.Size());
            builder.AddBlob(hash, wax::ByteSpan{blob.Data(), blob.Size()}, input.m_compression);
            const wax::String normalized =
                nectar::NormalizePath(wax::StringView{key, std::strlen(key)}, alloc);
            manifest.Add(normalized.View(), hash);
            ++local.m_blobCount;
            local.m_uncompressedBytes += blob.Size();
        }

        if (input.m_projectTomlBlob.Size() > 0)
        {
            const nectar::ContentHash hash =
                nectar::ContentHash::FromData(input.m_projectTomlBlob.Data(), input.m_projectTomlBlob.Size());
            builder.AddBlob(hash, input.m_projectTomlBlob, nectar::CompressionMethod::NONE);
            constexpr const char* kProjectKey = "__project.hive";
            manifest.Add(wax::StringView{kProjectKey, std::strlen(kProjectKey)}, hash);
            ++local.m_blobCount;
        }

        builder.SetManifest(manifest);
        if (!builder.Build(input.m_outputPath))
        {
            hive::LogError(LOG_SHIP_PAK, "Pak build failed: {}", wax::String{alloc, input.m_outputPath}.CStr());
            return false;
        }

        if (stats != nullptr)
        {
            *stats = local;
        }
        hive::LogInfo(LOG_SHIP_PAK, "Pak written: {} blobs, {} skipped, {} bytes raw", local.m_blobCount,
                      local.m_skippedCount, local.m_uncompressedBytes);
        return true;
    }
} // namespace waggle
