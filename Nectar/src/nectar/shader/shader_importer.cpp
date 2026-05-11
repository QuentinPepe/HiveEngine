#include <wax/containers/string.h>

#include <nectar/shader/shader_importer.h>

#include <cstring>

namespace nectar
{
    namespace
    {
        uint64_t Fnv1a64(const uint8_t* data, size_t size) noexcept
        {
            constexpr uint64_t kOffset = 0xCBF29CE484222325ull;
            constexpr uint64_t kPrime = 0x00000100000001B3ull;
            uint64_t hash = kOffset;
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= data[i];
                hash *= kPrime;
            }
            return hash;
        }

        bool EndsWith(wax::StringView haystack, wax::StringView suffix) noexcept
        {
            if (haystack.Size() < suffix.Size())
                return false;
            return std::memcmp(haystack.Data() + haystack.Size() - suffix.Size(), suffix.Data(), suffix.Size()) == 0;
        }

        ShaderStage StageFromSourceName(wax::StringView name) noexcept
        {
            if (EndsWith(name, ".vs.hlsl"))
                return ShaderStage::Vertex;
            if (EndsWith(name, ".ps.hlsl"))
                return ShaderStage::Pixel;
            if (EndsWith(name, ".cs.hlsl"))
                return ShaderStage::Compute;
            return ShaderStage::Vertex;
        }

        ShaderStage StageFromSettings(const HiveDocument& settings, ShaderStage fallback) noexcept
        {
            const wax::StringView stageStr = settings.GetString("import", "stage");
            if (stageStr.IsEmpty())
                return fallback;
            if (stageStr == wax::StringView{"vertex"} || stageStr == wax::StringView{"vs"})
                return ShaderStage::Vertex;
            if (stageStr == wax::StringView{"pixel"} || stageStr == wax::StringView{"ps"} ||
                stageStr == wax::StringView{"fragment"})
                return ShaderStage::Pixel;
            if (stageStr == wax::StringView{"compute"} || stageStr == wax::StringView{"cs"})
                return ShaderStage::Compute;
            return fallback;
        }
    } // namespace

    wax::Span<const char* const> ShaderImporter::SourceExtensions() const
    {
        static const char* const kExtensions[] = {".hlsl"};
        return {kExtensions, 1};
    }

    uint32_t ShaderImporter::Version() const
    {
        return 1;
    }

    wax::StringView ShaderImporter::TypeName() const
    {
        return "Shader";
    }

    ImportResult ShaderImporter::Import(wax::ByteSpan sourceData, const HiveDocument& settings, ImportContext& context)
    {
        ImportResult result{};

        if (sourceData.IsEmpty())
        {
            result.m_errorMessage = wax::String{"Empty shader source"};
            return result;
        }

        const ShaderStage stage = StageFromSettings(settings, StageFromSourceName(context.SourcePath()));
        const wax::StringView entryView = settings.GetString("import", "entry");
        const wax::StringView entry = entryView.IsEmpty() ? wax::StringView{"main"} : entryView;

        NsdrHeader header{};
        header.m_stage = stage;
        header.m_payloadKind = ShaderPayloadKind::HlslSource;
        header.m_sourceHash = Fnv1a64(sourceData.Data(), sourceData.Size());

        const size_t headerSize = sizeof(NsdrHeader);
        const uint32_t entryOffset = static_cast<uint32_t>(headerSize);
        // m_entryLength excludes the trailing '\0' that we write so callers can treat the
        // pointer as a C string.
        const uint32_t entryLength = static_cast<uint32_t>(entry.Size());
        const uint32_t payloadOffset = entryOffset + entryLength + 1;
        const uint32_t payloadLength = static_cast<uint32_t>(sourceData.Size());

        header.m_entryOffset = entryOffset;
        header.m_entryLength = entryLength;
        header.m_payloadOffset = payloadOffset;
        header.m_payloadLength = payloadLength;

        const size_t totalSize = headerSize + entryLength + 1 + payloadLength;
        result.m_intermediateData.Resize(totalSize);
        uint8_t* blob = result.m_intermediateData.Data();

        std::memcpy(blob, &header, headerSize);
        std::memcpy(blob + entryOffset, entry.Data(), entryLength);
        blob[entryOffset + entryLength] = 0;
        std::memcpy(blob + payloadOffset, sourceData.Data(), payloadLength);

        result.m_success = true;
        return result;
    }
} // namespace nectar
