#pragma once

#include <swarm/swarm.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>

#include <atomic>

#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <RenderStateCache.h>
#include <Shader.h>

namespace swarm
{
    struct RenderContext;

    class ShaderLibrary
    {
    public:
        ShaderLibrary() = default;
        ~ShaderLibrary();

        ShaderLibrary(const ShaderLibrary&) = delete;
        ShaderLibrary& operator=(const ShaderLibrary&) = delete;

        bool Initialize(Diligent::IRenderDevice* device);
        void Shutdown();

        // LIFO precedence; project paths added later override engine paths.
        void AddSearchPath(const char* directory);

        void SetDiskCachePath(const char* path);
        bool LoadDiskCache();
        bool SaveDiskCache();

        Diligent::IRenderStateCache* GetCache() noexcept { return m_cache; }
        Diligent::IShaderSourceInputStreamFactory* GetSourceFactory() noexcept { return m_compoundFactory; }

        uint32_t ReloadIfDirty();

        void RequestReload() noexcept { m_reloadPending.store(true, std::memory_order_release); }
        bool ConsumeReloadRequest() noexcept { return m_reloadPending.exchange(false, std::memory_order_acq_rel); }

    private:
        void RebuildCompoundFactory();

        Diligent::IRenderDevice* m_device{nullptr};
        Diligent::RefCntAutoPtr<Diligent::IRenderStateCache> m_cache;

        static constexpr uint32_t kMaxSearchPaths = 8;
        wax::String m_searchPaths[kMaxSearchPaths];
        uint32_t m_searchPathCount{0};
        Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> m_pathFactories[kMaxSearchPaths];
        Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> m_compoundFactory;

        wax::String m_diskCachePath;
        bool m_diskCacheLoaded{false};

        std::atomic<bool> m_reloadPending{false};
    };
} // namespace swarm
