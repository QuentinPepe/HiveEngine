#include <nectar/core/asset_id.h>
#include <nectar/core/file_util.h>
#include <nectar/hive/hive_document.h>
#include <nectar/hive/hive_parser.h>
#include <nectar/hive/hive_writer.h>
#include <nectar/material/material_serializer.h>

#include <cstdio>

namespace nectar
{
    bool SaveMaterial(const MaterialData& mat, wax::StringView path, comb::DefaultAllocator& alloc)
    {
        HiveDocument doc{alloc};

        doc.SetValue("material", "shader", HiveValue::MakeString(alloc, mat.m_shaderPath.View()));

        for (auto it = mat.m_paramOverrides.Begin(); it != mat.m_paramOverrides.End(); ++it)
        {
            HiveValue copy = it.Value();
            doc.SetValue("parameters", it.Key().View(), static_cast<HiveValue&&>(copy));
        }

        for (auto it = mat.m_textureBindings.Begin(); it != mat.m_textureBindings.End(); ++it)
        {
            char guid[35];
            it.Value().ToGuidString(guid);
            doc.SetValue("textures", it.Key().View(), HiveValue::MakeString(alloc, wax::StringView{guid, 34}));
        }

        for (auto it = mat.m_featureOverrides.Begin(); it != mat.m_featureOverrides.End(); ++it)
        {
            doc.SetValue("features", it.Key().View(), HiveValue::MakeBool(it.Value()));
        }

        wax::String content = HiveWriter::Write(doc, alloc);
        wax::String filePath{alloc, path};

        FILE* file = std::fopen(filePath.CStr(), "wb");
        if (!file)
            return false;

        if (content.Size() > 0)
            std::fwrite(content.CStr(), 1, content.Size(), file);

        std::fclose(file);
        return true;
    }

    bool LoadMaterial(MaterialData& mat, wax::StringView path, comb::DefaultAllocator& alloc)
    {
        wax::String filePath{alloc, path};
        FILE* file = std::fopen(filePath.CStr(), "rb");
        if (!file)
            return false;

        const int64_t fileSize = FileSize(file);
        if (fileSize <= 0)
        {
            std::fclose(file);
            return false;
        }

        wax::String content{alloc};
        content.Reserve(static_cast<size_t>(fileSize));

        char buffer[4096];
        size_t remaining = static_cast<size_t>(fileSize);
        while (remaining > 0)
        {
            const size_t toRead = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            const size_t bytesRead = std::fread(buffer, 1, toRead, file);
            if (bytesRead == 0)
                break;
            content.Append(buffer, bytesRead);
            remaining -= bytesRead;
        }
        std::fclose(file);

        auto parseResult = HiveParser::Parse(content.View(), alloc);
        if (!parseResult.Success())
            return false;

        const auto& doc = parseResult.m_document;

        mat.m_shaderPath = wax::String{alloc, doc.GetString("material", "shader")};

        constexpr wax::StringView kParametersSection{"parameters"};
        constexpr wax::StringView kTexturesSection{"textures"};
        constexpr wax::StringView kFeaturesSection{"features"};

        for (auto it = doc.Sections().Begin(); it != doc.Sections().End(); ++it)
        {
            const wax::StringView sectionName = it.Key().View();
            if (sectionName == kParametersSection)
            {
                for (auto kv = it.Value().Begin(); kv != it.Value().End(); ++kv)
                {
                    HiveValue copy = kv.Value();
                    mat.m_paramOverrides.Insert(wax::String{alloc, kv.Key().View()}, static_cast<HiveValue&&>(copy));
                }
            }
            else if (sectionName == kTexturesSection)
            {
                for (auto kv = it.Value().Begin(); kv != it.Value().End(); ++kv)
                {
                    if (kv.Value().m_type != HiveValue::Type::STRING)
                        continue;
                    const wax::StringView guid = kv.Value().AsString();
                    const AssetId id = AssetId::FromGuidString(guid.Data(), guid.Size());
                    if (!id.IsValid())
                        continue;
                    mat.m_textureBindings.Insert(wax::String{alloc, kv.Key().View()}, id);
                }
            }
            else if (sectionName == kFeaturesSection)
            {
                for (auto kv = it.Value().Begin(); kv != it.Value().End(); ++kv)
                {
                    if (kv.Value().m_type != HiveValue::Type::BOOL)
                        continue;
                    mat.m_featureOverrides.Insert(wax::String{alloc, kv.Key().View()}, kv.Value().AsBool());
                }
            }
        }

        return true;
    }
} // namespace nectar
