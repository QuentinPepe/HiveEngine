// Feature flags for Comb memory debugging. All features compile out when
// COMB_MEM_DEBUG=0 so release builds pay nothing. Enabling COMB_MEM_DEBUG=1
// is expected to be slow (2-10x); callstacks add another 10-100x.

#pragma once

#include <cstddef>
#include <cstdint>

// Build Configuration (Set by CMake)

// Default to disabled if not set by CMake
#ifndef COMB_MEM_DEBUG
#define COMB_MEM_DEBUG 0
#endif

#ifndef COMB_MEM_DEBUG_CALLSTACKS
#define COMB_MEM_DEBUG_CALLSTACKS 0
#endif

// Feature Flags

#if COMB_MEM_DEBUG

// Core Features (Always Enabled)

#define COMB_MEM_DEBUG_LEAK_DETECTION 1
#define COMB_MEM_DEBUG_DOUBLE_FREE 1
#define COMB_MEM_DEBUG_BUFFER_OVERRUN 1
#define COMB_MEM_DEBUG_TRACKING 1
#define COMB_MEM_DEBUG_STATS 1

// Optional Features (Can be Disabled for Performance)

// Fills freed memory with 0xFE so reads-after-free are visible.
#ifndef COMB_MEM_DEBUG_USE_AFTER_FREE
#define COMB_MEM_DEBUG_USE_AFTER_FREE 1
#endif

// Ring buffer of recent allocations for post-mortem analysis.
#ifndef COMB_MEM_DEBUG_HISTORY
#define COMB_MEM_DEBUG_HISTORY 1
#endif

#ifndef COMB_MEM_DEBUG_HISTORY_SIZE
#define COMB_MEM_DEBUG_HISTORY_SIZE 1000
#endif

// Callstack capture is defined by CMake (-DCOMB_ENABLE_CALLSTACKS=ON).
// Very expensive: only enable when debugging a specific leak.

// Debug Constants

#include <cstring> // For memcpy

namespace comb::debug
{
    // Guard magic value (0xDEADBEEF)
    constexpr uint32_t guardMagic = 0xDEADBEEF;

    // Memory patterns
    constexpr uint8_t allocatedMemoryPattern = 0xAA; // "Allocated" - 0b10101010
    constexpr uint8_t freedMemoryPattern = 0xFE;     // "Freed"     - 0b11111110
    constexpr uint8_t guardBytePattern = 0xBE;       // "BEef"      - 0b10111110

    // Guard size (before and after allocation)
    constexpr size_t guardSize = sizeof(uint32_t);   // 4 bytes
    constexpr size_t totalGuardSize = 2 * guardSize; // 8 bytes total

    // Callstack depth (if enabled)
    constexpr uint32_t maxCallstackDepth = 16;

    // Safe guard read/write (handles potentially misaligned back guards)
    inline void WriteGuard(void* addr) noexcept
    {
        uint32_t magic = guardMagic;
        std::memcpy(addr, &magic, sizeof(uint32_t));
    }

    inline uint32_t ReadGuard(const void* addr) noexcept
    {
        uint32_t value;
        std::memcpy(&value, addr, sizeof(uint32_t));
        return value;
    }
} // namespace comb::debug

#else // COMB_MEM_DEBUG = 0

// All Features Disabled (Zero Overhead)

#define COMB_MEM_DEBUG_LEAK_DETECTION 0
#define COMB_MEM_DEBUG_DOUBLE_FREE 0
#define COMB_MEM_DEBUG_BUFFER_OVERRUN 0
#define COMB_MEM_DEBUG_TRACKING 0
#define COMB_MEM_DEBUG_STATS 0
#define COMB_MEM_DEBUG_USE_AFTER_FREE 0
#define COMB_MEM_DEBUG_HISTORY 0
#define COMB_MEM_DEBUG_HISTORY_SIZE 0

// Callstack always disabled if MEM_DEBUG is off
#undef COMB_MEM_DEBUG_CALLSTACKS
#define COMB_MEM_DEBUG_CALLSTACKS 0

#endif // COMB_MEM_DEBUG

// Compile-Time Constants

namespace comb::debug
{
    // Mirrors of the preprocessor flags as constexpr bools, so callers can
    // gate code via `if constexpr` instead of #if.
    inline constexpr bool kMemDebugEnabled = (COMB_MEM_DEBUG != 0);
    inline constexpr bool kCallstacksEnabled = (COMB_MEM_DEBUG_CALLSTACKS != 0);
    inline constexpr bool kLeakDetectionEnabled = (COMB_MEM_DEBUG_LEAK_DETECTION != 0);
    inline constexpr bool kUseAfterFreeEnabled = (COMB_MEM_DEBUG_USE_AFTER_FREE != 0);
} // namespace comb::debug

// Static Assertions (Compile-Time Checks)

// Callstacks require MEM_DEBUG
static_assert(COMB_MEM_DEBUG_CALLSTACKS == 0 || COMB_MEM_DEBUG == 1,
              "COMB_MEM_DEBUG_CALLSTACKS requires COMB_MEM_DEBUG=1");

// Summary Log (Disabled - too verbose during compilation)
//
// Memory debugging configuration:
// - COMB_MEM_DEBUG: Enabled/Disabled
// - COMB_MEM_DEBUG_CALLSTACKS: Enabled/Disabled
// - COMB_MEM_DEBUG_HISTORY: Enabled/Disabled
//
// See CMakeLists.txt for build configuration
