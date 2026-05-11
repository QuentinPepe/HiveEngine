#pragma once

#include <hive/hive_config.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/string.h>
#include <wax/containers/string_view.h>

namespace nectar
{
    class VirtualFilesystem;
}

namespace swarm
{
    struct RenderContext;
    struct Mesh;
    struct Material;
} // namespace swarm

namespace waggle
{
    // Owns the GPU resources shared by all entities rendered through the standard pipeline:
    // a single standard Material, a builtin cube Mesh, and a name-keyed mesh cache populated
    // on demand by reading NMSH blobs through Nectar's VFS.
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
            return m_standardMaterial != nullptr;
        }

        [[nodiscard]] swarm::RenderContext* GetRenderContext() const noexcept
        {
            return m_context;
        }

        [[nodiscard]] swarm::Material* GetStandardMaterial() const noexcept
        {
            return m_standardMaterial;
        }

        // Resolve a mesh by name.
        //   "builtin:cube"     -> returns the embedded primitive cube.
        //   any other path     -> tries to load <vfs>/<name> as a NMSH blob, uploads it
        //                         to the GPU, and caches the result keyed by name.
        // `vfs` may be null, in which case only builtin names resolve.
        // Returns nullptr on miss; the miss is cached so repeat lookups stay cheap.
        [[nodiscard]] swarm::Mesh* AcquireMesh(wax::StringView name, nectar::VirtualFilesystem* vfs);

    private:
        swarm::Mesh* BuildCubeMesh();
        swarm::Mesh* LoadMeshFromVfs(wax::StringView name, nectar::VirtualFilesystem& vfs);

        swarm::RenderContext* m_context{nullptr};
        swarm::Material* m_standardMaterial{nullptr};
        swarm::Mesh* m_cubeMesh{nullptr};
        wax::HashMap<wax::String, swarm::Mesh*> m_meshCache;
    };
} // namespace waggle
