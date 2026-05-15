#include <waggle/systems/propolis_system.h>
#include <waggle/play_state.h>
#include <waggle/time.h>

#include <propolis/runtime/propolis_executor.h>
#include <propolis/runtime/propolis_script.h>
#include <propolis/runtime/script_registry.h>

#include <queen/world/world.h>

namespace waggle
{
    void RegisterPropolisSystem(queen::World& world)
    {
        if (world.Resource<propolis::ScriptRegistry>() == nullptr)
        {
            world.InsertResource(propolis::ScriptRegistry{});
        }

        if (world.Resource<propolis::ScriptTime>() == nullptr)
        {
            world.InsertResource(propolis::ScriptTime{});
        }

        if (world.Resource<propolis::ScriptExecutionGate>() == nullptr)
        {
            propolis::ScriptExecutionGate gate{};
            gate.m_enabled = (GetPlayState(world) == PlayState::PLAYING);
            world.InsertResource(gate);
        }

        // Sync waggle::Time → propolis::ScriptTime inside the executor systems
        // to avoid creating a separate system (which causes scheduler dependency cycles).
        propolis::RegisterPropolisExecutor(world, [](queen::World& w) {
            auto* time = w.Resource<Time>();
            auto* scriptTime = w.Resource<propolis::ScriptTime>();
            if (time && scriptTime)
            {
                scriptTime->m_dt = time->m_dt;
            }
        });
    }

    void PreparePropolisReload(queen::World& world)
    {
        propolis::PrepareScriptReload(world);
    }
} // namespace waggle
