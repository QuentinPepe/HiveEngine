#include <hive/core/log.h>

#include <comb/default_allocator.h>

#include <wax/containers/vector.h>

#include <nectar/assets/material_asset.h>
#include <nectar/assets/mesh_asset.h>
#include <nectar/assets/texture_asset.h>
#include <nectar/database/asset_database.h>
#include <nectar/database/asset_record.h>
#include <nectar/mesh/mesh_data.h>
#include <nectar/shader_program/shader_program_asset.h>
#include <nectar/texture/texture_data.h>
#include <nectar/vfs/virtual_filesystem.h>

#include <queen/world/world.h>

#include <waggle/components/mesh_reference.h>
#include <waggle/project/project_manager.h>
#include <waggle/render/render_module.h>

#include <chrono>

#include <swarm/swarm.h>

#include <cstddef>
#include <cstring>

namespace waggle
{
    namespace
    {
        const hive::LogCategory LOG_RENDER{"Waggle.Render"};

        constexpr wax::StringView kBuiltinCube{"builtin:cube"};
        constexpr wax::StringView kDefaultMaterialPath{"engine/standard.hmat"};
        constexpr wax::StringView kEditorGridMaterialPath{"engine/grid_xz.hmat"};
        constexpr wax::StringView kGizmoMaterialPath{"engine/gizmo_unlit.hmat"};

        constexpr uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept
        {
            return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                   (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
        }

        constexpr uint32_t kAxisColors[3] = {
            PackRGBA(216, 64, 64, 255),   // X = red
            PackRGBA(64, 216, 64, 255),   // Y = green
            PackRGBA(76, 128, 242, 255),  // Z = blue
        };

        constexpr uint32_t kAxisColorsHot[3] = {
            PackRGBA(255, 200, 80, 255),  // X hot = warm yellow tinted red
            PackRGBA(220, 255, 90, 255),  // Y hot = warm yellow tinted green
            PackRGBA(150, 210, 255, 255), // Z hot = lighter blue
        };

        comb::DefaultAllocator& GetCacheAllocator()
        {
            static comb::ModuleAllocator allocator{"Waggle.Render", 1 * 1024 * 1024};
            return allocator.Get();
        }

        // Mesh load buffers are transient: alloc → CPU parse → GPU upload → free.
        // We size the allocator per call to avoid keeping hundreds of MB reserved
        // between loads, and to accommodate arbitrarily large single meshes.
        //
        // The Buddy allocator rounds each allocation up to a power of two. The
        // single largest allocation (the mesh blob) consumes a power-of-two
        // sub-block; passing exactly blobSize for the capacity would force the
        // whole block to be eaten by that one allocation, leaving nothing for
        // the MeshAsset header and submesh vector. Doubling guarantees room.
        size_t RoundUpMeshAllocatorCapacity(size_t blobSize) noexcept
        {
            constexpr size_t kFloor = 64u * 1024u;
            const size_t doubled = blobSize > 0 ? blobSize * 2u : kFloor;
            return doubled < kFloor ? kFloor : doubled;
        }

        comb::DefaultAllocator& GetTextureLoadAllocator()
        {
            static comb::ModuleAllocator allocator{"Waggle.TextureLoad", 256 * 1024 * 1024};
            return allocator.Get();
        }

        // Holds parsed .hmat + .hshader data across the parallel parse phase.
        // Mutex-guarded shared pool; thread-safe for concurrent parses.
        comb::DefaultAllocator& GetMaterialLoadAllocator()
        {
            static comb::ModuleAllocator allocator{"Waggle.MaterialLoad", 32 * 1024 * 1024};
            return allocator.Get();
        }

        static_assert(sizeof(nectar::MeshVertex) == sizeof(swarm::Vertex),
                      "nectar::MeshVertex and swarm::Vertex must have the same size");
        static_assert(offsetof(nectar::MeshVertex, m_position) == offsetof(swarm::Vertex, m_position),
                      "position offset mismatch");
        static_assert(offsetof(nectar::MeshVertex, m_normal) == offsetof(swarm::Vertex, m_normal),
                      "normal offset mismatch");
        static_assert(offsetof(nectar::MeshVertex, m_tangent) == offsetof(swarm::Vertex, m_tangent),
                      "tangent offset mismatch");
        static_assert(offsetof(nectar::MeshVertex, m_uv) == offsetof(swarm::Vertex, m_uv), "uv offset mismatch");
        static_assert(offsetof(nectar::MeshVertex, m_color) == offsetof(swarm::Vertex, m_color),
                      "color offset mismatch");
    } // namespace

    RenderModule::RenderModule(swarm::RenderContext* context)
        : RenderModule{context, drone::JobSubmitter{}}
    {
    }

    RenderModule::RenderModule(swarm::RenderContext* context, drone::JobSubmitter jobs)
        : m_context{context}
        , m_meshCache{GetCacheAllocator()}
        , m_materialPathCache{GetCacheAllocator()}
        , m_textureCache{GetCacheAllocator()}
        , m_jobs{jobs}
    {
        if (m_context == nullptr)
        {
            hive::LogError(LOG_RENDER, "RenderModule: null render context");
            return;
        }

        m_cubeMesh = BuildCubeMesh();
        if (m_cubeMesh == nullptr)
        {
            hive::LogError(LOG_RENDER, "RenderModule: failed to build cube mesh");
        }

        m_editorGridMesh = BuildEditorGridMesh();
        if (m_editorGridMesh == nullptr)
        {
            hive::LogError(LOG_RENDER, "RenderModule: failed to build editor grid mesh");
        }

        for (uint8_t axis = 0; axis < 3; ++axis)
        {
            m_gizmoTranslateAxisMeshes[axis] = BuildGizmoTranslateAxisMesh(axis, false);
            m_gizmoRotateRingMeshes[axis] = BuildGizmoRotateRingMesh(axis, false);
            m_gizmoScaleAxisMeshes[axis] = BuildGizmoScaleAxisMesh(axis, false);
            m_gizmoTranslateAxisMeshesHot[axis] = BuildGizmoTranslateAxisMesh(axis, true);
            m_gizmoRotateRingMeshesHot[axis] = BuildGizmoRotateRingMesh(axis, true);
            m_gizmoScaleAxisMeshesHot[axis] = BuildGizmoScaleAxisMesh(axis, true);
        }

        constexpr uint8_t kWhitePixel[4]{255, 255, 255, 255};
        constexpr uint8_t kFlatNormalPixel[4]{128, 128, 255, 255};
        m_defaultWhite = BuildSolidTexture(kWhitePixel, "Default White");
        m_defaultNormal = BuildSolidTexture(kFlatNormalPixel, "Default Normal");

        swarm::SetDefaultBindlessTexture(m_context, swarm::kBindlessSlotDefaultWhite, m_defaultWhite);
        swarm::SetDefaultBindlessTexture(m_context, swarm::kBindlessSlotDefaultNormal, m_defaultNormal);
    }

