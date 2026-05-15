#pragma once

#include <queen/system/system_id.h>

#include <cstdint>

namespace queen
{
    enum class SystemState : uint8_t
    {
        PENDING, // Waiting for dependencies
        READY,   // Dependencies satisfied, ready to run
        RUNNING, // Currently executing
        COMPLETE // Execution finished
    };

    // Per-system node in the dependency graph. Adjacency lives in the graph's
    // external arrays; this node only holds id, runtime state, and the
    // dependency countdown used to detect "ready to run".
    class SystemNode
    {
    public:
        constexpr SystemNode() noexcept
            : m_systemId{}
            , m_state{SystemState::PENDING}
            , m_dependencyCount{0}
            , m_unfinishedDeps{0}
        {
        }

        constexpr explicit SystemNode(SystemId id) noexcept
            : m_systemId{id}
            , m_state{SystemState::PENDING}
            , m_dependencyCount{0}
            , m_unfinishedDeps{0}
        {
        }

        [[nodiscard]] constexpr SystemId Id() const noexcept
        {
            return m_systemId;
        }
        [[nodiscard]] constexpr SystemState State() const noexcept
        {
            return m_state;
        }
        [[nodiscard]] constexpr uint16_t DependencyCount() const noexcept
        {
            return m_dependencyCount;
        }
        [[nodiscard]] constexpr uint16_t UnfinishedDeps() const noexcept
        {
            return m_unfinishedDeps;
        }

        constexpr void SetState(SystemState state) noexcept
        {
            m_state = state;
        }
        constexpr void SetDependencyCount(uint16_t count) noexcept
        {
            m_dependencyCount = count;
            m_unfinishedDeps = count;
        }

        constexpr void IncrementDependencyCount() noexcept
        {
            ++m_dependencyCount;
            ++m_unfinishedDeps;
        }

        constexpr void Reset() noexcept
        {
            m_state = SystemState::PENDING;
            m_unfinishedDeps = m_dependencyCount;
        }

        // Returns true when the last unfinished dep was just satisfied, so the
        // caller can submit this node without re-querying its state.
        constexpr bool DecrementDeps() noexcept
        {
            if (m_unfinishedDeps > 0)
            {
                --m_unfinishedDeps;
            }
            return m_unfinishedDeps == 0;
        }

        [[nodiscard]] constexpr bool IsReady() const noexcept
        {
            return m_state == SystemState::PENDING && m_unfinishedDeps == 0;
        }

    private:
        SystemId m_systemId;
        SystemState m_state;
        uint16_t m_dependencyCount;
        uint16_t m_unfinishedDeps;
    };
} // namespace queen
