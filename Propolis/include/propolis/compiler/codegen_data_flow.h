#pragma once

#include <propolis/compiler/codegen.h>
#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>

namespace propolis
{
    [[nodiscard]] wax::String EmitNode(const Node& node, const NodeDescriptor& desc, CodegenContext& ctx);
    [[nodiscard]] wax::String EmitUserFunctionNode(const Node& node, CodegenContext& ctx);
    void EmitDataDeps(NodeId nodeId, CodegenContext& ctx, wax::String& code);
    void EmitDataFlowBody(NodeId lifecycleNode, CodegenContext& ctx, wax::String& code);
    [[nodiscard]] wax::Vector<NodeId> ReachableFrom(NodeId root, const Graph& graph);
} // namespace propolis
