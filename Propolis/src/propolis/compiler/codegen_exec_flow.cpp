#include <propolis/compiler/codegen_exec_flow.h>

#include <propolis/compiler/codegen_data_flow.h>
#include <propolis/compiler/cpp_type_mapper.h>

#include <cstring>

namespace propolis
{
    static void EmitBranch(const Node& node, CodegenContext& ctx, int indent, wax::String& code);
    static void EmitForEach(const Node& node, CodegenContext& ctx, int indent, wax::String& code);
    static void EmitWhile(const Node& node, CodegenContext& ctx, int indent, wax::String& code);

    void EmitExecChain(PinId execOutPin, CodegenContext& ctx, int indent, wax::String& code)
    {
        auto edges = ctx.m_graph->EdgesForPin(execOutPin);
        if (edges.Size() == 0)
        {
            return;
        }

        const Pin* targetPin = ctx.m_graph->FindPin(edges[0]->m_target);
        if (!targetPin)
        {
            return;
        }

        const Node* node = ctx.m_graph->FindNode(targetPin->m_owner);
        if (!node)
        {
            return;
        }

        const NodeDescriptor* desc = ctx.m_registry->Find(node->m_title.CStr());
        if (!desc)
        {
            // Void user-fn nodes carry exec pins; emit the call inline and continue the chain
            // through their exec output.
            if (node->m_userFn.m_qualifiedCppName.Size() > 0)
            {
                EmitDataDeps(node->m_id, ctx, code);
                code.Append(EmitUserFunctionNode(*node, ctx).CStr());
                ctx.m_emitted.Insert(node->m_id.m_value);
                for (size_t i = 0; i < node->m_outputs.Size(); ++i)
                {
                    const Pin* p = ctx.m_graph->FindPin(node->m_outputs[i]);
                    if (p && p->m_type.m_kind == PTypeKind::SIGNAL)
                    {
                        EmitExecChain(p->m_id, ctx, indent, code);
                        break;
                    }
                }
            }
            return;
        }

        EmitDataDeps(node->m_id, ctx, code);

        if (HasFlag(desc->m_flags, NodeFlag::BRANCH))
        {
            EmitBranch(*node, ctx, indent, code);
            ctx.m_emitted.Insert(node->m_id.m_value);
            return;
        }

        if (HasFlag(desc->m_flags, NodeFlag::SEQUENCE))
        {
            for (size_t i = 0; i < node->m_outputs.Size(); ++i)
            {
                const Pin* p = ctx.m_graph->FindPin(node->m_outputs[i]);
                if (p && p->m_type.m_kind == PTypeKind::SIGNAL)
                {
                    EmitExecChain(p->m_id, ctx, indent, code);
                }
            }
            ctx.m_emitted.Insert(node->m_id.m_value);
            return;
        }

        if (HasFlag(desc->m_flags, NodeFlag::FOR_EACH))
        {
            EmitForEach(*node, ctx, indent, code);
            ctx.m_emitted.Insert(node->m_id.m_value);
            return;
        }

        if (HasFlag(desc->m_flags, NodeFlag::WHILE_LOOP))
        {
            EmitWhile(*node, ctx, indent, code);
            ctx.m_emitted.Insert(node->m_id.m_value);
            return;
        }

        code.Append(EmitNode(*node, *desc, ctx).CStr());
        ctx.m_emitted.Insert(node->m_id.m_value);

        for (size_t i = 0; i < node->m_outputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node->m_outputs[i]);
            if (p && p->m_type.m_kind == PTypeKind::SIGNAL)
            {
                EmitExecChain(p->m_id, ctx, indent, code);
                break;
            }
        }
    }

    static void EmitBranch(const Node& node, CodegenContext& ctx, int indent, wax::String& code)
    {
        PinId condPin{};
        for (size_t i = 0; i < node.m_inputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node.m_inputs[i]);
            if (p && p->m_type.m_kind != PTypeKind::SIGNAL)
            {
                condPin = p->m_id;
                break;
            }
        }

        wax::String condExpr{"/* no condition */"};
        if (condPin)
        {
            auto condEdges = ctx.m_graph->EdgesForPin(condPin);
            if (condEdges.Size() > 0)
            {
                condExpr = VarName(condEdges[0]->m_source);
            }
        }

        AppendIndent(indent, code);
        code.Append("if (");
        code.Append(condExpr.CStr());
        code.Append(")\n");
        AppendIndent(indent, code);
        code.Append("{\n");

        PinId truePin{};
        PinId falsePin{};
        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node.m_outputs[i]);
            if (p && p->m_type.m_kind == PTypeKind::SIGNAL)
            {
                if (p->m_name == "True")        truePin = p->m_id;
                else if (p->m_name == "False")  falsePin = p->m_id;
            }
        }

        if (truePin)
        {
            EmitExecChain(truePin, ctx, indent + 1, code);
        }

        AppendIndent(indent, code);
        code.Append("}\n");

        if (falsePin)
        {
            auto falseEdges = ctx.m_graph->EdgesForPin(falsePin);
            if (falseEdges.Size() > 0)
            {
                AppendIndent(indent, code);
                code.Append("else\n");
                AppendIndent(indent, code);
                code.Append("{\n");
                EmitExecChain(falsePin, ctx, indent + 1, code);
                AppendIndent(indent, code);
                code.Append("}\n");
            }
        }
    }

    static void EmitForEach(const Node& node, CodegenContext& ctx, int indent, wax::String& code)
    {
        PinId countPin{};
        PinId indexPin{};
        PinId bodyPin{};
        PinId donePin{};

        for (size_t i = 0; i < node.m_inputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node.m_inputs[i]);
            if (p && p->m_type.m_kind != PTypeKind::SIGNAL && p->m_name == "Count")
            {
                countPin = p->m_id;
            }
        }

        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node.m_outputs[i]);
            if (!p)
            {
                continue;
            }
            if (p->m_type.m_kind == PTypeKind::SIGNAL && p->m_name == "Body")        bodyPin = p->m_id;
            else if (p->m_type.m_kind == PTypeKind::SIGNAL && p->m_name == "Done")   donePin = p->m_id;
            else if (p->m_name == "Index")                                            indexPin = p->m_id;
        }

        wax::String countExpr{"0"};
        if (countPin)
        {
            auto countEdges = ctx.m_graph->EdgesForPin(countPin);
            if (countEdges.Size() > 0)
            {
                countExpr = VarName(countEdges[0]->m_source);
            }
        }

        wax::String idxVar = indexPin ? VarName(indexPin) : wax::String{"_idx"};

        AppendIndent(indent, code);
        code.Append("for (int32_t ");
        code.Append(idxVar.CStr());
        code.Append(" = 0; ");
        code.Append(idxVar.CStr());
        code.Append(" < ");
        code.Append(countExpr.CStr());
        code.Append("; ++");
        code.Append(idxVar.CStr());
        code.Append(")\n");
        AppendIndent(indent, code);
        code.Append("{\n");

        if (bodyPin)
        {
            EmitExecChain(bodyPin, ctx, indent + 1, code);
        }

        AppendIndent(indent, code);
        code.Append("}\n");

        if (donePin)
        {
            EmitExecChain(donePin, ctx, indent, code);
        }
    }

    static void EmitWhile(const Node& node, CodegenContext& ctx, int indent, wax::String& code)
    {
        PinId condPin{};
        PinId bodyPin{};
        PinId donePin{};

        for (size_t i = 0; i < node.m_inputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node.m_inputs[i]);
            if (p && p->m_type.m_kind != PTypeKind::SIGNAL && p->m_name == "Condition")
            {
                condPin = p->m_id;
            }
        }

        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node.m_outputs[i]);
            if (!p)
            {
                continue;
            }
            if (p->m_type.m_kind == PTypeKind::SIGNAL && p->m_name == "Body")      bodyPin = p->m_id;
            else if (p->m_type.m_kind == PTypeKind::SIGNAL && p->m_name == "Done") donePin = p->m_id;
        }

        wax::String condExpr{"false"};
        if (condPin)
        {
            auto condEdges = ctx.m_graph->EdgesForPin(condPin);
            if (condEdges.Size() > 0)
            {
                condExpr = VarName(condEdges[0]->m_source);
            }
        }

        AppendIndent(indent, code);
        code.Append("while (");
        code.Append(condExpr.CStr());
        code.Append(")\n");
        AppendIndent(indent, code);
        code.Append("{\n");

        if (bodyPin)
        {
            EmitExecChain(bodyPin, ctx, indent + 1, code);
        }

        AppendIndent(indent, code);
        code.Append("}\n");

        if (donePin)
        {
            EmitExecChain(donePin, ctx, indent, code);
        }
    }

    static NodeId FindNodeByTitle(const Graph& graph, const char* title)
    {
        for (size_t i = 0; i < graph.Nodes().Size(); ++i)
        {
            if (graph.Nodes()[i].m_title == title)
            {
                return graph.Nodes()[i].m_id;
            }
        }
        return NodeId{};
    }

    static bool HasConnectedExecOutput(const Graph& graph, const Node& node)
    {
        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            const Pin* p = graph.FindPin(node.m_outputs[i]);
            if (p && p->m_type.m_kind == PTypeKind::SIGNAL && graph.EdgesForPin(p->m_id).Size() > 0)
            {
                return true;
            }
        }
        return false;
    }

    static PinId FirstExecOutput(const Graph& graph, const Node& node)
    {
        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            const Pin* p = graph.FindPin(node.m_outputs[i]);
            if (p && p->m_type.m_kind == PTypeKind::SIGNAL)
            {
                return p->m_id;
            }
        }
        return PinId{};
    }

    wax::String EmitLifecycleFunction(const char* lifecycle, CodegenContext& ctx)
    {
        NodeId lifecycleNode = FindNodeByTitle(*ctx.m_graph, lifecycle);
        if (!lifecycleNode)
        {
            return wax::String{};
        }

        wax::String code;
        wax::String safeName = SanitizeIdentifier(ctx.m_options->m_systemName);

        code.Append("    void ");
        code.Append(safeName.CStr());
        code.Append("_");
        code.Append(lifecycle);
        code.Append("(queen::Entity _entity, ");
        code.Append(safeName.CStr());
        code.Append("_State& _state, queen::World& _world");
        if (std::strcmp(lifecycle, "OnTick") == 0)
        {
            code.Append(", float _dt");
        }
        code.Append(")\n    {\n");

        const Node* lcNode = ctx.m_graph->FindNode(lifecycleNode);

        if (lcNode)
        {
            const NodeDescriptor* lcDesc = ctx.m_registry->Find(lcNode->m_title.CStr());
            if (lcDesc)
            {
                code.Append(EmitNode(*lcNode, *lcDesc, ctx).CStr());
                ctx.m_emitted.Insert(lifecycleNode.m_value);
            }
        }

        if (lcNode && HasConnectedExecOutput(*ctx.m_graph, *lcNode))
        {
            PinId execPin = FirstExecOutput(*ctx.m_graph, *lcNode);
            if (execPin)
            {
                EmitExecChain(execPin, ctx, 2, code);
            }
        }
        else
        {
            EmitDataFlowBody(lifecycleNode, ctx, code);
        }

        code.Append("    }\n\n");
        return code;
    }
} // namespace propolis