    RenderModule::~RenderModule()
    {
        for (auto it = m_materialPathCache.begin(); it != m_materialPathCache.end(); ++it)
        {
            if (it.Value() != nullptr)
            {
                swarm::DestroyMaterial(m_context, it.Value());
            }
        }
        m_materialPathCache.Clear();

        for (auto it = m_textureCache.begin(); it != m_textureCache.end(); ++it)
        {
            if (it.Value() != nullptr)
            {
                swarm::DestroyTexture(it.Value());
            }
        }
        m_textureCache.Clear();

        for (auto it = m_meshCache.begin(); it != m_meshCache.end(); ++it)
        {
            swarm::DestroyMesh(it.Value());
        }
        m_meshCache.Clear();

        if (m_defaultWhite != nullptr)
        {
            swarm::DestroyTexture(m_defaultWhite);
            m_defaultWhite = nullptr;
        }
        if (m_defaultNormal != nullptr)
        {
            swarm::DestroyTexture(m_defaultNormal);
            m_defaultNormal = nullptr;
        }

        if (m_cubeMesh != nullptr)
        {
            swarm::DestroyMesh(m_cubeMesh);
            m_cubeMesh = nullptr;
        }
        if (m_editorGridMesh != nullptr)
        {
            swarm::DestroyMesh(m_editorGridMesh);
            m_editorGridMesh = nullptr;
        }
        for (uint8_t axis = 0; axis < 3; ++axis)
        {
            for (swarm::Mesh** slot : {&m_gizmoTranslateAxisMeshes[axis], &m_gizmoRotateRingMeshes[axis],
                                       &m_gizmoScaleAxisMeshes[axis], &m_gizmoTranslateAxisMeshesHot[axis],
                                       &m_gizmoRotateRingMeshesHot[axis], &m_gizmoScaleAxisMeshesHot[axis]})
            {
                if (*slot != nullptr)
                {
                    swarm::DestroyMesh(*slot);
                    *slot = nullptr;
                }
            }
        }
    }

    swarm::Texture* RenderModule::BuildSolidTexture(const uint8_t (&rgba)[4], const char* name)
    {
        swarm::TextureMipUpload mip{};
        mip.m_width = 1;
        mip.m_height = 1;
        mip.m_pixels = rgba;
        mip.m_byteCount = 4;

        swarm::TextureDesc desc{};
        desc.m_debugName = name;
        desc.m_format = swarm::TextureFormat::RGBA8_UNORM;
        desc.m_width = 1;
        desc.m_height = 1;
        desc.m_mips = &mip;
        desc.m_mipCount = 1;
        return swarm::CreateTexture(m_context, desc);
    }

    swarm::Material* RenderModule::GetDefaultMaterial(ProjectManager* project)
    {
        return AcquireMaterial(kDefaultMaterialPath, project);
    }

    swarm::Material* RenderModule::GetEditorGridMaterial(ProjectManager* project)
    {
        return AcquireMaterial(kEditorGridMaterialPath, project);
    }

    swarm::Material* RenderModule::GetGizmoMaterial(ProjectManager* project)
    {
        return AcquireMaterial(kGizmoMaterialPath, project);
    }

    swarm::Material* RenderModule::AcquireMaterial(wax::StringView path, ProjectManager* project)
    {
        if (path.IsEmpty())
        {
            path = kDefaultMaterialPath;
        }

        wax::String pathKey{GetCacheAllocator(), path};
        if (auto* hit = m_materialPathCache.Find(pathKey))
            return *hit;

        swarm::Material* loaded = (project != nullptr) ? LoadMaterialFromVfs(path, *project) : nullptr;
        m_materialPathCache.Insert(static_cast<wax::String&&>(pathKey), loaded);
        return loaded;
    }

    swarm::Texture* RenderModule::AcquireTexture(nectar::AssetId id, wax::StringView path, ProjectManager& project)
    {
        if (!id.IsValid())
        {
            return nullptr;
        }

        wax::String key{GetCacheAllocator(), path};
        if (auto* hit = m_textureCache.Find(key))
            return *hit;

        swarm::Texture* loaded = LoadCookedTexture(id, path, project);
        m_textureCache.Insert(static_cast<wax::String&&>(key), loaded);
        return loaded;
    }

    RenderModule::ParsedMaterial RenderModule::ParseMaterialBlobs(wax::StringView path, ProjectManager& project,
                                                                    comb::DefaultAllocator& alloc)
    {
        ParsedMaterial out;
        out.m_path = wax::String{alloc, path};

        nectar::VirtualFilesystem& vfs = project.VFS();
        out.m_matBlob = vfs.ReadSync(path);
        if (out.m_matBlob.IsEmpty())
        {
            hive::LogWarning(LOG_RENDER, "ParseMaterial: '{}' not found in VFS", out.m_path.CStr());
            return out;
        }

        nectar::MaterialAssetLoader matLoader;
        out.m_matAsset = matLoader.Load(wax::ByteSpan{out.m_matBlob.Data(), out.m_matBlob.Size()}, alloc);
        if (out.m_matAsset == nullptr)
        {
            hive::LogError(LOG_RENDER, "ParseMaterial: failed to parse '{}'", out.m_path.CStr());
            return out;
        }

        const wax::StringView shaderPath = out.m_matAsset->m_data.m_shaderPath.View();
        if (shaderPath.IsEmpty())
        {
            hive::LogWarning(LOG_RENDER, "ParseMaterial: '{}' has no shader path", out.m_path.CStr());
            return out;
        }

        out.m_shaderBlob = vfs.ReadSync(shaderPath);
        if (out.m_shaderBlob.IsEmpty())
        {
            hive::LogWarning(LOG_RENDER, "ParseMaterial: shader '{}' not in VFS",
                             wax::String{alloc, shaderPath}.CStr());
            return out;
        }

        nectar::ShaderProgramAssetLoader progLoader;
        out.m_shaderAsset =
            progLoader.Load(wax::ByteSpan{out.m_shaderBlob.Data(), out.m_shaderBlob.Size()}, alloc);
        if (out.m_shaderAsset == nullptr)
        {
            hive::LogError(LOG_RENDER, "ParseMaterial: failed to parse shader '{}'",
                           wax::String{alloc, shaderPath}.CStr());
        }
        return out;
    }

    void RenderModule::ReleaseParsedMaterial(ParsedMaterial& parsed, comb::DefaultAllocator& alloc)
    {
        if (parsed.m_shaderAsset != nullptr)
        {
            nectar::ShaderProgramAssetLoader progLoader;
            progLoader.Unload(parsed.m_shaderAsset, alloc);
            parsed.m_shaderAsset = nullptr;
        }
        if (parsed.m_matAsset != nullptr)
        {
            nectar::MaterialAssetLoader matLoader;
            matLoader.Unload(parsed.m_matAsset, alloc);
            parsed.m_matAsset = nullptr;
        }
        parsed.m_matBlob = wax::ByteBuffer{};
        parsed.m_shaderBlob = wax::ByteBuffer{};
    }

    void RenderModule::CollectTextureRefsFromParsedMaterial(const ParsedMaterial& parsed, ProjectManager& project,
                                                              wax::Vector<TextureRef>& outRefs)
    {
        if (parsed.m_matAsset == nullptr || parsed.m_shaderAsset == nullptr)
        {
            return;
        }

        auto& alloc = GetCacheAllocator();
        nectar::AssetDatabase& db = project.Database();
        for (size_t i = 0; i < parsed.m_shaderAsset->m_textureAnnotations.Size(); ++i)
        {
            const auto& annotation = parsed.m_shaderAsset->m_textureAnnotations[i];
            wax::String key{alloc, annotation.m_name.View()};
            const nectar::AssetId* texId = parsed.m_matAsset->m_data.m_textureBindings.Find(key);
            if (texId == nullptr || !texId->IsValid())
            {
                continue;
            }
            const auto* record = db.FindByUuid(*texId);
            if (record == nullptr)
            {
                continue;
            }
            TextureRef ref{};
            ref.m_id = *texId;
            ref.m_debugPath = wax::String{alloc, record->m_path.View()};
            outRefs.PushBack(static_cast<TextureRef&&>(ref));
        }
    }

