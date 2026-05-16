#pragma once

#include <hive/hive_config.h>

#include <comb/default_allocator.h>

#include <wax/containers/string.h>
#include <wax/containers/string_view.h>

#include <queen/reflect/component_registry.h>

#include <cstdint>

namespace waggle
{
    class ProjectManager;

    enum class ShipStage : uint8_t
    {
        BUILDING_GAMEPLAY = 0,
        WALKING_SCENE,
        COOKING,
        WRITING_PAK,
        COPYING_RUNTIME,
        WRITING_PROJECT_FILE,
        DONE,
        FAILED,
    };

    struct ShipEvent
    {
        ShipStage m_stage{ShipStage::BUILDING_GAMEPLAY};
        uint32_t m_current{0};
        uint32_t m_total{0};
        const char* m_message{nullptr};
        bool m_isError{false};
    };

    struct ShipRequest
    {
        wax::StringView m_outputDir{};
        wax::StringView m_config{"Retail", 6};
        wax::StringView m_engineRoot{};
        bool m_buildGameplay{true};
        bool m_forceFreshCook{true};
    };

    using ShipEventFn = void (*)(const ShipEvent& event, void* userdata);

    class HIVE_API ShipPipeline
    {
    public:
        ShipPipeline(comb::DefaultAllocator& alloc, ProjectManager& project,
                     queen::ComponentRegistry<256>& registry);

        [[nodiscard]] bool Run(const ShipRequest& request, ShipEventFn callback, void* userdata);

    private:
        comb::DefaultAllocator* m_alloc;
        ProjectManager* m_project;
        queen::ComponentRegistry<256>* m_registry;
    };
} // namespace waggle
