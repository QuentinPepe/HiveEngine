#pragma once

#include <queen/core/entity.h>
#include <queen/reflect/component_reflector.h>

#include <cstdint>

namespace waggle
{
    enum class GizmoMode : uint8_t
    {
        TRANSLATE,
        ROTATE,
        SCALE,
    };

    enum class GizmoSpace : uint8_t
    {
        WORLD,
        LOCAL,
    };

    enum class GizmoAxis : uint8_t
    {
        X = 0,
        Y = 1,
        Z = 2,
    };

    enum class GizmoKind : uint8_t
    {
        TRANSLATE_AXIS,
        ROTATE_RING,
        SCALE_AXIS,
    };

    struct GizmoRoot
    {
        queen::Entity m_target{};
        GizmoMode m_mode{GizmoMode::TRANSLATE};
        GizmoSpace m_space{GizmoSpace::WORLD};

        static void Reflect(queen::ComponentReflector<>&) {}
    };

    struct GizmoPart
    {
        GizmoAxis m_axis{GizmoAxis::X};
        GizmoKind m_kind{GizmoKind::TRANSLATE_AXIS};
        bool m_hot{false};

        static void Reflect(queen::ComponentReflector<>&) {}
    };
} // namespace waggle
