#include <hive/core/env.h>
#include <hive/core/log.h>

#include <swarm/platform/diligent_swarm.h>
#include <swarm/precomp.h>
#include <swarm/swarm.h>
#include <swarm/swarm_log.h>
#include <swarm/swarmmodule.h>

#include <hive/platform/process.h>

#include <EngineFactoryVk.h>
#include <RefCntAutoPtr.hpp>
#include <diligent_bindless.h>
#include <diligent_internal.h>
#include <diligent_materials_buffer.h>
#include <diligent_shader_library.h>
#include <filesystem>
namespace swarm
{
    namespace
    {
        void ShutdownRenderContext(RenderContext& renderContext)
        {
            if (renderContext.m_shaderLibrary != nullptr)
            {
                auto& allocator = SwarmModule::GetInstance().GetAllocator();
                comb::Delete(allocator, renderContext.m_shaderLibrary);
                renderContext.m_shaderLibrary = nullptr;
            }

            if (renderContext.m_bindlessHeap != nullptr)
            {
                auto& allocator = SwarmModule::GetInstance().GetAllocator();
                comb::Delete(allocator, renderContext.m_bindlessHeap);
                renderContext.m_bindlessHeap = nullptr;
            }

            if (renderContext.m_materialsBuffer != nullptr)
            {
                auto& allocator = SwarmModule::GetInstance().GetAllocator();
                renderContext.m_materialsBuffer->Shutdown();
                comb::Delete(allocator, renderContext.m_materialsBuffer);
                renderContext.m_materialsBuffer = nullptr;
            }

            ShutdownSceneConstants(&renderContext);

            if (renderContext.m_swapchain != nullptr)
            {
                renderContext.m_swapchain->Release();
            }

            for (uint32_t i = 0; i < renderContext.m_deferredContextCount; ++i)
            {
                if (renderContext.m_deferredContexts[i] != nullptr)
                {
                    renderContext.m_deferredContexts[i]->FinishFrame();
                    renderContext.m_deferredContexts[i]->Release();
                    renderContext.m_deferredContexts[i] = nullptr;
                }
            }
            renderContext.m_deferredContextCount = 0;

            if (renderContext.m_context != nullptr)
            {
                renderContext.m_context->Release();
            }

            if (renderContext.m_device != nullptr)
            {
                renderContext.m_device->Release();
            }
        }
    } // namespace

    const hive::LogCategory LOG_DILIGENT{"Diligent", &LOG_SWARM};

    static void DiligentToHiveMessageCallback(Diligent::DEBUG_MESSAGE_SEVERITY severity, const Diligent::Char* message,
                                              const Diligent::Char* function, const Diligent::Char* file, int line)
    {
        switch (severity)
        {
            case Diligent::DEBUG_MESSAGE_SEVERITY_INFO:
                hive::LogInfo(LOG_DILIGENT, message);
                break;
            case Diligent::DEBUG_MESSAGE_SEVERITY_WARNING:
                hive::LogWarning(LOG_DILIGENT, message);
                break;
            case Diligent::DEBUG_MESSAGE_SEVERITY_ERROR:
            case Diligent::DEBUG_MESSAGE_SEVERITY_FATAL_ERROR:
                hive::LogError(LOG_DILIGENT, message);
                break;
            default:
                break;
        }
    }

    bool InitSystem()
    {
        Diligent::SetDebugMessageCallback(&DiligentToHiveMessageCallback);
        return true;
    }

    void ShutdownSystem()
    {
    }

