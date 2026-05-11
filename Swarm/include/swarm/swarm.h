#pragma once

#include <hive/hive_config.h>
#include <hive/math/types.h>

#include <cstdint>

namespace terra
{
    struct WindowContext;
}

namespace swarm
{
    struct RenderContext;
    struct Mesh;
    struct Material;

    // Layout must stay bit-compatible with nectar::MeshVertex.
    struct Vertex
    {
        float m_position[3];
        float m_normal[3];
        float m_tangent[4]; // xyz tangent + w bitangent sign
        float m_uv[2];
        uint32_t m_color;   // packed RGBA8, R in low byte
    };
    static_assert(sizeof(Vertex) == 52, "swarm::Vertex must be 52 bytes");

    struct Submesh
    {
        uint32_t m_indexOffset{0};
        uint32_t m_indexCount{0};
    };

    struct MeshDesc
    {
        const Vertex* m_vertices{nullptr};
        uint32_t m_vertexCount{0};
        const uint32_t* m_indices{nullptr};
        uint32_t m_indexCount{0};
        // If null, the whole index buffer is treated as a single submesh.
        const Submesh* m_submeshes{nullptr};
        uint32_t m_submeshCount{0};
        const char* m_debugName{nullptr};
    };

    struct ViewParams
    {
        hive::math::Mat4 m_view{hive::math::Mat4::Identity()};
        hive::math::Mat4 m_proj{hive::math::Mat4::Identity()};
        hive::math::Float3 m_eyeWorld{0.f, 0.f, 0.f};
    };

    struct LightingParams
    {
        hive::math::Float3 m_sunDirection{0.f, -1.f, 0.f}; // direction the light travels
        hive::math::Float3 m_sunColor{1.f, 1.f, 1.f};
        float m_sunIntensity{1.f};
        hive::math::Float3 m_ambientColor{0.1f, 0.1f, 0.1f};
    };

    HIVE_API bool InitSystem();
    HIVE_API void ShutdownSystem();

    HIVE_API RenderContext* CreateRenderContext(terra::WindowContext* window);
    HIVE_API void DestroyRenderContext(RenderContext* renderContext);

    HIVE_API void BeginFrame(RenderContext* renderContext);
    HIVE_API void EndFrame(RenderContext* renderContext);
    HIVE_API void WaitForIdle(RenderContext* renderContext);
    HIVE_API void ResizeSwapchain(RenderContext* renderContext, uint32_t width, uint32_t height);

    HIVE_API Mesh* CreateMesh(RenderContext* renderContext, const MeshDesc& descriptor);
    HIVE_API void DestroyMesh(Mesh* mesh);

    HIVE_API Material* CreateStandardMaterial(RenderContext* renderContext);
    HIVE_API void DestroyMaterial(Material* material);

    HIVE_API void SetView(RenderContext* renderContext, const ViewParams& view);
    HIVE_API void SetLighting(RenderContext* renderContext, const LightingParams& lighting);

    // Number of deferred contexts available for parallel command recording.
    // Workers can call the indexed DrawMesh overload with workerIndex < this value.
    HIVE_API uint32_t GetDeferredContextCount(const RenderContext* renderContext);

    // Single-thread overload: records into deferred context 0.
    // Must be called between BeginFrame and EndFrame so the render target is bound.
    HIVE_API void DrawMesh(RenderContext* renderContext, const Mesh* mesh, const Material* material,
                           const hive::math::Mat4& world);

    // Parallel overload: records into deferred context `workerIndex`. Workers calling this
    // concurrently must use distinct workerIndex values. The first call from worker N must
    // also bind the render target via PrepareWorkerFrame() — BeginFrame only binds worker 0.
    HIVE_API void DrawMesh(RenderContext* renderContext, uint32_t workerIndex, const Mesh* mesh,
                           const Material* material, const hive::math::Mat4& world);

    // Sets the render target on the given worker's deferred context. Required before the
    // worker draws if it is not worker 0 (BeginFrame already prepares worker 0).
    HIVE_API void PrepareWorkerFrame(RenderContext* renderContext, uint32_t workerIndex);

    struct ViewportRT;
    HIVE_API ViewportRT* CreateViewportRT(RenderContext* renderContext, uint32_t width, uint32_t height);
    HIVE_API void DestroyViewportRT(ViewportRT* viewportRT);
    HIVE_API void ResizeViewportRT(ViewportRT* viewportRT, uint32_t width, uint32_t height);
    HIVE_API uint32_t GetViewportRTWidth(const ViewportRT* viewportRT);
    HIVE_API uint32_t GetViewportRTHeight(const ViewportRT* viewportRT);
    HIVE_API void* GetViewportRTSRV(const ViewportRT* viewportRT);
    HIVE_API void BeginViewportRT(RenderContext* renderContext, ViewportRT* viewportRT);
    HIVE_API void EndViewportRT(RenderContext* renderContext, ViewportRT* viewportRT);
} // namespace swarm
