#include <forge/gizmo_interaction.h>

#include <antennae/mouse.h>

#include <hive/math/functions.h>
#include <hive/math/transforms.h>

#include <queen/query/query_term.h>
#include <queen/world/world.h>

#include <waggle/components/gizmo.h>
#include <waggle/components/transform.h>
#include <waggle/gizmo_state.h>
#include <waggle/math/ray_intersect.h>
#include <waggle/time.h>

#include <forge/editor_undo.h>
#include <forge/selection.h>

#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

namespace forge
{
    namespace
    {
        using hive::math::Float3;
        using hive::math::Quat;

        constexpr float kTranslateSnapMeters = 0.25f;
        constexpr float kRotateSnapRadians = hive::math::Radians(15.f);
        constexpr float kScaleSnapStep = 0.1f;

        bool IsSnapHeld() noexcept
        {
#ifdef _WIN32
            return (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
#else
            return false;
#endif
        }

        float SnapToStep(float value, float step) noexcept
        {
            if (step <= 0.f)
            {
                return value;
            }
            const float ratio = value / step;
            return step * (ratio >= 0.f ? std::floor(ratio + 0.5f) : std::ceil(ratio - 0.5f));
        }

        struct GizmoFrame
        {
            Float3 m_pivot{};
            float m_worldScale{1.f};
            Float3 m_axisDirsWorld[3]{};
            Quat m_orientation{};
            waggle::GizmoMode m_mode{waggle::GizmoMode::TRANSLATE};
            waggle::GizmoSpace m_space{waggle::GizmoSpace::WORLD};
            bool m_valid{false};
        };

        GizmoFrame ResolveGizmoFrame(queen::World& world)
        {
            GizmoFrame frame{};
            world.Query<queen::Read<waggle::GizmoRoot>, queen::Read<waggle::WorldMatrix>,
                        queen::Read<waggle::Transform>>()
                .Each([&](const waggle::GizmoRoot& root, const waggle::WorldMatrix& wm,
                          const waggle::Transform& transform) {
                    if (frame.m_valid)
                    {
                        return;
                    }
                    frame.m_pivot = Float3{wm.m_matrix.m_m[3][0], wm.m_matrix.m_m[3][1], wm.m_matrix.m_m[3][2]};
                    const float worldScale =
                        hive::math::Length(Float3{wm.m_matrix.m_m[0][0], wm.m_matrix.m_m[0][1], wm.m_matrix.m_m[0][2]});
                    frame.m_worldScale = worldScale > hive::math::kEpsilon ? worldScale : 1.f;
                    frame.m_orientation = transform.m_rotation;
                    frame.m_mode = root.m_mode;
                    frame.m_space = root.m_space;
                    frame.m_valid = true;
                });
            if (!frame.m_valid)
            {
                return frame;
            }

            world.Query<queen::Read<waggle::GizmoPart>, queen::Read<waggle::WorldMatrix>>().Each(
                [&](const waggle::GizmoPart& part, const waggle::WorldMatrix& wm) {
                    const Float3 dir{wm.m_matrix.m_m[0][0], wm.m_matrix.m_m[0][1], wm.m_matrix.m_m[0][2]};
                    const uint8_t axisIndex = static_cast<uint8_t>(part.m_axis);
                    if (axisIndex < 3)
                    {
                        frame.m_axisDirsWorld[axisIndex] = hive::math::Normalize(dir);
                    }
                });
            return frame;
        }

        struct PickResult
        {
            bool m_hit{false};
            uint8_t m_axis{0};
            waggle::GizmoKind m_kind{waggle::GizmoKind::TRANSLATE_AXIS};
            float m_t{1e30f};
            Float3 m_hitPointWorld{};
        };

        bool RingHitTest(const waggle::math::Ray& ray, const Float3& pivot, const Float3& axisDirWorld,
                         float ringRadius, float ringTube, float* outT, Float3* outHit)
        {
            float t = 0.f;
            if (!waggle::math::RayVsPlane(ray, pivot, axisDirWorld, &t))
            {
                return false;
            }
            const Float3 hit{ray.m_origin.m_x + ray.m_direction.m_x * t,
                             ray.m_origin.m_y + ray.m_direction.m_y * t,
                             ray.m_origin.m_z + ray.m_direction.m_z * t};
            const float dist = hive::math::Length(hit - pivot);
            if (dist < ringRadius - ringTube || dist > ringRadius + ringTube)
            {
                return false;
            }
            if (outT != nullptr)
            {
                *outT = t;
            }
            if (outHit != nullptr)
            {
                *outHit = hit;
            }
            return true;
        }

        PickResult PickGizmo(const waggle::math::Ray& ray, const GizmoFrame& frame)
        {
            PickResult best{};

            if (frame.m_mode == waggle::GizmoMode::ROTATE)
            {
                const float ringRadius = frame.m_worldScale;
                const float ringTube = frame.m_worldScale * 0.06f;
                for (uint8_t axis = 0; axis < 3; ++axis)
                {
                    float t = 0.f;
                    Float3 hit{};
                    if (!RingHitTest(ray, frame.m_pivot, frame.m_axisDirsWorld[axis], ringRadius, ringTube, &t, &hit))
                    {
                        continue;
                    }
                    if (t < best.m_t)
                    {
                        best.m_hit = true;
                        best.m_axis = axis;
                        best.m_kind = waggle::GizmoKind::ROTATE_RING;
                        best.m_t = t;
                        best.m_hitPointWorld = hit;
                    }
                }
                return best;
            }

            const float length = frame.m_worldScale;
            const float radius = frame.m_worldScale * 0.08f;
            const waggle::GizmoKind kind = frame.m_mode == waggle::GizmoMode::SCALE
                                               ? waggle::GizmoKind::SCALE_AXIS
                                               : waggle::GizmoKind::TRANSLATE_AXIS;
            for (uint8_t axis = 0; axis < 3; ++axis)
            {
                float t = 0.f;
                if (!waggle::math::RayVsCylinder(ray, frame.m_pivot, frame.m_axisDirsWorld[axis], length, radius, &t))
                {
                    continue;
                }
                if (t < best.m_t)
                {
                    best.m_hit = true;
                    best.m_axis = axis;
                    best.m_kind = kind;
                    best.m_t = t;
                    best.m_hitPointWorld =
                        Float3{ray.m_origin.m_x + ray.m_direction.m_x * t,
                               ray.m_origin.m_y + ray.m_direction.m_y * t,
                               ray.m_origin.m_z + ray.m_direction.m_z * t};
                }
            }
            return best;
        }
    } // namespace

