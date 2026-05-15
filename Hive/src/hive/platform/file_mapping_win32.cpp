#include <hive/platform/file_mapping.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace hive
{
    namespace
    {
        std::atomic<size_t> g_mappingCount{0};
        std::atomic<size_t> g_totalBytes{0};
    } // namespace

    FileMappingStatsSnapshot GetFileMappingStats() noexcept
    {
        return FileMappingStatsSnapshot{g_mappingCount.load(std::memory_order_relaxed),
                                        g_totalBytes.load(std::memory_order_relaxed)};
    }

    FileMapping::~FileMapping()
    {
        Close();
    }

    FileMapping::FileMapping(FileMapping&& other) noexcept
        : m_data{other.m_data}
        , m_size{other.m_size}
        , m_fileHandle{other.m_fileHandle}
        , m_mappingHandle{other.m_mappingHandle}
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_fileHandle = nullptr;
        other.m_mappingHandle = nullptr;
    }

    FileMapping& FileMapping::operator=(FileMapping&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_data = other.m_data;
            m_size = other.m_size;
            m_fileHandle = other.m_fileHandle;
            m_mappingHandle = other.m_mappingHandle;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_fileHandle = nullptr;
            other.m_mappingHandle = nullptr;
        }
        return *this;
    }

    bool FileMapping::Open(const char* path)
    {
        Close();

        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file, &fileSize))
        {
            CloseHandle(file);
            return false;
        }

        if (fileSize.QuadPart == 0)
        {
            m_fileHandle = file;
            m_mappingHandle = nullptr;
            m_data = reinterpret_cast<const std::byte*>(""); // non-null sentinel for empty file
            m_size = 0;
            g_mappingCount.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping == nullptr)
        {
            CloseHandle(file);
            return false;
        }

        void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        if (view == nullptr)
        {
            CloseHandle(mapping);
            CloseHandle(file);
            return false;
        }

        m_fileHandle = file;
        m_mappingHandle = mapping;
        m_data = static_cast<const std::byte*>(view);
        m_size = static_cast<size_t>(fileSize.QuadPart);

        g_mappingCount.fetch_add(1, std::memory_order_relaxed);
        g_totalBytes.fetch_add(m_size, std::memory_order_relaxed);
        return true;
    }

    void FileMapping::Close() noexcept
    {
        if (m_fileHandle == nullptr && m_mappingHandle == nullptr && m_data == nullptr)
        {
            return;
        }

        const size_t releasedSize = m_size;
        const bool wasMapped = m_data != nullptr;

        if (m_mappingHandle != nullptr)
        {
            UnmapViewOfFile(m_data);
            CloseHandle(static_cast<HANDLE>(m_mappingHandle));
        }
        if (m_fileHandle != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(m_fileHandle));
        }

        m_data = nullptr;
        m_size = 0;
        m_fileHandle = nullptr;
        m_mappingHandle = nullptr;

        if (wasMapped)
        {
            g_mappingCount.fetch_sub(1, std::memory_order_relaxed);
            g_totalBytes.fetch_sub(releasedSize, std::memory_order_relaxed);
        }
    }
} // namespace hive
