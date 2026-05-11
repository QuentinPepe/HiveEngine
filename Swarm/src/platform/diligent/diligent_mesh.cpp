#include <swarm/platform/diligent_swarm.h>
#include <swarm/precomp.h>
#include <swarm/swarm.h>
#include <swarm/swarm_log.h>
#include <swarm/swarmmodule.h>

#include <hive/core/log.h>

#include <diligent_internal.h>

#include <Buffer.h>
#include <GraphicsTypes.h>

#include <cstring>

namespace swarm
{
    Mesh* CreateMesh(RenderContext* context, const MeshDesc& descriptor)
    {
        using namespace Diligent;

        if (context == nullptr || context->m_device == nullptr)
        {
            return nullptr;
        }
        if (descriptor.m_vertices == nullptr || descriptor.m_vertexCount == 0)
        {
            return nullptr;
        }
        if (descriptor.m_indices == nullptr || descriptor.m_indexCount == 0)
        {
            return nullptr;
        }

        auto& allocator = SwarmModule::GetInstance().GetAllocator();

        BufferDesc vertexBufferDesc;
        vertexBufferDesc.Name = descriptor.m_debugName != nullptr ? descriptor.m_debugName : "swarm::Mesh VertexBuffer";
        vertexBufferDesc.Usage = USAGE_IMMUTABLE;
        vertexBufferDesc.BindFlags = BIND_VERTEX_BUFFER;
        vertexBufferDesc.Size = static_cast<Uint64>(descriptor.m_vertexCount) * sizeof(Vertex);

        BufferData vertexBufferData;
        vertexBufferData.pData = descriptor.m_vertices;
        vertexBufferData.DataSize = vertexBufferDesc.Size;

        IBuffer* vertexBuffer = nullptr;
        context->m_device->CreateBuffer(vertexBufferDesc, &vertexBufferData, &vertexBuffer);
        if (vertexBuffer == nullptr)
        {
            hive::LogError(LOG_SWARM, "CreateMesh: failed to create vertex buffer");
            return nullptr;
        }

        BufferDesc indexBufferDesc;
        indexBufferDesc.Name = descriptor.m_debugName != nullptr ? descriptor.m_debugName : "swarm::Mesh IndexBuffer";
        indexBufferDesc.Usage = USAGE_IMMUTABLE;
        indexBufferDesc.BindFlags = BIND_INDEX_BUFFER;
        indexBufferDesc.Size = static_cast<Uint64>(descriptor.m_indexCount) * sizeof(uint32_t);

        BufferData indexBufferData;
        indexBufferData.pData = descriptor.m_indices;
        indexBufferData.DataSize = indexBufferDesc.Size;

        IBuffer* indexBuffer = nullptr;
        context->m_device->CreateBuffer(indexBufferDesc, &indexBufferData, &indexBuffer);
        if (indexBuffer == nullptr)
        {
            vertexBuffer->Release();
            hive::LogError(LOG_SWARM, "CreateMesh: failed to create index buffer");
            return nullptr;
        }

        auto* mesh = comb::New<Mesh>(allocator);
        mesh->m_vertexBuffer = vertexBuffer;
        mesh->m_indexBuffer = indexBuffer;
        mesh->m_indexCount = descriptor.m_indexCount;

        const uint32_t submeshCount = (descriptor.m_submeshes != nullptr && descriptor.m_submeshCount > 0)
                                          ? descriptor.m_submeshCount
                                          : 1;
        mesh->m_submeshes = comb::NewArray<Submesh>(allocator, submeshCount);
        mesh->m_submeshCount = submeshCount;

        if (descriptor.m_submeshes != nullptr && descriptor.m_submeshCount > 0)
        {
            std::memcpy(mesh->m_submeshes, descriptor.m_submeshes, submeshCount * sizeof(Submesh));
        }
        else
        {
            mesh->m_submeshes[0].m_indexOffset = 0;
            mesh->m_submeshes[0].m_indexCount = descriptor.m_indexCount;
        }

        return mesh;
    }

    void DestroyMesh(Mesh* mesh)
    {
        if (mesh == nullptr)
        {
            return;
        }

        auto& allocator = SwarmModule::GetInstance().GetAllocator();

        if (mesh->m_vertexBuffer != nullptr)
        {
            mesh->m_vertexBuffer->Release();
        }
        if (mesh->m_indexBuffer != nullptr)
        {
            mesh->m_indexBuffer->Release();
        }
        if (mesh->m_submeshes != nullptr)
        {
            comb::DeleteArray(allocator, mesh->m_submeshes, mesh->m_submeshCount);
        }

        comb::Delete(allocator, mesh);
    }
} // namespace swarm
