#pragma once

#include <hive/hive_config.h>

#include <drone/job_submitter.h>

namespace queen
{
    class World;
}

namespace waggle
{
    class RenderModule;
    struct RenderFrame;

    // Game-thread step: reads ECS (camera, lights, mesh references) and populates the
    // RenderFrame draw list + view/lighting params. Resolves mesh assets via RenderModule's
    // cache. Does not touch any Swarm context — safe to run while a previous frame is being
    // executed on the render thread.
    HIVE_API void BuildRenderFrame(queen::World& world, RenderModule& renderModule, float aspect, RenderFrame& frame);

    // Render-thread step: replays a built RenderFrame into Swarm. Records draw commands across
    // deferred contexts when `submitter` is valid and the worker pool has enough capacity.
    HIVE_API void ExecuteRenderFrame(RenderModule& renderModule, const RenderFrame& frame,
                                     const drone::JobSubmitter& submitter = {});

    // Convenience: Build + Execute on the calling thread. Used by the legacy single-thread
    // path and by host apps that have not opted in to the render thread.
    HIVE_API void RenderMeshes(queen::World& world, RenderModule& renderModule, float aspect,
                               const drone::JobSubmitter& submitter = {});
} // namespace waggle
