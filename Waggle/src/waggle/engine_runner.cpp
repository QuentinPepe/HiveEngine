#include <hive/core/log.h>
#include <hive/core/moduleregistry.h>
#include <hive/profiling/profiler.h>

#include <comb/default_allocator.h>
#include <comb/new.h>

#include <waggle/engine_runner.h>
#include <waggle/render/render_module.h>
#include <waggle/systems/editor_camera_system.h>
#include <waggle/systems/mesh_render_system.h>

#include <swarm/swarm.h>

#include <terra/terra.h>

#include <antennae/input.h>
#include <antennae/keyboard.h>

static const hive::LogCategory LOG_ENGINE{"Waggle.EngineRunner"};

namespace waggle
{
    extern void RegisterModule();
}
namespace
{
    class EngineSession
    {
    public:
        EngineSession(const waggle::EngineConfig& config, const waggle::EngineCallbacks& callbacks)
            : m_config{config}
            , m_callbacks{callbacks}
            , m_app{config.m_app}
        {
            m_context.m_app = &m_app;
            m_context.m_world = &m_app.GetWorld();
            m_context.m_jobs = config.m_jobs;
            if (config.m_jobs.IsValid())
                m_app.SetJobSubmitter(config.m_jobs);
            m_app.GetWorld().InsertResource(waggle::RuntimeContext{config.m_mode});

            terra::SetWindowTitle(m_windowContext, config.m_windowTitle);
            terra::SetWindowSize(m_windowContext, static_cast<int>(config.m_windowWidth),
                                 static_cast<int>(config.m_windowHeight));
        }

        ~EngineSession()
        {
            InvokeShutdownCallback();
            Cleanup();
        }

        int Run()
        {
            if (!InitializeModules())
            {
                return 1;
            }

            if (!InitializeRuntime())
            {
                return 1;
            }

            if (!RunSetupCallback())
            {
                return 1;
            }

            RunMainLoop();
            return 0;
        }

    private:
        [[nodiscard]] bool InitializeModules()
        {
            if (m_callbacks.m_onRegisterModules != nullptr)
            {
                m_callbacks.m_onRegisterModules();
            }

            waggle::RegisterModule();
            m_moduleRegistry.CreateModules();
            m_moduleRegistry.ConfigureModules();
            m_moduleRegistry.InitModules();
            m_modulesInitialized = true;
            return true;
        }

        [[nodiscard]] bool InitializeRuntime()
        {
            if (!IsGraphical() || m_config.m_deferWindow)
            {
                return true;
            }

            if (!InitializeWindow())
            {
                return false;
            }

            if (m_config.m_autoRenderer && !InitializeRenderer())
            {
                return false;
            }

            return true;
        }

        [[nodiscard]] bool InitializeWindow()
        {
            if (!terra::InitSystem())
            {
                hive::LogError(LOG_ENGINE, "Failed to initialize windowing backend");
                return false;
            }

            m_windowSystemInitialized = true;

            m_windowContext =
                terra::CreateWindowContext(m_config.m_windowTitle, static_cast<int>(m_config.m_windowWidth),
                                           static_cast<int>(m_config.m_windowHeight));

            if (m_windowContext == nullptr)
            {
                hive::LogError(LOG_ENGINE, "Failed to create window");
                return false;
            }

            m_windowInitialized = true;
            m_context.m_window = m_windowContext;

            if (!m_config.m_deferWindow)
                terra::SetWindowVisible(m_windowContext, true);

            return true;
        }

        [[nodiscard]] bool InitializeRenderer()
        {
            if (!swarm::InitSystem())
            {
                hive::LogError(LOG_ENGINE, "Failed to initialize Swarm");
                return false;
            }

            m_rendererSystemInitialized = true;

            m_renderContext = swarm::CreateRenderContext(m_windowContext);
            if (m_renderContext == nullptr)
            {
                hive::LogError(LOG_ENGINE, "Failed to create render context");
                return false;
            }

            m_rendererInitialized = true;
            m_context.m_renderContext = m_renderContext;

            m_renderModule = comb::New<waggle::RenderModule>(comb::GetDefaultAllocator(), m_renderContext);
            if (m_renderModule != nullptr && !m_renderModule->IsReady())
            {
                hive::LogWarning(LOG_ENGINE, "RenderModule failed to initialize; mesh rendering disabled");
            }
            return true;
        }

