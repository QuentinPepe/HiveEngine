#include <hive/platform/process.h>

#include <unistd.h>

namespace hive
{
    bool GetExecutablePath(char* out, size_t outSize) noexcept
    {
        if (out == nullptr || outSize == 0)
        {
            return false;
        }
        const ssize_t n = readlink("/proc/self/exe", out, outSize - 1);
        if (n <= 0 || static_cast<size_t>(n) >= outSize)
        {
            out[0] = '\0';
            return false;
        }
        out[n] = '\0';
        return true;
    }
} // namespace hive
