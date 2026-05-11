#include <nectar/core/asset_id.h>
#include <nectar/hive/hive_document.h>
#include <nectar/hive/hive_parser.h>
#include <nectar/material/material_importer.h>
#include <nectar/pipeline/import_context.h>

namespace nectar
{
    wax::Span<const char* const> MaterialImporter::SourceExtensions() const
    {
        static const char* const kExts[] = {".hmat"};
        return {kExts, 1};
    }

    uint32_t MaterialImporter::Version() const
    {
        return 2;
    }

    wax::StringView MaterialImporter::TypeName() const
    {
        return "Material";
    }

    ImportResult MaterialImporter::Import(wax::ByteSpan sourceData, const HiveDocument&, ImportContext& context)
    {
        ImportResult result;
        auto& alloc = context.GetAllocator();

        wax::StringView content{reinterpret_cast<const char*>(sourceData.Data()), sourceData.Size()};
        auto parseResult = HiveParser::Parse(content, alloc);
        if (!parseResult.Success())
        {
            result.m_errorMessage = wax::String{alloc, wax::StringView{"Failed to parse .hmat"}};
            return result;
        }

        const auto& doc = parseResult.m_document;

        const wax::StringView shaderPath = doc.GetString("material", "shader");
        if (!shaderPath.IsEmpty())
        {
            const AssetId shaderId = context.ResolveByPath(shaderPath);
            if (shaderId.IsValid())
                context.DeclareHardDep(shaderId);
        }

        constexpr wax::StringView kTexturesSection{"textures"};
        for (auto it = doc.Sections().Begin(); it != doc.Sections().End(); ++it)
        {
            if (it.Key().View() != kTexturesSection)
                continue;
            for (auto kv = it.Value().Begin(); kv != it.Value().End(); ++kv)
            {
                if (kv.Value().m_type != HiveValue::Type::STRING)
                    continue;
                const wax::StringView guid = kv.Value().AsString();
                const AssetId id = AssetId::FromGuidString(guid.Data(), guid.Size());
                if (id.IsValid())
                    context.DeclareHardDep(id);
            }
        }

        result.m_success = true;
        result.m_intermediateData = wax::ByteBuffer{alloc};
        result.m_intermediateData.Append(sourceData.Data(), sourceData.Size());
        return result;
    }
} // namespace nectar
