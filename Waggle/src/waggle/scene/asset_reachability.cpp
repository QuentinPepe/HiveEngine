#include <waggle/scene/asset_reachability.h>

#include <hive/core/log.h>

#include <wax/containers/string.h>

#include <queen/reflect/field_attributes.h>
#include <queen/reflect/field_info.h>
#include <queen/storage/archetype.h>
#include <queen/world/world.h>

#include <nectar/assets/material_asset.h>
#include <nectar/shader_program/shader_program_asset.h>
#include <nectar/vfs/virtual_filesystem.h>

#include <waggle/project/project_manager.h>
#include <waggle/scene/scene_io.h>

#include <cctype>
#include <cstring>
#include <filesystem>

namespace waggle::scene
{
    namespace
    {
        const hive::LogCategory LOG_REACH{"Waggle.Reachability"};

        bool PathSeen(const wax::Vector<wax::String>& vec, wax::StringView path)
        {
            for (size_t i = 0; i < vec.Size(); ++i)
            {
                if (vec[i].View() == path)
                {
                    return true;
                }
            }
            return false;
        }

        bool IdSeen(const wax::Vector<nectar::AssetId>& vec, nectar::AssetId id)
        {
            for (size_t i = 0; i < vec.Size(); ++i)
            {
                if (vec[i] == id)
                {
                    return true;
                }
            }
            return false;
        }

        void PushPath(wax::Vector<wax::String>& vec, wax::StringView path, comb::DefaultAllocator& alloc)
        {
            if (path.IsEmpty())
            {
                return;
            }
            if (PathSeen(vec, path))
            {
                return;
            }
            vec.PushBack(wax::String{alloc, path});
        }

        void PushId(wax::Vector<nectar::AssetId>& vec, nectar::AssetId id)
        {
            if (!id.IsValid())
            {
                return;
            }
            if (IdSeen(vec, id))
            {
                return;
            }
            vec.PushBack(id);
        }

