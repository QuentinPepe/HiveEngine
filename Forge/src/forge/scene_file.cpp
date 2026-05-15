#include <forge/scene_file.h>

#include <waggle/scene/scene_io.h>

namespace forge
{
    bool SaveScene(queen::World& world, const queen::ComponentRegistry<256>& registry, const char* path)
    {
        return waggle::SaveScene(world, registry, path);
    }

    bool LoadScene(queen::World& world, const queen::ComponentRegistry<256>& registry, const char* path)
    {
        return waggle::LoadScene(world, registry, path);
    }

    void EnsureRuntimeTransformComponents(queen::World& world)
    {
        waggle::EnsureRuntimeTransformComponents(world);
    }
}
