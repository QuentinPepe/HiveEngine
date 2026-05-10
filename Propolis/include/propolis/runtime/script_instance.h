#pragma once

#include <propolis/runtime/script_registry.h>

#include <queen/core/entity.h>

namespace propolis
{
    struct ScriptInstance
    {
        const ScriptEntry* m_entry{nullptr};
        void* m_state{nullptr};
    };

    inline void TickScript(const ScriptInstance& inst, queen::Entity entity,
                           queen::World& world, float dt)
    {
        if (inst.m_entry && inst.m_entry->m_onTick && inst.m_state)
        {
            inst.m_entry->m_onTick(entity, inst.m_state, world, dt);
        }
    }

    inline void AttachScript(const ScriptInstance& inst, queen::Entity entity,
                             queen::World& world)
    {
        if (inst.m_entry && inst.m_entry->m_onAttach && inst.m_state)
        {
            inst.m_entry->m_onAttach(entity, inst.m_state, world);
        }
    }

    inline void DetachScript(const ScriptInstance& inst, queen::Entity entity,
                             queen::World& world)
    {
        if (inst.m_entry && inst.m_entry->m_onDetach && inst.m_state)
        {
            inst.m_entry->m_onDetach(entity, inst.m_state, world);
        }
    }
} // namespace propolis
