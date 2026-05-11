#pragma once

#include <hive/hive_config.h>

#include <nectar/pipeline/asset_importer.h>
#include <nectar/shader_program/shader_program_data.h>

namespace nectar
{
    class HIVE_API ShaderProgramImporter final : public AssetImporter<NshpHeader>
    {
    public:
        wax::Span<const char* const> SourceExtensions() const override;
        uint32_t Version() const override;
        wax::StringView TypeName() const override;

        ImportResult Import(wax::ByteSpan sourceData, const HiveDocument& settings, ImportContext& context) override;
    };
} // namespace nectar
