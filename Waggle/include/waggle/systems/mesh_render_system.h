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

    // Renders all entities matching <WorldMatrix, MeshReference> through the standard pipeline.
    // Picks the first <Transform, WorldMatrix, Camera> entity as the active camera, the first
    // DirectionalLight as the sun, and the first AmbientLight as the ambient term.
    // When `submitter` is valid, draw commands are recorded into Swarm deferred contexts in
    // parallel via ParallelFor; otherwise everything is recorded on deferred context 0.
    // No-op when no camera is found or when the render module is not ready.
    HIVE_API void RenderMeshes(queen::World& world, RenderModule& renderModule, float aspect,
                               const drone::JobSubmitter& submitter = {});
} // namespace waggle