    void GizmoInteractionBridge::SetContext(queen::World* world, EditorSelection* selection, EditorUndoManager* undo)
    {
        m_world = world;
        m_selection = selection;
        m_undo = undo;
    }

    void GizmoInteractionBridge::Tick(float viewportWidth, float viewportHeight)
    {
        if (m_world == nullptr)
        {
            return;
        }
        auto* mouse = m_world->Resource<antennae::Mouse>();
        if (mouse == nullptr)
        {
            return;
        }

        const bool lmbDown = mouse->IsDown(terra::MouseButton::LEFT);
        const float mouseX = mouse->m_x;
        const float mouseY = mouse->m_y;

        UpdateHoverHighlight(mouseX, mouseY, viewportWidth, viewportHeight);

        if (lmbDown && !m_prevLmbDown)
        {
            OnMousePress(mouseX, mouseY, viewportWidth, viewportHeight);
        }
        else if (!lmbDown && m_prevLmbDown)
        {
            OnMouseRelease();
        }
        else if (lmbDown && m_state.m_active)
        {
            OnMouseMove(mouseX, mouseY, viewportWidth, viewportHeight);
        }
        m_prevLmbDown = lmbDown;
    }

    bool GizmoInteractionBridge::OnMousePress(float mouseX, float mouseY, float viewportWidth, float viewportHeight)
    {
        if (m_world == nullptr || m_selection == nullptr || m_selection->IsEmpty())
        {
            return false;
        }
        if (viewportWidth <= 1.f || viewportHeight <= 1.f)
        {
            return false;
        }

        const auto* viewParams = m_world->Resource<waggle::EditorViewParams>();
        if (viewParams == nullptr || !viewParams->m_valid)
        {
            return false;
        }

        const auto frame = ResolveGizmoFrame(*m_world);
        if (!frame.m_valid)
        {
            return false;
        }

        const waggle::math::Ray ray = waggle::math::ScreenToRay(
            viewParams->m_view, viewParams->m_proj, viewportWidth, viewportHeight, mouseX, mouseY);

        const PickResult pick = PickGizmo(ray, frame);
        if (!pick.m_hit)
        {
            return false;
        }

        auto* state = m_world->Resource<waggle::GizmoStateResource>();
        if (state != nullptr)
        {
            state->m_isUsing = true;
        }

        m_state.m_active = true;
        m_state.m_axis = static_cast<waggle::GizmoAxis>(pick.m_axis);
        m_state.m_kind = pick.m_kind;
        m_state.m_pivot = frame.m_pivot;
        m_state.m_axisDirWorld = frame.m_axisDirsWorld[pick.m_axis];
        m_state.m_ringInitialDir = Float3{};

        m_state.m_snapshots.clear();
        const auto& entities = m_selection->All();
        m_state.m_snapshots.reserve(entities.Size());
        for (size_t i = 0; i < entities.Size(); ++i)
        {
            const auto entity = entities[i];
            if (!m_world->IsAlive(entity))
            {
                continue;
            }
            const auto* transform = m_world->Get<waggle::Transform>(entity);
            if (transform == nullptr)
            {
                continue;
            }
            EntitySnapshot snap{};
            snap.m_entity = entity;
            snap.m_position = transform->m_position;
            snap.m_rotation = transform->m_rotation;
            snap.m_scale = transform->m_scale;
            m_state.m_snapshots.push_back(snap);
        }

        if (m_state.m_kind == waggle::GizmoKind::ROTATE_RING)
        {
            const Float3 hit = pick.m_hitPointWorld;
            const Float3 raw = hit - m_state.m_pivot;
            const float length = hive::math::Length(raw);
            m_state.m_ringInitialDir = length > hive::math::kEpsilon
                                           ? Float3{raw.m_x / length, raw.m_y / length, raw.m_z / length}
                                           : Float3{1.f, 0.f, 0.f};
            m_state.m_initialParam = 0.f;
        }
        else
        {
            float tRay = 0.f;
            float tLine = 0.f;
            if (waggle::math::ClosestPointsRayLine(ray.m_origin, ray.m_direction, m_state.m_pivot,
                                                   m_state.m_axisDirWorld, &tRay, &tLine))
            {
                m_state.m_initialParam = tLine;
            }
            else
            {
                m_state.m_initialParam = 0.f;
            }
        }
        return true;
    }

