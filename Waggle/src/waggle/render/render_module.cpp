#include <waggle/render/render_module.h>

#include <hive/core/log.h>

#include <comb/default_allocator.h>

#include <wax/containers/vector.h>

#include <nectar/assets/mesh_asset.h>
#include <nectar/mesh/mesh_data.h>
#include <nectar/vfs/virtual_filesystem.h>

#include <swarm/swarm.h>

#include <cstddef>
#include <cstring>

namespace waggle
{
    namespace
    {
        const hive::LogCategory LOG_RENDER{"Waggle.Render"};

        constexpr wax::StringView kBuiltinCube{"builtin:cube"};

        comb::DefaultAllocator& GetCacheAllocator()
        {
            static comb::ModuleAllocator allocator{"Waggle.Render", 1 * 1024 * 1024};
            return allocator.Get();
        }

        comb::DefaultAllocator& GetMeshLoadAllocator()
        {
            // Reused for every Nectar MeshAsset parse — the asset is freed as soon as the
            // GPU upload finishes, so a 64 MB buddy is enough headroom for one mesh at a time.
            static comb::ModuleAllocator allocator{"Waggle.MeshLoad", 64 * 1024 * 1024};
            return allocator.Get();
        }

        // The standard pipeline assumes nectar::MeshVertex and swarm::Vertex have the
        // exact same in-memory layout so we can reinterpret_cast between them.
        static_assert(sizeof(nectar::MeshVertex) == sizeof(swarm::Vertex),
                      "nectar::MeshVertex and swarm::Vertex must have the same size");
        static_assert(offsetof(nectar::MeshVertex, m_position) == offsetof(swarm::Vertex, m_position),
                      "position offset mismatch");
        static_assert(offsetof(nectar::MeshVertex, m_normal) == offsetof(swarm::Vertex, m_normal),
                      "normal offset mismatch");
        static_assert(offsetof(nectar::MeshVertex, m_tangent) == offsetof(swarm::Vertex, m_tangent),
                      "tangent offset mismatch");
        static_assert(offsetof(nectar::MeshVertex, m_uv) == offsetof(swarm::Vertex, m_uv),
                      "uv offset mismatch");
        static_assert(offsetof(nectar::MeshVertex, m_color) == offsetof(swarm::Vertex, m_color),
                      "color offset mismatch");
    } // namespace

    RenderModule::RenderModule(swarm::RenderContext* context)
        : m_context{context}
        , m_meshCache{GetCacheAllocator()}
    {
        if (m_context == nullptr)
        {
            hive::LogError(LOG_RENDER, "RenderModule: null render context");
            return;
        }

        m_standardMaterial = swarm::CreateStandardMaterial(m_context);
        if (m_standardMaterial == nullptr)
        {
            hive::LogError(LOG_RENDER, "RenderModule: failed to create standard material");
            return;
        }

        m_cubeMesh = BuildCubeMesh();
        if (m_cubeMesh == nullptr)
        {
            hive::LogError(LOG_RENDER, "RenderModule: failed to build cube mesh");
        }
    }

