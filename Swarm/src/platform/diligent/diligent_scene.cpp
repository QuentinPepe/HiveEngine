#include <hive/core/log.h>
#include <hive/math/functions.h>

#include <swarm/platform/diligent_swarm.h>
#include <swarm/precomp.h>
#include <swarm/swarm.h>
#include <swarm/swarm_log.h>

#include <Buffer.h>
#include <DeviceContext.h>
#include <GraphicsTypes.h>
#include <MapHelper.hpp>
#include <atomic>
#include <diligent_internal.h>

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

        {
            BufferDesc descriptor;
            descriptor.Name = "Swarm TimeConstants";
            descriptor.Size = sizeof(TimeConstantsGpu);
            descriptor.Usage = USAGE_DYNAMIC;
            descriptor.BindFlags = BIND_UNIFORM_BUFFER;
            descriptor.CPUAccessFlags = CPU_ACCESS_WRITE;
            context->m_device->CreateBuffer(descriptor, nullptr, &context->m_timeConstantBuffer);
        }
        if (context->m_timeConstantBuffer == nullptr)
        {
            context->m_objectConstantBuffer->Release();
            context->m_objectConstantBuffer = nullptr;
            context->m_viewConstantBuffer->Release();
            context->m_viewConstantBuffer = nullptr;
            hive::LogError(LOG_SWARM, "InitSceneConstants: failed to create time constant buffer");
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

        if (context->m_timeConstantBuffer != nullptr)
        {
            context->m_timeConstantBuffer->Release();
            context->m_timeConstantBuffer = nullptr;
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

    void SetTime(RenderContext* context, float seconds, float deltaSeconds)
    {
        if (context == nullptr)
        {
            return;
        }
        context->m_timeSeconds = seconds;
        context->m_deltaSeconds = deltaSeconds;
        for (uint32_t i = 0; i < context->m_deferredContextCount; ++i)
        {
            context->m_timeDirty[i] = true;
        }
    }

    static void FlushTimeConstantsIfDirty(RenderContext* context, uint32_t deferredIndex)
    {
        using namespace Diligent;

        if (context == nullptr || context->m_timeConstantBuffer == nullptr ||
            deferredIndex >= context->m_deferredContextCount)
        {
            return;
        }
        if (!context->m_timeDirty[deferredIndex])
        {
            return;
        }

        IDeviceContext* target = context->m_deferredContexts[deferredIndex];
        MapHelper<TimeConstantsGpu> mapped{target, context->m_timeConstantBuffer, MAP_WRITE, MAP_FLAG_DISCARD};
        if (mapped == nullptr)
        {
            return;
        }
        mapped->m_timeSeconds = context->m_timeSeconds;
        mapped->m_deltaSeconds = context->m_deltaSeconds;
        context->m_timeDirty[deferredIndex] = false;
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
                  const hive::math::Mat4& world, int32_t submeshIndex)
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

        // Only StaticMesh vertex layout is implemented; other domains would feed garbage to the VS.
        if (material->m_domain != VertexDomain::STATIC_MESH)
        {
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true, std::memory_order_relaxed))
            {
                hive::LogWarning(LOG_SWARM,
                                 "DrawMesh: material expects domain {} but only STATIC_MESH "
                                 "is supported; draw skipped",
                                 static_cast<int>(material->m_domain));
            }
            return;
        }

        context->m_deferredHasWork[deferredIndex] = true;
        FlushViewConstantsIfDirty(context, deferredIndex);
        FlushTimeConstantsIfDirty(context, deferredIndex);

        {
            MapHelper<ObjectConstantsGpu> mapped{target, context->m_objectConstantBuffer, MAP_WRITE, MAP_FLAG_DISCARD};
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

        const uint32_t first = (submeshIndex < 0) ? 0u : static_cast<uint32_t>(submeshIndex);
        const uint32_t last = (submeshIndex < 0) ? mesh->m_submeshCount : static_cast<uint32_t>(submeshIndex) + 1u;
        if (first >= mesh->m_submeshCount)
        {
            return;
        }

        for (uint32_t i = first; i < last && i < mesh->m_submeshCount; ++i)
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

    void DrawMesh(RenderContext* context, const Mesh* mesh, const Material* material, const hive::math::Mat4& world,
                  int32_t submeshIndex)
    {
        DrawMesh(context, 0, mesh, material, world, submeshIndex);
    }
} // namespace swarm
