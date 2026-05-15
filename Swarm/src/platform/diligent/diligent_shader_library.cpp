#include <diligent_shader_library.h>

#include <swarm/swarm_log.h>

#include <hive/core/log.h>

#include <ArchiverFactoryLoader.h>
#include <DataBlobImpl.hpp>
#include <DefaultShaderSourceStreamFactory.h>
#include <FileWrapper.hpp>
#include <RenderStateCache.h>
#include <ShaderSourceFactoryUtils.h>

#include <cstdio>
#include <cstring>

namespace swarm
{
    namespace
    {
        using namespace Diligent;

        IArchiverFactory* GetSharedArchiverFactory()
        {
            // Single global factory; Diligent guarantees it is thread-safe and lives forever.
            static IArchiverFactory* s_factory = LoadAndGetArchiverFactory();
            return s_factory;
        }

        // Read whole file into a heap buffer. Returns nullptr on error.
        FILE* OpenFile(const char* path, const char* mode)
        {
            FILE* file = nullptr;
#if defined(_MSC_VER)
            fopen_s(&file, path, mode);
#else
            file = std::fopen(path, mode);
#endif
            return file;
        }
    } // namespace

    ShaderLibrary::~ShaderLibrary()
    {
        Shutdown();
    }

    bool ShaderLibrary::Initialize(IRenderDevice* device)
    {
        if (device == nullptr)
        {
            return false;
        }
        m_device = device;

        IArchiverFactory* archiverFactory = GetSharedArchiverFactory();
        if (archiverFactory == nullptr)
        {
            hive::LogError(LOG_SWARM, "ShaderLibrary: archiver factory unavailable; PSO cache disabled");
            return false;
        }

        RenderStateCacheCreateInfo cacheInfo{};
        cacheInfo.pDevice = device;
        cacheInfo.pArchiverFactory = archiverFactory;
        cacheInfo.LogLevel = RENDER_STATE_CACHE_LOG_LEVEL_NORMAL;
        cacheInfo.FileHashMode = RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT;
        cacheInfo.EnableHotReload = true;

        CreateRenderStateCache(cacheInfo, &m_cache);
        if (m_cache == nullptr)
        {
            hive::LogError(LOG_SWARM, "ShaderLibrary: failed to create IRenderStateCache");
            return false;
        }

        RebuildCompoundFactory();
        return true;
    }

    void ShaderLibrary::Shutdown()
    {
        if (!m_diskCachePath.IsEmpty() && m_cache != nullptr)
        {
            SaveDiskCache();
        }
        m_compoundFactory.Release();
        for (uint32_t i = 0; i < kMaxSearchPaths; ++i)
        {
            m_pathFactories[i].Release();
            m_searchPaths[i].Clear();
        }
        m_searchPathCount = 0;
        m_cache.Release();
        m_diskCachePath.Clear();
        m_diskCacheLoaded = false;
        m_device = nullptr;
        m_reloadPending.store(false, std::memory_order_release);
    }

    void ShaderLibrary::AddSearchPath(const char* directory)
    {
        if (directory == nullptr || directory[0] == '\0')
        {
            return;
        }
        for (uint32_t i = 0; i < m_searchPathCount; ++i)
        {
            if (std::strcmp(m_searchPaths[i].CStr(), directory) == 0)
            {
                return;
            }
        }
        if (m_searchPathCount >= kMaxSearchPaths)
        {
            hive::LogWarning(LOG_SWARM, "ShaderLibrary: search path limit ({}) reached, dropping '{}'",
                             kMaxSearchPaths, directory);
            return;
        }
        m_searchPaths[m_searchPathCount].Clear();
        m_searchPaths[m_searchPathCount].Append(directory, std::strlen(directory));
        ++m_searchPathCount;
        RebuildCompoundFactory();
    }

    void ShaderLibrary::SetDiskCachePath(const char* path)
    {
        m_diskCachePath.Clear();
        if (path != nullptr && path[0] != '\0')
        {
            m_diskCachePath.Append(path, std::strlen(path));
        }
    }

