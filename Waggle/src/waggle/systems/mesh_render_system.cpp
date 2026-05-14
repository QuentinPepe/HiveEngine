#include <hive/math/functions.h>
#include <hive/math/transforms.h>

#include <comb/default_allocator.h>

#include <drone/worker_context.h>

#include <queen/query/query_term.h>
#include <queen/world/world.h>

#include <waggle/components/camera.h>
#include <waggle/components/disabled.h>
#include <waggle/components/editor_grid.h>
#include <waggle/components/editor_only.h>
#include <waggle/components/gizmo.h>
#include <waggle/components/lighting.h>
#include <waggle/components/mesh_reference.h>
#include <waggle/components/transform.h>
#include <waggle/gizmo_state.h>
#include <waggle/play_state.h>
#include <waggle/project/project_context.h>
#include <waggle/project/project_manager.h>
#include <waggle/render/render_frame.h>
#include <waggle/render/render_module.h>
#include <waggle/systems/mesh_render_system.h>
#include <waggle/time.h>

#include <swarm/swarm.h>

namespace waggle
{
    namespace
    {
        hive::math::Float3 ExtractTranslation(const hive::math::Mat4& m)
        {
            return hive::math::Float3{m.m_m[3][0], m.m_m[3][1], m.m_m[3][2]};
        }

        bool ResolveActiveCamera(queen::World& world, PlayState playState, hive::math::Mat4* outWorld,
                                 const Camera** outCamera)
        {
            const Camera* found = nullptr;
            hive::math::Mat4 foundWorld = hive::math::Mat4::Identity();

            if (playState == PlayState::PLAYING)
            {
                world.Query<queen::Read<WorldMatrix>, queen::Read<Camera>, queen::Without<EditorOnly>>().Each(
                    [&](const WorldMatrix& worldMatrix, const Camera& camera) {
                        if (found == nullptr)
                        {
                            found = &camera;
                            foundWorld = worldMatrix.m_matrix;
                        }
                    });
            }
            else
            {
                world.Query<queen::Read<WorldMatrix>, queen::Read<Camera>, queen::Read<EditorOnly>>().Each(
                    [&](const WorldMatrix& worldMatrix, const Camera& camera, const EditorOnly&) {
                        if (found == nullptr)
                        {
                            found = &camera;
                            foundWorld = worldMatrix.m_matrix;
                        }
                    });
                if (found == nullptr)
                {
                    world.Query<queen::Read<WorldMatrix>, queen::Read<Camera>>().Each(
                        [&](const WorldMatrix& worldMatrix, const Camera& camera) {
                            if (found == nullptr)
                            {
                                found = &camera;
                                foundWorld = worldMatrix.m_matrix;
                            }
                        });
                }
            }

            if (found == nullptr)
            {
                return false;
            }
            *outCamera = found;
            *outWorld = foundWorld;
            return true;
        }
    } // namespace

