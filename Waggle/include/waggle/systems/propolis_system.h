#pragma once

#include <hive/hive_config.h>

namespace queen
{
    class World;
}

namespace waggle
{
    // Inserts ScriptRegistry + ScriptTime resources and registers:
    //   - Propolis.Init / Propolis.Tick / OnRemove observer (via RegisterPropolisExecutor)
    //   - dt sync from waggle::Time via preTick callback (no separate system)
    // Called from RegisterEngineSystems(). Safe to call after ClearSystems().
    HIVE_API void RegisterPropolisSystem(queen::World& world);

    // Safe teardown before DLL hot-reload: detaches all scripts, frees state, clears registry.
    HIVE_API void PreparePropolisReload(queen::World& world);
} // namespace waggle
