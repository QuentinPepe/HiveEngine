#pragma once

#include <hive/math/types.h>

#include <queen/core/entity.h>

#include <waggle/components/gizmo.h>

#include <forge/forge_module.h>

#include <memory>
#include <vector>

namespace queen
{
    class World;
}

namespace forge
{
    class EditorSelection;
    class EditorUndoManager;

    class GizmoInteractionBridge
    {
    public:
        GizmoInteractionBridge() = default;

        void SetContext(queen::World* world, EditorSelection* selection, EditorUndoManager* undo);

        // Called every tick by the launcher. Reads antennae::Mouse from the world,
        // detects LMB transitions and dispatches press / move / release. Bypasses
        // Qt's nativeEventFilter because foreign GLFW windows deliver mouse events
        // straight to their own WndProc.
        void Tick(float viewportWidth, float viewportHeight);

        [[nodiscard]] bool IsDragging() const noexcept { return m_state.m_active; }

    private:
        struct EntitySnapshot
        {
            queen::Entity m_entity{};
            hive::math::Float3 m_position{};
            hive::math::Quat m_rotation{};
            hive::math::Float3 m_scale{};
        };

        struct DragState
        {
            bool m_active{false};
            waggle::GizmoAxis m_axis{waggle::GizmoAxis::X};
            waggle::GizmoKind m_kind{waggle::GizmoKind::TRANSLATE_AXIS};
            hive::math::Float3 m_pivot{};
            hive::math::Float3 m_axisDirWorld{};
            hive::math::Float3 m_ringInitialDir{};
            float m_initialParam{0.f};
            std::vector<EntitySnapshot> m_snapshots;
        };

        void ApplyTranslateDelta(hive::math::Float3 deltaWorld);
        void ApplyRotateDelta(float angleRad);
        void ApplyScaleDelta(float factor);
        void BumpTransformVersion(queen::Entity entity);

        bool OnMousePress(float mouseX, float mouseY, float viewportWidth, float viewportHeight);
        void OnMouseMove(float mouseX, float mouseY, float viewportWidth, float viewportHeight);
        void OnMouseRelease();
        void UpdateHoverHighlight(float mouseX, float mouseY, float viewportWidth, float viewportHeight);

        DragState m_state{};
        queen::World* m_world{nullptr};
        EditorSelection* m_selection{nullptr};
        EditorUndoManager* m_undo{nullptr};
        bool m_prevLmbDown{false};
    };
} // namespace forge