    void BuildRenderFrame(queen::World& world, RenderModule& renderModule, float aspect, RenderFrame& frame)
    {
        ResetRenderFrame(frame);
        frame.m_aspect = aspect > 0.f ? aspect : 1.f;

        if (const auto* time = world.Resource<Time>(); time != nullptr)
        {
            frame.m_timeSeconds = time->m_elapsed;
            frame.m_deltaSeconds = time->m_dt;
        }

        if (!renderModule.IsReady())
        {
            return;
        }

        const PlayState playState = GetPlayState(world);

        const Camera* activeCamera = nullptr;
        hive::math::Mat4 cameraWorld = hive::math::Mat4::Identity();
        if (!ResolveActiveCamera(world, playState, &cameraWorld, &activeCamera))
        {
            return;
        }

        frame.m_view.m_view = hive::math::Inverse(cameraWorld);
        frame.m_view.m_proj = hive::math::Perspective(activeCamera->m_fovRad, frame.m_aspect, activeCamera->m_zNear,
                                                      activeCamera->m_zFar);
        frame.m_view.m_eyeWorld = ExtractTranslation(cameraWorld);
        frame.m_hasCamera = true;

        auto* viewMirror = world.Resource<EditorViewParams>();
        if (viewMirror != nullptr)
        {
            viewMirror->m_view = frame.m_view.m_view;
            viewMirror->m_proj = frame.m_view.m_proj;
            viewMirror->m_eyeWorld = frame.m_view.m_eyeWorld;
            viewMirror->m_valid = true;
        }

        bool sunFound = false;
        world.Query<queen::Read<DirectionalLight>>().Each([&](const DirectionalLight& light) {
            if (!sunFound)
            {
                frame.m_lighting.m_sunDirection = light.m_direction;
                frame.m_lighting.m_sunColor = light.m_color;
                frame.m_lighting.m_sunIntensity = light.m_intensity;
                sunFound = true;
            }
        });
        bool ambientFound = false;
        world.Query<queen::Read<AmbientLight>>().Each([&](const AmbientLight& a) {
            if (!ambientFound)
            {
                frame.m_lighting.m_ambientColor = a.m_color;
                ambientFound = true;
            }
        });

        nectar::VirtualFilesystem* vfs = nullptr;
        ProjectManager* projectMgr = nullptr;
        if (auto* projectCtx = world.Resource<ProjectContext>();
            projectCtx != nullptr && projectCtx->m_manager != nullptr)
        {
            projectMgr = projectCtx->m_manager;
            vfs = &projectMgr->VFS();

            wax::Vector<wax::String> changedMaterials{comb::GetDefaultAllocator()};
            projectMgr->ConsumeChangedMaterials(changedMaterials);
            for (size_t i = 0; i < changedMaterials.Size(); ++i)
                renderModule.InvalidateMaterial(changedMaterials[i].View());
        }

        swarm::Material* defaultMaterial = renderModule.GetDefaultMaterial(projectMgr);

        auto pushDraw = [&](const WorldMatrix& wm, const MeshReference& mr) {
            swarm::Mesh* mesh = renderModule.AcquireMesh(mr.m_meshName.View(), vfs);
            if (mesh == nullptr)
                return;
            swarm::Material* material = mr.m_material.IsEmpty()
                                            ? defaultMaterial
                                            : renderModule.AcquireMaterial(mr.m_material.View(), projectMgr);
            if (material == nullptr)
                material = defaultMaterial;
            if (material == nullptr)
                return;
            frame.m_draws.PushBack(DrawItem{mesh, material, wm.m_matrix, mr.m_meshIndex});
        };

        frame.m_draws.Reserve(world.EntityCount());
        if (playState == PlayState::PLAYING)
        {
            world.Query<queen::Read<WorldMatrix>, queen::Read<MeshReference>, queen::Without<HierarchyDisabled>,
                        queen::Without<EditorOnly>>()
                .Each([&](const WorldMatrix& wm, const MeshReference& mr) { pushDraw(wm, mr); });
        }
        else
        {
            world.Query<queen::Read<WorldMatrix>, queen::Read<MeshReference>, queen::Without<HierarchyDisabled>>().Each(
                [&](const WorldMatrix& wm, const MeshReference& mr) { pushDraw(wm, mr); });

            swarm::Mesh* gridMesh = renderModule.GetEditorGridMesh();
            swarm::Material* gridMaterial = renderModule.GetEditorGridMaterial(projectMgr);
            if (gridMesh != nullptr && gridMaterial != nullptr)
            {
                world.Query<queen::Read<WorldMatrix>, queen::Read<EditorGrid>>().Each(
                    [&](const WorldMatrix& wm, const EditorGrid&) {
                        frame.m_draws.PushBack(DrawItem{gridMesh, gridMaterial, wm.m_matrix, -1});
                    });
            }

            swarm::Material* gizmoMaterial = renderModule.GetGizmoMaterial(projectMgr);
            if (gizmoMaterial != nullptr)
            {
                world.Query<queen::Read<WorldMatrix>, queen::Read<GizmoPart>>().Each(
                    [&](const WorldMatrix& wm, const GizmoPart& part) {
                        const uint8_t axis = static_cast<uint8_t>(part.m_axis);
                        const bool hot = part.m_hot;
                        swarm::Mesh* mesh = nullptr;
                        switch (part.m_kind)
                        {
                            case GizmoKind::TRANSLATE_AXIS:
                                mesh = renderModule.GetGizmoTranslateAxisMesh(axis, hot);
                                break;
                            case GizmoKind::ROTATE_RING:
                                mesh = renderModule.GetGizmoRotateRingMesh(axis, hot);
                                break;
                            case GizmoKind::SCALE_AXIS:
                                mesh = renderModule.GetGizmoScaleAxisMesh(axis, hot);
                                break;
                        }
                        if (mesh != nullptr)
                            frame.m_draws.PushBack(DrawItem{mesh, gizmoMaterial, wm.m_matrix, -1});
                    });
            }
        }
    }

