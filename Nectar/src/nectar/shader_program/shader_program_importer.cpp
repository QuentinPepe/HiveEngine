#include <wax/containers/string.h>

#include <nectar/hive/hive_document.h>
#include <nectar/hive/hive_parser.h>
#include <nectar/pipeline/import_context.h>
#include <nectar/shader_program/shader_program_importer.h>

#include <cstring>

namespace nectar
{
    wax::Span<const char* const> ShaderProgramImporter::SourceExtensions() const
    {
        static const char* const kExtensions[] = {".hshader"};
        return {kExtensions, 1};
    }

    uint32_t ShaderProgramImporter::Version() const
    {
        return 1;
    }

    wax::StringView ShaderProgramImporter::TypeName() const
    {
        return "ShaderProgram";
    }

    ImportResult ShaderProgramImporter::Import(wax::ByteSpan sourceData, const HiveDocument& /*settings*/,
                                               ImportContext& context)
    {
        ImportResult result{};
        comb::DefaultAllocator& alloc = context.GetAllocator();

        if (sourceData.IsEmpty())
        {
            result.m_errorMessage = wax::String{alloc, "Empty .hshader source"};
            return result;
        }

        const wax::StringView manifestText{reinterpret_cast<const char*>(sourceData.Data()), sourceData.Size()};
        auto parsed = HiveParser::Parse(manifestText, alloc);
        if (!parsed.m_errors.IsEmpty())
        {
            result.m_errorMessage = wax::String{alloc, "Invalid .hshader manifest"};
            return result;
        }

        const wax::StringView vsPath = parsed.m_document.GetString("shader", "vertex");
        const wax::StringView psPath = parsed.m_document.GetString("shader", "pixel");
        if (vsPath.IsEmpty() || psPath.IsEmpty())
        {
            result.m_errorMessage = wax::String{alloc, "[shader] section missing 'vertex' or 'pixel' path"};
            return result;
        }

        // Declare deps so the asset DB knows this program rebuilds when its HLSL sources change.
        const AssetId vsId = context.ResolveByPath(vsPath);
        if (vsId != AssetId::Invalid())
            context.DeclareHardDep(vsId);
        const AssetId psId = context.ResolveByPath(psPath);
        if (psId != AssetId::Invalid())
            context.DeclareHardDep(psId);

        const size_t headerSize = sizeof(NshpHeader);
        const uint32_t manifestOffset = static_cast<uint32_t>(headerSize);
        const uint32_t manifestLength = static_cast<uint32_t>(sourceData.Size());

        NshpHeader header{};
        header.m_manifestOffset = manifestOffset;
        header.m_manifestLength = manifestLength;

        const size_t totalSize = headerSize + manifestLength;
        result.m_intermediateData.Resize(totalSize);
        uint8_t* blob = result.m_intermediateData.Data();
        std::memcpy(blob, &header, headerSize);
        std::memcpy(blob + manifestOffset, sourceData.Data(), manifestLength);

        result.m_success = true;
        return result;
    }
} // namespace nectar
