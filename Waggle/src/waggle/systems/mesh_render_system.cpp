#include <waggle/systems/mesh_render_system.h>

#include <hive/math/functions.h>
#include <hive/math/transforms.h>

#include <drone/worker_context.h>

#include <queen/query/query_term.h>
#include <queen/world/world.h>

#include <swarm/swarm.h>

#include <waggle/components/camera.h>
#include <waggle/components/editor_only.h>
#include <waggle/components/lighting.h>
#include <waggle/components/mesh_reference.h>
#include <waggle/components/transform.h>
#include <waggle/project/project_context.h>
#include <waggle/project/project_manager.h>
#include <waggle/render/render_module.h>

#include <vector>

namespace waggle
{
    namespace
    {
        hive::math::Float3 ExtractTranslation(const hive::math::Mat4& m)
        {
            return hive::math::Float3{m.m_m[3][0], m.m_m[3][1], m.m_m[3][2]};
        }
    } // namespace

    void RenderMeshes(queen::World& world, RenderModule& renderModule, float aspect,
                      const drone::JobSubmitter& submitter)
    {
        if (!renderModule.IsReady())
        {
            return;
        }

        swarm::RenderContext* ctx = renderModule.GetRenderContext();
        if (ctx == nullptr)
        {
            return;
        }

        const Camera* activeCamera = nullptr;
        hive::math::Mat4 cameraWorld = hive::math::Mat4::Identity();

        // Editor camera (if present) takes precedence: the user is navigating with it.
        // Once the engine grows a play-mode toggle, this preference would flip.
        world.Query<queen::Read<WorldMatrix>, queen::Read<Camera>, queen::Read<EditorOnly>>().Each(
            [&](const WorldMatrix& worldMatrix, const Camera& camera, const EditorOnly&) {
                if (activeCamera == nullptr)
                {
                    activeCamera = &camera;
                    cameraWorld = worldMatrix.m_matrix;
                }
            });
        if (activeCamera == nullptr)
        {
            world.Query<queen::Read<WorldMatrix>, queen::Read<Camera>>().Each(
                [&](const WorldMatrix& worldMatrix, const Camera& camera) {
                    if (activeCamera == nullptr)
                    {
                        activeCamera = &camera;
                        cameraWorld = worldMatrix.m_matrix;
                    }
                });
        }
        if (activeCamera == nullptr)
        {
            return;
        }

        swarm::ViewParams view;
        view.m_view = hive::math::Inverse(cameraWorld);
        view.m_proj = hive::math::Perspective(activeCamera->m_fovRad, aspect > 0.f ? aspect : 1.f,
                                              activeCamera->m_zNear, activeCamera->m_zFar);
        view.m_eyeWorld = ExtractTranslation(cameraWorld);
        swarm::SetView(ctx, view);

        swarm::LightingParams lighting;
        bool sunFound = false;
        world.Query<queen::Read<DirectionalLight>>().Each([&](const DirectionalLight& light) {
            if (!sunFound)
            {
                lighting.m_sunDirection = light.m_direction;
                lighting.m_sunColor = light.m_color;
                lighting.m_sunIntensity = light.m_intensity;
                sunFound = true;
            }
        });
        bool ambientFound = false;
        world.Query<queen::Read<AmbientLight>>().Each([&](const AmbientLight& a) {
            if (!ambientFound)
            {
                lighting.m_ambientColor = a.m_color;
                ambientFound = true;
            }
        });
        swarm::SetLighting(ctx, lighting);

        swarm::Material* material = renderModule.GetStandardMaterial();
        if (material == nullptr)
        {
            return;
        }

        nectar::VirtualFilesystem* vfs = nullptr;
        if (auto* projectCtx = world.Resource<ProjectContext>();
            projectCtx != nullptr && projectCtx->m_manager != nullptr)
        {
            vfs = &projectCtx->m_manager->VFS();
        }

        struct DrawCommand
        {
            swarm::Mesh* m_mesh;
            hive::math::Mat4 m_world;
        };
        std::vector<DrawCommand> commands;
        commands.reserve(world.EntityCount());

        world.Query<queen::Read<WorldMatrix>, queen::Read<MeshReference>>().Each(
            [&](const WorldMatrix& wm, const MeshReference& mr) {
                swarm::Mesh* mesh = renderModule.AcquireMesh(mr.m_meshName.View(), vfs);
                if (mesh == nullptr)
                {
                    return;
                }
                commands.push_back({mesh, wm.m_matrix});
            });

        if (commands.empty())
        {
            return;
        }

        const uint32_t deferredCount = swarm::GetDeferredContextCount(ctx);
        const uint32_t workerCount = submitter.IsValid() ? static_cast<uint32_t>(submitter.WorkerCount()) : 0;
        // Each worker needs its own deferred ctx to avoid concurrent recording into the same
        // context. If the pool has more workers than contexts, fall back to serial recording.
        const bool parallel = submitter.IsValid() && deferredCount >= workerCount && workerCount > 1 &&
                              commands.size() > 1;

        if (!parallel)
        {
            for (const auto& command : commands)
            {
                swarm::DrawMesh(ctx, 0, command.m_mesh, material, command.m_world);
            }
            return;
        }

        // Prepare each deferred context's RT binding before workers start drawing. Deferred
        // ctx 0 was already prepared by swarm::BeginFrame; the rest need explicit binding.
        for (uint32_t i = 1; i < workerCount; ++i)
        {
            swarm::PrepareWorkerFrame(ctx, i);
        }

        struct ParallelDrawContext
        {
            swarm::RenderContext* m_renderContext;
            const DrawCommand* m_commands;
            swarm::Material* m_material;
        };

        ParallelDrawContext parallelContext{ctx, commands.data(), material};

        auto draw = [](size_t index, void* userData) {
            auto& parallel = *static_cast<ParallelDrawContext*>(userData);
            const size_t workerIndex = drone::WorkerContext::CurrentWorkerIndex();
            // ParallelFor only dispatches to workers; main thread blocks in Wait().
            const uint32_t deferredIndex =
                workerIndex == drone::WorkerContext::kMainThread ? 0 : static_cast<uint32_t>(workerIndex);
            const DrawCommand& command = parallel.m_commands[index];
            swarm::DrawMesh(parallel.m_renderContext, deferredIndex, command.m_mesh, parallel.m_material,
                            command.m_world);
        };

        submitter.ParallelFor(0, commands.size(), draw, &parallelContext, 0);
    }
} // namespace waggle