        [[nodiscard]] bool RunSetupCallback()
        {
            if (m_callbacks.m_onSetup != nullptr && !m_callbacks.m_onSetup(m_context, m_callbacks.m_userData))
            {
                return false;
            }

            m_setupCompleted = true;
            return true;
        }

        void RunMainLoop()
        {
            if (IsGraphical() && m_windowInitialized)
            {
                RunGraphicalLoop();
                return;
            }

            if (IsGraphical() && m_config.m_deferWindow)
            {
                RunDeferredLoop();
                return;
            }

            RunHeadlessLoop();
        }

        float ComputeAspect(terra::WindowContext* window) const
        {
            if (window == nullptr)
            {
                return 1.f;
            }
            const int w = terra::GetWindowWidth(window);
            const int h = terra::GetWindowHeight(window);
            if (w <= 0 || h <= 0)
            {
                return 1.f;
            }
            return static_cast<float>(w) / static_cast<float>(h);
        }

        void EnsureRenderModule(swarm::RenderContext* renderContext)
        {
            if (m_renderModule != nullptr || renderContext == nullptr)
            {
                return;
            }
            m_renderModule = comb::New<waggle::RenderModule>(comb::GetDefaultAllocator(), renderContext);
            if (m_renderModule != nullptr && !m_renderModule->IsReady())
            {
                hive::LogWarning(LOG_ENGINE, "RenderModule failed to initialize; mesh rendering disabled");
            }
        }

        void RenderECS(terra::WindowContext* window)
        {
            if (m_renderModule == nullptr)
            {
                return;
            }
            waggle::RenderMeshes(m_app.GetWorld(), *m_renderModule, ComputeAspect(window), m_config.m_jobs);
        }

        void TickEditorCameras()
        {
            waggle::UpdateEditorCameras(m_app.GetWorld());
        }

        void RunDeferredLoop()
        {
            while (m_app.IsRunning())
            {
                HIVE_PROFILE_SCOPE_N("Frame");

                if (m_context.m_window != nullptr)
                {
                    // PollEvents clears mouse deltas before pumping. Run Qt's pump *after*
                    // so messages it dispatches accumulate into the freshly-cleared delta
                    // (and our native filter writes m_keys for the same frame UpdateInput reads).
                    terra::PollEvents(m_context.m_window);
                    if (m_callbacks.m_onPrePoll != nullptr)
                    {
                        m_callbacks.m_onPrePoll(m_context, m_callbacks.m_userData);
                    }
                    antennae::UpdateInput(m_app.GetWorld(), m_context.m_window);
                    TickEditorCameras();

                    if (terra::ShouldWindowClose(m_context.m_window))
                        break;
                }
                else if (m_callbacks.m_onPrePoll != nullptr)
                {
                    // Before any Terra window exists (Project Hub, deferred-window startup),
                    // pump Qt's event loop directly so the editor UI can show and respond.
                    m_callbacks.m_onPrePoll(m_context, m_callbacks.m_userData);
                }

                if (m_config.m_autoTick)
                {
                    m_app.Tick();
                }

                if (m_context.m_renderContext != nullptr)
                {
                    EnsureRenderModule(m_context.m_renderContext);
                    swarm::BeginFrame(m_context.m_renderContext);
                }

                if (m_callbacks.m_onFrame != nullptr)
                {
                    m_callbacks.m_onFrame(m_context, m_callbacks.m_userData);
                }

                if (m_context.m_renderContext != nullptr)
                {
                    RenderECS(m_context.m_window);
                    swarm::EndFrame(m_context.m_renderContext);
                }
            }
        }

        void RunGraphicalLoop()
        {
            while (!terra::ShouldWindowClose(m_windowContext) && m_app.IsRunning())
            {
                HIVE_PROFILE_SCOPE_N("Frame");
                terra::PollEvents(m_windowContext);
                if (m_callbacks.m_onPrePoll != nullptr)
                {
                    m_callbacks.m_onPrePoll(m_context, m_callbacks.m_userData);
                }
                antennae::UpdateInput(m_app.GetWorld(), m_windowContext);
                TickEditorCameras();

                if (m_config.m_autoTick)
                {
                    m_app.Tick();
                }

                if (m_rendererInitialized)
                {
                    swarm::BeginFrame(m_renderContext);
                }

                if (m_callbacks.m_onFrame != nullptr)
                {
                    m_callbacks.m_onFrame(m_context, m_callbacks.m_userData);
                }

                if (m_rendererInitialized)
                {
                    RenderECS(m_windowContext);
                    swarm::EndFrame(m_renderContext);
                }
            }
        }

