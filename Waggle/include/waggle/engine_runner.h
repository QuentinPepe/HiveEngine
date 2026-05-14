#pragma once

#include <hive/hive_config.h>

#include <drone/job_submitter.h>

#include <waggle/app.h>
#include <waggle/runtime_context.h>

namespace terra
{
    struct WindowContext;
}

namespace swarm
{
    struct RenderContext;
}

namespace waggle
{
    class RenderModule;

    struct EngineConfig
    {
        const char* m_windowTitle{"HiveEngine"};
        uint32_t m_windowWidth{1280};
        uint32_t m_windowHeight{720};
        EngineMode m_mode{EngineMode::GAME};
        bool m_autoTick{true};
        bool m_autoRenderer{true};
        bool m_autoSystems{true};
        bool m_deferWindow{false};
        // When true, Swarm commands run on a dedicated render thread while the game thread
        // prepares frame N+1 in parallel. Off by default until the path is validated against
        // every host (editor Qt loop, headless tools, single-frame benches).
        bool m_useRenderThread{false};
        AppConfig m_app{};
        drone::JobSubmitter m_jobs{};
    };

    struct EngineContext
    {
        waggle::App* m_app{nullptr};
        queen::World* m_world{nullptr};
        terra::WindowContext* m_window{nullptr};
        swarm::RenderContext* m_renderContext{nullptr};
        waggle::RenderModule* m_renderModule{nullptr};
        drone::JobSubmitter m_jobs{};
    };

    struct EngineCallbacks
    {
        using RegisterModulesFn = void (*)();
        using SetupFn = bool (*)(EngineContext& ctx, void* userData);
        using FrameFn = void (*)(EngineContext& ctx, void* userData);
        using ShutdownFn = void (*)(EngineContext& ctx, void* userData);

        RegisterModulesFn m_onRegisterModules{nullptr};
        SetupFn m_onSetup{nullptr};
        // Called at the very start of each frame before terra::PollEvents. Editor hosts
        // (e.g. Forge) pump Qt's event loop here so native input filters write into Terra's
        // InputState before antennae::UpdateInput samples it.
        FrameFn m_onPrePoll{nullptr};
        FrameFn m_onFrame{nullptr};
        ShutdownFn m_onShutdown{nullptr};

        void* m_userData{nullptr};
    };

    HIVE_API int Run(const EngineConfig& config, const EngineCallbacks& callbacks);

    HIVE_API bool CreateWindowAndRenderer(EngineContext& ctx, const char* title, uint32_t width, uint32_t height);
} // namespace waggle
