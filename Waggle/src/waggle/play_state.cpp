#include <propolis/runtime/propolis_executor.h>

#include <queen/world/world.h>

#include <waggle/play_state.h>

namespace waggle
{
    void SetPlayState(queen::World& world, PlayState state)
    {
        world.InsertResource(state);

        auto* gate = world.Resource<propolis::ScriptExecutionGate>();
        if (gate != nullptr)
        {
            gate->m_enabled = (state == PlayState::PLAYING);
        }
    }

    PlayState GetPlayState(const queen::World& world) noexcept
    {
        const PlayState* current = const_cast<queen::World&>(world).Resource<PlayState>();
        return current != nullptr ? *current : PlayState::EDITING;
    }
} // namespace waggle
