#pragma once

#include <hive/math/types.h>

#include <queen/reflect/component_reflector.h>
#include <queen/reflect/field_attributes.h>

namespace waggle
{
    inline constexpr float kEditorCameraPitchLimitRad = 1.5f;
    inline constexpr float kEditorCameraMinSpeed = 0.1f;
    inline constexpr float kEditorCameraMaxSpeed = 5000.f;

    struct EditorCameraController
    {
        float m_moveSpeed{5.f};
        float m_boostMultiplier{3.f};
        float m_lookSensitivity{0.003f}; // radians per pixel
        float m_yawRad{0.f};
        float m_pitchRad{0.f};

        static void Reflect(queen::ComponentReflector<>& reflector)
        {
            reflector.Field("move_speed", &EditorCameraController::m_moveSpeed)
                .Range(kEditorCameraMinSpeed, kEditorCameraMaxSpeed);
            reflector.Field("boost_multiplier", &EditorCameraController::m_boostMultiplier).Range(1.f, 20.f);
            reflector.Field("look_sensitivity", &EditorCameraController::m_lookSensitivity).Range(0.0001f, 0.05f);
            reflector.Field("yaw", &EditorCameraController::m_yawRad).Flag(queen::FieldFlag::ANGLE);
            reflector.Field("pitch", &EditorCameraController::m_pitchRad).Flag(queen::FieldFlag::ANGLE);
        }
    };
} // namespace waggle
