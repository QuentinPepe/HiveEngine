#pragma once

#include <hive/hive_config.h>

#include <comb/default_allocator.h>

#include <wax/containers/string.h>
#include <wax/containers/string_view.h>
#include <wax/containers/vector.h>

#include <queen/reflect/component_registry.h>

#include <nectar/core/asset_id.h>

namespace waggle
{
    class ProjectManager;
}

namespace waggle::scene
{
    struct ReachableAssets
    {
        explicit ReachableAssets(comb::DefaultAllocator& alloc)
            : m_vfsPaths{alloc}
            , m_cookedIds{alloc}
        {
        }

        wax::Vector<wax::String> m_vfsPaths;
        wax::Vector<nectar::AssetId> m_cookedIds;
    };

    HIVE_API ReachableAssets CollectFromScene(queen::ComponentRegistry<256>& registry,
                                              ProjectManager& project,
                                              wax::StringView sceneRelativePath,
                                              comb::DefaultAllocator& alloc);
}
