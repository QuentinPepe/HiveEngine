#pragma once

#include <hive/hive_config.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/string.h>
#include <wax/containers/string_view.h>

#include <nectar/core/asset_id.h>

namespace nectar
{
    class VirtualFilesystem;
    class AssetDatabase;
} // namespace nectar

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

        [[nodiscard]] swarm::Mesh* AcquireMesh(wax::StringView name, nectar::VirtualFilesystem* vfs);

        [[nodiscard]] swarm::Material* AcquireMaterial(wax::StringView path, ProjectManager* project);

        [[nodiscard]] swarm::Texture* AcquireTexture(nectar::AssetId id, wax::StringView path, ProjectManager& project);

        void InvalidateMaterial(wax::StringView path);

    private:
        swarm::Mesh* BuildCubeMesh();
        swarm::Texture* BuildSolidTexture(const uint8_t (&rgba)[4], const char* name);
        swarm::Mesh* LoadMeshFromVfs(wax::StringView name, nectar::VirtualFilesystem& vfs);
        swarm::Material* LoadMaterialFromVfs(wax::StringView path, ProjectManager& project);
        swarm::Texture* LoadCookedTexture(nectar::AssetId id, wax::StringView debugPath, ProjectManager& project);

        swarm::RenderContext* m_context{nullptr};
        swarm::Mesh* m_cubeMesh{nullptr};
        swarm::Texture* m_defaultWhite{nullptr};
        swarm::Texture* m_defaultNormal{nullptr};
        wax::HashMap<wax::String, swarm::Mesh*> m_meshCache;
        wax::HashMap<wax::String, swarm::Material*> m_materialPathCache;
        wax::HashMap<wax::String, swarm::Texture*> m_textureCache;
    };
} // namespace waggle
