#pragma once

#include <hive/hive_config.h>

namespace queen
{
    class World;
} // namespace queen

namespace propolis
{
    // Gate resource for Init/Tick systems. When m_enabled is false both systems
    // early-return, keeping scripts dormant until the host switches to a runtime
    // mode (editor Play, standalone game start, etc.). Defaults to disabled so
    // freshly-loaded editor scenes do not invoke OnAttach until Play.
    struct ScriptExecutionGate
    {
        bool m_enabled{false};
    };

    // Registers Queen systems and observers for script lifecycle dispatch.
    // Must be called after ScriptRegistry and ScriptTime are inserted as World resources.
    // Creates:
    //   - "Propolis.Init"   : allocates state + calls OnAttach for new PropolisScript entities
    //   - "Propolis.Tick"   : calls OnTick each frame for all scripted entities
    //   - OnRemove observer : calls OnDetach + frees state when PropolisScript is removed
    // Limitation: Queen::Despawn bypasses OnRemove — use Remove<PropolisScript> before Despawn.
    using PreTickFn = void (*)(queen::World&);

    HIVE_API void RegisterPropolisExecutor(queen::World& world, PreTickFn preTick = nullptr);

    // Calls OnDetach on every live PropolisScript and frees its state blob,
    // without touching the ScriptRegistry. Use when leaving Play to release
    // runtime state before despawning entities (Queen::Despawn bypasses OnRemove).
    HIVE_API void DetachAllScripts(queen::World& world);

    // Call before unloading the gameplay DLL.
    // Calls OnDetach on all active scripts, frees state blobs, clears the ScriptRegistry.
    // Entity PropolisScript components are kept (m_nameHash preserved) so Init system
    // re-initializes them automatically after the new DLL re-registers its scripts.
    HIVE_API void PrepareScriptReload(queen::World& world);
} // namespace propolis
