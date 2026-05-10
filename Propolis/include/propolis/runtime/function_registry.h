#pragma once

#include <propolis/types/ptype.h>

#include <hive/hive_config.h>

#include <wax/containers/vector.h>

#include <cstdint>

namespace queen
{
    class World;
} // namespace queen

namespace propolis
{
    struct ParamInfo
    {
        const char* m_name;
        PType m_type;
    };

    // Describes a C++ function exposed to blueprints. POD by design — populated by
    // HIVE_BLUEPRINT_FUNCTION_N macros at static init time, collected into a
    // FunctionRegistry resource on the World by RegisterAllBlueprintFunctions().
    //
    // m_name = short name shown in the editor palette (last segment of qualified name).
    // m_qualifiedCppName = full C++ identifier the codegen emits for the call site
    //                      (ex: "mygame::SmoothDamp"). Must be reachable from the
    //                      generated .propolis.cpp's compilation unit.
    struct FunctionEntry
    {
        const char* m_name;
        const char* m_category;
        const char* m_qualifiedCppName;
        PType m_returnType;
        const ParamInfo* m_params;
        size_t m_paramCount;
        FunctionEntry* m_next{nullptr};
    };

    // World resource. Populated from a gameplay module's intrusive linked list at
    // registration time, cleared on hot-reload.
    class HIVE_API FunctionRegistry
    {
    public:
        void Register(const FunctionEntry& entry);
        [[nodiscard]] const FunctionEntry* Find(const char* name) const noexcept;
        [[nodiscard]] const wax::Vector<FunctionEntry>& All() const noexcept { return m_entries; }
        void Clear() noexcept;
        [[nodiscard]] size_t Count() const noexcept { return m_entries.Size(); }

    private:
        wax::Vector<FunctionEntry> m_entries;
    };

    namespace detail
    {
        // Module-local linked list head. Each shared library that links Propolis as
        // a static lib gets its own copy via this function-local static. The
        // gameplay DLL's static initializers populate the gameplay DLL's copy only.
        [[nodiscard]] HIVE_API FunctionEntry*& BlueprintFunctionHead() noexcept;

        struct FunctionRegistrar
        {
            explicit FunctionRegistrar(FunctionEntry* entry) noexcept
            {
                entry->m_next = BlueprintFunctionHead();
                BlueprintFunctionHead() = entry;
            }
        };

        // Compile-time mapping: C++ type → PType. Specializations cover every
        // type the macro accepts. Unsupported types fail to compile (= deleted primary).
        template <typename T> PType BPTypeOf() = delete;
    } // namespace detail

    // Walks the calling module's linked list and inserts each entry into the World's
    // FunctionRegistry. Must be called from inside the gameplay DLL (typically from
    // HiveGameplayRegister), otherwise the head pointer is empty.
    HIVE_API void RegisterAllBlueprintFunctions(queen::World& world);
} // namespace propolis
