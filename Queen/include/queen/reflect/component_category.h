#pragma once

#include <cstdint>

namespace queen
{
    enum class ComponentCategory : uint8_t
    {
        // Default: addable/removable via the editor inspector.
        USER = 0,
        // Engine-managed: hidden from "Add Component" picker, visible if already present.
        SYSTEM,
        // Internal scaffolding (Parent, Children, etc.): never shown in the inspector.
        INTERNAL,
        // Component has a non-default payload (e.g. an asset reference, a script name hash)
        // that requires a dedicated picker — hidden from the generic Components list.
        // The picker is responsible for adding the component with its payload filled in.
        PARAMETERIZED,
    };
} // namespace queen
