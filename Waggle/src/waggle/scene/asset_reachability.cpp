#include <waggle/scene/asset_reachability.h>

#include <hive/core/log.h>

#include <wax/containers/string.h>

#include <queen/world/world.h>

#include <nectar/assets/material_asset.h>
#include <nectar/shader_program/shader_program_asset.h>
#include <nectar/vfs/virtual_filesystem.h>

#include <waggle/components/mesh_reference.h>
#include <waggle/project/project_manager.h>
#include <waggle/scene/scene_io.h>

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

        void CollectFromMaterial(wax::StringView materialPath, ProjectManager& project,
                                 wax::Vector<wax::String>& outPaths, wax::Vector<nectar::AssetId>& outIds,
                                 comb::DefaultAllocator& alloc)
        {
            nectar::VirtualFilesystem& vfs = project.VFS();
            wax::ByteBuffer matBlob = vfs.ReadSync(materialPath);
            if (matBlob.IsEmpty())
            {
                hive::LogWarning(LOG_REACH, "Material not in VFS: {}", wax::String{alloc, materialPath}.CStr());
                return;
            }

            nectar::MaterialAssetLoader matLoader;
            nectar::MaterialAsset* matAsset =
                matLoader.Load(wax::ByteSpan{matBlob.Data(), matBlob.Size()}, alloc);
            if (matAsset == nullptr)
            {
                hive::LogWarning(LOG_REACH, "Failed to parse material: {}", wax::String{alloc, materialPath}.CStr());
                return;
            }

            const wax::StringView shaderPath = matAsset->m_data.m_shaderPath.View();
            PushPath(outPaths, shaderPath, alloc);

            for (auto it = matAsset->m_data.m_textureBindings.Begin();
                 it != matAsset->m_data.m_textureBindings.End(); ++it)
            {
                PushId(outIds, it.Value());
            }

            if (!shaderPath.IsEmpty())
            {
                wax::ByteBuffer shaderBlob = vfs.ReadSync(shaderPath);
                if (!shaderBlob.IsEmpty())
                {
                    nectar::ShaderProgramAssetLoader progLoader;
                    nectar::ShaderProgramAsset* progAsset =
                        progLoader.Load(wax::ByteSpan{shaderBlob.Data(), shaderBlob.Size()}, alloc);
                    if (progAsset != nullptr)
                    {
                        PushPath(outPaths, progAsset->m_vertexPath.View(), alloc);
                        PushPath(outPaths, progAsset->m_pixelPath.View(), alloc);
                        progLoader.Unload(progAsset, alloc);
                    }
                }
            }

            matLoader.Unload(matAsset, alloc);
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

        wax::Vector<wax::String> materials{alloc};
        tempWorld.Query<queen::Read<MeshReference>>().Each([&](const MeshReference& mr) {
            PushPath(result.m_vfsPaths, mr.m_meshName.View(), alloc);
            if (!mr.m_material.IsEmpty() && !PathSeen(materials, mr.m_material.View()))
            {
                materials.PushBack(wax::String{alloc, mr.m_material.View()});
                PushPath(result.m_vfsPaths, mr.m_material.View(), alloc);
            }
        });

        for (size_t i = 0; i < materials.Size(); ++i)
        {
            CollectFromMaterial(materials[i].View(), project, result.m_vfsPaths, result.m_cookedIds, alloc);
        }

        ForceIncludeEngineAssets(result.m_vfsPaths, alloc);

        hive::LogInfo(LOG_REACH, "Reachability: {} vfs paths, {} cooked ids", result.m_vfsPaths.Size(),
                      result.m_cookedIds.Size());
        return result;
    }
} // namespace waggle::scene
