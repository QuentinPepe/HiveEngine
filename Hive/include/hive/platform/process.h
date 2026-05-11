#pragma once

#include <hive/hive_config.h>

#include <cstddef>

namespace hive
{
    // Writes the absolute path of the running executable into `out` (UTF-8, null-terminated).
    // Returns false if the path can't be retrieved or the buffer is too small.
    // Caller-provided buffer keeps this header dependency-free (no <filesystem>, no allocators).
    HIVE_API bool GetExecutablePath(char* out, size_t outSize) noexcept;
} // namespace hive
