#pragma once

#include <comb/new.h>

#include <nectar/hive/hive_parser.h>
#include <nectar/material/material_data.h>
#include <nectar/server/asset_loader.h>

#include <cstring>

namespace nectar
{
    struct MaterialAsset
    {
        explicit MaterialAsset(comb::DefaultAllocator& alloc)
            : m_data{alloc}
        {
        }

        MaterialData m_data;
    };

    class MaterialAssetLoader final : public AssetLoader<MaterialAsset>
    {
    public:
        [[nodiscard]] MaterialAsset* Load(wax::ByteSpan data, comb::DefaultAllocator& alloc) override
        {
            wax::StringView content{reinterpret_cast<const char*>(data.Data()), data.Size()};
            auto parseResult = HiveParser::Parse(content, alloc);
            if (!parseResult.Success())
                return nullptr;

            auto* asset = comb::New<MaterialAsset>(alloc, alloc);
            const auto& doc = parseResult.m_document;

            asset->m_data.m_shaderPath = wax::String{alloc, doc.GetString("material", "shader")};

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
                        asset->m_data.m_paramOverrides.Insert(wax::String{alloc, kv.Key().View()},
                                                              static_cast<HiveValue&&>(copy));
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
                        asset->m_data.m_textureBindings.Insert(wax::String{alloc, kv.Key().View()}, id);
                    }
                }
                else if (sectionName == kFeaturesSection)
                {
                    for (auto kv = it.Value().Begin(); kv != it.Value().End(); ++kv)
                    {
                        if (kv.Value().m_type != HiveValue::Type::BOOL)
                            continue;
                        asset->m_data.m_featureOverrides.Insert(wax::String{alloc, kv.Key().View()},
                                                                kv.Value().AsBool());
                    }
                }
            }

            return asset;
        }

        void Unload(MaterialAsset* asset, comb::DefaultAllocator& alloc) override
        {
            if (asset)
                comb::Delete(alloc, asset);
        }

        [[nodiscard]] size_t SizeOf(const MaterialAsset*) const override
        {
            return sizeof(MaterialAsset);
        }
    };

} // namespace nectar