    void ExecuteRenderFrame(RenderModule& renderModule, const RenderFrame& frame, const drone::JobSubmitter& submitter)
    {
        if (!frame.m_hasCamera || !renderModule.IsReady())
        {
            return;
        }

        swarm::RenderContext* ctx = renderModule.GetRenderContext();
        if (ctx == nullptr)
        {
            return;
        }

        swarm::SetView(ctx, frame.m_view);
        swarm::SetLighting(ctx, frame.m_lighting);
        swarm::SetTime(ctx, frame.m_timeSeconds, frame.m_deltaSeconds);

        if (frame.m_draws.IsEmpty())
            return;

        const uint32_t deferredCount = swarm::GetDeferredContextCount(ctx);
        const uint32_t workerCount = submitter.IsValid() ? static_cast<uint32_t>(submitter.WorkerCount()) : 0;
        const bool parallel =
            submitter.IsValid() && deferredCount >= workerCount && workerCount > 1 && frame.m_draws.Size() > 1;

        if (!parallel)
        {
            for (size_t i = 0; i < frame.m_draws.Size(); ++i)
            {
                const DrawItem& draw = frame.m_draws[i];
                swarm::DrawMesh(ctx, 0, draw.m_mesh, draw.m_material, draw.m_world, draw.m_submeshIndex);
            }
            return;
        }

        for (uint32_t i = 1; i < workerCount; ++i)
        {
            swarm::PrepareWorkerFrame(ctx, i);
        }

        struct ParallelDrawContext
        {
            swarm::RenderContext* m_renderContext;
            const DrawItem* m_draws;
        };
        ParallelDrawContext parallelContext{ctx, frame.m_draws.Data()};

        auto draw = [](size_t index, void* userData) {
            auto& parallel = *static_cast<ParallelDrawContext*>(userData);
            const size_t workerIndex = drone::WorkerContext::CurrentWorkerIndex();
            const uint32_t deferredIndex =
                workerIndex == drone::WorkerContext::kMainThread ? 0 : static_cast<uint32_t>(workerIndex);
            const DrawItem& item = parallel.m_draws[index];
            swarm::DrawMesh(parallel.m_renderContext, deferredIndex, item.m_mesh, item.m_material, item.m_world,
                            item.m_submeshIndex);
        };

        submitter.ParallelFor(0, frame.m_draws.Size(), draw, &parallelContext, 0);
    }

    void RenderMeshes(queen::World& world, RenderModule& renderModule, float aspect,
                      const drone::JobSubmitter& submitter)
    {
        RenderFrame frame{comb::GetDefaultMemoryResource()};
        BuildRenderFrame(world, renderModule, aspect, frame);
        ExecuteRenderFrame(renderModule, frame, submitter);
    }
} // namespace waggle
