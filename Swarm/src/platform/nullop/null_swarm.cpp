#include <swarm/platform/null_swarm.h>

namespace swarm
{
    bool InitSystem()
    {
        return false;
    }

    void ShutdownSystem()
    {
    }

    RenderContext* CreateRenderContext(terra::WindowContext* /*window*/)
    {
        return nullptr;
    }

    void DestroyRenderContext(RenderContext* /*renderContext*/)
    {
    }

    void BeginFrame(RenderContext* /*ctx*/)
    {
    }
    void EndFrame(RenderContext* /*ctx*/)
    {
    }
    void WaitForIdle(RenderContext* /*ctx*/)
    {
    }
    void ResizeSwapchain(RenderContext* /*ctx*/, uint32_t /*width*/, uint32_t /*height*/)
    {
    }

    Mesh* CreateMesh(RenderContext* /*ctx*/, const MeshDesc& /*desc*/)
    {
        return nullptr;
    }
    void DestroyMesh(Mesh* /*mesh*/)
    {
    }

    Material* CreateMaterial(RenderContext* /*ctx*/, const MaterialDesc& /*desc*/)
    {
        return nullptr;
    }
    void DestroyMaterial(RenderContext* /*ctx*/, Material* /*material*/)
    {
    }

    void AddShaderSearchPath(RenderContext* /*ctx*/, const char* /*directory*/)
    {
    }
    void RequestShaderReload(RenderContext* /*ctx*/)
    {
    }
    bool FlushShaderCache(RenderContext* /*ctx*/)
    {
        return false;
    }

    void SetView(RenderContext* /*ctx*/, const ViewParams& /*view*/)
    {
    }
    void SetLighting(RenderContext* /*ctx*/, const LightingParams& /*lighting*/)
    {
    }
    void SetTime(RenderContext* /*ctx*/, float /*seconds*/, float /*deltaSeconds*/)
    {
    }
    void DrawMesh(RenderContext* /*ctx*/, const Mesh* /*mesh*/, const Material* /*material*/,
                  const hive::math::Mat4& /*world*/, int32_t /*submeshIndex*/)
    {
    }
    void DrawMesh(RenderContext* /*ctx*/, uint32_t /*workerIndex*/, const Mesh* /*mesh*/, const Material* /*material*/,
                  const hive::math::Mat4& /*world*/, int32_t /*submeshIndex*/)
    {
    }
    uint32_t GetDeferredContextCount(const RenderContext* /*ctx*/)
    {
        return 0;
    }
    void PrepareWorkerFrame(RenderContext* /*ctx*/, uint32_t /*workerIndex*/)
    {
    }

    ViewportRT* CreateViewportRT(RenderContext* /*ctx*/, uint32_t /*width*/, uint32_t /*height*/)
    {
        return nullptr;
    }

    void DestroyViewportRT(ViewportRT* /*rt*/)
    {
    }

    void ResizeViewportRT(ViewportRT* /*rt*/, uint32_t /*width*/, uint32_t /*height*/)
    {
    }

    uint32_t GetViewportRTWidth(const ViewportRT* /*rt*/)
    {
        return 0;
    }

    uint32_t GetViewportRTHeight(const ViewportRT* /*rt*/)
    {
        return 0;
    }

    void* GetViewportRTSRV(const ViewportRT* /*rt*/)
    {
        return nullptr;
    }

    void BeginViewportRT(RenderContext* /*ctx*/, ViewportRT* /*rt*/)
    {
    }
    void EndViewportRT(RenderContext* /*ctx*/, ViewportRT* /*rt*/)
    {
    }

    TextureHandle RegisterTexture(RenderContext* /*ctx*/, Texture* /*texture*/)
    {
        return TextureHandle{};
    }
    void UnregisterTexture(RenderContext* /*ctx*/, TextureHandle /*handle*/)
    {
    }
    void SetDefaultBindlessTexture(RenderContext* /*ctx*/, uint32_t /*slot*/, Texture* /*texture*/)
    {
    }
} // namespace swarm
