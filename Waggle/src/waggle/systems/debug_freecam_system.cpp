#include <waggle/systems/debug_freecam_system.h>

#include <hive/math/functions.h>
#include <hive/math/transforms.h>

#include <antennae/keyboard.h>
#include <antennae/mouse.h>

#include <queen/world/world.h>

#include <terra/input/keys.h>

#include <waggle/time.h>

namespace waggle
{
    namespace
    {
#if HIVE_FEATURE_DEBUG_FREECAM
        constexpr float kPitchLimit = 1.5f;
        constexpr float kMinSpeed = 0.001f;
        constexpr float kMaxSpeed = 2.f;
        constexpr float kBaseSpeed = 6.f;

        [[nodiscard]] hive::math::Quat YawPitchToQuat(float yawRadians, float pitchRadians)
        {
            const hive::math::Quat yaw =
                hive::math::QuatFromAxisAngle(hive::math::Float3{0.f, 1.f, 0.f}, yawRadians);
            const hive::math::Quat pitch =
                hive::math::QuatFromAxisAngle(hive::math::Float3{1.f, 0.f, 0.f}, pitchRadians);
            return yaw * pitch;
        }

        void UpdateMatrix(DebugFreeCam& cam)
        {
            const hive::math::Quat rotation = YawPitchToQuat(cam.m_yawRad, cam.m_pitchRad);
            cam.m_worldMatrix = hive::math::TRS(cam.m_position, rotation, hive::math::Float3{1.f, 1.f, 1.f});
        }
#endif
    }

    void RegisterDebugFreeCam(queen::World& world)
    {
#if HIVE_FEATURE_DEBUG_FREECAM
        if (world.Resource<DebugFreeCam>() == nullptr)
        {
            DebugFreeCam cam{};
            UpdateMatrix(cam);
            world.InsertResource(cam);
        }
#else
        (void)world;
#endif
    }

    void UpdateDebugFreeCam(queen::World& world)
    {
#if HIVE_FEATURE_DEBUG_FREECAM
        auto* cam = world.Resource<DebugFreeCam>();
        auto* keyboard = world.Resource<antennae::Keyboard>();
        auto* mouse = world.Resource<antennae::Mouse>();
        const auto* time = world.Resource<Time>();
        if (cam == nullptr || keyboard == nullptr || mouse == nullptr || time == nullptr)
        {
            return;
        }

        const bool f1Down = keyboard->IsDown(terra::Key::F1);
        if (f1Down && !cam->m_toggleEdgeLatch)
        {
            cam->m_active = !cam->m_active;
        }
        cam->m_toggleEdgeLatch = f1Down;

        if (!cam->m_active)
        {
            return;
        }

        const float deltaTime = time->m_dt;
        if (deltaTime <= 0.f)
        {
            return;
        }

        const bool rotating = mouse->IsDown(terra::MouseButton::RIGHT);
        if (rotating && (mouse->m_dx != 0.f || mouse->m_dy != 0.f))
        {
            cam->m_yawRad -= mouse->m_dx * cam->m_lookSensitivity;
            cam->m_pitchRad = hive::math::Clamp(-kPitchLimit,
                                                cam->m_pitchRad - mouse->m_dy * cam->m_lookSensitivity,
                                                kPitchLimit);
        }

        if (mouse->m_scrollY != 0.f)
        {
            const float factor = mouse->m_scrollY > 0.f ? 1.25f : 0.8f;
            cam->m_moveSpeed = hive::math::Clamp(kMinSpeed, cam->m_moveSpeed * factor, kMaxSpeed);
        }

        const float yawSin = hive::math::Sin(cam->m_yawRad);
        const float yawCos = hive::math::Cos(cam->m_yawRad);
        const float pitchSin = hive::math::Sin(cam->m_pitchRad);
        const float pitchCos = hive::math::Cos(cam->m_pitchRad);
        const hive::math::Float3 forward{-yawSin * pitchCos, pitchSin, -yawCos * pitchCos};
        const hive::math::Float3 right{yawCos, 0.f, -yawSin};
        const hive::math::Float3 up{0.f, 1.f, 0.f};

        hive::math::Float3 move{0.f, 0.f, 0.f};
        if (keyboard->IsDown(terra::Key::W) || keyboard->IsDown(terra::Key::Z))
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
        if (keyboard->IsDown(terra::Key::LEFT_CONTROL) || keyboard->IsDown(terra::Key::RIGHT_CONTROL)
            || keyboard->IsDown(terra::Key::A))
        {
            move -= up;
        }

        const float distanceFactor = hive::math::Max(1.f, hive::math::Abs(cam->m_position.m_y) / 20.f);
        const float baseSpeed = kBaseSpeed * cam->m_moveSpeed * distanceFactor;
        const float speed = (keyboard->IsDown(terra::Key::LEFT_SHIFT)
                             || keyboard->IsDown(terra::Key::RIGHT_SHIFT))
                                ? baseSpeed * cam->m_boostMultiplier
                                : baseSpeed;
        const float lengthSquared = move.m_x * move.m_x + move.m_y * move.m_y + move.m_z * move.m_z;
        if (lengthSquared > 0.0001f)
        {
            const float scale = speed * deltaTime / hive::math::Sqrt(lengthSquared);
            cam->m_position += move * scale;
        }

        UpdateMatrix(*cam);

        // Consume input so the gameplay camera or editor camera doesn't double-apply
        // the same frame's mouse delta / scroll.
        if (rotating)
        {
            mouse->m_dx = 0.f;
            mouse->m_dy = 0.f;
        }
        mouse->m_scrollY = 0.f;
#else
        (void)world;
#endif
    }
}
