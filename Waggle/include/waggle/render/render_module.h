#pragma once

#include <hive/hive_config.h>

#include <comb/default_allocator.h>

#include <drone/job_submitter.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/string.h>
#include <wax/containers/string_view.h>
#include <wax/containers/vector.h>
#include <wax/serialization/byte_buffer.h>

#include <nectar/core/asset_id.h>

namespace nectar
{
    class VirtualFilesystem;
    class AssetDatabase;
    struct TextureAsset;
    struct MaterialAsset;
    struct ShaderProgramAsset;
} // namespace nectar

namespace queen
{
    class World;
}

namespace waggle
{
    class ProjectManager;
}

namespace swarm
{
    struct RenderContext;
    struct Mesh;
    struct Material;
    struct Texture;
} // namespace swarm

namespace waggle
{
    class HIVE_API RenderModule
    {
    public:
        explicit RenderModule(swarm::RenderContext* context);
        RenderModule(swarm::RenderContext* context, drone::JobSubmitter jobs);
        ~RenderModule();

        RenderModule(const RenderModule&) = delete;
        RenderModule& operator=(const RenderModule&) = delete;
        RenderModule(RenderModule&&) = delete;
        RenderModule& operator=(RenderModule&&) = delete;

        [[nodiscard]] bool IsReady() const noexcept
        {
            return m_context != nullptr;
        }

        [[nodiscard]] swarm::RenderContext* GetRenderContext() const noexcept
        {
            return m_context;
        }

        [[nodiscard]] swarm::Material* GetDefaultMaterial(ProjectManager* project);

        [[nodiscard]] swarm::Mesh* GetEditorGridMesh() const noexcept
        {
            return m_editorGridMesh;
        }
        [[nodiscard]] swarm::Material* GetEditorGridMaterial(ProjectManager* project);

        [[nodiscard]] swarm::Mesh* GetGizmoTranslateAxisMesh(uint8_t axisIndex, bool hot) const noexcept
        {
            if (axisIndex >= 3)
            {
                return nullptr;
            }
            return hot ? m_gizmoTranslateAxisMeshesHot[axisIndex] : m_gizmoTranslateAxisMeshes[axisIndex];
        }
        [[nodiscard]] swarm::Mesh* GetGizmoRotateRingMesh(uint8_t axisIndex, bool hot) const noexcept
        {
            if (axisIndex >= 3)
            {
                return nullptr;
            }
            return hot ? m_gizmoRotateRingMeshesHot[axisIndex] : m_gizmoRotateRingMeshes[axisIndex];
        }
        [[nodiscard]] swarm::Mesh* GetGizmoScaleAxisMesh(uint8_t axisIndex, bool hot) const noexcept
        {
            if (axisIndex >= 3)
            {
                return nullptr;
            }
            return hot ? m_gizmoScaleAxisMeshesHot[axisIndex] : m_gizmoScaleAxisMeshes[axisIndex];
        }
        [[nodiscard]] swarm::Material* GetGizmoMaterial(ProjectManager* project);

        [[nodiscard]] swarm::Mesh* AcquireMesh(wax::StringView name, nectar::VirtualFilesystem* vfs);

        [[nodiscard]] swarm::Material* AcquireMaterial(wax::StringView path, ProjectManager* project);

        [[nodiscard]] swarm::Texture* AcquireTexture(nectar::AssetId id, wax::StringView path, ProjectManager& project);

        using PreloadProgressFn = void (*)(const char* step, uint32_t current, uint32_t total, void* userdata);
        void PreloadScene(queen::World& world, ProjectManager* project, PreloadProgressFn progress,
                          void* userdata);

        void InvalidateMaterial(wax::StringView path);

    private:
        swarm::Mesh* BuildCubeMesh();
        swarm::Mesh* BuildEditorGridMesh();
        swarm::Mesh* BuildGizmoTranslateAxisMesh(uint8_t axisIndex, bool hot);
        swarm::Mesh* BuildGizmoRotateRingMesh(uint8_t axisIndex, bool hot);
        swarm::Mesh* BuildGizmoScaleAxisMesh(uint8_t axisIndex, bool hot);
        swarm::Texture* BuildSolidTexture(const uint8_t (&rgba)[4], const char* name);
        swarm::Mesh* LoadMeshFromVfs(wax::StringView name, nectar::VirtualFilesystem& vfs);
        swarm::Material* LoadMaterialFromVfs(wax::StringView path, ProjectManager& project);
        swarm::Texture* LoadCookedTexture(nectar::AssetId id, wax::StringView debugPath, ProjectManager& project);

        struct ParsedCookedTexture
        {
            nectar::TextureAsset* m_asset{nullptr};
            wax::ByteBuffer m_blob;
        };
        ParsedCookedTexture ParseCookedTextureBlob(nectar::AssetId id, wax::StringView debugPath,
                                                    ProjectManager& project, comb::DefaultAllocator& alloc);
        swarm::Texture* UploadParsedTexture(const ParsedCookedTexture& parsed, wax::StringView debugPath,
                                             comb::DefaultAllocator& alloc);

        struct TextureRef
        {
            nectar::AssetId m_id;
            wax::String m_debugPath;
        };

        struct ParsedMaterial
        {
            wax::String m_path;
            wax::ByteBuffer m_matBlob;
            wax::ByteBuffer m_shaderBlob;
            nectar::MaterialAsset* m_matAsset{nullptr};
            nectar::ShaderProgramAsset* m_shaderAsset{nullptr};
        };
        ParsedMaterial ParseMaterialBlobs(wax::StringView path, ProjectManager& project,
                                            comb::DefaultAllocator& alloc);
        void ReleaseParsedMaterial(ParsedMaterial& parsed, comb::DefaultAllocator& alloc);
        swarm::Material* UploadParsedMaterial(const ParsedMaterial& parsed, ProjectManager& project,
                                                comb::DefaultAllocator& alloc);
        void CollectTextureRefsFromParsedMaterial(const ParsedMaterial& parsed, ProjectManager& project,
                                                    wax::Vector<TextureRef>& outRefs);

        swarm::RenderContext* m_context{nullptr};
        swarm::Mesh* m_cubeMesh{nullptr};
        swarm::Mesh* m_editorGridMesh{nullptr};
        swarm::Mesh* m_gizmoTranslateAxisMeshes[3]{};
        swarm::Mesh* m_gizmoRotateRingMeshes[3]{};
        swarm::Mesh* m_gizmoScaleAxisMeshes[3]{};
        swarm::Mesh* m_gizmoTranslateAxisMeshesHot[3]{};
        swarm::Mesh* m_gizmoRotateRingMeshesHot[3]{};
        swarm::Mesh* m_gizmoScaleAxisMeshesHot[3]{};
        swarm::Texture* m_defaultWhite{nullptr};
        swarm::Texture* m_defaultNormal{nullptr};
        wax::HashMap<wax::String, swarm::Mesh*> m_meshCache;
        wax::HashMap<wax::String, swarm::Material*> m_materialPathCache;
        wax::HashMap<wax::String, swarm::Texture*> m_textureCache;
        drone::JobSubmitter m_jobs{};
    };
} // namespace waggle