    bool InitRenderContext(RenderContext* renderContext, terra::WindowContext* window);
    RenderContext* CreateRenderContext(terra::WindowContext* windowContext)
    {
        auto& allocator = SwarmModule::GetInstance().GetAllocator();

        RenderContext* context = comb::New<RenderContext>(allocator);
        if (!InitRenderContext(context, windowContext))
        {
            comb::Delete(allocator, context);
            return nullptr;
        }

        context->m_shaderLibrary = comb::New<ShaderLibrary>(allocator);
        if (!context->m_shaderLibrary->Initialize(context->m_device))
        {
            hive::LogError(LOG_SWARM, "CreateRenderContext: ShaderLibrary init failed");
            ShutdownRenderContext(*context);
            comb::Delete(allocator, context);
            return nullptr;
        }

        context->m_bindlessHeap = comb::New<BindlessHeap>(allocator);
        context->m_materialsBuffer = comb::New<MaterialsBuffer>(allocator);
        if (!context->m_materialsBuffer->Initialize(context->m_device))
        {
            hive::LogError(LOG_SWARM, "CreateRenderContext: MaterialsBuffer init failed");
            ShutdownRenderContext(*context);
            comb::Delete(allocator, context);
            return nullptr;
        }

        // Engine shaders are staged next to the running executable. Project paths get
        // added later via swarm::AddShaderSearchPath. The PSO cache lands in
        // HIVE_SHADER_CACHE when set, else in the OS user cache dir.
        {
            std::filesystem::path executableDir;
            char overrideBuf[1024]{};
            if (hive::core::ReadEnvVar("HIVE_SWARM_MODULE_DIR", overrideBuf, sizeof(overrideBuf)))
            {
                executableDir = overrideBuf;
            }
            else
            {
                char buf[4096]{};
                if (hive::GetExecutablePath(buf, sizeof(buf)))
                {
                    executableDir = std::filesystem::path{buf}.parent_path();
                }
                if (executableDir.empty())
                {
                    executableDir = std::filesystem::current_path();
                }
            }
            const auto engineShaderDir = executableDir / "shaders" / "engine";
            context->m_shaderLibrary->AddSearchPath(engineShaderDir.string().c_str());

            char cacheOverride[1024]{};
            std::filesystem::path cachePath;
            if (hive::core::ReadEnvVar("HIVE_SHADER_CACHE", cacheOverride, sizeof(cacheOverride)))
            {
                cachePath = cacheOverride;
            }
            else
            {
                char localAppData[1024]{};
                if (hive::core::ReadEnvVar("LOCALAPPDATA", localAppData, sizeof(localAppData)) &&
                    localAppData[0] != '\0')
                {
                    cachePath = std::filesystem::path{localAppData} / "HiveEngine" / "shader_cache";
                }
                else if (hive::core::ReadEnvVar("XDG_CACHE_HOME", localAppData, sizeof(localAppData)) &&
                         localAppData[0] != '\0')
                {
                    cachePath = std::filesystem::path{localAppData} / "hiveengine" / "shader_cache";
                }
                else if (hive::core::ReadEnvVar("HOME", localAppData, sizeof(localAppData)) && localAppData[0] != '\0')
                {
                    cachePath = std::filesystem::path{localAppData} / ".cache" / "hiveengine" / "shader_cache";
                }
            }
            if (!cachePath.empty())
            {
                std::error_code ec;
                std::filesystem::create_directories(cachePath, ec);
                cachePath /= "swarm_pso_v1.bin";
                context->m_shaderLibrary->SetDiskCachePath(cachePath.string().c_str());
                context->m_shaderLibrary->LoadDiskCache();
            }
        }

        if (!InitSceneConstants(context))
        {
            ShutdownRenderContext(*context);
            comb::Delete(allocator, context);
            return nullptr;
        }

        return context;
    }

    void DestroyRenderContext(RenderContext* renderContext)
    {
        if (renderContext == nullptr)
        {
            return;
        }
        ShutdownRenderContext(*renderContext);

        auto& allocator = SwarmModule::GetInstance().GetAllocator();
        comb::Delete(allocator, renderContext);
    }