    bool ShaderLibrary::LoadDiskCache()
    {
        if (m_cache == nullptr || m_diskCachePath.IsEmpty())
        {
            return false;
        }

        FILE* file = OpenFile(m_diskCachePath.CStr(), "rb");
        if (file == nullptr)
        {
            return false;
        }
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (size <= 0)
        {
            std::fclose(file);
            return false;
        }

        auto blob = DataBlobImpl::Create(static_cast<size_t>(size));
        const size_t read = std::fread(blob->GetDataPtr(), 1, static_cast<size_t>(size), file);
        std::fclose(file);
        if (read != static_cast<size_t>(size))
        {
            hive::LogWarning(LOG_SWARM, "ShaderLibrary: short read on cache '{}'", m_diskCachePath.CStr());
            return false;
        }

        const bool ok = m_cache->Load(blob, ~0u, false);
        if (ok)
        {
            m_diskCacheLoaded = true;
        }
        else
        {
            hive::LogWarning(LOG_SWARM, "ShaderLibrary: cache rejected (version mismatch?) '{}'",
                             m_diskCachePath.CStr());
        }
        return ok;
    }

    bool ShaderLibrary::SaveDiskCache()
    {
        if (m_cache == nullptr || m_diskCachePath.IsEmpty())
        {
            hive::LogWarning(LOG_SWARM, "SaveDiskCache skipped: cache={} path={}",
                             (m_cache != nullptr) ? "ok" : "null",
                             m_diskCachePath.IsEmpty() ? "empty" : m_diskCachePath.CStr());
            return false;
        }

        RefCntAutoPtr<IDataBlob> blob;
        const bool writeOk = m_cache->WriteToBlob(~0u, &blob);
        if (!writeOk || blob == nullptr)
        {
            hive::LogWarning(LOG_SWARM, "SaveDiskCache failed: WriteToBlob returned={}, blob={}",
                             writeOk ? "true" : "false", (blob != nullptr) ? "non-null" : "null");
            return false;
        }
        hive::LogInfo(LOG_SWARM, "SaveDiskCache: writing {} bytes", blob->GetSize());

        FILE* file = OpenFile(m_diskCachePath.CStr(), "wb");
        if (file == nullptr)
        {
            hive::LogWarning(LOG_SWARM, "ShaderLibrary: cannot open cache '{}' for write",
                             m_diskCachePath.CStr());
            return false;
        }
        const size_t written = std::fwrite(blob->GetConstDataPtr(), 1, blob->GetSize(), file);
        std::fclose(file);
        if (written != blob->GetSize())
        {
            hive::LogWarning(LOG_SWARM, "ShaderLibrary: short write to cache '{}'",
                             m_diskCachePath.CStr());
            return false;
        }
        return true;
    }

    uint32_t ShaderLibrary::ReloadIfDirty()
    {
        if (m_cache == nullptr)
        {
            return 0;
        }
        const Uint32 count = m_cache->Reload(nullptr, nullptr);
        if (count > 0)
        {
            hive::LogInfo(LOG_SWARM, "Hot-reloaded {} shader/PSO object(s)", count);
        }
        return count;
    }

    void ShaderLibrary::RebuildCompoundFactory()
    {
        m_compoundFactory.Release();
        for (uint32_t i = 0; i < kMaxSearchPaths; ++i)
        {
            m_pathFactories[i].Release();
        }

        uint32_t factoryCount = 0;
        // LIFO: iterate from most recent to oldest so newer paths win on conflicts.
        for (uint32_t i = m_searchPathCount; i-- > 0;)
        {
            RefCntAutoPtr<IShaderSourceInputStreamFactory> factory;
            CreateDefaultShaderSourceStreamFactory(m_searchPaths[i].CStr(), &factory);
            if (factory != nullptr)
            {
                m_pathFactories[factoryCount++] = std::move(factory);
            }
            else
            {
                hive::LogWarning(LOG_SWARM, "ShaderLibrary: failed to wrap search path '{}'",
                                 m_searchPaths[i].CStr());
            }
        }

        if (factoryCount == 0)
        {
            return;
        }

        IShaderSourceInputStreamFactory* rawFactories[kMaxSearchPaths]{};
        for (uint32_t i = 0; i < factoryCount; ++i)
        {
            rawFactories[i] = m_pathFactories[i].RawPtr();
        }

        CompoundShaderSourceFactoryCreateInfo compoundInfo;
        compoundInfo.ppFactories = rawFactories;
        compoundInfo.NumFactories = factoryCount;
        CreateCompoundShaderSourceFactory(compoundInfo, &m_compoundFactory);
    }
} // namespace swarm
