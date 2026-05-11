#pragma once

#include <cstdint>

namespace nectar
{
    enum class VertexDomain : uint8_t
    {
        StaticMesh = 0,
        SkinnedMesh = 1,
        UI = 2,
    };

    enum class ShadingModel : uint8_t
    {
        Unlit = 0,
        Standard = 1,
    };

    // NSHP layout: NshpHeader, then the .hshader manifest text — the loader re-parses it
    // so new manifest keys don't require bumping the header version.
    static constexpr uint32_t kNshpMagic = 0x5048534E; // "NSHP" in little-endian

    struct NshpHeader
    {
        uint32_t m_magic{kNshpMagic};
        uint32_t m_version{1};
        uint32_t m_manifestOffset{0};
        uint32_t m_manifestLength{0};
    };
} // namespace nectar
