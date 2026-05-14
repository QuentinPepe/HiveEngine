#pragma once

#include <hive/hive_config.h>

#if HIVE_MODE_EDITOR

#include <filesystem>

namespace queen
{
    class World;
}

namespace brood::launcher
{
    struct LauncherState;

    std::filesystem::path GetEditorCameraStatePath(const LauncherState& state,
                                                   const std::filesystem::path& scenePath);

    bool SaveEditorCameraState(queen::World& world, const LauncherState& state,
                               const std::filesystem::path& scenePath);

    bool LoadEditorCameraState(queen::World& world, const LauncherState& state,
                               const std::filesystem::path& scenePath);

    // Move the persisted state to follow a scene rename, or remove it when the scene is deleted.
    void RenameEditorCameraState(const LauncherState& state, const std::filesystem::path& oldScenePath,
                                 const std::filesystem::path& newScenePath);
    void DeleteEditorCameraState(const LauncherState& state, const std::filesystem::path& scenePath);
} // namespace brood::launcher

#endif
