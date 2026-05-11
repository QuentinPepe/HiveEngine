#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace hive::core
{
    // Cross-platform safe wrapper for environment variable reads. Copies the value into
    // the caller-provided buffer; returns false if the variable is unset or the buffer
    // is too small. Uses getenv_s on MSVC so we don't hit the _CRT_SECURE deprecation.
    inline bool ReadEnvVar(const char* name, char* out, size_t outSize) noexcept
    {
        if (out == nullptr || outSize == 0)
        {
            return false;
        }
        out[0] = '\0';
#if defined(_MSC_VER)
        size_t valueSize = 0;
        if (getenv_s(&valueSize, out, outSize, name) != 0 || valueSize == 0)
        {
            return false;
        }
        return true;
#else
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            return false;
        }
        const size_t length = std::strlen(value);
        if (length + 1 > outSize)
        {
            return false;
        }
        std::memcpy(out, value, length + 1);
        return true;
#endif
    }
} // namespace hive::core