    void GizmoInteractionBridge::OnMouseMove(float mouseX, float mouseY, float viewportWidth, float viewportHeight)
    {
        if (!m_state.m_active || m_world == nullptr)
        {
            return;
        }
        if (viewportWidth <= 1.f || viewportHeight <= 1.f)
        {
            return;
        }

        const auto* viewParams = m_world->Resource<waggle::EditorViewParams>();
        if (viewParams == nullptr || !viewParams->m_valid)
        {
            return;
        }

        const waggle::math::Ray ray = waggle::math::ScreenToRay(
            viewParams->m_view, viewParams->m_proj, viewportWidth, viewportHeight, mouseX, mouseY);

        const bool snap = IsSnapHeld();
        if (m_state.m_kind == waggle::GizmoKind::TRANSLATE_AXIS)
        {
            float tRay = 0.f;
            float tLine = 0.f;
            if (!waggle::math::ClosestPointsRayLine(ray.m_origin, ray.m_direction, m_state.m_pivot,
                                                    m_state.m_axisDirWorld, &tRay, &tLine))
            {
                return;
            }
            float deltaParam = tLine - m_state.m_initialParam;
            if (snap)
            {
                deltaParam = SnapToStep(deltaParam, kTranslateSnapMeters);
            }
            const Float3 deltaWorld{m_state.m_axisDirWorld.m_x * deltaParam,
                                    m_state.m_axisDirWorld.m_y * deltaParam,
                                    m_state.m_axisDirWorld.m_z * deltaParam};
            ApplyTranslateDelta(deltaWorld);
        }
        else if (m_state.m_kind == waggle::GizmoKind::SCALE_AXIS)
        {
            float tRay = 0.f;
            float tLine = 0.f;
            if (!waggle::math::ClosestPointsRayLine(ray.m_origin, ray.m_direction, m_state.m_pivot,
                                                    m_state.m_axisDirWorld, &tRay, &tLine))
            {
                return;
            }
            const float initial = m_state.m_initialParam;
            if (initial > -hive::math::kEpsilon && initial < hive::math::kEpsilon)
            {
                return;
            }
            float factor = tLine / initial;
            if (snap)
            {
                factor = SnapToStep(factor, kScaleSnapStep);
            }
            ApplyScaleDelta(factor);
        }
        else if (m_state.m_kind == waggle::GizmoKind::ROTATE_RING)
        {
            float t = 0.f;
            if (!waggle::math::RayVsPlane(ray, m_state.m_pivot, m_state.m_axisDirWorld, &t))
            {
                return;
            }
            const Float3 hit{ray.m_origin.m_x + ray.m_direction.m_x * t,
                             ray.m_origin.m_y + ray.m_direction.m_y * t,
                             ray.m_origin.m_z + ray.m_direction.m_z * t};
            const Float3 raw = hit - m_state.m_pivot;
            const float length = hive::math::Length(raw);
            if (length < hive::math::kEpsilon)
            {
                return;
            }
            const Float3 currentDir{raw.m_x / length, raw.m_y / length, raw.m_z / length};
            const Float3 init = m_state.m_ringInitialDir;
            const Float3 cross = hive::math::Cross(init, currentDir);
            const float sinTheta = hive::math::Dot(cross, m_state.m_axisDirWorld);
            const float cosTheta = hive::math::Dot(init, currentDir);
            float angle = std::atan2(sinTheta, cosTheta);
            if (snap)
            {
                angle = SnapToStep(angle, kRotateSnapRadians);
            }
            ApplyRotateDelta(angle);
        }
    }