    void RenderModule::PreloadScene(queen::World& world, ProjectManager* project, PreloadProgressFn progress,
                                    void* userdata)
    {
        if (!IsReady() || project == nullptr)
        {
            return;
        }

        auto& alloc = GetCacheAllocator();
        wax::Vector<wax::String> meshes{alloc};
        wax::Vector<wax::String> materials{alloc};

        world.Query<queen::Read<MeshReference>>().Each([&](const MeshReference& mr) {
            if (!mr.m_meshName.IsEmpty())
            {
                wax::String key{alloc, mr.m_meshName.View()};
                if (m_meshCache.Find(key) == nullptr)
                {
                    bool seen = false;
                    for (size_t i = 0; i < meshes.Size(); ++i)
                    {
                        if (meshes[i] == key)
                        {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen)
                    {
                        meshes.PushBack(static_cast<wax::String&&>(key));
                    }
                }
            }
            if (!mr.m_material.IsEmpty())
            {
                wax::String key{alloc, mr.m_material.View()};
                if (m_materialPathCache.Find(key) == nullptr)
                {
                    bool seen = false;
                    for (size_t i = 0; i < materials.Size(); ++i)
                    {
                        if (materials[i] == key)
                        {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen)
                    {
                        materials.PushBack(static_cast<wax::String&&>(key));
                    }
                }
            }
        });

        nectar::VirtualFilesystem& vfs = project->VFS();

        const uint32_t meshTotal = static_cast<uint32_t>(meshes.Size());
        if (meshTotal > 0 && progress != nullptr)
        {
            progress("Loading meshes", 0, meshTotal, userdata);
        }
        for (uint32_t i = 0; i < meshTotal; ++i)
        {
            (void)AcquireMesh(meshes[i].View(), &vfs);
            if (progress != nullptr)
            {
                progress("Loading meshes", i + 1, meshTotal, userdata);
            }
        }

        // Parallel parse phase for materials (.hmat + .hshader). The parse stage is pure CPU /
        // allocator work and runs on worker threads; the GPU upload (CreateMaterial + texture
        // binding) stays on the main thread because Diligent is not thread-safe. We retain the
        // ParsedMaterial structs so UploadParsedMaterial below can reuse the same data the
        // texture-ref pass walked over (no double parse).
        auto& matAlloc = GetMaterialLoadAllocator();
        const uint32_t materialTotal = static_cast<uint32_t>(materials.Size());
        wax::Vector<ParsedMaterial> parsedMaterials{alloc};
        parsedMaterials.Resize(materialTotal);

        if (materialTotal > 0)
        {
            const auto matParseStart = std::chrono::steady_clock::now();
            const bool useMatJobs = m_jobs.IsValid() && materialTotal > 1;

            struct MatJobCtx
            {
                RenderModule* m_self;
                const wax::String* m_paths;
                ParsedMaterial* m_parsed;
                ProjectManager* m_project;
                comb::DefaultAllocator* m_alloc;
            };

            if (useMatJobs)
            {
                MatJobCtx ctx{this, materials.Data(), parsedMaterials.Data(), project, &matAlloc};
                m_jobs.ParallelFor(0, materialTotal,
                                   +[](size_t i, void* data) {
                                       auto* jobCtx = static_cast<MatJobCtx*>(data);
                                       jobCtx->m_parsed[i] = jobCtx->m_self->ParseMaterialBlobs(
                                           jobCtx->m_paths[i].View(), *jobCtx->m_project, *jobCtx->m_alloc);
                                   },
                                   &ctx, 1);
            }
            else
            {
                for (uint32_t i = 0; i < materialTotal; ++i)
                {
                    parsedMaterials[i] = ParseMaterialBlobs(materials[i].View(), *project, matAlloc);
                }
            }

            const auto matParseEnd = std::chrono::steady_clock::now();
            const double matParseMs =
                std::chrono::duration<double, std::milli>(matParseEnd - matParseStart).count();
            hive::LogInfo(LOG_RENDER, "Material parse: {} parsed in {:.1f}ms ({})", materialTotal, matParseMs,
                          useMatJobs ? "parallel" : "serial");
        }

        wax::Vector<TextureRef> textureRefs{alloc};
        for (size_t i = 0; i < parsedMaterials.Size(); ++i)
        {
            CollectTextureRefsFromParsedMaterial(parsedMaterials[i], *project, textureRefs);
        }

        wax::Vector<TextureRef> uniqueTextures{alloc};
        for (size_t i = 0; i < textureRefs.Size(); ++i)
        {
            const auto& ref = textureRefs[i];
            if (m_textureCache.Find(ref.m_debugPath) != nullptr)
            {
                continue;
            }
            bool seen = false;
            for (size_t j = 0; j < uniqueTextures.Size(); ++j)
            {
                if (uniqueTextures[j].m_id == ref.m_id)
                {
                    seen = true;
                    break;
                }
            }
            if (!seen)
            {
                uniqueTextures.PushBack(TextureRef{ref.m_id, wax::String{alloc, ref.m_debugPath.View()}});
            }
        }

        const uint32_t textureTotal = static_cast<uint32_t>(uniqueTextures.Size());
        if (textureTotal > 0)
        {
            if (progress != nullptr)
            {
                progress("Loading textures", 0, textureTotal, userdata);
            }

            // Each parsed texture pins its mip pyramid in the shared texture allocator until upload
            // completes. Holding all of them simultaneously easily overflows the 256 MB pool for
            // even moderate scenes (Sponza alone exceeds 1 GB of decompressed pixels). Microbatch
            // so peak memory stays bounded; parallelism still amortizes I/O + parse cost.
            constexpr uint32_t kTextureMicrobatch = 8;

            auto& texAlloc = GetTextureLoadAllocator();
            const auto parseStart = std::chrono::steady_clock::now();
            const bool useJobs = m_jobs.IsValid() && textureTotal > 1;
            double accumulatedParseMs = 0.0;

            nectar::TextureAssetLoader loader;
            wax::Vector<ParsedCookedTexture> parsed{alloc};
            parsed.Resize(kTextureMicrobatch);

            struct JobCtx
            {
                RenderModule* m_self;
                const TextureRef* m_refs;
                ParsedCookedTexture* m_parsed;
                ProjectManager* m_project;
                comb::DefaultAllocator* m_alloc;
                uint32_t m_offset;
            };

            for (uint32_t batchStart = 0; batchStart < textureTotal; batchStart += kTextureMicrobatch)
            {
                const uint32_t batchEnd = batchStart + kTextureMicrobatch < textureTotal
                                              ? batchStart + kTextureMicrobatch
                                              : textureTotal;
                const uint32_t batchSize = batchEnd - batchStart;

                const auto batchParseStart = std::chrono::steady_clock::now();
                if (useJobs && batchSize > 1)
                {
                    JobCtx ctx{this, uniqueTextures.Data(), parsed.Data(), project, &texAlloc, batchStart};
                    m_jobs.ParallelFor(0, batchSize,
                                       +[](size_t i, void* data) {
                                           auto* jobCtx = static_cast<JobCtx*>(data);
                                           const uint32_t globalIndex =
                                               jobCtx->m_offset + static_cast<uint32_t>(i);
                                           jobCtx->m_parsed[i] = jobCtx->m_self->ParseCookedTextureBlob(
                                               jobCtx->m_refs[globalIndex].m_id,
                                               jobCtx->m_refs[globalIndex].m_debugPath.View(),
                                               *jobCtx->m_project, *jobCtx->m_alloc);
                                       },
                                       &ctx, 1);
                }
                else
                {
                    for (uint32_t i = 0; i < batchSize; ++i)
                    {
                        parsed[i] =
                            ParseCookedTextureBlob(uniqueTextures[batchStart + i].m_id,
                                                   uniqueTextures[batchStart + i].m_debugPath.View(), *project,
                                                   texAlloc);
                    }
                }
                const auto batchParseEnd = std::chrono::steady_clock::now();
                accumulatedParseMs +=
                    std::chrono::duration<double, std::milli>(batchParseEnd - batchParseStart).count();

                for (uint32_t i = 0; i < batchSize; ++i)
                {
                    const uint32_t globalIndex = batchStart + i;
                    swarm::Texture* tex = UploadParsedTexture(
                        parsed[i], uniqueTextures[globalIndex].m_debugPath.View(), texAlloc);
                    if (parsed[i].m_asset != nullptr)
                    {
                        loader.Unload(parsed[i].m_asset, texAlloc);
                        parsed[i].m_asset = nullptr;
                    }
                    parsed[i].m_blob = wax::ByteBuffer{};
                    m_textureCache.Insert(wax::String{alloc, uniqueTextures[globalIndex].m_debugPath.View()}, tex);
                    if (progress != nullptr)
                    {
                        progress("Loading textures", globalIndex + 1, textureTotal, userdata);
                    }
                }
            }

            const auto parseEnd = std::chrono::steady_clock::now();
            const double totalMs = std::chrono::duration<double, std::milli>(parseEnd - parseStart).count();
            hive::LogInfo(LOG_RENDER,
                          "Texture batch: {} parsed in {:.1f}ms ({}, microbatch={}, parse-only={:.1f}ms)",
                          textureTotal, totalMs, useJobs ? "parallel" : "serial", kTextureMicrobatch,
                          accumulatedParseMs);
        }

        if (materialTotal > 0 && progress != nullptr)
        {
            progress("Loading materials", 0, materialTotal, userdata);
        }
        for (uint32_t i = 0; i < materialTotal; ++i)
        {
            swarm::Material* material = nullptr;
            ParsedMaterial& parsed = parsedMaterials[i];
            if (parsed.m_matAsset != nullptr && parsed.m_shaderAsset != nullptr)
            {
                material = UploadParsedMaterial(parsed, *project, matAlloc);
            }
            ReleaseParsedMaterial(parsed, matAlloc);
            m_materialPathCache.Insert(wax::String{alloc, materials[i].View()}, material);
            if (progress != nullptr)
            {
                progress("Loading materials", i + 1, materialTotal, userdata);
            }
        }
    }

    void RenderModule::InvalidateMaterial(wax::StringView path)
    {
        wax::String key{GetCacheAllocator(), path};
        if (auto* hit = m_materialPathCache.Find(key))
        {
            if (*hit != nullptr)
            {
                swarm::DestroyMaterial(m_context, *hit);
            }
            m_materialPathCache.Remove(key);
        }
    }

    swarm::Mesh* RenderModule::AcquireMesh(wax::StringView name, nectar::VirtualFilesystem* vfs)
    {
        if (name.IsEmpty())
        {
            return nullptr;
        }

        if (name == kBuiltinCube)
        {
            return m_cubeMesh;
        }

        wax::String key{GetCacheAllocator(), name};
        if (auto* hit = m_meshCache.Find(key))
            return *hit;

        swarm::Mesh* mesh = nullptr;
        if (vfs != nullptr)
        {
            mesh = LoadMeshFromVfs(name, *vfs);
            if (mesh == nullptr)
            {
                hive::LogWarning(LOG_RENDER, "RenderModule::AcquireMesh: failed to load '{}'", key.CStr());
            }
        }
        else
        {
            hive::LogWarning(LOG_RENDER, "RenderModule::AcquireMesh: no VFS, cannot load '{}'", key.CStr());
        }

        m_meshCache.Insert(static_cast<wax::String&&>(key), mesh);
        return mesh;
    }

    swarm::Mesh* RenderModule::LoadMeshFromVfs(wax::StringView name, nectar::VirtualFilesystem& vfs)
    {
        wax::ByteBuffer blob = vfs.ReadSync(name);
        if (blob.IsEmpty())
        {
            return nullptr;
        }

        comb::ModuleAllocator allocatorOwner{"Waggle.MeshLoad", RoundUpMeshAllocatorCapacity(blob.Size())};
        auto& allocator = allocatorOwner.Get();
        nectar::MeshAssetLoader loader;
        nectar::MeshAsset* asset = loader.Load(wax::ByteSpan{blob.Data(), blob.Size()}, allocator);
        if (asset == nullptr)
        {
            hive::LogError(LOG_RENDER, "LoadMeshFromVfs: NMSH parse failed for '{}'",
                           wax::String{GetCacheAllocator(), name}.CStr());
            return nullptr;
        }

        wax::Vector<swarm::Submesh> submeshes{allocator};
        submeshes.Reserve(asset->header.m_submeshCount);
        const nectar::SubMesh* nectarSubmeshes = asset->Submeshes();
        for (uint32_t i = 0; i < asset->header.m_submeshCount; ++i)
        {
            swarm::Submesh submesh;
            submesh.m_indexOffset = nectarSubmeshes[i].m_indexOffset;
            submesh.m_indexCount = nectarSubmeshes[i].m_indexCount;
            submeshes.PushBack(submesh);
        }

        swarm::MeshDesc descriptor;
        descriptor.m_vertices = reinterpret_cast<const swarm::Vertex*>(asset->Vertices());
        descriptor.m_vertexCount = asset->header.m_vertexCount;
        descriptor.m_indices = asset->Indices();
        descriptor.m_indexCount = asset->header.m_indexCount;
        descriptor.m_submeshes = submeshes.Data();
        descriptor.m_submeshCount = static_cast<uint32_t>(submeshes.Size());
        descriptor.m_debugName = "Waggle NMSH";

        swarm::Mesh* mesh = swarm::CreateMesh(m_context, descriptor);
        loader.Unload(asset, allocator);

        if (mesh != nullptr)
        {
            hive::LogInfo(LOG_RENDER, "Loaded mesh '{}' ({} vertices, {} indices, {} submeshes)",
                          wax::String{GetCacheAllocator(), name}.CStr(), descriptor.m_vertexCount,
                          descriptor.m_indexCount, descriptor.m_submeshCount);
        }
        return mesh;
    }

    swarm::Mesh* RenderModule::BuildCubeMesh()
    {
        constexpr float kHalfExtent = 0.5f;
        constexpr uint32_t kWhite = 0xFFFFFFFFu;

        const swarm::Vertex vertices[] = {
            {{-kHalfExtent, kHalfExtent, -kHalfExtent}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-kHalfExtent, kHalfExtent, kHalfExtent}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            {{kHalfExtent, kHalfExtent, kHalfExtent}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{kHalfExtent, kHalfExtent, -kHalfExtent}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent, kHalfExtent}, {0.f, -1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent, -kHalfExtent}, {0.f, -1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            {{kHalfExtent, -kHalfExtent, -kHalfExtent}, {0.f, -1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{kHalfExtent, -kHalfExtent, kHalfExtent}, {0.f, -1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent, kHalfExtent}, {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{kHalfExtent, -kHalfExtent, kHalfExtent}, {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
            {{kHalfExtent, kHalfExtent, kHalfExtent}, {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{-kHalfExtent, kHalfExtent, kHalfExtent}, {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            {{kHalfExtent, -kHalfExtent, -kHalfExtent}, {0.f, 0.f, -1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent, -kHalfExtent}, {0.f, 0.f, -1.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
            {{-kHalfExtent, kHalfExtent, -kHalfExtent}, {0.f, 0.f, -1.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{kHalfExtent, kHalfExtent, -kHalfExtent}, {0.f, 0.f, -1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            {{kHalfExtent, -kHalfExtent, kHalfExtent}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f, 1.f}, {0.f, 0.f}, kWhite},
            {{kHalfExtent, -kHalfExtent, -kHalfExtent}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f, 1.f}, {1.f, 0.f}, kWhite},
            {{kHalfExtent, kHalfExtent, -kHalfExtent}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f, 1.f}, {1.f, 1.f}, kWhite},
            {{kHalfExtent, kHalfExtent, kHalfExtent}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f, 1.f}, {0.f, 1.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent, -kHalfExtent}, {-1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent, kHalfExtent}, {-1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 1.f}, {1.f, 0.f}, kWhite},
            {{-kHalfExtent, kHalfExtent, kHalfExtent}, {-1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 1.f}, {1.f, 1.f}, kWhite},
            {{-kHalfExtent, kHalfExtent, -kHalfExtent}, {-1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 1.f}, {0.f, 1.f}, kWhite},
        };

        uint32_t indices[36];
        for (uint32_t face = 0; face < 6; ++face)
        {
            const uint32_t base = face * 4;
            indices[face * 6 + 0] = base + 0;
            indices[face * 6 + 1] = base + 1;
            indices[face * 6 + 2] = base + 2;
            indices[face * 6 + 3] = base + 0;
            indices[face * 6 + 4] = base + 2;
            indices[face * 6 + 5] = base + 3;
        }

        swarm::MeshDesc descriptor;
        descriptor.m_vertices = vertices;
        descriptor.m_vertexCount = static_cast<uint32_t>(sizeof(vertices) / sizeof(vertices[0]));
        descriptor.m_indices = indices;
        descriptor.m_indexCount = static_cast<uint32_t>(sizeof(indices) / sizeof(indices[0]));
        descriptor.m_debugName = "Waggle Builtin Cube";

        return swarm::CreateMesh(m_context, descriptor);
    }

    swarm::Mesh* RenderModule::BuildEditorGridMesh()
    {
        constexpr float h = 0.5f;
        constexpr uint32_t kWhite = 0xFFFFFFFFu;
        const swarm::Vertex vertices[] = {
            {{-h, 0.f, -h}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-h, 0.f, h}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            {{h, 0.f, h}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{h, 0.f, -h}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
        };
        const uint32_t indices[] = {0, 1, 2, 0, 2, 3};

        swarm::MeshDesc descriptor;
        descriptor.m_vertices = vertices;
        descriptor.m_vertexCount = static_cast<uint32_t>(sizeof(vertices) / sizeof(vertices[0]));
        descriptor.m_indices = indices;
        descriptor.m_indexCount = static_cast<uint32_t>(sizeof(indices) / sizeof(indices[0]));
        descriptor.m_debugName = "Waggle Editor Grid XZ";

        return swarm::CreateMesh(m_context, descriptor);
    }

    namespace
    {
        constexpr float kTau = 6.28318530717958647692f;

        hive::math::Float3 CanonicalToAxis(uint8_t axis, float x, float y, float z) noexcept
        {
            // Canonical geometry is built along +X. Rotate so canonical (1,0,0) maps to the
            // chosen axis: identity for X, +90° around Z for Y, -90° around Y for Z.
            switch (axis)
            {
                case 0: return hive::math::Float3{x, y, z};
                case 1: return hive::math::Float3{-y, x, z};
                case 2: return hive::math::Float3{-z, y, x};
                default: return hive::math::Float3{x, y, z};
            }
        }

        swarm::Vertex MakeVertex(const hive::math::Float3& pos, const hive::math::Float3& normal,
                                 uint32_t color)
        {
            swarm::Vertex v{};
            v.m_position[0] = pos.m_x;
            v.m_position[1] = pos.m_y;
            v.m_position[2] = pos.m_z;
            v.m_normal[0] = normal.m_x;
            v.m_normal[1] = normal.m_y;
            v.m_normal[2] = normal.m_z;
            v.m_tangent[0] = 1.f;
            v.m_tangent[1] = 0.f;
            v.m_tangent[2] = 0.f;
            v.m_tangent[3] = 1.f;
            v.m_uv[0] = 0.f;
            v.m_uv[1] = 0.f;
            v.m_color = color;
            return v;
        }

        void EmitCylinder(wax::Vector<swarm::Vertex>& verts, wax::Vector<uint32_t>& indices,
                          uint8_t axis, uint32_t color, float xStart, float xEnd, float radius,
                          uint32_t segments)
        {
            const uint32_t baseIdx = static_cast<uint32_t>(verts.Size());
            for (uint32_t i = 0; i <= segments; ++i)
            {
                const float angle = (static_cast<float>(i) / static_cast<float>(segments)) * kTau;
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                const hive::math::Float3 nLocal{0.f, c, s};
                const hive::math::Float3 n = CanonicalToAxis(axis, nLocal.m_x, nLocal.m_y, nLocal.m_z);
                verts.PushBack(MakeVertex(CanonicalToAxis(axis, xStart, c * radius, s * radius), n, color));
                verts.PushBack(MakeVertex(CanonicalToAxis(axis, xEnd, c * radius, s * radius), n, color));
            }
            for (uint32_t i = 0; i < segments; ++i)
            {
                const uint32_t a = baseIdx + i * 2;
                const uint32_t b = baseIdx + i * 2 + 1;
                const uint32_t c2 = baseIdx + (i + 1) * 2;
                const uint32_t d = baseIdx + (i + 1) * 2 + 1;
                indices.PushBack(a);
                indices.PushBack(c2);
                indices.PushBack(b);
                indices.PushBack(b);
                indices.PushBack(c2);
                indices.PushBack(d);
            }
        }

        void EmitCone(wax::Vector<swarm::Vertex>& verts, wax::Vector<uint32_t>& indices, uint8_t axis,
                      uint32_t color, float xBase, float xTip, float baseRadius, uint32_t segments)
        {
            const uint32_t baseIdx = static_cast<uint32_t>(verts.Size());
            const hive::math::Float3 axisDirLocal{1.f, 0.f, 0.f};
            const hive::math::Float3 axisDir =
                CanonicalToAxis(axis, axisDirLocal.m_x, axisDirLocal.m_y, axisDirLocal.m_z);
            for (uint32_t i = 0; i <= segments; ++i)
            {
                const float angle = (static_cast<float>(i) / static_cast<float>(segments)) * kTau;
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                const hive::math::Float3 nLocal{0.f, c, s};
                const hive::math::Float3 n = CanonicalToAxis(axis, nLocal.m_x, nLocal.m_y, nLocal.m_z);
                verts.PushBack(MakeVertex(CanonicalToAxis(axis, xBase, c * baseRadius, s * baseRadius), n, color));
            }
            const uint32_t tipIdx = static_cast<uint32_t>(verts.Size());
            verts.PushBack(MakeVertex(CanonicalToAxis(axis, xTip, 0.f, 0.f), axisDir, color));
            for (uint32_t i = 0; i < segments; ++i)
            {
                indices.PushBack(baseIdx + i);
                indices.PushBack(baseIdx + i + 1);
                indices.PushBack(tipIdx);
            }
        }

        void EmitBox(wax::Vector<swarm::Vertex>& verts, wax::Vector<uint32_t>& indices, uint8_t axis,
                     uint32_t color, float xCenter, float halfSize)
        {
            const float h = halfSize;
            const hive::math::Float3 corners[8] = {
                CanonicalToAxis(axis, xCenter - h, -h, -h), CanonicalToAxis(axis, xCenter + h, -h, -h),
                CanonicalToAxis(axis, xCenter + h, h, -h),  CanonicalToAxis(axis, xCenter - h, h, -h),
                CanonicalToAxis(axis, xCenter - h, -h, h),  CanonicalToAxis(axis, xCenter + h, -h, h),
                CanonicalToAxis(axis, xCenter + h, h, h),   CanonicalToAxis(axis, xCenter - h, h, h),
            };
            const uint32_t baseIdx = static_cast<uint32_t>(verts.Size());
            const hive::math::Float3 nUp = CanonicalToAxis(axis, 0.f, 1.f, 0.f);
            for (uint32_t i = 0; i < 8; ++i)
            {
                verts.PushBack(MakeVertex(corners[i], nUp, color));
            }
            const uint32_t kBoxIndices[36] = {
                0, 1, 2, 0, 2, 3, // -Z
                4, 6, 5, 4, 7, 6, // +Z
                0, 4, 5, 0, 5, 1, // -Y
                3, 2, 6, 3, 6, 7, // +Y
                0, 3, 7, 0, 7, 4, // -X canonical
                1, 5, 6, 1, 6, 2, // +X canonical
            };
            for (uint32_t i = 0; i < 36; ++i)
            {
                indices.PushBack(baseIdx + kBoxIndices[i]);
            }
        }

        void EmitTorus(wax::Vector<swarm::Vertex>& verts, wax::Vector<uint32_t>& indices, uint8_t axis,
                       uint32_t color, float majorRadius, float minorRadius, uint32_t majorSegments,
                       uint32_t minorSegments)
        {
            // Torus axis is the gizmo axis itself; the ring lies in the plane perpendicular to it.
            // Canonical build: ring around +X (so the ring lies in YZ plane). Then CanonicalToAxis
            // rotates the entire ring to the target axis (X stays, Y→XZ, Z→XY).
            const uint32_t baseIdx = static_cast<uint32_t>(verts.Size());
            for (uint32_t i = 0; i < majorSegments; ++i)
            {
                const float majorAngle = (static_cast<float>(i) / static_cast<float>(majorSegments)) * kTau;
                const float mc = std::cos(majorAngle);
                const float ms = std::sin(majorAngle);
                for (uint32_t j = 0; j < minorSegments; ++j)
                {
                    const float minorAngle = (static_cast<float>(j) / static_cast<float>(minorSegments)) * kTau;
                    const float nc = std::cos(minorAngle);
                    const float ns = std::sin(minorAngle);
                    const float ringX = nc * minorRadius;
                    const float ringR = majorRadius + ns * minorRadius;
                    const hive::math::Float3 pos = CanonicalToAxis(axis, ringX, mc * ringR, ms * ringR);
                    const hive::math::Float3 nrm =
                        CanonicalToAxis(axis, nc, mc * ns, ms * ns);
                    verts.PushBack(MakeVertex(pos, nrm, color));
                }
            }
            for (uint32_t i = 0; i < majorSegments; ++i)
            {
                const uint32_t iNext = (i + 1) % majorSegments;
                for (uint32_t j = 0; j < minorSegments; ++j)
                {
                    const uint32_t jNext = (j + 1) % minorSegments;
                    const uint32_t a = baseIdx + i * minorSegments + j;
                    const uint32_t b = baseIdx + iNext * minorSegments + j;
                    const uint32_t c = baseIdx + iNext * minorSegments + jNext;
                    const uint32_t d = baseIdx + i * minorSegments + jNext;
                    indices.PushBack(a);
                    indices.PushBack(b);
                    indices.PushBack(c);
                    indices.PushBack(a);
                    indices.PushBack(c);
                    indices.PushBack(d);
                }
            }
        }

        swarm::Mesh* CreateProceduralMesh(swarm::RenderContext* context, comb::DefaultAllocator& alloc,
                                          const wax::Vector<swarm::Vertex>& verts,
                                          const wax::Vector<uint32_t>& indices, const char* debugName)
        {
            (void)alloc;
            swarm::MeshDesc desc{};
            desc.m_vertices = verts.Data();
            desc.m_vertexCount = static_cast<uint32_t>(verts.Size());
            desc.m_indices = indices.Data();
            desc.m_indexCount = static_cast<uint32_t>(indices.Size());
            desc.m_debugName = debugName;
            return swarm::CreateMesh(context, desc);
        }
    } // namespace

    swarm::Mesh* RenderModule::BuildGizmoTranslateAxisMesh(uint8_t axisIndex, bool hot)
    {
        if (axisIndex >= 3)
        {
            return nullptr;
        }
        auto& alloc = GetCacheAllocator();
        wax::Vector<swarm::Vertex> verts{alloc};
        wax::Vector<uint32_t> indices{alloc};
        const uint32_t color = hot ? kAxisColorsHot[axisIndex] : kAxisColors[axisIndex];
        EmitCylinder(verts, indices, 0, color, 0.f, 0.85f, 0.022f, 12);
        EmitCone(verts, indices, 0, color, 0.85f, 1.0f, 0.07f, 16);
        const char* kNames[3] = {"Gizmo Translate X", "Gizmo Translate Y", "Gizmo Translate Z"};
        const char* kNamesHot[3] = {"Gizmo Translate X Hot", "Gizmo Translate Y Hot", "Gizmo Translate Z Hot"};
        return CreateProceduralMesh(m_context, alloc, verts, indices,
                                    hot ? kNamesHot[axisIndex] : kNames[axisIndex]);
    }

    swarm::Mesh* RenderModule::BuildGizmoRotateRingMesh(uint8_t axisIndex, bool hot)
    {
        if (axisIndex >= 3)
        {
            return nullptr;
        }
        auto& alloc = GetCacheAllocator();
        wax::Vector<swarm::Vertex> verts{alloc};
        wax::Vector<uint32_t> indices{alloc};
        // Alpha=128 marks ring vertices so the PS skips axis fade (rings stay solid edge-on).
        const uint32_t baseColor = hot ? kAxisColorsHot[axisIndex] : kAxisColors[axisIndex];
        const uint32_t ringColor = (baseColor & 0x00FFFFFFu) | (128u << 24);
        EmitTorus(verts, indices, 0, ringColor, 1.0f, 0.022f, 48, 8);
        const char* kNames[3] = {"Gizmo Rotate X", "Gizmo Rotate Y", "Gizmo Rotate Z"};
        const char* kNamesHot[3] = {"Gizmo Rotate X Hot", "Gizmo Rotate Y Hot", "Gizmo Rotate Z Hot"};
        return CreateProceduralMesh(m_context, alloc, verts, indices,
                                    hot ? kNamesHot[axisIndex] : kNames[axisIndex]);
    }

    swarm::Mesh* RenderModule::BuildGizmoScaleAxisMesh(uint8_t axisIndex, bool hot)
    {
        if (axisIndex >= 3)
        {
            return nullptr;
        }
        auto& alloc = GetCacheAllocator();
        wax::Vector<swarm::Vertex> verts{alloc};
        wax::Vector<uint32_t> indices{alloc};
        const uint32_t color = hot ? kAxisColorsHot[axisIndex] : kAxisColors[axisIndex];
        EmitCylinder(verts, indices, 0, color, 0.f, 0.88f, 0.022f, 12);
        EmitBox(verts, indices, 0, color, 0.94f, 0.055f);
        const char* kNames[3] = {"Gizmo Scale X", "Gizmo Scale Y", "Gizmo Scale Z"};
        const char* kNamesHot[3] = {"Gizmo Scale X Hot", "Gizmo Scale Y Hot", "Gizmo Scale Z Hot"};
        return CreateProceduralMesh(m_context, alloc, verts, indices,
                                    hot ? kNamesHot[axisIndex] : kNames[axisIndex]);
    }

    namespace
    {
        swarm::VertexDomain TranslateDomain(nectar::VertexDomain d)
        {
            switch (d)
            {
                case nectar::VertexDomain::SkinnedMesh:
                    return swarm::VertexDomain::SKINNED_MESH;
                case nectar::VertexDomain::UI:
                    return swarm::VertexDomain::UI;
                case nectar::VertexDomain::StaticMesh:
                default:
                    return swarm::VertexDomain::STATIC_MESH;
            }
        }

        swarm::CullMode ParseCull(wax::StringView s)
        {
            if (s == wax::StringView{"None"})
            {
                return swarm::CullMode::NONE;
            }
            if (s == wax::StringView{"Front"})
            {
                return swarm::CullMode::FRONT;
            }
            return swarm::CullMode::BACK;
        }

        swarm::BlendMode ParseBlend(wax::StringView s)
        {
            if (s == wax::StringView{"AlphaBlend"})
            {
                return swarm::BlendMode::ALPHA_BLEND;
            }
            if (s == wax::StringView{"Additive"})
            {
                return swarm::BlendMode::ADDITIVE;
            }
            return swarm::BlendMode::OPAQUE_;
        }

        uint32_t EncodeParam(const nectar::HiveValue& value, wax::Vector<uint8_t>& outBlob)
        {
            switch (value.m_type)
            {
                case nectar::HiveValue::Type::FLOAT: {
                    const float f = static_cast<float>(value.AsFloat());
                    const size_t start = outBlob.Size();
                    outBlob.Resize(start + sizeof(float));
                    std::memcpy(outBlob.Data() + start, &f, sizeof(float));
                    return sizeof(float);
                }
                case nectar::HiveValue::Type::INT: {
                    const float f = static_cast<float>(value.AsInt());
                    const size_t start = outBlob.Size();
                    outBlob.Resize(start + sizeof(float));
                    std::memcpy(outBlob.Data() + start, &f, sizeof(float));
                    return sizeof(float);
                }
                case nectar::HiveValue::Type::FLOAT_ARRAY: {
                    const auto& arr = value.AsFloatArray();
                    const size_t bytes = arr.Size() * sizeof(float);
                    const size_t start = outBlob.Size();
                    outBlob.Resize(start + bytes);
                    for (size_t i = 0; i < arr.Size(); ++i)
                    {
                        const float f = static_cast<float>(arr[i]);
                        std::memcpy(outBlob.Data() + start + i * sizeof(float), &f, sizeof(float));
                    }
                    return static_cast<uint32_t>(bytes);
                }
                default:
                    return 0;
            }
        }
    } // namespace

    RenderModule::ParsedCookedTexture RenderModule::ParseCookedTextureBlob(nectar::AssetId id,
                                                                            wax::StringView debugPath,
                                                                            ProjectManager& project,
                                                                            comb::DefaultAllocator& alloc)
    {
        ParsedCookedTexture out;
        out.m_blob = project.ReadCookedBlob(id);
        if (out.m_blob.IsEmpty())
        {
            hive::LogWarning(LOG_RENDER, "LoadTexture: no cooked blob for '{}' (id valid={})",
                             wax::String{GetCacheAllocator(), debugPath}.CStr(), id.IsValid() ? "true" : "false");
            return out;
        }

        nectar::TextureAssetLoader loader;
        out.m_asset = loader.Load(wax::ByteSpan{out.m_blob.Data(), out.m_blob.Size()}, alloc);
        if (out.m_asset == nullptr)
        {
            hive::LogError(LOG_RENDER, "LoadTexture: NTEX parse failed for '{}'",
                           wax::String{GetCacheAllocator(), debugPath}.CStr());
            return out;
        }

        const nectar::PixelFormat format = out.m_asset->header.m_format;
        if (format != nectar::PixelFormat::RGBA8 && format != nectar::PixelFormat::BC7)
        {
            hive::LogWarning(LOG_RENDER, "LoadTexture: '{}' format {} unsupported (need RGBA8 or BC7)",
                             wax::String{GetCacheAllocator(), debugPath}.CStr(),
                             static_cast<int>(format));
            loader.Unload(out.m_asset, alloc);
            out.m_asset = nullptr;
        }
        return out;
    }

    swarm::Texture* RenderModule::UploadParsedTexture(const ParsedCookedTexture& parsed, wax::StringView debugPath,
                                                       comb::DefaultAllocator& alloc)
    {
        if (parsed.m_asset == nullptr)
        {
            return nullptr;
        }

        wax::Vector<swarm::TextureMipUpload> mips{alloc};
        mips.Reserve(parsed.m_asset->header.m_mipCount);
        const nectar::TextureMipLevel* levels = parsed.m_asset->MipLevels();
        for (uint8_t i = 0; i < parsed.m_asset->header.m_mipCount; ++i)
        {
            swarm::TextureMipUpload m{};
            m.m_width = levels[i].m_width;
            m.m_height = levels[i].m_height;
            m.m_pixels = parsed.m_asset->MipData(i);
            m.m_byteCount = levels[i].m_size;
            mips.PushBack(m);
        }

        wax::String debugName{alloc, debugPath};
        swarm::TextureDesc desc{};
        desc.m_debugName = debugName.CStr();
        const bool isSrgb = parsed.m_asset->header.m_srgb;
        if (parsed.m_asset->header.m_format == nectar::PixelFormat::BC7)
        {
            desc.m_format = isSrgb ? swarm::TextureFormat::BC7_UNORM_SRGB : swarm::TextureFormat::BC7_UNORM;
        }
        else
        {
            desc.m_format = isSrgb ? swarm::TextureFormat::RGBA8_SRGB : swarm::TextureFormat::RGBA8_UNORM;
        }
        desc.m_width = parsed.m_asset->header.m_width;
        desc.m_height = parsed.m_asset->header.m_height;
        desc.m_mips = mips.Data();
        desc.m_mipCount = static_cast<uint32_t>(mips.Size());

        swarm::Texture* tex = swarm::CreateTexture(m_context, desc);
        if (tex != nullptr)
        {
            hive::LogInfo(LOG_RENDER, "Loaded texture '{}'", debugName.CStr());
        }
        return tex;
    }

    swarm::Texture* RenderModule::LoadCookedTexture(nectar::AssetId id, wax::StringView debugPath,
                                                    ProjectManager& project)
    {
        auto& alloc = GetTextureLoadAllocator();
        ParsedCookedTexture parsed = ParseCookedTextureBlob(id, debugPath, project, alloc);
        swarm::Texture* tex = UploadParsedTexture(parsed, debugPath, alloc);
        if (parsed.m_asset != nullptr)
        {
            nectar::TextureAssetLoader loader;
            loader.Unload(parsed.m_asset, alloc);
        }
        return tex;
    }

    swarm::Material* RenderModule::UploadParsedMaterial(const ParsedMaterial& parsed, ProjectManager& project,
                                                          comb::DefaultAllocator& alloc)
    {
        if (parsed.m_matAsset == nullptr || parsed.m_shaderAsset == nullptr)
        {
            hive::LogError(LOG_RENDER, "UploadMaterial: missing parsed data for '{}'", parsed.m_path.CStr());
            return nullptr;
        }

        nectar::AssetDatabase& db = project.Database();
        const nectar::MaterialAsset* matAsset = parsed.m_matAsset;
        const nectar::ShaderProgramAsset* prog = parsed.m_shaderAsset;

        wax::Vector<uint8_t> paramBlob{alloc};
        wax::Vector<swarm::MaterialParamBinding> paramBindings{alloc};
        wax::Vector<wax::String> paramNames{alloc};
        wax::Vector<size_t> paramOffsets{alloc};

        for (size_t i = 0; i < prog->m_paramAnnotations.Size(); ++i)
        {
            const auto& annotation = prog->m_paramAnnotations[i];
            wax::String key{alloc, annotation.m_name.View()};
            const nectar::HiveValue* override = matAsset->m_data.m_paramOverrides.Find(key);

            const size_t blobStart = paramBlob.Size();
            uint32_t size = 0;
            if (override != nullptr)
            {
                size = EncodeParam(*override, paramBlob);
            }
            if (size == 0 && annotation.m_default.Size() > 0)
            {
                size = static_cast<uint32_t>(annotation.m_default.Size() * sizeof(float));
                paramBlob.Resize(blobStart + size);
                std::memcpy(paramBlob.Data() + blobStart, annotation.m_default.Data(), size);
            }
            if (size == 0)
            {
                continue;
            }

            paramNames.PushBack(static_cast<wax::String&&>(key));
            paramOffsets.PushBack(blobStart);
        }

        wax::Vector<swarm::MaterialTextureBinding> textureBindings{alloc};
        textureBindings.Reserve(prog->m_textureAnnotations.Size());
        for (size_t i = 0; i < prog->m_textureAnnotations.Size(); ++i)
        {
            const auto& annotation = prog->m_textureAnnotations[i];
            swarm::Texture* texture = nullptr;
            wax::String key{alloc, annotation.m_name.View()};
            const nectar::AssetId* texId = matAsset->m_data.m_textureBindings.Find(key);
            if (texId != nullptr)
            {
                const auto* record = db.FindByUuid(*texId);
                if (record != nullptr)
                {
                    texture = AcquireTexture(*texId, record->m_path.View(), project);
                }
            }

            const bool isNormal = annotation.m_name.View().Contains(wax::StringView{"normal"});
            const uint32_t fallbackSlot =
                isNormal ? swarm::kBindlessSlotDefaultNormal : swarm::kBindlessSlotDefaultWhite;

            swarm::TextureHandle handle{fallbackSlot};
            if (texture != nullptr)
            {
                handle = swarm::RegisterTexture(m_context, texture);
                if (!handle.IsValid())
                {
                    handle = swarm::TextureHandle{fallbackSlot};
                }
            }
            else
            {
                texture = isNormal ? m_defaultNormal : m_defaultWhite;
            }

            if (texture != nullptr)
            {
                swarm::MaterialTextureBinding tb{};
                tb.m_name = annotation.m_name.CStr();
                tb.m_texture = texture;
                tb.m_sampler.m_filter = swarm::SamplerFilter::LINEAR;
                tb.m_sampler.m_address = swarm::SamplerAddress::WRAP;
                tb.m_sampler.m_mipmaps = true;
                textureBindings.PushBack(tb);
            }

            wax::String indexName{alloc, annotation.m_name.View()};
            indexName.Append(wax::StringView{"_index"});
            const size_t blobStart = paramBlob.Size();
            paramBlob.Resize(blobStart + sizeof(uint32_t));
            std::memcpy(paramBlob.Data() + blobStart, &handle.m_index, sizeof(uint32_t));
            paramNames.PushBack(static_cast<wax::String&&>(indexName));
            paramOffsets.PushBack(blobStart);
        }

        for (size_t i = 0; i < paramNames.Size(); ++i)
        {
            swarm::MaterialParamBinding binding{};
            binding.m_name = paramNames[i].CStr();
            binding.m_data = paramBlob.Data() + paramOffsets[i];
            const size_t end = (i + 1 < paramOffsets.Size()) ? paramOffsets[i + 1] : paramBlob.Size();
            binding.m_size = static_cast<uint32_t>(end - paramOffsets[i]);
            paramBindings.PushBack(binding);
        }

        swarm::MaterialDesc desc{};
        desc.m_debugName = parsed.m_path.CStr();
        desc.m_vertexShader.m_path = prog->m_vertexPath.CStr();
        desc.m_pixelShader.m_path = prog->m_pixelPath.CStr();
        desc.m_params = paramBindings.Data();
        desc.m_paramCount = static_cast<uint32_t>(paramBindings.Size());
        desc.m_textures = textureBindings.Data();
        desc.m_textureCount = static_cast<uint32_t>(textureBindings.Size());
        desc.m_domain = TranslateDomain(prog->m_domain);
        desc.m_cullMode = ParseCull(prog->m_renderState.m_cull.View());
        desc.m_blendMode = ParseBlend(prog->m_renderState.m_blend.View());
        desc.m_depthTest = prog->m_renderState.m_depthTest;
        desc.m_depthWrite = prog->m_renderState.m_depthWrite;
        desc.m_frontCCW = prog->m_renderState.m_frontCcw;
        desc.m_depthBias = prog->m_renderState.m_depthBias;
        desc.m_slopeScaledDepthBias = prog->m_renderState.m_slopeScaledDepthBias;

        swarm::Material* material = swarm::CreateMaterial(m_context, desc);
        if (material != nullptr)
        {
            hive::LogInfo(LOG_RENDER, "Loaded material '{}'", parsed.m_path.CStr());
        }
        else
        {
            hive::LogError(LOG_RENDER, "UploadMaterial: CreateMaterial failed for '{}'", parsed.m_path.CStr());
        }
        return material;
    }

    swarm::Material* RenderModule::LoadMaterialFromVfs(wax::StringView path, ProjectManager& project)
    {
        auto& alloc = GetMaterialLoadAllocator();
        ParsedMaterial parsed = ParseMaterialBlobs(path, project, alloc);
        swarm::Material* material = nullptr;
        if (parsed.m_matAsset != nullptr && parsed.m_shaderAsset != nullptr)
        {
            material = UploadParsedMaterial(parsed, project, alloc);
        }
        ReleaseParsedMaterial(parsed, alloc);
        return material;
    }
} // namespace waggle
