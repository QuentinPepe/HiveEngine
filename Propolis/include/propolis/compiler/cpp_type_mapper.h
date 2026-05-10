#pragma once

#include <propolis/graph/graph_types.h>
#include <propolis/types/ptype.h>

#include <wax/containers/string.h>

namespace propolis
{
    [[nodiscard]] const char* PTypeToCppType(PType type) noexcept;
    [[nodiscard]] const char* PTypeDefaultValue(PType type) noexcept;
    [[nodiscard]] wax::String SanitizeIdentifier(const wax::String& name);
    [[nodiscard]] wax::String VarName(PinId pin);
    void AppendIndent(int level, wax::String& code);
} // namespace propolis
