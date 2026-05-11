#pragma once

#include <cstdint>

namespace nectar
{
    enum class ShaderStage : uint8_t
    {
        Vertex = 0,
        Pixel = 1,
        Compute = 2,
    };

    enum class ShaderPayloadKind : uint8_t
    {
        HlslSource = 0, // raw HLSL text — Diligent recompiles per backend at runtime
        Spirv = 1,      // reserved (Phase 8)
        Dxil = 2,       // reserved (Phase 8)
    };

    // NSDR intermediate format (Nectar Shader).
    // Layout: NsdrHeader, then [entry chars][payload bytes] back-to-back.
    static constexpr uint32_t kNsdrMagic = 0x5244534E; // "NSDR" in little-endian

    struct NsdrHeader
    {
        uint32_t m_magic{kNsdrMagic};
        uint32_t m_version{1};
        ShaderStage m_stage{ShaderStage::Vertex};
        ShaderPayloadKind m_payloadKind{ShaderPayloadKind::HlslSource};
        uint8_t m_padding[2]{};
        uint32_t m_entryOffset{0};
        uint32_t m_entryLength{0};
        uint32_t m_payloadOffset{0};
        uint32_t m_payloadLength{0};
        uint64_t m_sourceHash{0};
    };
} // namespace nectar