    void BeginFrame(RenderContext* renderContext)
    {
        using namespace Diligent;
        if (renderContext == nullptr || renderContext->m_swapchain == nullptr ||
            renderContext->m_deferredContextCount == 0)
        {
            return;
        }

        // Hot reload drain. Owned by the render thread: any thread can post a request via
        // RequestShaderReload; we consume it here so reload runs while no command list is open.
        if (renderContext->m_shaderLibrary != nullptr && renderContext->m_shaderLibrary->ConsumeReloadRequest())
        {
            renderContext->m_shaderLibrary->ReloadIfDirty();
        }

        // Lazy Begin: only deferred[0] is guaranteed to record (clear + at least one draw in
        // serial mode). Workers >0 are Begun by PrepareWorkerFrame only when they actually
        // run. This avoids Begin/FinishCommandList on contexts that never record anything,
        // which crashes the Vulkan validation layer (lazy VkCommandBuffer allocation).
        for (uint32_t i = 0; i < renderContext->m_deferredContextCount; ++i)
        {
            renderContext->m_viewDirty[i] = true;
            renderContext->m_deferredHasWork[i] = false;
        }

        IDeviceContext* primary = renderContext->m_deferredContexts[0];
        if (primary == nullptr)
        {
            return;
        }
        primary->Begin(0);

        ITextureView* renderTargetView = renderContext->m_swapchain->GetCurrentBackBufferRTV();
        ITextureView* depthStencilView = renderContext->m_swapchain->GetDepthBufferDSV();
        primary->SetRenderTargets(1, &renderTargetView, depthStencilView, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        constexpr float kClearColor[] = {0.350f, 0.350f, 0.350f, 1.0f};
        primary->ClearRenderTarget(renderTargetView, kClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        primary->ClearDepthStencil(depthStencilView, CLEAR_DEPTH_FLAG, 1.f, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        renderContext->m_deferredHasWork[0] = true;
    }

    void EndFrame(RenderContext* renderContext)
    {
        using namespace Diligent;
        if (renderContext == nullptr || renderContext->m_swapchain == nullptr || renderContext->m_context == nullptr)
        {
            return;
        }

        // Finish and submit every deferred context that recorded at least one command. Diligent
        // allocates the underlying VkCommandBuffer lazily, so FinishCommandList on an untouched
        // deferred ctx (Begin'd but never written) crashes the validation layer.
        ICommandList* commandLists[kMaxDeferredContexts]{};
        uint32_t commandListCount = 0;
        for (uint32_t i = 0; i < renderContext->m_deferredContextCount; ++i)
        {
            IDeviceContext* deferred = renderContext->m_deferredContexts[i];
            if (deferred == nullptr || !renderContext->m_deferredHasWork[i])
            {
                continue;
            }
            ICommandList* commandList = nullptr;
            deferred->FinishCommandList(&commandList);
            if (commandList != nullptr)
            {
                commandLists[commandListCount++] = commandList;
            }
        }

        if (commandListCount > 0)
        {
            renderContext->m_context->ExecuteCommandLists(commandListCount, commandLists);
            for (uint32_t i = 0; i < commandListCount; ++i)
            {
                commandLists[i]->Release();
            }
        }

        renderContext->m_swapchain->Present();

        // FinishFrame releases stale dynamic pages so they can be reused next frame.
        for (uint32_t i = 0; i < renderContext->m_deferredContextCount; ++i)
        {
            if (renderContext->m_deferredContexts[i] != nullptr)
            {
                renderContext->m_deferredContexts[i]->FinishFrame();
            }
        }
    }

    void WaitForIdle(RenderContext* ctx)
    {
        using namespace Diligent;
        if (ctx == nullptr || ctx->m_context == nullptr)
        {
            return;
        }

        // Flush every deferred context that has recorded work. Skipping untouched ones avoids
        // the same VVL crash described in EndFrame (lazy VkCommandBuffer allocation).
        ICommandList* commandLists[kMaxDeferredContexts]{};
        uint32_t commandListCount = 0;
        for (uint32_t i = 0; i < ctx->m_deferredContextCount; ++i)
        {
            if (ctx->m_deferredContexts[i] == nullptr || !ctx->m_deferredHasWork[i])
            {
                continue;
            }
            ICommandList* commandList = nullptr;
            ctx->m_deferredContexts[i]->FinishCommandList(&commandList);
            if (commandList != nullptr)
            {
                commandLists[commandListCount++] = commandList;
            }
            ctx->m_deferredHasWork[i] = false;
        }
        if (commandListCount > 0)
        {
            ctx->m_context->ExecuteCommandLists(commandListCount, commandLists);
            for (uint32_t i = 0; i < commandListCount; ++i)
            {
                commandLists[i]->Release();
            }
        }

        ctx->m_context->Flush();
        ctx->m_context->WaitForIdle();

        if (ctx->m_device != nullptr)
        {
            ctx->m_device->IdleGPU();
        }
    }

    void ResizeSwapchain(RenderContext* ctx, uint32_t width, uint32_t height)
    {
        if (ctx->m_swapchain != nullptr && width > 0 && height > 0)
            ctx->m_swapchain->Resize(width, height);
    }

    void AddShaderSearchPath(RenderContext* ctx, const char* directory)
    {
        if (ctx == nullptr || ctx->m_shaderLibrary == nullptr)
        {
            return;
        }
        ctx->m_shaderLibrary->AddSearchPath(directory);
    }

    void RequestShaderReload(RenderContext* ctx)
    {
        if (ctx == nullptr || ctx->m_shaderLibrary == nullptr)
        {
            return;
        }
        ctx->m_shaderLibrary->RequestReload();
    }

    bool FlushShaderCache(RenderContext* ctx)
    {
        if (ctx == nullptr || ctx->m_shaderLibrary == nullptr)
        {
            return false;
        }
        return ctx->m_shaderLibrary->SaveDiskCache();
    }

    uint32_t GetDeferredContextCount(const RenderContext* renderContext)
    {
        return renderContext == nullptr ? 0 : renderContext->m_deferredContextCount;
    }

    void PrepareWorkerFrame(RenderContext* renderContext, uint32_t workerIndex)
    {
        using namespace Diligent;
        if (renderContext == nullptr || workerIndex >= renderContext->m_deferredContextCount ||
            renderContext->m_swapchain == nullptr)
        {
            return;
        }
        IDeviceContext* worker = renderContext->m_deferredContexts[workerIndex];
        if (worker == nullptr)
        {
            return;
        }

        // Worker 0 already had Begin/RT bound by BeginFrame (which also cleared it).
        if (workerIndex == 0)
        {
            return;
        }

        // Lazy Begin: this worker is about to record commands.
        worker->Begin(0);
        renderContext->m_deferredHasWork[workerIndex] = true;

        ITextureView* renderTargetView = renderContext->m_swapchain->GetCurrentBackBufferRTV();
        ITextureView* depthStencilView = renderContext->m_swapchain->GetDepthBufferDSV();
        // Worker 0's command list runs first and transitions the RT to RENDER_TARGET state.
        // Use MODE_NONE here so deferred ctx N>0 doesn't try to emit a transition from an
        // unknown state (it cannot know worker 0 already moved the resource).
        worker->SetRenderTargets(1, &renderTargetView, depthStencilView, RESOURCE_STATE_TRANSITION_MODE_NONE);
    }

    // ---- Viewport render target (offscreen) ----

    struct ViewportRT
    {
        Diligent::RefCntAutoPtr<Diligent::ITexture> m_color;
        Diligent::RefCntAutoPtr<Diligent::ITexture> m_depth;
        Diligent::ITextureView* m_rtv{nullptr};
        Diligent::ITextureView* m_dsv{nullptr};
        Diligent::ITextureView* m_srv{nullptr};
        Diligent::IRenderDevice* m_device{nullptr};
        uint32_t m_width{0};
        uint32_t m_height{0};
    };

    static void CreateViewportRTTextures(ViewportRT* rt, uint32_t width, uint32_t height,
                                         Diligent::TEXTURE_FORMAT colorFormat, Diligent::TEXTURE_FORMAT depthFormat)
    {
        using namespace Diligent;
        rt->m_color.Release();
        rt->m_depth.Release();
        rt->m_width = width;
        rt->m_height = height;

        Diligent::TextureDesc colorDesc;
        colorDesc.Name = "ViewportRT Color";
        colorDesc.Type = RESOURCE_DIM_TEX_2D;
        colorDesc.Width = width;
        colorDesc.Height = height;
        colorDesc.Format = colorFormat;
        colorDesc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        rt->m_device->CreateTexture(colorDesc, nullptr, &rt->m_color);
        rt->m_rtv = rt->m_color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        rt->m_srv = rt->m_color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        Diligent::TextureDesc depthDesc;
        depthDesc.Name = "ViewportRT Depth";
        depthDesc.Type = RESOURCE_DIM_TEX_2D;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.Format = depthFormat;
        depthDesc.BindFlags = BIND_DEPTH_STENCIL;
        rt->m_device->CreateTexture(depthDesc, nullptr, &rt->m_depth);
        rt->m_dsv = rt->m_depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    }

    ViewportRT* CreateViewportRT(RenderContext* ctx, uint32_t width, uint32_t height)
    {
        if (ctx == nullptr || ctx->m_swapchain == nullptr)
        {
            return nullptr;
        }
        auto& allocator = SwarmModule::GetInstance().GetAllocator();
        auto* rt = comb::New<ViewportRT>(allocator);
        rt->m_device = ctx->m_device;
        const auto& swapchainDesc = ctx->m_swapchain->GetDesc();
        CreateViewportRTTextures(rt, width, height, swapchainDesc.ColorBufferFormat, swapchainDesc.DepthBufferFormat);
        return rt;
    }

    void DestroyViewportRT(ViewportRT* rt)
    {
        auto& allocator = SwarmModule::GetInstance().GetAllocator();
        comb::Delete(allocator, rt);
    }

    void ResizeViewportRT(ViewportRT* rt, uint32_t width, uint32_t height)
    {
        if (rt == nullptr || width == 0 || height == 0)
        {
            return;
        }
        if (rt->m_width == width && rt->m_height == height)
        {
            return;
        }
        const auto colorFormat = rt->m_color->GetDesc().Format;
        const auto depthFormat = rt->m_depth->GetDesc().Format;
        CreateViewportRTTextures(rt, width, height, colorFormat, depthFormat);
    }

    uint32_t GetViewportRTWidth(const ViewportRT* rt)
    {
        return rt->m_width;
    }
    uint32_t GetViewportRTHeight(const ViewportRT* rt)
    {
        return rt->m_height;
    }
    void* GetViewportRTSRV(const ViewportRT* rt)
    {
        return rt->m_srv;
    }

    void BeginViewportRT(RenderContext* ctx, ViewportRT* rt)
    {
        using namespace Diligent;
        if (ctx == nullptr || ctx->m_context == nullptr || rt == nullptr)
        {
            return;
        }
        ctx->m_context->SetRenderTargets(1, &rt->m_rtv, rt->m_dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        constexpr float kViewportClearColor[] = {0.180f, 0.180f, 0.180f, 1.0f};
        ctx->m_context->ClearRenderTarget(rt->m_rtv, kViewportClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ctx->m_context->ClearDepthStencil(rt->m_dsv, CLEAR_DEPTH_FLAG, 1.f, 0,
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void EndViewportRT(RenderContext* ctx, ViewportRT* rt)
    {
        using namespace Diligent;

        // The offscreen color target is sampled by ImGui in the same frame.
        // Transition it explicitly before leaving the viewport pass.
        const StateTransitionDesc barrier{rt->m_color, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE,
                                          STATE_TRANSITION_FLAG_UPDATE_STATE};
        ctx->m_context->TransitionResourceStates(1, &barrier);
        ctx->m_context->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
    }
} // namespace swarm
