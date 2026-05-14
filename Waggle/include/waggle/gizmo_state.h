#pragma once

#include <hive/hive_config.h>
#include <hive/math/types.h>

#include <wax/containers/vector.h>

#include <queen/core/entity.h>

#include <waggle/components/gizmo.h>

namespace queen
{
    class World;
}

namespace waggle
{
    struct SelectionState
    {
        wax::Vector<queen::Entity> m_entities;
    };

    struct GizmoStateResource
    {
        GizmoMode m_mode{GizmoMode::TRANSLATE};
        GizmoSpace m_space{GizmoSpace::WORLD};
        bool m_isUsing{false};
    };

    // Mirror of the last RenderFrame view/proj on the game thread so the editor
    // can run CPU picking (screen-to-ray) without touching the render pipeline.
    struct EditorViewParams
    {
        hive::math::Mat4 m_view{hive::math::Mat4::Identity()};
        hive::math::Mat4 m_proj{hive::math::Mat4::Identity()};
        hive::math::Float3 m_eyeWorld{};
        bool m_valid{false};
    };

    HIVE_API void EnsureGizmoResources(queen::World& world);
} // namespace waggle