    void GizmoInteractionBridge::OnMouseRelease()
    {
        if (!m_state.m_active)
        {
            return;
        }

        if (m_world != nullptr && m_undo != nullptr && !m_state.m_snapshots.empty())
        {
            auto before = std::make_shared<std::vector<EntitySnapshot>>(m_state.m_snapshots);
            auto after = std::make_shared<std::vector<EntitySnapshot>>();
            after->reserve(m_state.m_snapshots.size());

            bool changed = false;
            for (const auto& snap : m_state.m_snapshots)
            {
                EntitySnapshot now{};
                now.m_entity = snap.m_entity;
                if (!m_world->IsAlive(snap.m_entity))
                {
                    after->push_back(snap);
                    continue;
                }
                const auto* transform = m_world->Get<waggle::Transform>(snap.m_entity);
                if (transform == nullptr)
                {
                    after->push_back(snap);
                    continue;
                }
                now.m_position = transform->m_position;
                now.m_rotation = transform->m_rotation;
                now.m_scale = transform->m_scale;
                after->push_back(now);
                const float driftSum =
                    std::abs(now.m_position.m_x - snap.m_position.m_x) +
                    std::abs(now.m_position.m_y - snap.m_position.m_y) +
                    std::abs(now.m_position.m_z - snap.m_position.m_z) +
                    std::abs(now.m_scale.m_x - snap.m_scale.m_x) +
                    std::abs(now.m_scale.m_y - snap.m_scale.m_y) +
                    std::abs(now.m_scale.m_z - snap.m_scale.m_z) +
                    std::abs(now.m_rotation.m_x - snap.m_rotation.m_x) +
                    std::abs(now.m_rotation.m_y - snap.m_rotation.m_y) +
                    std::abs(now.m_rotation.m_z - snap.m_rotation.m_z) +
                    std::abs(now.m_rotation.m_w - snap.m_rotation.m_w);
                if (driftSum > 1e-5f)
                {
                    changed = true;
                }
            }

            if (changed)
            {
                queen::World* world = m_world;
                auto restore = [world](const std::vector<EntitySnapshot>& snaps) {
                    for (const auto& snap : snaps)
                    {
                        if (!world->IsAlive(snap.m_entity))
                        {
                            continue;
                        }
                        auto* transform = world->Get<waggle::Transform>(snap.m_entity);
                        if (transform == nullptr)
                        {
                            continue;
                        }
                        transform->m_position = snap.m_position;
                        transform->m_rotation = snap.m_rotation;
                        transform->m_scale = snap.m_scale;
                        auto* version = world->Get<waggle::TransformVersion>(snap.m_entity);
                        if (version != nullptr)
                        {
                            auto* time = world->Resource<waggle::Time>();
                            if (time != nullptr)
                            {
                                version->m_lastModified = time->m_tick + 1;
                            }
                            else
                            {
                                ++version->m_lastModified;
                            }
                        }
                    }
                };
                m_undo->Push([before, restore] { restore(*before); },
                             [after, restore] { restore(*after); });
            }
        }

        if (m_world != nullptr)
        {
            auto* state = m_world->Resource<waggle::GizmoStateResource>();
            if (state != nullptr)
            {
                state->m_isUsing = false;
            }
        }
        m_state.m_active = false;
        m_state.m_snapshots.clear();
    }

