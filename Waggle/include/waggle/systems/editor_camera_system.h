#pragma once

#include <hive/hive_config.h>

namespace queen
{
    class World;
}

namespace waggle
{
    HIVE_API void UpdateEditorCameras(queen::World& world);
    HIVE_API void UpdateEditorGrid(queen::World& world);
} // namespace waggle
