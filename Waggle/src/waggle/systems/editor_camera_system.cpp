#include <waggle/systems/editor_camera_system.h>

#include <hive/math/functions.h>
#include <hive/math/transforms.h>

#include <antennae/keyboard.h>
#include <antennae/mouse.h>

#include <queen/query/query_term.h>
#include <queen/world/world.h>

#include <terra/input/keys.h>

#include <waggle/components/editor_camera.h>
#include <waggle/components/editor_grid.h>
#include <waggle/components/editor_only.h>
#include <waggle/components/transform.h>
#include <waggle/time.h>

namespace waggle
{
    namespace
    {
        [[nodiscard]] bool IsForwardDown(const antennae::Keyboard& keyboard)
        {
            return keyboard.IsDown(terra::Key::W) || keyboard.IsDown(terra::Key::Z);
        }

        [[nodiscard]] hive::math::Quat YawPitchToQuat(float yawRadians, float pitchRadians)
        {
            const hive::math::Quat yaw =
                hive::math::QuatFromAxisAngle(hive::math::Float3{0.f, 1.f, 0.f}, yawRadians);
            const hive::math::Quat pitch =
                hive::math::QuatFromAxisAngle(hive::math::Float3{1.f, 0.f, 0.f}, pitchRadians);
            return yaw * pitch;
        }
    } // namespace

    void UpdateEditorCameras(queen::World& world)
    {
        auto* keyboard = world.Resource<antennae::Keyboard>();
        auto* mouse = world.Resource<antennae::Mouse>();
        const auto* time = world.Resource<Time>();
        if (keyboard == nullptr || mouse == nullptr || time == nullptr)
        {
            return;
        }

        const float deltaTime = time->m_dt;
        if (deltaTime <= 0.f)
        {
            return;
        }

        const bool rotating = mouse->IsDown(terra::MouseButton::RIGHT);

        world.Query<queen::Write<Transform>, queen::Write<EditorCameraController>,
                    queen::Write<TransformVersion>, queen::Write<WorldMatrix>>()
            .Each([&](Transform& transform, EditorCameraController& controller,
                      TransformVersion& version, WorldMatrix& worldMatrix) {
                bool dirty = false;

                if (rotating && (mouse->m_dx != 0.f || mouse->m_dy != 0.f))
                {
                    controller.m_yawRad -= mouse->m_dx * controller.m_lookSensitivity;
                    controller.m_pitchRad =
                        hive::math::Clamp(-kEditorCameraPitchLimitRad,
                                          controller.m_pitchRad - mouse->m_dy * controller.m_lookSensitivity,
                                          kEditorCameraPitchLimitRad);
                    dirty = true;
                }

                if (mouse->m_scrollY != 0.f)
                {
                    const float factor = mouse->m_scrollY > 0.f ? 1.25f : 0.8f;
                    controller.m_moveSpeed =
                        hive::math::Clamp(kEditorCameraMinSpeed,
                                          controller.m_moveSpeed * factor,
                                          kEditorCameraMaxSpeed);
                }

                if (rotating)
                {
                    const float yawSin = hive::math::Sin(controller.m_yawRad);
                    const float yawCos = hive::math::Cos(controller.m_yawRad);
                    const float pitchSin = hive::math::Sin(controller.m_pitchRad);
                    const float pitchCos = hive::math::Cos(controller.m_pitchRad);
                    const hive::math::Float3 forward{-yawSin * pitchCos, pitchSin, -yawCos * pitchCos};
                    const hive::math::Float3 right{yawCos, 0.f, -yawSin};
                    const hive::math::Float3 up{0.f, 1.f, 0.f};

                    hive::math::Float3 move{0.f, 0.f, 0.f};
                    if (IsForwardDown(*keyboard))
                    {
                        move += forward;
                    }
                    if (keyboard->IsDown(terra::Key::S))
                    {
                        move -= forward;
                    }
                    if (keyboard->IsDown(terra::Key::D))
                    {
                        move += right;
                    }
                    if (keyboard->IsDown(terra::Key::Q))
                    {
                        move -= right;
                    }
                    if (keyboard->IsDown(terra::Key::SPACE) || keyboard->IsDown(terra::Key::E))
                    {
                        move += up;
                    }
                    if (keyboard->IsDown(terra::Key::LEFT_CONTROL) ||
                        keyboard->IsDown(terra::Key::RIGHT_CONTROL) ||
                        keyboard->IsDown(terra::Key::A))
                    {
                        move -= up;
                    }

                    const float distanceFactor =
                        hive::math::Max(1.f, hive::math::Abs(transform.m_position.m_y) / 20.f);
                    const float baseSpeed =
                        kEditorCameraBaseSpeed * controller.m_moveSpeed * distanceFactor;
                    const float speed = (keyboard->IsDown(terra::Key::LEFT_SHIFT) ||
                                         keyboard->IsDown(terra::Key::RIGHT_SHIFT))
                                            ? baseSpeed * controller.m_boostMultiplier
                                            : baseSpeed;
                    const float lengthSquared =
                        move.m_x * move.m_x + move.m_y * move.m_y + move.m_z * move.m_z;
                    if (lengthSquared > 0.0001f)
                    {
                        const float scale = speed * deltaTime / hive::math::Sqrt(lengthSquared);
                        transform.m_position += move * scale;
                        dirty = true;
                    }
                }

                if (dirty)
                {
                    transform.m_rotation = YawPitchToQuat(controller.m_yawRad, controller.m_pitchRad);
                    worldMatrix.m_matrix = hive::math::TRS(transform.m_position,
                                                           transform.m_rotation,
                                                           transform.m_scale);
                    version.m_lastModified = time->m_tick;
                }
            });

        // Consume the deltas so other systems don't double-apply them.
        mouse->m_dx = 0.f;
        mouse->m_dy = 0.f;
        mouse->m_scrollY = 0.f;
    }

    void UpdateEditorGrid(queen::World& world)
    {
        hive::math::Float3 camPos{0.f, 1.f, 0.f};
        bool found = false;
        world.Query<queen::Read<WorldMatrix>, queen::Read<EditorCameraController>, queen::Read<EditorOnly>>().Each(
            [&](const WorldMatrix& wm, const EditorCameraController&, const EditorOnly&) {
                if (!found)
                {
                    camPos = hive::math::Float3{wm.m_matrix.m_m[3][0], wm.m_matrix.m_m[3][1], wm.m_matrix.m_m[3][2]};
                    found = true;
                }
            });
        if (!found)
        {
            return;
        }

        const float refDist = hive::math::Max(hive::math::Abs(camPos.m_y), 1.f);
        const float span = refDist * 2000.f;
        const hive::math::Float3 gridPos{camPos.m_x, 0.f, camPos.m_z};
        const hive::math::Float3 gridScale{span, 1.f, span};
        const hive::math::Mat4 gridWorld = hive::math::TRS(gridPos, hive::math::Quat{}, gridScale);

        world.Query<queen::Write<Transform>, queen::Write<WorldMatrix>, queen::Read<EditorGrid>>().Each(
            [&](Transform& transform, WorldMatrix& worldMatrix, const EditorGrid&) {
                transform.m_position = gridPos;
                transform.m_scale = gridScale;
                worldMatrix.m_matrix = gridWorld;
            });
    }
} // namespace waggle