        bool ExtensionMatches(wax::StringView path, const char* suffix)
        {
            const size_t suffixLen = std::strlen(suffix);
            if (path.Size() < suffixLen)
            {
                return false;
            }
            const char* tail = path.Data() + (path.Size() - suffixLen);
            for (size_t i = 0; i < suffixLen; ++i)
            {
                const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(tail[i])));
                const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
                if (a != b)
                {
                    return false;
                }
            }
            return true;
        }

        void ExtractFieldsFromComponent(const uint8_t* compData, const queen::ComponentReflection& refl,
                                        ReachableAssets& out, comb::DefaultAllocator& alloc)
        {
            for (size_t i = 0; i < refl.m_fieldCount; ++i)
            {
                const auto& field = refl.m_fields[i];
                if (field.m_type != queen::FieldType::STRING)
                {
                    continue;
                }
                if (field.m_attributes == nullptr ||
                    !field.m_attributes->HasFlag(queen::FieldFlag::FILE_PATH))
                {
                    continue;
                }
                const char* str = reinterpret_cast<const char*>(compData + field.m_offset);
                size_t len = 0;
                while (len < field.m_size && str[len] != '\0')
                {
                    ++len;
                }
                if (len == 0)
                {
                    continue;
                }
                PushPath(out.m_vfsPaths, wax::StringView{str, len}, alloc);
            }
        }

        void WalkComponents(queen::World& world, const queen::ComponentRegistry<256>& registry,
                            ReachableAssets& out, comb::DefaultAllocator& alloc)
        {
            world.ForEachArchetype([&](const auto& archetype) {
                const auto& types = archetype.GetComponentTypes();
                const uint32_t entityCount = static_cast<uint32_t>(archetype.EntityCount());
                for (size_t t = 0; t < types.Size(); ++t)
                {
                    const queen::TypeId typeId = types[t];
                    const queen::RegisteredComponent* reg = registry.Find(typeId);
                    if (reg == nullptr || !reg->HasReflection())
                    {
                        continue;
                    }
                    for (uint32_t row = 0; row < entityCount; ++row)
                    {
                        const void* raw = archetype.GetComponentRaw(row, typeId);
                        if (raw == nullptr)
                        {
                            continue;
                        }
                        ExtractFieldsFromComponent(static_cast<const uint8_t*>(raw), reg->m_reflection, out,
                                                   alloc);
                    }
                }
            });
        }

        void ResolveMaterial(wax::StringView matPath, ProjectManager& project, ReachableAssets& out,
                             comb::DefaultAllocator& alloc)
        {
            nectar::VirtualFilesystem& vfs = project.VFS();
            wax::ByteBuffer matBlob = vfs.ReadSync(matPath);
            if (matBlob.IsEmpty())
            {
                return;
            }

            nectar::MaterialAssetLoader matLoader;
            nectar::MaterialAsset* matAsset =
                matLoader.Load(wax::ByteSpan{matBlob.Data(), matBlob.Size()}, alloc);
            if (matAsset == nullptr)
            {
                return;
            }

            PushPath(out.m_vfsPaths, matAsset->m_data.m_shaderPath.View(), alloc);
            for (auto it = matAsset->m_data.m_textureBindings.Begin();
                 it != matAsset->m_data.m_textureBindings.End(); ++it)
            {
                PushId(out.m_cookedIds, it.Value());
            }
            matLoader.Unload(matAsset, alloc);
        }

        void ResolveShader(wax::StringView shaderPath, ProjectManager& project, ReachableAssets& out,
                           comb::DefaultAllocator& alloc)
        {
            nectar::VirtualFilesystem& vfs = project.VFS();
            wax::ByteBuffer blob = vfs.ReadSync(shaderPath);
            if (blob.IsEmpty())
            {
                return;
            }

            nectar::ShaderProgramAssetLoader progLoader;
            nectar::ShaderProgramAsset* prog =
                progLoader.Load(wax::ByteSpan{blob.Data(), blob.Size()}, alloc);
            if (prog == nullptr)
            {
                return;
            }
            PushPath(out.m_vfsPaths, prog->m_vertexPath.View(), alloc);
            PushPath(out.m_vfsPaths, prog->m_pixelPath.View(), alloc);
            progLoader.Unload(prog, alloc);
        }

        void ApplyResolversFixedPoint(ProjectManager& project, ReachableAssets& out, comb::DefaultAllocator& alloc)
        {
            size_t cursor = 0;
            while (cursor < out.m_vfsPaths.Size())
            {
                const size_t snapshot = out.m_vfsPaths.Size();
                for (size_t i = cursor; i < snapshot; ++i)
                {
                    const wax::String pathCopy{alloc, out.m_vfsPaths[i].View()};
                    const wax::StringView path = pathCopy.View();
                    if (ExtensionMatches(path, ".hmat"))
                    {
                        ResolveMaterial(path, project, out, alloc);
                    }
                    else if (ExtensionMatches(path, ".hshader"))
                    {
                        ResolveShader(path, project, out, alloc);
                    }
                }
                cursor = snapshot;
            }
        }

        void CollectScripts(ProjectManager& project, ReachableAssets& out, comb::DefaultAllocator& alloc)
        {
            const wax::StringView assetsRoot = project.Paths().m_assets.View();
            if (assetsRoot.IsEmpty())
            {
                return;
            }
            const std::filesystem::path assetsDir{std::string{assetsRoot.Data(), assetsRoot.Size()}};
            std::error_code ec;
            if (!std::filesystem::is_directory(assetsDir, ec))
            {
                return;
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator{assetsDir, ec})
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }
                const std::string extStr = entry.path().extension().string();
                if (extStr != ".propolis")
                {
                    continue;
                }
                const auto rel = std::filesystem::relative(entry.path(), assetsDir, ec).generic_string();
                if (ec || rel.empty())
                {
                    continue;
                }
                PushPath(out.m_vfsPaths, wax::StringView{rel.c_str(), rel.size()}, alloc);
            }
        }

        void ForceIncludeEngineAssets(wax::Vector<wax::String>& outPaths, comb::DefaultAllocator& alloc)
        {
            constexpr const char* kEngineAssets[] = {
                "engine/standard.hshader",
                "engine/standard.hmat",
                "engine/standard.vs.hlsl",
                "engine/standard.ps.hlsl",
                "engine/common.hlsli",
            };
            for (const char* path : kEngineAssets)
            {
                PushPath(outPaths, wax::StringView{path, std::strlen(path)}, alloc);
            }
        }
    } // namespace

    ReachableAssets CollectFromScene(queen::ComponentRegistry<256>& registry, ProjectManager& project,
                                     wax::StringView sceneRelativePath, comb::DefaultAllocator& alloc)
    {
        ReachableAssets result{alloc};

        if (sceneRelativePath.IsEmpty())
        {
            hive::LogWarning(LOG_REACH, "Empty scene path");
            ForceIncludeEngineAssets(result.m_vfsPaths, alloc);
            return result;
        }

        PushPath(result.m_vfsPaths, sceneRelativePath, alloc);

        const wax::StringView assetsRoot = project.Paths().m_assets.View();
        std::filesystem::path scenePath{std::string{assetsRoot.Data(), assetsRoot.Size()}};
        scenePath /= std::string{sceneRelativePath.Data(), sceneRelativePath.Size()};
        const std::string sceneFull = scenePath.generic_string();

        queen::World tempWorld{};
        if (!LoadScene(tempWorld, registry, sceneFull.c_str()))
        {
            hive::LogError(LOG_REACH, "Failed to load scene: {}", sceneFull.c_str());
            ForceIncludeEngineAssets(result.m_vfsPaths, alloc);
            return result;
        }

        WalkComponents(tempWorld, registry, result, alloc);
        ApplyResolversFixedPoint(project, result, alloc);
        CollectScripts(project, result, alloc);
        ForceIncludeEngineAssets(result.m_vfsPaths, alloc);
        ApplyResolversFixedPoint(project, result, alloc);

        hive::LogInfo(LOG_REACH, "Reachability: {} vfs paths, {} cooked ids", result.m_vfsPaths.Size(),
                      result.m_cookedIds.Size());
        return result;
    }
} // namespace waggle::scene
