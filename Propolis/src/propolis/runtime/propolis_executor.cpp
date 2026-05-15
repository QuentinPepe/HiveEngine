#include <propolis/runtime/propolis_executor.h>

#include <propolis/core/log.h>
#include <propolis/runtime/function_registry.h>
#include <propolis/runtime/propolis_script.h>
#include <propolis/runtime/script_instance.h>
#include <propolis/runtime/script_registry.h>

#include <hive/core/assert.h>

#include <queen/core/entity.h>
#include <queen/observer/observer_event.h>
#include <queen/query/query_term.h>
#include <queen/world/world.h>

#include <cstring>

namespace propolis
{
    static bool IsExecutionEnabled(queen::World& world) noexcept
    {
        const auto* gate = world.Resource<ScriptExecutionGate>();
        return gate != nullptr && gate->m_enabled;
    }

    static void InitSystem(queen::World& world)
    {
        if (!IsExecutionEnabled(world))
        {
            return;
        }

        auto* registry = world.Resource<ScriptRegistry>();
        if (registry == nullptr)
        {
            return;
        }

        auto query = world.Query<queen::Write<PropolisScript>>();
        query.EachWithEntity([&](queen::Entity entity, PropolisScript& script) {
            if (script.m_state != nullptr || script.m_nameHash == 0)
            {
                return;
            }

            const ScriptEntry* entry = registry->FindByHash(script.m_nameHash);
            if (!entry)
            {
                hive::LogWarning(LOG_PROPOLIS_RUNTIME,
                                 "Init: entity references unknown script nameHash={:#x}",
                                 script.m_nameHash);
                return;
            }
            if (entry->m_stateSize == 0)
            {
                return;
            }

            auto& alloc = world.GetComponentAllocator();
            script.m_state = alloc.Allocate(entry->m_stateSize, entry->m_stateAlignment);
            hive::Check(script.m_state != nullptr);
            std::memset(script.m_state, 0, entry->m_stateSize);

            ScriptInstance inst{entry, script.m_state};
            AttachScript(inst, entity, world);
        });
    }

    static void TickSystem(queen::World& world)
    {
        if (!IsExecutionEnabled(world))
        {
            return;
        }

        auto* registry = world.Resource<ScriptRegistry>();
        auto* time = world.Resource<ScriptTime>();
        if (registry == nullptr || time == nullptr)
        {
            return;
        }

        float dt = time->m_dt;

        auto query = world.Query<queen::Read<PropolisScript>>();
        query.EachWithEntity([&](queen::Entity entity, const PropolisScript& script) {
            if (!script.m_state || script.m_nameHash == 0)
            {
                return;
            }

            const ScriptEntry* entry = registry->FindByHash(script.m_nameHash);
            if (!entry)
            {
                return;
            }

            ScriptInstance inst{entry, script.m_state};
            TickScript(inst, entity, world, dt);
        });
    }

    void RegisterPropolisExecutor(queen::World& world, PreTickFn preTick)
    {
        world.System("Propolis.Init")
            .Exclusive()
            .Run([](queen::World& w) { InitSystem(w); });

        world.System("Propolis.Tick")
            .After("Propolis.Init")
            .Exclusive()
            .Run([preTick](queen::World& w) {
                if (preTick)
                {
                    preTick(w);
                }
                TickSystem(w);
            });

        world.Observer<queen::OnRemove<PropolisScript>>("Propolis.Detach")
            .EachWithWorld([](queen::World& w, queen::Entity entity, const PropolisScript& script) {
                if (!script.m_state)
                {
                    return;
                }

                auto* registry = w.Resource<ScriptRegistry>();
                if (registry)
                {
                    const ScriptEntry* entry = registry->FindByHash(script.m_nameHash);
                    if (entry)
                    {
                        ScriptInstance inst{entry, script.m_state};
                        DetachScript(inst, entity, w);
                    }
                }

                w.GetComponentAllocator().Deallocate(script.m_state);
            });
    }

    void DetachAllScripts(queen::World& world)
    {
        auto* registry = world.Resource<ScriptRegistry>();

        auto query = world.Query<queen::Write<PropolisScript>>();
        query.EachWithEntity([&](queen::Entity entity, PropolisScript& script) {
            if (script.m_state == nullptr)
            {
                return;
            }

            if (registry != nullptr)
            {
                const ScriptEntry* entry = registry->FindByHash(script.m_nameHash);
                if (entry != nullptr)
                {
                    ScriptInstance inst{entry, script.m_state};
                    DetachScript(inst, entity, world);
                }
            }

            world.GetComponentAllocator().Deallocate(script.m_state);
            script.m_state = nullptr;
        });
    }

    void PrepareScriptReload(queen::World& world)
    {
        DetachAllScripts(world);

        auto* registry = world.Resource<ScriptRegistry>();
        if (registry != nullptr)
        {
            registry->Clear();
        }

        auto* fnRegistry = world.Resource<FunctionRegistry>();
        if (fnRegistry != nullptr)
        {
            fnRegistry->Clear();
        }
    }
} // namespace propolis
