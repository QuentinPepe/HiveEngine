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

#include <waggle/project/project_manager.h>
#include <waggle/render/render_module.h>

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

        comb::DefaultAllocator& GetCacheAllocator()
        {
            static comb::ModuleAllocator allocator{"Waggle.Render", 1 * 1024 * 1024};
            return allocator.Get();
        }

        comb::DefaultAllocator& GetMeshLoadAllocator()
        {
            static comb::ModuleAllocator allocator{"Waggle.MeshLoad", 64 * 1024 * 1024};
            return allocator.Get();
        }

        comb::DefaultAllocator& GetTextureLoadAllocator()
        {
            static comb::ModuleAllocator allocator{"Waggle.TextureLoad", 256 * 1024 * 1024};
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
        : m_context{context}
        , m_meshCache{GetCacheAllocator()}
        , m_materialPathCache{GetCacheAllocator()}
        , m_textureCache{GetCacheAllocator()}
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

        constexpr uint8_t kWhitePixel[4]{255, 255, 255, 255};
        constexpr uint8_t kFlatNormalPixel[4]{128, 128, 255, 255};
        m_defaultWhite = BuildSolidTexture(kWhitePixel, "Default White");
        m_defaultNormal = BuildSolidTexture(kFlatNormalPixel, "Default Normal");
    }

    RenderModule::~RenderModule()
    {
        for (auto it = m_materialPathCache.begin(); it != m_materialPathCache.end(); ++it)
        {
            if (it.Value() != nullptr)
                swarm::DestroyMaterial(it.Value());
        }
        m_materialPathCache.Clear();

        for (auto it = m_textureCache.begin(); it != m_textureCache.end(); ++it)
        {
            if (it.Value() != nullptr)
                swarm::DestroyTexture(it.Value());
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

    swarm::Material* RenderModule::AcquireMaterial(wax::StringView path, ProjectManager* project)
    {
        if (path.IsEmpty())
            path = kDefaultMaterialPath;

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
            return nullptr;

        wax::String key{GetCacheAllocator(), path};
        if (auto* hit = m_textureCache.Find(key))
            return *hit;

        swarm::Texture* loaded = LoadCookedTexture(id, path, project);
        m_textureCache.Insert(static_cast<wax::String&&>(key), loaded);
        return loaded;
    }

    void RenderModule::InvalidateMaterial(wax::StringView path)
    {
        wax::String key{GetCacheAllocator(), path};
        if (auto* hit = m_materialPathCache.Find(key))
        {
            if (*hit != nullptr)
                swarm::DestroyMaterial(*hit);
            m_materialPathCache.Remove(key);
        }
    }

    swarm::Mesh* RenderModule::AcquireMesh(wax::StringView name, nectar::VirtualFilesystem* vfs)
    {
        if (name.IsEmpty())
            return nullptr;

        if (name == kBuiltinCube)
            return m_cubeMesh;

        wax::String key{GetCacheAllocator(), name};
        if (auto* hit = m_meshCache.Find(key))
            return *hit;

        swarm::Mesh* mesh = nullptr;
        if (vfs != nullptr)
        {
            mesh = LoadMeshFromVfs(name, *vfs);
            if (mesh == nullptr)
                hive::LogWarning(LOG_RENDER, "RenderModule::AcquireMesh: failed to load '{}'", key.CStr());
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
            return nullptr;

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
                return swarm::CullMode::NONE;
            if (s == wax::StringView{"Front"})
                return swarm::CullMode::FRONT;
            return swarm::CullMode::BACK;
        }

        swarm::BlendMode ParseBlend(wax::StringView s)
        {
            if (s == wax::StringView{"AlphaBlend"})
                return swarm::BlendMode::ALPHA_BLEND;
            if (s == wax::StringView{"Additive"})
                return swarm::BlendMode::ADDITIVE;
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

    swarm::Texture* RenderModule::LoadCookedTexture(nectar::AssetId id, wax::StringView debugPath,
                                                    ProjectManager& project)
    {
        wax::ByteBuffer bytes = project.ReadCookedBlob(id);
        if (bytes.IsEmpty())
        {
            hive::LogWarning(LOG_RENDER, "LoadTexture: no cooked blob for '{}' (id valid={})",
                             wax::String{GetCacheAllocator(), debugPath}.CStr(), id.IsValid() ? "true" : "false");
            return nullptr;
        }
        const wax::StringView path = debugPath;

        auto& alloc = GetTextureLoadAllocator();
        nectar::TextureAssetLoader loader;
        nectar::TextureAsset* asset = loader.Load(wax::ByteSpan{bytes.Data(), bytes.Size()}, alloc);
        if (asset == nullptr)
        {
            hive::LogError(LOG_RENDER, "LoadTexture: NTEX parse failed for '{}'",
                           wax::String{GetCacheAllocator(), path}.CStr());
            return nullptr;
        }

        if (asset->header.m_format != nectar::PixelFormat::RGBA8)
        {
            hive::LogWarning(LOG_RENDER, "LoadTexture: '{}' format {} unsupported (need RGBA8)",
                             wax::String{GetCacheAllocator(), path}.CStr(), static_cast<int>(asset->header.m_format));
            loader.Unload(asset, alloc);
            return nullptr;
        }

        wax::Vector<swarm::TextureMipUpload> mips{alloc};
        mips.Reserve(asset->header.m_mipCount);
        const nectar::TextureMipLevel* levels = asset->MipLevels();
        for (uint8_t i = 0; i < asset->header.m_mipCount; ++i)
        {
            swarm::TextureMipUpload m{};
            m.m_width = levels[i].m_width;
            m.m_height = levels[i].m_height;
            m.m_pixels = asset->MipData(i);
            m.m_byteCount = levels[i].m_size;
            mips.PushBack(m);
        }

        wax::String debugName{alloc, path};
        swarm::TextureDesc desc{};
        desc.m_debugName = debugName.CStr();
        desc.m_format = asset->header.m_srgb ? swarm::TextureFormat::RGBA8_SRGB : swarm::TextureFormat::RGBA8_UNORM;
        desc.m_width = asset->header.m_width;
        desc.m_height = asset->header.m_height;
        desc.m_mips = mips.Data();
        desc.m_mipCount = static_cast<uint32_t>(mips.Size());

        swarm::Texture* tex = swarm::CreateTexture(m_context, desc);
        loader.Unload(asset, alloc);

        if (tex != nullptr)
            hive::LogInfo(LOG_RENDER, "Loaded texture '{}'", debugName.CStr());
        return tex;
    }

    swarm::Material* RenderModule::LoadMaterialFromVfs(wax::StringView path, ProjectManager& project)
    {
        nectar::VirtualFilesystem& vfs = project.VFS();
        nectar::AssetDatabase* db = &project.Database();
        wax::ByteBuffer matBytes = vfs.ReadSync(path);
        if (matBytes.IsEmpty())
        {
            hive::LogWarning(LOG_RENDER, "LoadMaterial: '{}' not found in VFS",
                             wax::String{GetCacheAllocator(), path}.CStr());
            return nullptr;
        }

        auto& alloc = GetCacheAllocator();
        nectar::MaterialAssetLoader matLoader;
        nectar::MaterialAsset* matAsset = matLoader.Load(wax::ByteSpan{matBytes.Data(), matBytes.Size()}, alloc);
        if (matAsset == nullptr)
        {
            hive::LogError(LOG_RENDER, "LoadMaterial: failed to parse '{}'",
                           wax::String{GetCacheAllocator(), path}.CStr());
            return nullptr;
        }

        const wax::StringView shaderPath = matAsset->m_data.m_shaderPath.View();
        if (shaderPath.IsEmpty())
        {
            hive::LogWarning(LOG_RENDER, "LoadMaterial: '{}' has no shader path",
                             wax::String{GetCacheAllocator(), path}.CStr());
            matLoader.Unload(matAsset, alloc);
            return nullptr;
        }

        wax::ByteBuffer shaderBytes = vfs.ReadSync(shaderPath);
        if (shaderBytes.IsEmpty())
        {
            hive::LogWarning(LOG_RENDER, "LoadMaterial: shader '{}' not in VFS",
                             wax::String{GetCacheAllocator(), shaderPath}.CStr());
            matLoader.Unload(matAsset, alloc);
            return nullptr;
        }

        nectar::ShaderProgramAssetLoader progLoader;
        nectar::ShaderProgramAsset* prog =
            progLoader.Load(wax::ByteSpan{shaderBytes.Data(), shaderBytes.Size()}, alloc);
        if (prog == nullptr)
        {
            hive::LogError(LOG_RENDER, "LoadMaterial: failed to parse shader '{}'",
                           wax::String{GetCacheAllocator(), shaderPath}.CStr());
            matLoader.Unload(matAsset, alloc);
            return nullptr;
        }

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
                continue;

            paramNames.PushBack(static_cast<wax::String&&>(key));
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

        wax::Vector<swarm::MaterialTextureBinding> textureBindings{alloc};
        textureBindings.Reserve(prog->m_textureAnnotations.Size());
        for (size_t i = 0; i < prog->m_textureAnnotations.Size(); ++i)
        {
            const auto& annotation = prog->m_textureAnnotations[i];
            swarm::Texture* texture = nullptr;
            if (db != nullptr)
            {
                wax::String key{alloc, annotation.m_name.View()};
                if (const nectar::AssetId* texId = matAsset->m_data.m_textureBindings.Find(key))
                {
                    const auto* record = db->FindByUuid(*texId);
                    if (record != nullptr)
                        texture = AcquireTexture(*texId, record->m_path.View(), project);
                }
            }
            if (texture == nullptr)
            {
                texture =
                    annotation.m_name.View().Contains(wax::StringView{"normal"}) ? m_defaultNormal : m_defaultWhite;
            }
            if (texture == nullptr)
                continue;
            swarm::MaterialTextureBinding tb{};
            tb.m_name = annotation.m_name.CStr();
            tb.m_texture = texture;
            tb.m_sampler.m_filter = swarm::SamplerFilter::LINEAR;
            tb.m_sampler.m_address = swarm::SamplerAddress::WRAP;
            tb.m_sampler.m_mipmaps = true;
            textureBindings.PushBack(tb);
        }

        swarm::MaterialDesc desc{};
        wax::String debugName{alloc, path};
        desc.m_debugName = debugName.CStr();
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

        swarm::Material* material = swarm::CreateMaterial(m_context, desc);

        progLoader.Unload(prog, alloc);
        matLoader.Unload(matAsset, alloc);

        if (material != nullptr)
        {
            hive::LogInfo(LOG_RENDER, "Loaded material '{}'", wax::String{GetCacheAllocator(), path}.CStr());
        }
        else
        {
            hive::LogError(LOG_RENDER, "LoadMaterial: CreateMaterial failed for '{}'",
                           wax::String{GetCacheAllocator(), path}.CStr());
        }
        return material;
    }
} // namespace waggle
