#pragma once

#include <hive/hive_config.h>

#include <nectar/pipeline/asset_importer.h>
#include <nectar/shader/shader_data.h>

namespace nectar
{
    // Imports HLSL shader sources (.vs.hlsl, .ps.hlsl, .cs.hlsl) into NSDR intermediate.
    // Settings (optional, [import] section): entry="main", stage="vertex"|"pixel"|"compute".
    // Without an explicit stage, the filename suffix is used.
    class HIVE_API ShaderImporter final : public AssetImporter<NsdrHeader>
    {
    public:
        wax::Span<const char* const> SourceExtensions() const override;
        uint32_t Version() const override;
        wax::StringView TypeName() const override;

        ImportResult Import(wax::ByteSpan sourceData, const HiveDocument& settings, ImportContext& context) override;
    };
} // namespace nectar
