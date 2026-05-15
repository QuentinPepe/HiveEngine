#pragma once

#include <comb/allocator_concepts.h>

#include <queen/core/tick.h>
#include <queen/query/query_descriptor.h>
#include <queen/system/access_descriptor.h>
#include <queen/system/system_id.h>

#include <cstring>

namespace queen
{
    class World;

    enum class SystemExecutor : uint8_t
    {
        SEQUENTIAL, // Runs on main thread only
        PARALLEL,   // Can run with non-conflicting systems
        EXCLUSIVE,  // Requires exclusive world access
    };

    // Type-erased executor signature so lambdas of arbitrary capture can be stored
    // alongside the user-data pointer that holds their state.
    using SystemExecutorFn = void (*)(World& world, void* userData);

    // Self-contained metadata + executor for a registered system. The descriptor
    // owns its executor's user-data block via a manual destructor pointer so we
    // can store arbitrary lambdas without RTTI.
    template <comb::Allocator Allocator> class SystemDescriptor
    {
    public:
        static constexpr size_t kMaxNameLength = 63;
        static constexpr size_t kMaxExplicitDeps = 8;

        SystemDescriptor(Allocator& allocator, SystemId id, const char* name)
            : m_id{id}
            , m_allocator{&allocator}
            , m_access{allocator}
            , m_query{allocator}
            , m_executorFn{nullptr}
            , m_userData{nullptr}
            , m_destructorFn{nullptr}
            , m_executorMode{SystemExecutor::PARALLEL}
            , m_enabled{true}
        {
            if (name != nullptr)
            {
                size_t len = std::strlen(name);
                if (len > kMaxNameLength)
                    len = kMaxNameLength;
                std::memcpy(m_name, name, len);
                m_name[len] = '\0';
            }
            else
            {
                m_name[0] = '\0';
            }
        }

        ~SystemDescriptor()
        {
            if (m_userData != nullptr)
            {
                if (m_destructorFn != nullptr)
                {
                    m_destructorFn(m_userData);
                }
                m_allocator->Deallocate(m_userData);
            }
        }

        SystemDescriptor(const SystemDescriptor&) = delete;
        SystemDescriptor& operator=(const SystemDescriptor&) = delete;
        SystemDescriptor(SystemDescriptor&& other) noexcept
            : m_id{other.m_id}
            , m_allocator{other.m_allocator}
            , m_access{std::move(other.m_access)}
            , m_query{std::move(other.m_query)}
            , m_executorFn{other.m_executorFn}
            , m_userData{other.m_userData}
            , m_destructorFn{other.m_destructorFn}
            , m_executorMode{other.m_executorMode}
            , m_enabled{other.m_enabled}
            , m_afterCount{other.m_afterCount}
            , m_beforeCount{other.m_beforeCount}
            , m_lastRunTick{other.m_lastRunTick}
        {
            std::memcpy(m_name, other.m_name, sizeof(m_name));
            std::memcpy(m_explicitAfter, other.m_explicitAfter, sizeof(SystemId) * m_afterCount);
            std::memcpy(m_explicitBefore, other.m_explicitBefore, sizeof(SystemId) * m_beforeCount);
            other.m_userData = nullptr;
            other.m_destructorFn = nullptr;
        }

        SystemDescriptor& operator=(SystemDescriptor&& other) noexcept
        {
            if (this != &other)
            {
                if (m_userData != nullptr)
                {
                    if (m_destructorFn != nullptr)
                    {
                        m_destructorFn(m_userData);
                    }
                    m_allocator->Deallocate(m_userData);
                }

                m_id = other.m_id;
                m_allocator = other.m_allocator;
                std::memcpy(m_name, other.m_name, sizeof(m_name));
                m_access = std::move(other.m_access);
                m_query = std::move(other.m_query);
                m_executorFn = other.m_executorFn;
                m_userData = other.m_userData;
                m_destructorFn = other.m_destructorFn;
                m_executorMode = other.m_executorMode;
                m_enabled = other.m_enabled;
                m_afterCount = other.m_afterCount;
                m_beforeCount = other.m_beforeCount;
                std::memcpy(m_explicitAfter, other.m_explicitAfter, sizeof(SystemId) * m_afterCount);
                std::memcpy(m_explicitBefore, other.m_explicitBefore, sizeof(SystemId) * m_beforeCount);
                m_lastRunTick = other.m_lastRunTick;

                other.m_userData = nullptr;
                other.m_destructorFn = nullptr;
            }
            return *this;
        }

        [[nodiscard]] SystemId Id() const noexcept
        {
            return m_id;
        }
        [[nodiscard]] const char* Name() const noexcept
        {
            return m_name;
        }
        [[nodiscard]] const AccessDescriptor<Allocator>& Access() const noexcept
        {
            return m_access;
        }
        [[nodiscard]] AccessDescriptor<Allocator>& Access() noexcept
        {
            return m_access;
        }
        [[nodiscard]] const QueryDescriptor<Allocator>& Query() const noexcept
        {
            return m_query;
        }
        [[nodiscard]] QueryDescriptor<Allocator>& Query() noexcept
        {
            return m_query;
        }
        [[nodiscard]] SystemExecutor ExecutorMode() const noexcept
        {
            return m_executorMode;
        }
        [[nodiscard]] bool IsEnabled() const noexcept
        {
            return m_enabled;
        }
        [[nodiscard]] Tick LastRunTick() const noexcept
        {
            return m_lastRunTick;
        }

        void SetExecutorMode(SystemExecutor mode) noexcept
        {
            m_executorMode = mode;
            if (mode == SystemExecutor::EXCLUSIVE)
            {
                m_access.SetWorldAccess(WorldAccess::EXCLUSIVE);
            }
        }

        void SetEnabled(bool enabled) noexcept
        {
            m_enabled = enabled;
        }

        void SetExecutor(SystemExecutorFn fn, void* userData, void (*destructor)(void*))
        {
            if (m_userData != nullptr)
            {
                if (m_destructorFn != nullptr)
                {
                    m_destructorFn(m_userData);
                }
                m_allocator->Deallocate(m_userData);
            }
            m_executorFn = fn;
            m_userData = userData;
            m_destructorFn = destructor;
        }

        // Records currentTick on success so change-detection queries can compare
        // against the last successful run.
        void Execute(World& world, Tick currentTick)
        {
            if (m_executorFn != nullptr && m_enabled)
            {
                m_executorFn(world, m_userData);
                m_lastRunTick = currentTick;
            }
        }

        [[nodiscard]] bool HasExecutor() const noexcept
        {
            return m_executorFn != nullptr;
        }

        void AddAfter(SystemId id) noexcept
        {
            if (m_afterCount < kMaxExplicitDeps)
            {
                m_explicitAfter[m_afterCount++] = id;
            }
        }

        void AddBefore(SystemId id) noexcept
        {
            if (m_beforeCount < kMaxExplicitDeps)
            {
                m_explicitBefore[m_beforeCount++] = id;
            }
        }

        [[nodiscard]] uint8_t AfterCount() const noexcept
        {
            return m_afterCount;
        }
        [[nodiscard]] uint8_t BeforeCount() const noexcept
        {
            return m_beforeCount;
        }
        [[nodiscard]] SystemId AfterDep(uint8_t i) const noexcept
        {
            return m_explicitAfter[i];
        }
        [[nodiscard]] SystemId BeforeDep(uint8_t i) const noexcept
        {
            return m_explicitBefore[i];
        }

    private:
        SystemId m_id;
        Allocator* m_allocator;
        char m_name[kMaxNameLength + 1];
        AccessDescriptor<Allocator> m_access;
        QueryDescriptor<Allocator> m_query;
        SystemExecutorFn m_executorFn;
        void* m_userData;
        void (*m_destructorFn)(void*);
        SystemExecutor m_executorMode;
        bool m_enabled;
        uint8_t m_afterCount{0};
        uint8_t m_beforeCount{0};
        SystemId m_explicitAfter[kMaxExplicitDeps];
        SystemId m_explicitBefore[kMaxExplicitDeps];
        Tick m_lastRunTick{0};
    };
} // namespace queen
