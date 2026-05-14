#pragma once

#include <hive/hive_config.h>

#include <cstdint>

namespace queen
{
    class World;
}

namespace waggle
{
    enum class PlayState : uint8_t
    {
        EDITING,
        PLAYING,
        PAUSED,
    };

    HIVE_API void SetPlayState(queen::World& world, PlayState state);
    HIVE_API PlayState GetPlayState(const queen::World& world) noexcept;
} // namespace waggle
