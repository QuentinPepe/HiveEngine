#pragma once

#include <propolis/compiler/codegen.h>
#include <propolis/graph/graph.h>

#include <wax/containers/string.h>

namespace propolis
{
    void EmitExecChain(PinId execOutPin, CodegenContext& ctx, int indent, wax::String& code);
    [[nodiscard]] wax::String EmitLifecycleFunction(const char* lifecycle, CodegenContext& ctx);
} // namespace propolis
