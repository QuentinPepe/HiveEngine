#include <hive/platform/process.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace hive
{
    bool GetExecutablePath(char* out, size_t outSize) noexcept
    {
        if (out == nullptr || outSize == 0)
        {
            return false;
        }
        const DWORD length = GetModuleFileNameA(nullptr, out, static_cast<DWORD>(outSize));
        if (length == 0 || length >= outSize)
        {
            out[0] = '\0';
            return false;
        }
        out[length] = '\0';
        return true;
    }
} // namespace hive
