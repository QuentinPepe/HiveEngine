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
        if (!world.Resource<FunctionRegistry>())
        {
            world.InsertResource(FunctionRegistry{});
        }

        auto* registry = world.Resource<FunctionRegistry>();

        FunctionEntry* node = detail::BlueprintFunctionHead();
        size_t count = 0;
        while (node)
        {
            registry->Register(*node);
            node = node->m_next;
            ++count;
        }

        hive::LogInfo(LOG_PROPOLIS_RUNTIME,
                      "Registered {} blueprint functions from gameplay module", count);
    }
} // namespace propolis
