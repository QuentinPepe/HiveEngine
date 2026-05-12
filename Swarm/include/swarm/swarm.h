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
    struct Texture;

    // Layout must stay bit-compatible with nectar::MeshVertex.
    struct Vertex
    {
        float m_position[3];
        float m_normal[3];
        float m_tangent[4]; // xyz tangent + w bitangent sign
        float m_uv[2];
        uint32_t m_color; // packed RGBA8, R in low byte
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

    // ---- Shader pipeline ------------------------------------------------------------------

    HIVE_API void AddShaderSearchPath(RenderContext* renderContext, const char* directory);

    struct ShaderMacro
    {
        const char* m_name{nullptr};
        const char* m_value{nullptr};
    };

    enum class TextureFormat : uint8_t
    {
        RGBA8_UNORM = 0,
        RGBA8_SRGB = 1,
        R8_UNORM = 2,
    };

    enum class SamplerFilter : uint8_t
    {
        NEAREST = 0,
        LINEAR = 1,
    };

    enum class SamplerAddress : uint8_t
    {
        WRAP = 0,
        CLAMP = 1,
    };

    struct SamplerDesc
    {
        SamplerFilter m_filter{SamplerFilter::LINEAR};
        SamplerAddress m_address{SamplerAddress::WRAP};
        bool m_mipmaps{true};
    };

    struct TextureMipUpload
    {
        uint32_t m_width{0};
        uint32_t m_height{0};
        const void* m_pixels{nullptr};
        uint32_t m_byteCount{0};
    };

    struct TextureDesc
    {
        const char* m_debugName{nullptr};
        TextureFormat m_format{TextureFormat::RGBA8_SRGB};
        uint32_t m_width{0};
        uint32_t m_height{0};
        const TextureMipUpload* m_mips{nullptr};
        uint32_t m_mipCount{0};
    };

    HIVE_API Texture* CreateTexture(RenderContext* renderContext, const TextureDesc& desc);
    HIVE_API void DestroyTexture(Texture* texture);

    // Slot 0 is reserved as "invalid" — shaders fall back to default white.
    struct TextureHandle
    {
        uint32_t m_index{0};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_index != 0;
        }
    };

    inline constexpr uint32_t kBindlessSlotInvalid = 0;
    inline constexpr uint32_t kBindlessSlotDefaultWhite = 1;
    inline constexpr uint32_t kBindlessSlotDefaultNormal = 2;
    inline constexpr uint32_t kBindlessHeapCapacity = 8192;

    HIVE_API TextureHandle RegisterTexture(RenderContext* renderContext, Texture* texture);
    HIVE_API void UnregisterTexture(RenderContext* renderContext, TextureHandle handle);
    HIVE_API void SetDefaultBindlessTexture(RenderContext* renderContext, uint32_t slot, Texture* texture);

    struct MaterialTextureBinding
    {
        const char* m_name{nullptr};
        Texture* m_texture{nullptr};
        SamplerDesc m_sampler{};
    };

    struct MaterialParamBinding
    {
        const char* m_name{nullptr};
        const void* m_data{nullptr};
        uint32_t m_size{0};
    };

    enum class VertexDomain : uint8_t
    {
        STATIC_MESH = 0,
        SKINNED_MESH = 1,
        UI = 2,
    };

    enum class CullMode : uint8_t
    {
        NONE = 0,
        FRONT,
        BACK,
    };

    enum class FillMode : uint8_t
    {
        SOLID = 0,
        WIREFRAME,
    };

    enum class BlendMode : uint8_t
    {
        OPAQUE_ = 0,
        ALPHA_BLEND,
        ADDITIVE,
    };

    // Shader source for a material stage. Set either m_bytes (asset-driven, preferred —
    // bytes must outlive the CreateMaterial call) or m_path (loose-file, dev-only).
    struct MaterialShaderRef
    {
        const uint8_t* m_bytes{nullptr};
        uint32_t m_byteCount{0};
        const char* m_entry{nullptr}; // optional override; falls back to MaterialDesc::m_*Entry
        const char* m_path{nullptr};
    };

    struct MaterialDesc
    {
        const char* m_debugName{nullptr};
        MaterialShaderRef m_vertexShader{};
        MaterialShaderRef m_pixelShader{};
        const char* m_vertexEntry{"main"};
        const char* m_pixelEntry{"main"};

        const ShaderMacro* m_macros{nullptr};
        uint32_t m_macroCount{0};

        const MaterialTextureBinding* m_textures{nullptr};
        uint32_t m_textureCount{0};

        const MaterialParamBinding* m_params{nullptr};
        uint32_t m_paramCount{0};

        VertexDomain m_domain{VertexDomain::STATIC_MESH};

        CullMode m_cullMode{CullMode::BACK};
        FillMode m_fillMode{FillMode::SOLID};
        BlendMode m_blendMode{BlendMode::OPAQUE_};
        bool m_depthTest{true};
        bool m_depthWrite{true};
        bool m_frontCCW{true};
    };

    HIVE_API Material* CreateMaterial(RenderContext* renderContext, const MaterialDesc& desc);
    HIVE_API void DestroyMaterial(RenderContext* renderContext, Material* material);

    // Hot-reload entry point. Drained by the render thread at the next BeginFrame.
    HIVE_API void RequestShaderReload(RenderContext* renderContext);

    // Persists the PSO cache to disk. Safe to call at any time; idempotent.
    HIVE_API bool FlushShaderCache(RenderContext* renderContext);

    HIVE_API void SetView(RenderContext* renderContext, const ViewParams& view);
    HIVE_API void SetLighting(RenderContext* renderContext, const LightingParams& lighting);
    HIVE_API void SetTime(RenderContext* renderContext, float seconds, float deltaSeconds);

    // Number of deferred contexts available for parallel command recording.
    // Workers can call the indexed DrawMesh overload with workerIndex < this value.
    HIVE_API uint32_t GetDeferredContextCount(const RenderContext* renderContext);

    // Single-thread overload: records into deferred context 0.
    // Must be called between BeginFrame and EndFrame so the render target is bound.
    // submeshIndex == -1 draws all submeshes; otherwise draws only that submesh.
    HIVE_API void DrawMesh(RenderContext* renderContext, const Mesh* mesh, const Material* material,
                           const hive::math::Mat4& world, int32_t submeshIndex = -1);

    // Parallel overload: records into deferred context `workerIndex`. Workers calling this
    // concurrently must use distinct workerIndex values. The first call from worker N must
    // also bind the render target via PrepareWorkerFrame() — BeginFrame only binds worker 0.
    HIVE_API void DrawMesh(RenderContext* renderContext, uint32_t workerIndex, const Mesh* mesh,
                           const Material* material, const hive::math::Mat4& world, int32_t submeshIndex = -1);

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
