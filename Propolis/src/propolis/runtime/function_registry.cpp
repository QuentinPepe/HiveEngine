#include <propolis/runtime/function_registry.h>

#include <propolis/core/log.h>

#include <queen/world/world.h>

#include <cstring>

namespace propolis
{
    namespace detail
    {
        FunctionEntry*& BlueprintFunctionHead() noexcept
        {
            static FunctionEntry* head = nullptr;
            return head;
        }
    } // namespace detail

    void FunctionRegistry::Register(const FunctionEntry& entry)
    {
        // m_next is valid only inside the DLL's intrusive collection list. Once copied into
        // this vector, the pointer is stale — null it so a future reader can't follow garbage.
        FunctionEntry copy = entry;
        copy.m_next = nullptr;
        m_entries.PushBack(static_cast<FunctionEntry&&>(copy));
    }

    const FunctionEntry* FunctionRegistry::Find(const char* name) const noexcept
    {
        for (size_t i = 0; i < m_entries.Size(); ++i)
        {
            if (std::strcmp(m_entries[i].m_name, name) == 0)
            {
                return &m_entries[i];
            }
        }
        return nullptr;
    }

    void FunctionRegistry::Clear() noexcept
    {
        m_entries.Clear();
    }

    void RegisterAllBlueprintFunctions(queen::World& world)
    {
        // The engine pre-inserts an empty FunctionRegistry from RegisterPropolisSystem
        // so the resource's destructor and the inner Vector's allocator both live in
        // the engine module — surviving gameplay DLL hot-reloads. If absent here, the
        // engine didn't run its init path (test harness etc.); fall back to inserting
        // module-locally with the understanding that hot-reload won't be supported.
        if (world.Resource<FunctionRegistry>() == nullptr)
        {
            world.InsertResource(FunctionRegistry{});
        }

        auto* registry = world.Resource<FunctionRegistry>();

        FunctionEntry* node = detail::BlueprintFunctionHead();
        size_t count = 0;
        while (node != nullptr)
        {
            registry->Register(*node);
            node = node->m_next;
            ++count;
        }

        // In shared-engine builds, BlueprintFunctionHead lives in hive_engine.dll and
        // is shared across the gameplay DLL hot-reload cycle. The FunctionEntry nodes
        // themselves live in the gameplay DLL's read-only segment, so after unload
        // their addresses become dangling. Reset the head now that we've drained it
        // — when the next gameplay DLL loads, its static initializers will see a
        // null head and rebuild a clean list pointing only into the new DLL.
        detail::BlueprintFunctionHead() = nullptr;

        hive::LogInfo(LOG_PROPOLIS_RUNTIME,
                      "Registered {} blueprint functions from gameplay module", count);
    }
} // namespace propolis
