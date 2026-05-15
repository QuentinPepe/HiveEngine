#include <launcher/launcher_scene.h>

#include <waggle/scene/scene_io.h>

namespace brood::launcher
{
    void RegisterSceneComponentTypes(LauncherState& state)
    {
        waggle::RegisterEngineSceneComponentTypes(state.m_componentRegistry);
    }
}
