#pragma once

#include <hive/hive_config.h>

#include <comb/default_allocator.h>

#include <wax/containers/string_view.h>
#include <wax/serialization/byte_span.h>

#include <nectar/pak/npak_format.h>

namespace waggle
{
    class ProjectManager;
}

namespace waggle::scene
{
    struct ReachableAssets;
}

namespace waggle
{
    struct ShipPakInput
    {
        const scene::ReachableAssets* m_assets{nullptr};
        ProjectManager* m_project{nullptr};
        wax::StringView m_outputPath{};
        wax::ByteSpan m_projectTomlBlob{};
        nectar::CompressionMethod m_compression{nectar::CompressionMethod::LZ4};
    };

    struct ShipPakStats
    {
        size_t m_blobCount{0};
        size_t m_skippedCount{0};
        size_t m_uncompressedBytes{0};
    };

    using ShipPakProgressFn = void (*)(uint32_t current, uint32_t total, const char* entryName, void* userdata);

    [[nodiscard]] HIVE_API bool WriteShipPak(const ShipPakInput& input, ShipPakStats* stats,
                                             ShipPakProgressFn progress, void* userdata,
                                             comb::DefaultAllocator& alloc);
} // namespace waggle
