#pragma once

#include <propolis/runtime/propolis_script.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>

#include <cstdint>

namespace queen
{
    class World;
    class Entity;
} // namespace queen

namespace propolis
{
    using ScriptOnTickFn   = void (*)(queen::Entity, void* state, queen::World&, float dt);
    using ScriptOnAttachFn = void (*)(queen::Entity, void* state, queen::World&);
    using ScriptOnDetachFn = void (*)(queen::Entity, void* state, queen::World&);

    struct ScriptEntry
    {
        wax::String m_name;
        uint32_t m_nameHash{0};
        uint32_t m_stateSize{0};
        uint32_t m_stateAlignment{0};
        ScriptOnTickFn m_onTick{nullptr};
        ScriptOnAttachFn m_onAttach{nullptr};
        ScriptOnDetachFn m_onDetach{nullptr};
    };

    // Registry that gameplay DLLs populate at load time.
    // Cleared and repopulated on hot-reload. Entries matched by m_nameHash (FNV-1a).
    class ScriptRegistry
    {
    public:
        void Register(ScriptEntry entry)
        {
            if (entry.m_nameHash == 0 && entry.m_name.Size() > 0)
            {
                entry.m_nameHash = ScriptNameHash(entry.m_name.CStr());
            }
            m_entries.PushBack(static_cast<ScriptEntry&&>(entry));
        }

        [[nodiscard]] const ScriptEntry* Find(const char* name) const noexcept
        {
            for (size_t i = 0; i < m_entries.Size(); ++i)
            {
                if (m_entries[i].m_name == name)
                {
                    return &m_entries[i];
                }
            }
            return nullptr;
        }

        [[nodiscard]] const ScriptEntry* FindByHash(uint32_t hash) const noexcept
        {
            for (size_t i = 0; i < m_entries.Size(); ++i)
            {
                if (m_entries[i].m_nameHash == hash)
                {
                    return &m_entries[i];
                }
            }
            return nullptr;
        }

        void Clear() { m_entries.Clear(); }
        [[nodiscard]] const wax::Vector<ScriptEntry>& All() const noexcept { return m_entries; }
        [[nodiscard]] size_t Count() const noexcept { return m_entries.Size(); }

    private:
        wax::Vector<ScriptEntry> m_entries;
    };
} // namespace propolis
