#pragma once

#include <hive/hive_config.h>
#include <hive/math/types.h>

#include <comb/memory_resource.h>

#include <wax/containers/vector.h>

#include <swarm/swarm.h>

namespace waggle
{
    // One queued draw call. Built from ECS state by BuildRenderFrame (game thread) and
    // consumed by ExecuteRenderFrame (render thread). The Mesh/Material pointers are owned
    // by RenderModule whose lifetime exceeds any in-flight frame.
    struct DrawItem
    {
        swarm::Mesh* m_mesh{nullptr};
        swarm::Material* m_material{nullptr};
        hive::math::Mat4 m_world{hive::math::Mat4::Identity()};
        int32_t m_submeshIndex{-1}; // -1 = draw all submeshes
    };

    // Per-frame payload exchanged between game and render threads. All data is copied from
    // the ECS so the game thread can mutate state for frame N+1 while the render thread
    // reads frame N. Allocated once and reused across frames (Vector capacity persists).
    struct RenderFrame
    {
        wax::Vector<DrawItem> m_draws;
        swarm::ViewParams m_view{};
        swarm::LightingParams m_lighting{};
        float m_aspect{1.f};
        float m_timeSeconds{0.f};
        float m_deltaSeconds{0.f};
        bool m_hasCamera{false};

        // Swapchain resize request. Set by the game thread (UI/window event), executed by
        // the render thread before BeginFrame to avoid concurrent Present + resize.
        uint32_t m_resizeWidth{0};
        uint32_t m_resizeHeight{0};
        bool m_resizePending{false};

        // Set by the game thread on shutdown so the render thread exits its loop cleanly.
        bool m_terminationRequested{false};

        RenderFrame() = default;
        explicit RenderFrame(comb::MemoryResource allocator)
            : m_draws{allocator}
        {
        }
    };

    inline void ResetRenderFrame(RenderFrame& frame)
    {
        frame.m_draws.Clear();
        frame.m_view = swarm::ViewParams{};
        frame.m_lighting = swarm::LightingParams{};
        frame.m_aspect = 1.f;
        frame.m_timeSeconds = 0.f;
        frame.m_deltaSeconds = 0.f;
        frame.m_hasCamera = false;
        frame.m_resizePending = false;
        frame.m_resizeWidth = 0;
        frame.m_resizeHeight = 0;
        frame.m_terminationRequested = false;
    }
} // namespace waggle
