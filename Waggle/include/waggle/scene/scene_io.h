#pragma once

#include <hive/hive_config.h>

#include <queen/reflect/component_registry.h>

namespace queen
{
    class World;
}

namespace waggle
{
    // Adds the engine component types that .hscene files can contain. Game and
    // editor builds both call this when wiring up a fresh ComponentRegistry —
    // gameplay code can call Register<T>() on top to add project-specific types.
    HIVE_API void RegisterEngineSceneComponentTypes(queen::ComponentRegistry<256>& registry);

    // Serialize the world to a .hscene JSON file. EditorOnly entities are skipped.
    HIVE_API bool SaveScene(queen::World& world,
                            const queen::ComponentRegistry<256>& registry,
                            const char* path);

    // Load a .hscene JSON file into the world. The world is NOT cleared first
    // (additive load). On success, runtime-only Transform companions
    // (WorldMatrix / TransformVersion) are reattached so the systems pick the
    // entities up on the first tick.
    HIVE_API bool LoadScene(queen::World& world,
                            const queen::ComponentRegistry<256>& registry,
                            const char* path);

    // For every entity that has a Transform but is missing WorldMatrix or
    // TransformVersion, add them with values derived from the Transform. Call
    // after any deserialization pass — these components are runtime-only and
    // never persisted.
    HIVE_API void EnsureRuntimeTransformComponents(queen::World& world);
}
