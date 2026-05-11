#include <waggle/systems/editor_camera_system.h>

#include <hive/math/functions.h>
#include <hive/math/transforms.h>

#include <antennae/keyboard.h>
#include <antennae/mouse.h>

#include <queen/query/query_term.h>
#include <queen/world/world.h>

#include <terra/input/keys.h>

#include <waggle/components/editor_camera.h>
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
                    if (keyboard->IsDown(terra::Key::A))
                    {
                        move -= right;
                    }
                    if (keyboard->IsDown(terra::Key::SPACE) || keyboard->IsDown(terra::Key::E))
                    {
                        move += up;
                    }
                    if (keyboard->IsDown(terra::Key::LEFT_CONTROL) ||
                        keyboard->IsDown(terra::Key::RIGHT_CONTROL) ||
                        keyboard->IsDown(terra::Key::Q))
                    {
                        move -= up;
                    }

                    const float speed = (keyboard->IsDown(terra::Key::LEFT_SHIFT) ||
                                         keyboard->IsDown(terra::Key::RIGHT_SHIFT))
                                            ? controller.m_moveSpeed * controller.m_boostMultiplier
                                            : controller.m_moveSpeed;
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
} // namespace waggle
