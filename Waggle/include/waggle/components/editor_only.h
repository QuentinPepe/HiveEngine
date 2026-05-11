#pragma once

#include <queen/reflect/component_reflector.h>

namespace waggle
{
    // Tag attached to entities that belong to the editor itself (e.g. the persistent
    // editor camera). Scene loading must not despawn them and SaveScene must not serialize
    // them — they exist for the editor session only.
    struct EditorOnly
    {
        static void Reflect(queen::ComponentReflector<>&) {}
    };
} // namespace waggle
