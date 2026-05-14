#include <waggle/gizmo_state.h>

#include <comb/default_allocator.h>

#include <queen/world/world.h>

namespace waggle
{
    void EnsureGizmoResources(queen::World& world)
    {
        if (world.Resource<SelectionState>() == nullptr)
        {
            world.InsertResource(SelectionState{wax::Vector<queen::Entity>{comb::GetDefaultAllocator()}});
        }
        if (world.Resource<GizmoStateResource>() == nullptr)
        {
            world.InsertResource(GizmoStateResource{});
        }
        if (world.Resource<EditorViewParams>() == nullptr)
        {
            world.InsertResource(EditorViewParams{});
        }
    }
} // namespace waggle
