#pragma once

#include <hive/hive_config.h>

#include <hive/math/types.h>

namespace queen
{
    class World;
}

namespace waggle
{
    // Transient debug freecam. When m_active is true, the renderer uses the
    // stored view/projection in place of the active gameplay camera. Toggled
    // by F1 (consumed in UpdateDebugFreeCam). Compile-out via
    // HIVE_FEATURE_DEBUG_FREECAM=0 strips the system body and the resource
    // never becomes active.
    struct DebugFreeCam
    {
        bool m_active{false};
        hive::math::Float3 m_position{0.f, 1.f, 5.f};
        float m_yawRad{0.f};
        float m_pitchRad{0.f};
        float m_moveSpeed{1.f};
        float m_boostMultiplier{3.f};
        float m_lookSensitivity{0.003f};
        float m_fovRad{1.0472f};
        float m_zNear{0.1f};
        float m_zFar{1000.f};
        hive::math::Mat4 m_worldMatrix{hive::math::Mat4::Identity()};
        bool m_toggleEdgeLatch{false};
    };

    HIVE_API void RegisterDebugFreeCam(queen::World& world);
    HIVE_API void UpdateDebugFreeCam(queen::World& world);
}
