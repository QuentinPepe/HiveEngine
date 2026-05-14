#pragma once

#include <waggle/engine_runner.h>

#include <brood/launcher/launcher_types.h>
#include <cstdint>
#include <filesystem>

namespace brood::launcher
{

    using BootProgressFn = void (*)(const char* step, uint32_t current, uint32_t total, void* userdata);

    void TryLoadGameplayModule(waggle::EngineContext& ctx, LauncherState& state);
    bool OpenProject(waggle::EngineContext& ctx, LauncherState& state, const std::filesystem::path& requestedPath,
                     BootProgressFn progress = nullptr, void* progressUd = nullptr);

#if HIVE_MODE_EDITOR
    bool CreateProjectFromHub(waggle::EngineContext& ctx, LauncherState& state, BootProgressFn progress = nullptr,
                              void* progressUd = nullptr);
#endif

} // namespace brood::launcher
