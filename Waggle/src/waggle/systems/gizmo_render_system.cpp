#include <waggle/systems/gizmo_render_system.h>

#include <comb/default_allocator.h>

#include <hive/math/functions.h>
#include <hive/math/transforms.h>

#include <queen/query/query_term.h>
#include <queen/world/world.h>

#include <waggle/components/camera.h>
#include <waggle/components/editor_camera.h>
#include <waggle/components/editor_only.h>
#include <waggle/components/gizmo.h>
#include <waggle/components/name.h>
#include <waggle/components/transform.h>
#include <waggle/gizmo_state.h>
#include <waggle/play_state.h>

#include <wax/containers/vector.h>

namespace waggle
{
    namespace
    {
        GizmoKind KindForMode(GizmoMode mode) noexcept
        {
            switch (mode)
            {
                case GizmoMode::ROTATE: return GizmoKind::ROTATE_RING;
                case GizmoMode::SCALE: return GizmoKind::SCALE_AXIS;
                case GizmoMode::TRANSLATE:
                default: return GizmoKind::TRANSLATE_AXIS;
            }
        }

        bool ResolveEditorCamera(queen::World& world, hive::math::Float3* outPosition,
                                 const Camera** outCamera)
        {
            bool found = false;
            world.Query<queen::Read<WorldMatrix>, queen::Read<Camera>, queen::Read<EditorOnly>>().Each(
                [&](const WorldMatrix& wm, const Camera& cam, const EditorOnly&) {
                    if (!found)
                    {
                        *outPosition =
                            hive::math::Float3{wm.m_matrix.m_m[3][0], wm.m_matrix.m_m[3][1], wm.m_matrix.m_m[3][2]};
                        *outCamera = &cam;
                        found = true;
                    }
                });
            return found;
        }

        float ComputeScreenConstantScale(const hive::math::Float3& pivot,
                                         const hive::math::Float3& camPos, float fovRad)
        {
            const hive::math::Float3 d = pivot - camPos;
            const float dist =
                hive::math::Sqrt(d.m_x * d.m_x + d.m_y * d.m_y + d.m_z * d.m_z);
            const float halfHeight = dist * hive::math::Tan(fovRad * 0.5f);
            constexpr float kScreenFraction = 0.27f;
            return hive::math::Max(0.001f, halfHeight * kScreenFraction);
        }
    } // namespace

    void UpdateGizmoVisual(queen::World& world)
    {
        wax::Vector<queen::Entity> toDespawn{comb::GetDefaultAllocator()};
        world.Query<queen::Read<GizmoPart>>().EachWithEntity(
            [&](queen::Entity entity, const GizmoPart&) { toDespawn.PushBack(entity); });
        world.Query<queen::Read<GizmoRoot>>().EachWithEntity(
            [&](queen::Entity entity, const GizmoRoot&) { toDespawn.PushBack(entity); });
        for (queen::Entity entity : toDespawn)
        {
            if (world.IsAlive(entity))
                world.Despawn(entity);
        }

        if (GetPlayState(world) == PlayState::PLAYING)
            return;

        const auto* selection = world.Resource<SelectionState>();
        const auto* gizmoState = world.Resource<GizmoStateResource>();
        if (selection == nullptr || gizmoState == nullptr || selection->m_entities.IsEmpty())
            return;

        hive::math::Float3 pivot{};
        uint32_t pivotCount = 0;
        for (size_t i = 0; i < selection->m_entities.Size(); ++i)
        {
            const auto entity = selection->m_entities[i];
            if (!world.IsAlive(entity))
                continue;
            const auto* wm = world.Get<WorldMatrix>(entity);
            if (wm == nullptr)
                continue;
            pivot.m_x += wm->m_matrix.m_m[3][0];
            pivot.m_y += wm->m_matrix.m_m[3][1];
            pivot.m_z += wm->m_matrix.m_m[3][2];
            ++pivotCount;
        }
        if (pivotCount == 0)
            return;
        const float inv = 1.f / static_cast<float>(pivotCount);
        pivot.m_x *= inv;
        pivot.m_y *= inv;
        pivot.m_z *= inv;

        hive::math::Float3 camPos{0.f, 0.f, 0.f};
        const Camera* camera = nullptr;
        if (!ResolveEditorCamera(world, &camPos, &camera))
            return;

        const float scale = ComputeScreenConstantScale(pivot, camPos, camera->m_fovRad);

        hive::math::Quat orientation{};
        if (gizmoState->m_space == GizmoSpace::LOCAL)
        {
            const auto primary = selection->m_entities[0];
            const auto* transform = world.IsAlive(primary) ? world.Get<Transform>(primary) : nullptr;
            if (transform != nullptr)
                orientation = transform->m_rotation;
        }

        const hive::math::Float3 scaleVec{scale, scale, scale};
        const hive::math::Mat4 rootMatrix = hive::math::TRS(pivot, orientation, scaleVec);

        const queen::Entity rootEntity = world.Spawn(Name{wax::FixedString{"Editor Gizmo"}},
                                                     GizmoRoot{queen::Entity{},
                                                               gizmoState->m_mode,
                                                               gizmoState->m_space},
                                                     Transform{pivot, orientation, scaleVec},
                                                     WorldMatrix{rootMatrix},
                                                     TransformVersion{},
                                                     EditorOnly{});
        (void)rootEntity;

        const GizmoKind kind = KindForMode(gizmoState->m_mode);
        const hive::math::Quat perAxisRot[3] = {
            hive::math::Quat{0.f, 0.f, 0.f, 1.f},
            hive::math::QuatFromAxisAngle(hive::math::Float3{0.f, 0.f, 1.f}, hive::math::kHalfPi),
            hive::math::QuatFromAxisAngle(hive::math::Float3{0.f, 1.f, 0.f}, -hive::math::kHalfPi),
        };
        for (uint8_t axis = 0; axis < 3; ++axis)
        {
            const hive::math::Quat partRot = orientation * perAxisRot[axis];
            const hive::math::Mat4 partMatrix = hive::math::TRS(pivot, partRot, scaleVec);
            (void)world.Spawn(Name{wax::FixedString{"Gizmo Part"}},
                              GizmoPart{static_cast<GizmoAxis>(axis), kind, false},
                              Transform{pivot, partRot, scaleVec},
                              WorldMatrix{partMatrix},
                              TransformVersion{},
                              EditorOnly{});
        }
    }
} // namespace waggle
