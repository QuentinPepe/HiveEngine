#pragma once

#include <comb/default_allocator.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/string.h>

#include <nectar/core/asset_id.h>
#include <nectar/hive/hive_value.h>

namespace nectar
{
    struct MaterialData
    {
        explicit MaterialData(comb::DefaultAllocator& alloc)
            : m_shaderPath{alloc}
            , m_paramOverrides{alloc, 8}
            , m_textureBindings{alloc, 8}
            , m_featureOverrides{alloc, 4}
        {
        }

        wax::String m_shaderPath;
        wax::HashMap<wax::String, HiveValue> m_paramOverrides;
        wax::HashMap<wax::String, AssetId> m_textureBindings;
        wax::HashMap<wax::String, bool> m_featureOverrides;
    };
} // namespace nectar
