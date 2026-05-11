#pragma once

namespace swarm
{
    struct RenderContext;
}

namespace waggle
{
    class RenderModule;

    // World resource that exposes the render module + raw render context to gameplay/systems.
    // Inserted by the engine the first time the renderer is created; absent in headless mode.
    struct RenderResource
    {
        RenderModule* m_module{nullptr};
        swarm::RenderContext* m_context{nullptr};
    };
} // namespace waggle
