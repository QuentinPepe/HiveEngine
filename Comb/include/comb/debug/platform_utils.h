// Cross-platform timestamps, thread IDs, and optional callstack capture
// for memory debugging. Compiled out when COMB_MEM_DEBUG=0.

#pragma once

#include <comb/debug/mem_debug_config.h>

#if COMB_MEM_DEBUG

#include <hive/core/assert.h>

#include <cinttypes>
#include <cstdint>
#include <cstdio>

// Platform includes (use Hive's platform detection macros)
#if HIVE_PLATFORM_WINDOWS
#include <Windows.h>
#include <intrin.h> // For __rdtsc
#elif HIVE_PLATFORM_LINUX || HIVE_PLATFORM_MACOS
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h> // For __rdtsc on GCC/Clang
#endif
#endif

// Callstack capture (optional)
#if COMB_MEM_DEBUG_CALLSTACKS
#if HIVE_PLATFORM_WINDOWS
#include <dbghelp.h>
#elif HIVE_PLATFORM_LINUX || HIVE_PLATFORM_MACOS
#include <execinfo.h>
#endif
#endif

namespace comb::debug
{

    // High-Resolution Timestamp

    // Monotonic nanosecond timestamp. Avoids __rdtsc() because it is not
    // portable to ARM and is affected by frequency scaling.
    inline uint64_t GetTimestamp() noexcept
    {
#if HIVE_PLATFORM_WINDOWS
        // Windows: Use QueryPerformanceCounter (QPC)
        LARGE_INTEGER counter, frequency;
        QueryPerformanceCounter(&counter);
        QueryPerformanceFrequency(&frequency);

        // Convert to nanoseconds: (counter * 1e9) / frequency
        return static_cast<uint64_t>((static_cast<uint64_t>(counter.QuadPart) * 1000000000ULL) /
                                     static_cast<uint64_t>(frequency.QuadPart));

#elif HIVE_PLATFORM_LINUX || HIVE_PLATFORM_MACOS
        // POSIX: Use clock_gettime (CLOCK_MONOTONIC)
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);

        // Convert to nanoseconds
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);

#else
#error "Unsupported platform for GetTimestamp()"
#endif
    }

// True on x86/x64, false on ARM/RISC-V. Gate GetCycleCounter() callers
// with `if constexpr (kHasCycleCounter)`.
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    inline constexpr bool kHasCycleCounter = true;
#else
    inline constexpr bool kHasCycleCounter = false;
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    inline uint64_t GetCycleCounter() noexcept
    {
        return __rdtsc();
    }
#endif

    // Thread ID

    // Numeric thread ID suitable for logging. Not the same as std::thread::id,
    // which is opaque and can't be printed directly.
    inline uint32_t GetThreadId() noexcept
    {
#if HIVE_PLATFORM_WINDOWS
        return static_cast<uint32_t>(GetCurrentThreadId());

#elif HIVE_PLATFORM_LINUX || HIVE_PLATFORM_MACOS
        // pthread_self() returns pthread_t (opaque type, often pointer-sized)
        // Cast to uintptr_t first, then truncate to uint32_t
        pthread_t tid = pthread_self();
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tid));

#else
#error "Unsupported platform for GetThreadId()"
#endif
    }

    // Callstack Capture (Optional, Expensive)

#if COMB_MEM_DEBUG_CALLSTACKS

    // Skips the first frame (this function itself). Very slow — only use when
    // chasing a specific leak.
    inline void CaptureCallstack(void** frames, uint32_t& depth) noexcept
    {
        hive::Assert(frames != nullptr, "frames must not be null");

#if HIVE_PLATFORM_WINDOWS
        // Windows: CaptureStackBackTrace (fast, kernel32.dll)
        // Skip 1 frame (this function)
        depth = static_cast<uint32_t>(CaptureStackBackTrace(1, maxCallstackDepth, frames, nullptr));

#elif HIVE_PLATFORM_LINUX || HIVE_PLATFORM_MACOS
        // POSIX: backtrace (requires linking with -rdynamic for symbols)
        int count = backtrace(frames, maxCallstackDepth);
        depth = count > 0 ? static_cast<uint32_t>(count) : 0;

        // Skip first frame (this function)
        if (depth > 0)
        {
            for (uint32_t i = 0; i < depth - 1; ++i)
            {
                frames[i] = frames[i + 1];
            }
            --depth;
        }

#else
#error "Unsupported platform for CaptureCallstack()"
#endif
    }

    // Requires dbghelp.lib + .pdb on Windows, -rdynamic on POSIX, otherwise
    // frames stay unresolved.
    void PrintCallstack(void* const* frames, uint32_t depth);

#endif // COMB_MEM_DEBUG_CALLSTACKS

    // Utility: Format Time Duration

    // Returns a thread-local buffer scaled to ns/us/ms/s.
    inline const char* FormatDuration(uint64_t nanos)
    {
        static thread_local char s_buffer[64];

        if (nanos < 1000)
        {
            snprintf(s_buffer, sizeof(s_buffer), "%" PRIu64 "ns", nanos);
        }
        else if (nanos < 1000000)
        {
            snprintf(s_buffer, sizeof(s_buffer), "%.1fµs", static_cast<double>(nanos) / 1000.0);
        }
        else if (nanos < 1000000000)
        {
            snprintf(s_buffer, sizeof(s_buffer), "%.1fms", static_cast<double>(nanos) / 1000000.0);
        }
        else
        {
            snprintf(s_buffer, sizeof(s_buffer), "%.1fs", static_cast<double>(nanos) / 1000000000.0);
        }

        return s_buffer;
    }

} // namespace comb::debug

#endif // COMB_MEM_DEBUG