        void RunHeadlessLoop()
        {
            while (m_app.IsRunning())
            {
                HIVE_PROFILE_SCOPE_N("Frame");

                if (m_config.m_autoTick)
                {
                    m_app.Tick();
                }

                if (m_callbacks.m_onFrame != nullptr)
                {
                    m_callbacks.m_onFrame(m_context, m_callbacks.m_userData);
                }
            }
        }

        void InvokeShutdownCallback()
        {
            if (!m_setupCompleted || m_shutdownCallbackInvoked || m_callbacks.m_onShutdown == nullptr)
            {
                return;
            }

            m_callbacks.m_onShutdown(m_context, m_callbacks.m_userData);
            m_shutdownCallbackInvoked = true;
        }

        void Cleanup()
        {
            if (m_renderModule != nullptr)
            {
                comb::Delete(comb::GetDefaultAllocator(), m_renderModule);
                m_renderModule = nullptr;
            }

            // Either path that owned a render context implies swarm::InitSystem was called.
            // Capture the "need shutdown" flag before destroying the context.
            const bool needRendererShutdown =
                m_rendererSystemInitialized || m_context.m_renderContext != nullptr;
            if (m_context.m_renderContext != nullptr)
            {
                swarm::DestroyRenderContext(m_context.m_renderContext);
                m_context.m_renderContext = nullptr;
            }
            m_renderContext = nullptr;
            m_rendererInitialized = false;
            if (needRendererShutdown)
            {
                swarm::ShutdownSystem();
                m_rendererSystemInitialized = false;
            }

            const bool needWindowShutdown = m_windowSystemInitialized || m_context.m_window != nullptr;
            if (m_context.m_window != nullptr)
            {
                terra::DestroyWindowContext(m_context.m_window);
                m_context.m_window = nullptr;
            }
            m_windowContext = nullptr;
            m_windowInitialized = false;
            if (needWindowShutdown)
            {
                terra::ShutdownSystem();
                m_windowSystemInitialized = false;
            }

            if (m_modulesInitialized)
            {
                m_moduleRegistry.ShutdownModules();
                m_modulesInitialized = false;
            }
        }

        [[nodiscard]] bool IsGraphical() const
        {
            return m_config.m_mode != waggle::EngineMode::HEADLESS;
        }

        const waggle::EngineConfig& m_config;
        const waggle::EngineCallbacks& m_callbacks;
        hive::ModuleRegistry m_moduleRegistry{};
        waggle::App m_app;
        waggle::EngineContext m_context{};
        bool m_modulesInitialized{false};
        bool m_setupCompleted{false};
        bool m_shutdownCallbackInvoked{false};

        terra::WindowContext* m_windowContext{nullptr};
        bool m_windowSystemInitialized{false};
        bool m_windowInitialized{false};

        swarm::RenderContext* m_renderContext{nullptr};
        bool m_rendererSystemInitialized{false};
        bool m_rendererInitialized{false};

        waggle::RenderModule* m_renderModule{nullptr};
    };
} // namespace

namespace waggle
{

    int Run(const EngineConfig& config, const EngineCallbacks& callbacks)
    {
        EngineSession session{config, callbacks};
        return session.Run();
    }

    bool CreateWindowAndRenderer(EngineContext& ctx, const char* title, uint32_t width, uint32_t height)
    {
        if (ctx.m_window != nullptr)
            return true;

        if (!terra::InitSystem())
        {
            hive::LogError(LOG_ENGINE, "Failed to initialize windowing backend");
            return false;
        }

        auto* window = terra::CreateWindowContext(title, static_cast<int>(width), static_cast<int>(height));
        if (window == nullptr)
        {
            hive::LogError(LOG_ENGINE, "Failed to create window");
            return false;
        }
        terra::SetWindowVisible(window, false);
        ctx.m_window = window;

        if (!swarm::InitSystem())
        {
            hive::LogError(LOG_ENGINE, "Failed to initialize Swarm");
            return false;
        }

        auto* renderCtx = swarm::CreateRenderContext(window);
        if (renderCtx == nullptr)
        {
            hive::LogError(LOG_ENGINE, "Failed to create render context");
            return false;
        }

        ctx.m_renderContext = renderCtx;
        return true;
    }

} // namespace waggle
