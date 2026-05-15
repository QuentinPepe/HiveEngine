#include <hive/core/log.h>
#include <hive/core/moduleregistry.h>
#include <hive/profiling/profiler.h>

#include <comb/default_allocator.h>
#include <comb/new.h>

#include <drone/counter.h>
#include <drone/frame_pipeline.h>
#include <drone/signal.h>

#include <waggle/engine_runner.h>
#include <waggle/profiling/frame_bench.h>
#include <waggle/render/render_frame.h>
#include <waggle/render/render_module.h>
#include <waggle/render/render_resource.h>
#include <waggle/systems/debug_freecam_system.h>
#include <waggle/systems/editor_camera_system.h>
#include <waggle/systems/gizmo_render_system.h>
#include <waggle/systems/mesh_render_system.h>

#include <swarm/swarm.h>

#include <terra/terra.h>

#include <antennae/input.h>
#include <antennae/keyboard.h>

#include <atomic>
#include <thread>

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
            , m_bench{m_app}
        {
            m_context.m_app = &m_app;
            m_context.m_world = &m_app.GetWorld();
            m_context.m_jobs = config.m_jobs;
            if (config.m_jobs.IsValid())
            {
                m_app.SetJobSubmitter(config.m_jobs);
            }
            m_app.GetWorld().InsertResource(waggle::RuntimeContext{config.m_mode});

            terra::SetWindowTitle(m_windowContext, config.m_windowTitle);
            terra::SetWindowSize(m_windowContext, static_cast<int>(config.m_windowWidth),
                                 static_cast<int>(config.m_windowHeight));

            m_bench.Configure(config.m_useRenderThread ? "render_thread" : "inline");
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
            {
                terra::SetWindowVisible(m_windowContext, true);
            }

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

            m_renderModule = comb::New<waggle::RenderModule>(comb::GetDefaultAllocator(), m_renderContext,
                                                              m_context.m_jobs);
            if (m_renderModule != nullptr && !m_renderModule->IsReady())
            {
                hive::LogWarning(LOG_ENGINE, "RenderModule failed to initialize; mesh rendering disabled");
            }
            m_context.m_renderModule = m_renderModule;
            m_app.GetWorld().InsertResource(waggle::RenderResource{m_renderModule, m_renderContext});
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
            if (m_context.m_renderModule != nullptr)
            {
                m_renderModule = m_context.m_renderModule;
                return;
            }
            m_renderModule = comb::New<waggle::RenderModule>(comb::GetDefaultAllocator(), renderContext,
                                                              m_context.m_jobs);
            if (m_renderModule != nullptr && !m_renderModule->IsReady())
            {
                hive::LogWarning(LOG_ENGINE, "RenderModule failed to initialize; mesh rendering disabled");
            }
            m_context.m_renderModule = m_renderModule;
            m_app.GetWorld().InsertResource(waggle::RenderResource{m_renderModule, renderContext});
        }

        void RenderECSInline(terra::WindowContext* window, swarm::RenderContext* renderContext)
        {
            if (m_renderModule == nullptr || renderContext == nullptr)
            {
                return;
            }
            waggle::RenderFrame frame{comb::GetDefaultMemoryResource()};
            m_bench.BeginBuild();
            waggle::BuildRenderFrame(m_app.GetWorld(), *m_renderModule, ComputeAspect(window), frame);
            m_bench.EndBuild();
            m_bench.BeginExecute();
            waggle::ExecuteRenderFrame(*m_renderModule, frame, m_config.m_jobs);
            m_bench.EndExecute();
        }

        void TickEditorCameras()
        {
            // Debug freecam runs before the editor camera so that, when active, it
            // consumes the mouse delta and the editor camera sees zeroed input.
            waggle::UpdateDebugFreeCam(m_app.GetWorld());
            waggle::UpdateEditorCameras(m_app.GetWorld());
            waggle::UpdateEditorGrid(m_app.GetWorld());
            waggle::UpdateGizmoVisual(m_app.GetWorld());
        }

        void StartRenderThreadIfReady(swarm::RenderContext* renderContext)
        {
            if (m_renderThreadActive || !m_config.m_useRenderThread || renderContext == nullptr ||
                m_renderModule == nullptr)
            {
                return;
            }
            m_renderShouldStop.store(false, std::memory_order_release);
            m_renderDone.Reset(0);
            m_renderKick.Reset();
            m_pendingRenderFrame = false;
            m_renderThread = std::thread{&EngineSession::RenderThreadMain, this};
            m_renderThreadActive = true;
            hive::LogInfo(LOG_ENGINE, "Render thread started");
        }

        void StopRenderThread()
        {
            if (!m_renderThreadActive)
            {
                return;
            }
            // Drain any in-flight frame before signaling termination so the renderer never
            // touches Swarm after we tear it down.
            if (m_pendingRenderFrame)
            {
                m_renderDone.Wait();
                m_pendingRenderFrame = false;
            }
            m_renderShouldStop.store(true, std::memory_order_release);
            m_renderKick.Set();
            if (m_renderThread.joinable())
            {
                m_renderThread.join();
            }
            m_renderThreadActive = false;
            hive::LogInfo(LOG_ENGINE, "Render thread stopped");
        }

        void RenderThreadMain()
        {
            HIVE_PROFILE_THREAD("Render");
            while (!m_renderShouldStop.load(std::memory_order_acquire))
            {
                m_renderKick.WaitAndReset();
                if (m_renderShouldStop.load(std::memory_order_acquire))
                {
                    break;
                }

                const waggle::RenderFrame& frame = m_renderFrames.ForRead(m_frameIndex);
                if (frame.m_terminationRequested)
                {
                    m_renderDone.Decrement();
                    break;
                }

                swarm::RenderContext* renderContext = m_context.m_renderContext;
                if (renderContext != nullptr)
                {
                    HIVE_PROFILE_SCOPE_N("RenderThread::Frame");
                    if (frame.m_resizePending && frame.m_resizeWidth > 0 && frame.m_resizeHeight > 0)
                    {
                        swarm::ResizeSwapchain(renderContext, frame.m_resizeWidth, frame.m_resizeHeight);
                    }
                    m_bench.BeginExecute();
                    swarm::BeginFrame(renderContext);
                    waggle::ExecuteRenderFrame(*m_renderModule, frame, m_config.m_jobs);
                    swarm::EndFrame(renderContext);
                    m_bench.EndExecute();
                }

                m_renderDone.Decrement();
            }
        }

        void BuildAndKickRenderFrame(terra::WindowContext* window, swarm::RenderContext* renderContext)
        {
            // Wait for previous frame's render submission to complete before reusing its
            // FrameData slot. FramePipeline pattern: write[t] is read[t-1] after the flip.
            if (m_pendingRenderFrame)
            {
                m_renderDone.Wait();
                m_pendingRenderFrame = false;
            }
            m_frameIndex.Flip();

            waggle::RenderFrame& writeFrame = m_renderFrames.ForWrite(m_frameIndex);
            waggle::ResetRenderFrame(writeFrame);
            m_bench.BeginBuild();
            waggle::BuildRenderFrame(m_app.GetWorld(), *m_renderModule, ComputeAspect(window), writeFrame);
            m_bench.EndBuild();

            m_renderDone.Reset(1);
            m_renderKick.Set();
            m_pendingRenderFrame = true;
        }

        void RunDeferredLoop()
        {
            while (m_app.IsRunning())
            {
                HIVE_PROFILE_SCOPE_N("Frame");
                m_bench.BeginFrame();

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
                    {
                        break;
                    }
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

                if (m_context.m_renderContext != nullptr && m_renderModule == nullptr)
                {
                    EnsureRenderModule(m_context.m_renderContext);
                }
                StartRenderThreadIfReady(m_context.m_renderContext);

                if (m_callbacks.m_onFrame != nullptr)
                {
                    m_callbacks.m_onFrame(m_context, m_callbacks.m_userData);
                }

                if (m_renderThreadActive)
                {
                    BuildAndKickRenderFrame(m_context.m_window, m_context.m_renderContext);
                }
                else if (m_context.m_renderContext != nullptr)
                {
                    swarm::BeginFrame(m_context.m_renderContext);
                    RenderECSInline(m_context.m_window, m_context.m_renderContext);
                    swarm::EndFrame(m_context.m_renderContext);
                }

                m_bench.EndFrame();
            }
        }

        void RunGraphicalLoop()
        {
            StartRenderThreadIfReady(m_renderContext);
            while (!terra::ShouldWindowClose(m_windowContext) && m_app.IsRunning())
            {
                HIVE_PROFILE_SCOPE_N("Frame");
                m_bench.BeginFrame();
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

                if (m_callbacks.m_onFrame != nullptr)
                {
                    m_callbacks.m_onFrame(m_context, m_callbacks.m_userData);
                }

                if (m_renderThreadActive)
                {
                    BuildAndKickRenderFrame(m_windowContext, m_renderContext);
                }
                else if (m_rendererInitialized)
                {
                    swarm::BeginFrame(m_renderContext);
                    RenderECSInline(m_windowContext, m_renderContext);
                    swarm::EndFrame(m_renderContext);
                }

                m_bench.EndFrame();
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
            // Drain and stop the render thread before tearing down Swarm so we never destroy
            // a render context that the render thread is still touching.
            StopRenderThread();

            if (m_renderModule != nullptr)
            {
                comb::Delete(comb::GetDefaultAllocator(), m_renderModule);
                m_renderModule = nullptr;
            }

            // Either path that owned a render context implies swarm::InitSystem was called.
            // Capture the "need shutdown" flag before destroying the context.
            const bool needRendererShutdown = m_rendererSystemInitialized || m_context.m_renderContext != nullptr;
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

        // Render thread state. FrameData double-buffers the RenderFrame so the game thread
        // writes frame N+1 while the render thread reads frame N (one frame of latency).
        drone::FrameIndex m_frameIndex{};
        drone::FrameData<waggle::RenderFrame> m_renderFrames;
        drone::Signal m_renderKick{};
        drone::Counter m_renderDone{};
        std::thread m_renderThread{};
        std::atomic<bool> m_renderShouldStop{false};
        bool m_renderThreadActive{false};
        bool m_pendingRenderFrame{false};

        waggle::FrameBench m_bench;
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
        {
            return true;
        }

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

        if (ctx.m_renderModule == nullptr && ctx.m_world != nullptr)
        {
            auto* renderModule =
                comb::New<waggle::RenderModule>(comb::GetDefaultAllocator(), renderCtx, ctx.m_jobs);
            if (renderModule != nullptr && !renderModule->IsReady())
            {
                hive::LogWarning(LOG_ENGINE, "RenderModule failed to initialize; mesh rendering disabled");
            }
            ctx.m_renderModule = renderModule;
            ctx.m_world->InsertResource(waggle::RenderResource{renderModule, renderCtx});
        }
        return true;
    }

} // namespace waggle
