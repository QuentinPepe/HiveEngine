#pragma once

#include <swarm/swarm.h>

#include <RenderDevice.h>
#include <cstdint>

namespace swarm
{
    class ShaderLibrary;

    inline constexpr uint32_t kMaxDeferredContexts = 16;

    struct RenderContext
    {
        Diligent::IRenderDevice* m_device{nullptr};
        // Immediate context: owns submission to GPU. Used only for ExecuteCommandLists + Present.
        Diligent::IDeviceContext* m_context{nullptr};
        Diligent::ISwapChain* m_swapchain{nullptr};

        // Deferred contexts: workers record draw commands here. Index 0 is also used when
        // rendering single-threaded so the codepath is identical.
        Diligent::IDeviceContext* m_deferredContexts[kMaxDeferredContexts]{};
        uint32_t m_deferredContextCount{0};

        // Shared scene constants. View buffer (b0) is per-frame; object buffer (b1) is per-draw.
        // Both are bound as STATIC variables on every material pipeline state. Time is also
        // STATIC and updated once per frame by swarm::SetTime.
        Diligent::IBuffer* m_viewConstantBuffer{nullptr};
        Diligent::IBuffer* m_objectConstantBuffer{nullptr};
        Diligent::IBuffer* m_timeConstantBuffer{nullptr};

        // CPU mirror of the view cbuffer. Updated by SetView/SetLighting,
        // uploaded lazily before each DrawMesh when dirty.
        ViewParams m_viewParams{};
        LightingParams m_lightingParams{};
        float m_timeSeconds{0.f};
        float m_deltaSeconds{0.f};
        bool m_timeDirty[kMaxDeferredContexts]{};
        // Tracks per-deferred-context dirty state for view constants. Dynamic buffers have
        // per-context suballocations, so each deferred context must flush its own copy.
        bool m_viewDirty[kMaxDeferredContexts]{};
        // True if any command (draw, SetRenderTargets, Clear, ...) was recorded into the
        // deferred ctx this frame. FinishCommandList on an untouched deferred ctx crashes
        // the Vulkan validation layer because Diligent allocates the underlying VkCommandBuffer
        // lazily on first recording.
        bool m_deferredHasWork[kMaxDeferredContexts]{};

        // Compiles, caches, and hot-reloads shaders + PSOs. Created in InitRenderContext,
        // destroyed in ShutdownRenderContext. Lives in RenderContext because it owns
        // device-affine state (cache blob, source factory).
        ShaderLibrary* m_shaderLibrary{nullptr};
    };
} // namespace swarm