    RenderModule::~RenderModule()
    {
        for (auto it = m_meshCache.begin(); it != m_meshCache.end(); ++it)
        {
            swarm::DestroyMesh(it.Value());
        }
        m_meshCache.Clear();

        if (m_cubeMesh != nullptr)
        {
            swarm::DestroyMesh(m_cubeMesh);
            m_cubeMesh = nullptr;
        }

        if (m_standardMaterial != nullptr)
        {
            swarm::DestroyMaterial(m_standardMaterial);
            m_standardMaterial = nullptr;
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
        {
            return *hit;
        }

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

        auto& allocator = GetMeshLoadAllocator();
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
                          wax::String{GetCacheAllocator(), name}.CStr(),
                          descriptor.m_vertexCount, descriptor.m_indexCount, descriptor.m_submeshCount);
        }
        return mesh;
    }

    swarm::Mesh* RenderModule::BuildCubeMesh()
    {
        // 24 vertices (4 per face) for distinct per-face normals/UVs.
        constexpr float kHalfExtent = 0.5f;
        constexpr uint32_t kWhite = 0xFFFFFFFFu;

        const swarm::Vertex vertices[] = {
            // +Y top
            {{-kHalfExtent,  kHalfExtent, -kHalfExtent}, { 0.f,  1.f,  0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-kHalfExtent,  kHalfExtent,  kHalfExtent}, { 0.f,  1.f,  0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            {{ kHalfExtent,  kHalfExtent,  kHalfExtent}, { 0.f,  1.f,  0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{ kHalfExtent,  kHalfExtent, -kHalfExtent}, { 0.f,  1.f,  0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
            // -Y bottom
            {{-kHalfExtent, -kHalfExtent,  kHalfExtent}, { 0.f, -1.f,  0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent, -kHalfExtent}, { 0.f, -1.f,  0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            {{ kHalfExtent, -kHalfExtent, -kHalfExtent}, { 0.f, -1.f,  0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{ kHalfExtent, -kHalfExtent,  kHalfExtent}, { 0.f, -1.f,  0.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
            // +Z front
            {{-kHalfExtent, -kHalfExtent,  kHalfExtent}, { 0.f,  0.f,  1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{ kHalfExtent, -kHalfExtent,  kHalfExtent}, { 0.f,  0.f,  1.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
            {{ kHalfExtent,  kHalfExtent,  kHalfExtent}, { 0.f,  0.f,  1.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{-kHalfExtent,  kHalfExtent,  kHalfExtent}, { 0.f,  0.f,  1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            // -Z back
            {{ kHalfExtent, -kHalfExtent, -kHalfExtent}, { 0.f,  0.f, -1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent, -kHalfExtent}, { 0.f,  0.f, -1.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 0.f}, kWhite},
            {{-kHalfExtent,  kHalfExtent, -kHalfExtent}, { 0.f,  0.f, -1.f}, {1.f, 0.f, 0.f, 1.f}, {1.f, 1.f}, kWhite},
            {{ kHalfExtent,  kHalfExtent, -kHalfExtent}, { 0.f,  0.f, -1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f}, kWhite},
            // +X right
            {{ kHalfExtent, -kHalfExtent,  kHalfExtent}, { 1.f,  0.f,  0.f}, {0.f, 0.f, -1.f, 1.f}, {0.f, 0.f}, kWhite},
            {{ kHalfExtent, -kHalfExtent, -kHalfExtent}, { 1.f,  0.f,  0.f}, {0.f, 0.f, -1.f, 1.f}, {1.f, 0.f}, kWhite},
            {{ kHalfExtent,  kHalfExtent, -kHalfExtent}, { 1.f,  0.f,  0.f}, {0.f, 0.f, -1.f, 1.f}, {1.f, 1.f}, kWhite},
            {{ kHalfExtent,  kHalfExtent,  kHalfExtent}, { 1.f,  0.f,  0.f}, {0.f, 0.f, -1.f, 1.f}, {0.f, 1.f}, kWhite},
            // -X left
            {{-kHalfExtent, -kHalfExtent, -kHalfExtent}, {-1.f,  0.f,  0.f}, {0.f, 0.f, 1.f, 1.f}, {0.f, 0.f}, kWhite},
            {{-kHalfExtent, -kHalfExtent,  kHalfExtent}, {-1.f,  0.f,  0.f}, {0.f, 0.f, 1.f, 1.f}, {1.f, 0.f}, kWhite},
            {{-kHalfExtent,  kHalfExtent,  kHalfExtent}, {-1.f,  0.f,  0.f}, {0.f, 0.f, 1.f, 1.f}, {1.f, 1.f}, kWhite},
            {{-kHalfExtent,  kHalfExtent, -kHalfExtent}, {-1.f,  0.f,  0.f}, {0.f, 0.f, 1.f, 1.f}, {0.f, 1.f}, kWhite},
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
} // namespace waggle
