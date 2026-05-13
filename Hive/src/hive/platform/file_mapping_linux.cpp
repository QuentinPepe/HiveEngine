#include <hive/platform/file_mapping.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
        , m_fd{other.m_fd}
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_fd = -1;
    }

    FileMapping& FileMapping::operator=(FileMapping&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_data = other.m_data;
            m_size = other.m_size;
            m_fd = other.m_fd;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_fd = -1;
        }
        return *this;
    }

    bool FileMapping::Open(const char* path)
    {
        Close();

        int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return false;

        struct stat st{};
        if (::fstat(fd, &st) != 0)
        {
            ::close(fd);
            return false;
        }

        const auto fileSize = static_cast<size_t>(st.st_size);

        if (fileSize == 0)
        {
            m_fd = fd;
            m_data = reinterpret_cast<const std::byte*>(""); // non-null sentinel for empty file
            m_size = 0;
            g_mappingCount.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        void* view = ::mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
        if (view == MAP_FAILED)
        {
            ::close(fd);
            return false;
        }

        ::madvise(view, fileSize, MADV_SEQUENTIAL);

        m_fd = fd;
        m_data = static_cast<const std::byte*>(view);
        m_size = fileSize;

        g_mappingCount.fetch_add(1, std::memory_order_relaxed);
        g_totalBytes.fetch_add(m_size, std::memory_order_relaxed);
        return true;
    }

    void FileMapping::Close() noexcept
    {
        if (m_fd < 0 && m_data == nullptr)
            return;

        const size_t releasedSize = m_size;
        const bool wasMapped = m_data != nullptr;

        if (m_data && m_size > 0)
            ::munmap(const_cast<std::byte*>(m_data), m_size);
        if (m_fd >= 0)
            ::close(m_fd);

        m_data = nullptr;
        m_size = 0;
        m_fd = -1;

        if (wasMapped)
        {
            g_mappingCount.fetch_sub(1, std::memory_order_relaxed);
            g_totalBytes.fetch_sub(releasedSize, std::memory_order_relaxed);
        }
    }
} // namespace hive
