#include <swarm/platform/diligent_swarm.h>
#include <swarm/precomp.h>
#include <swarm/swarm.h>
#include <swarm/swarm_log.h>

#include <hive/core/log.h>
#include <hive/math/functions.h>

#include <diligent_internal.h>

#include <Buffer.h>
#include <DeviceContext.h>
#include <GraphicsTypes.h>
#include <MapHelper.hpp>

namespace swarm
{
    namespace
    {
        hive::math::Float4 ToFloat4(hive::math::Float3 vector, float w)
        {
            return hive::math::Float4{vector.m_x, vector.m_y, vector.m_z, w};
        }
    } // namespace

    bool InitSceneConstants(RenderContext* context)
    {
        using namespace Diligent;

        if (context == nullptr || context->m_device == nullptr)
        {
            return false;
        }

        {
            BufferDesc descriptor;
            descriptor.Name = "Swarm ViewConstants";
            descriptor.Size = sizeof(ViewConstantsGpu);
            descriptor.Usage = USAGE_DYNAMIC;
            descriptor.BindFlags = BIND_UNIFORM_BUFFER;
            descriptor.CPUAccessFlags = CPU_ACCESS_WRITE;
            context->m_device->CreateBuffer(descriptor, nullptr, &context->m_viewConstantBuffer);
        }
        if (context->m_viewConstantBuffer == nullptr)
        {
            hive::LogError(LOG_SWARM, "InitSceneConstants: failed to create view constant buffer");
            return false;
        }

        {
            BufferDesc descriptor;
            descriptor.Name = "Swarm ObjectConstants";
            descriptor.Size = sizeof(ObjectConstantsGpu);
            descriptor.Usage = USAGE_DYNAMIC;
            descriptor.BindFlags = BIND_UNIFORM_BUFFER;
            descriptor.CPUAccessFlags = CPU_ACCESS_WRITE;
            context->m_device->CreateBuffer(descriptor, nullptr, &context->m_objectConstantBuffer);
        }
        if (context->m_objectConstantBuffer == nullptr)
        {
            context->m_viewConstantBuffer->Release();
            context->m_viewConstantBuffer = nullptr;
            hive::LogError(LOG_SWARM, "InitSceneConstants: failed to create object constant buffer");
            return false;
        }

        for (uint32_t i = 0; i < context->m_deferredContextCount; ++i)
        {
            context->m_viewDirty[i] = true;
        }
        return true;
    }

    void ShutdownSceneConstants(RenderContext* context)
    {
        if (context == nullptr)
        {
            return;
        }

        if (context->m_objectConstantBuffer != nullptr)
        {
            context->m_objectConstantBuffer->Release();
            context->m_objectConstantBuffer = nullptr;
        }
        if (context->m_viewConstantBuffer != nullptr)
        {
            context->m_viewConstantBuffer->Release();
            context->m_viewConstantBuffer = nullptr;
        }
    }

    void SetView(RenderContext* context, const ViewParams& view)
    {
        if (context == nullptr)
        {
            return;
        }
        context->m_viewParams = view;
        for (uint32_t i = 0; i < context->m_deferredContextCount; ++i)
        {
            context->m_viewDirty[i] = true;
        }
    }

    void SetLighting(RenderContext* context, const LightingParams& lighting)
    {
        if (context == nullptr)
        {
            return;
        }
        context->m_lightingParams = lighting;
        for (uint32_t i = 0; i < context->m_deferredContextCount; ++i)
        {
            context->m_viewDirty[i] = true;
        }
    }

    static void FlushViewConstantsIfDirty(RenderContext* context, uint32_t deferredIndex)
    {
        using namespace Diligent;

        if (context == nullptr || context->m_viewConstantBuffer == nullptr ||
            deferredIndex >= context->m_deferredContextCount)
        {
            return;
        }
        if (!context->m_viewDirty[deferredIndex])
        {
            return;
        }

        IDeviceContext* target = context->m_deferredContexts[deferredIndex];
        MapHelper<ViewConstantsGpu> mapped{target, context->m_viewConstantBuffer, MAP_WRITE, MAP_FLAG_DISCARD};
        if (mapped == nullptr)
        {
            return;
        }

        mapped->m_view = context->m_viewParams.m_view;
        mapped->m_proj = context->m_viewParams.m_proj;
        mapped->m_eyeWorld = ToFloat4(context->m_viewParams.m_eyeWorld, 0.f);
        mapped->m_sunDirection = ToFloat4(context->m_lightingParams.m_sunDirection, 0.f);
        const hive::math::Float3 scaledSun{
            context->m_lightingParams.m_sunColor.m_x * context->m_lightingParams.m_sunIntensity,
            context->m_lightingParams.m_sunColor.m_y * context->m_lightingParams.m_sunIntensity,
            context->m_lightingParams.m_sunColor.m_z * context->m_lightingParams.m_sunIntensity,
        };
        mapped->m_sunColor = ToFloat4(scaledSun, 0.f);
        mapped->m_ambient = ToFloat4(context->m_lightingParams.m_ambientColor, 0.f);

        context->m_viewDirty[deferredIndex] = false;
    }

    void DrawMesh(RenderContext* context, uint32_t deferredIndex, const Mesh* mesh, const Material* material,
                  const hive::math::Mat4& world)
    {
        using namespace Diligent;

        if (context == nullptr || deferredIndex >= context->m_deferredContextCount)
        {
            return;
        }
        IDeviceContext* target = context->m_deferredContexts[deferredIndex];
        if (target == nullptr)
        {
            return;
        }
        if (mesh == nullptr || mesh->m_vertexBuffer == nullptr || mesh->m_indexBuffer == nullptr)
        {
            return;
        }
        if (material == nullptr || material->m_pipelineState == nullptr)
        {
            return;
        }

        context->m_deferredHasWork[deferredIndex] = true;
        FlushViewConstantsIfDirty(context, deferredIndex);

        {
            MapHelper<ObjectConstantsGpu> mapped{target, context->m_objectConstantBuffer,
                                                 MAP_WRITE, MAP_FLAG_DISCARD};
            if (mapped == nullptr)
            {
                return;
            }
            mapped->m_world = world;
            mapped->m_worldInvTranspose = hive::math::Transpose(hive::math::Inverse(world));
        }

        IBuffer* vertexBuffers[] = {mesh->m_vertexBuffer};
        const Uint64 offsets[] = {0};
        target->SetVertexBuffers(0, 1, vertexBuffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 SET_VERTEX_BUFFERS_FLAG_RESET);
        target->SetIndexBuffer(mesh->m_indexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        target->SetPipelineState(material->m_pipelineState);
        if (material->m_resourceBinding != nullptr)
        {
            target->CommitShaderResources(material->m_resourceBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        for (uint32_t i = 0; i < mesh->m_submeshCount; ++i)
        {
            const Submesh& submesh = mesh->m_submeshes[i];
            if (submesh.m_indexCount == 0)
            {
                continue;
            }

            DrawIndexedAttribs draw;
            draw.IndexType = VT_UINT32;
            draw.NumIndices = submesh.m_indexCount;
            draw.FirstIndexLocation = submesh.m_indexOffset;
            draw.Flags = DRAW_FLAG_VERIFY_ALL;
            target->DrawIndexed(draw);
        }
    }

    void DrawMesh(RenderContext* context, const Mesh* mesh, const Material* material,
                  const hive::math::Mat4& world)
    {
        DrawMesh(context, 0, mesh, material, world);
    }
} // namespace swarm