    void GizmoInteractionBridge::UpdateHoverHighlight(float mouseX, float mouseY, float viewportWidth,
                                                      float viewportHeight)
    {
        if (m_world == nullptr)
        {
            return;
        }
        if (viewportWidth <= 1.f || viewportHeight <= 1.f)
        {
            return;
        }

        bool hotAxes[3]{false, false, false};
        waggle::GizmoKind hotKind = waggle::GizmoKind::TRANSLATE_AXIS;
        if (m_state.m_active)
        {
            const uint8_t axisIndex = static_cast<uint8_t>(m_state.m_axis);
            if (axisIndex < 3)
            {
                hotAxes[axisIndex] = true;
            }
            hotKind = m_state.m_kind;
        }
        else
        {
            const auto* viewParams = m_world->Resource<waggle::EditorViewParams>();
            if (viewParams != nullptr && viewParams->m_valid)
            {
                const auto frame = ResolveGizmoFrame(*m_world);
                if (frame.m_valid)
                {
                    const waggle::math::Ray ray = waggle::math::ScreenToRay(
                        viewParams->m_view, viewParams->m_proj, viewportWidth, viewportHeight, mouseX, mouseY);
                    const PickResult pick = PickGizmo(ray, frame);
                    if (pick.m_hit && pick.m_axis < 3)
                    {
                        hotAxes[pick.m_axis] = true;
                        hotKind = pick.m_kind;
                    }
                }
            }
        }

        m_world->Query<queen::Write<waggle::GizmoPart>>().Each([&](waggle::GizmoPart& part) {
            const uint8_t axisIndex = static_cast<uint8_t>(part.m_axis);
            part.m_hot = (axisIndex < 3) && hotAxes[axisIndex] && part.m_kind == hotKind;
        });
    }

    void GizmoInteractionBridge::ApplyTranslateDelta(Float3 deltaWorld)
    {
        if (m_world == nullptr)
        {
            return;
        }
        for (const auto& snap : m_state.m_snapshots)
        {
            if (!m_world->IsAlive(snap.m_entity))
            {
                continue;
            }
            auto* transform = m_world->Get<waggle::Transform>(snap.m_entity);
            if (transform == nullptr)
            {
                continue;
            }
            transform->m_position = Float3{snap.m_position.m_x + deltaWorld.m_x,
                                           snap.m_position.m_y + deltaWorld.m_y,
                                           snap.m_position.m_z + deltaWorld.m_z};
            BumpTransformVersion(snap.m_entity);
        }
    }

    void GizmoInteractionBridge::ApplyRotateDelta(float angleRad)
    {
        if (m_world == nullptr)
        {
            return;
        }
        const Quat delta = hive::math::QuatFromAxisAngle(m_state.m_axisDirWorld, angleRad);
        for (const auto& snap : m_state.m_snapshots)
        {
            if (!m_world->IsAlive(snap.m_entity))
            {
                continue;
            }
            auto* transform = m_world->Get<waggle::Transform>(snap.m_entity);
            if (transform == nullptr)
            {
                continue;
            }
            // Rotate the entity's position offset from the pivot so multi-select rotates
            // rigidly around the centroid (single-select has offset == 0, so position is preserved).
            transform->m_rotation = hive::math::Normalize(delta * snap.m_rotation);
            const Float3 offset = snap.m_position - m_state.m_pivot;
            const Float3 rotated = hive::math::RotateByQuat(offset, delta);
            transform->m_position = m_state.m_pivot + rotated;
            BumpTransformVersion(snap.m_entity);
        }
    }

    void GizmoInteractionBridge::ApplyScaleDelta(float factor)
    {
        if (m_world == nullptr)
        {
            return;
        }
        const uint8_t axis = static_cast<uint8_t>(m_state.m_axis);
        for (const auto& snap : m_state.m_snapshots)
        {
            if (!m_world->IsAlive(snap.m_entity))
            {
                continue;
            }
            auto* transform = m_world->Get<waggle::Transform>(snap.m_entity);
            if (transform == nullptr)
            {
                continue;
            }
            Float3 newScale = snap.m_scale;
            switch (axis)
            {
                case 0: newScale.m_x = snap.m_scale.m_x * factor; break;
                case 1: newScale.m_y = snap.m_scale.m_y * factor; break;
                case 2: newScale.m_z = snap.m_scale.m_z * factor; break;
                default: break;
            }
            transform->m_scale = newScale;
            BumpTransformVersion(snap.m_entity);
        }
    }

    void GizmoInteractionBridge::BumpTransformVersion(queen::Entity entity)
    {
        if (m_world == nullptr)
        {
            return;
        }
        auto* version = m_world->Get<waggle::TransformVersion>(entity);
        if (version == nullptr)
        {
            return;
        }
        auto* time = m_world->Resource<waggle::Time>();
        if (time != nullptr)
        {
            version->m_lastModified = time->m_tick + 1;
        }
        else
        {
            ++version->m_lastModified;
        }
    }
} // namespace forge
