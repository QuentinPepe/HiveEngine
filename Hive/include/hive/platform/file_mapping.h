#pragma once

#include <hive/hive_config.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hive
{
    struct FileMappingStatsSnapshot
    {
        size_t m_count;
        size_t m_totalBytes;
    };

    [[nodiscard]] HIVE_API FileMappingStatsSnapshot GetFileMappingStats() noexcept;

    class HIVE_API FileMapping
    {
    public:
        FileMapping() = default;
        ~FileMapping();

        FileMapping(const FileMapping&) = delete;
        FileMapping& operator=(const FileMapping&) = delete;
        FileMapping(FileMapping&& other) noexcept;
        FileMapping& operator=(FileMapping&& other) noexcept;

        [[nodiscard]] bool Open(const char* path);
        void Close() noexcept;

        [[nodiscard]] const std::byte* Data() const noexcept
        {
            return m_data;
        }
        [[nodiscard]] size_t Size() const noexcept
        {
            return m_size;
        }
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_data != nullptr;
        }

    private:
        const std::byte* m_data{nullptr};
        size_t m_size{0};

#if HIVE_PLATFORM_WINDOWS
        void* m_fileHandle{nullptr};
        void* m_mappingHandle{nullptr};
#else
        int m_fd{-1};
#endif
    };
} // namespace hive
