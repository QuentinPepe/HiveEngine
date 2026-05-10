#include <propolis/compiler/codegen_data_flow.h>

#include <propolis/compiler/cpp_type_mapper.h>
#include <propolis/core/log.h>

#include <wax/containers/string_view.h>

#include <cstdio>

namespace propolis
{
    static wax::String SubstituteTemplate(const wax::String& tmpl, const Node& node, CodegenContext& ctx)
    {
        wax::String result;
        size_t len = tmpl.Size();
        const char* s = tmpl.CStr();

        for (size_t i = 0; i < len; ++i)
        {
            if (s[i] == '{')
            {
                size_t end = i + 1;
                while (end < len && s[end] != '}')
                {
                    ++end;
                }

                if (end < len)
                {
                    wax::String pinName{wax::StringView{s + i + 1, end - i - 1}};

                    const Pin* pin = ctx.m_graph->FindPinByName(node.m_id, PinDirection::INPUT, pinName.CStr());
                    if (pin)
                    {
                        auto edges = ctx.m_graph->EdgesForPin(pin->m_id);
                        if (edges.Size() > 0)
                        {
                            result.Append(VarName(edges[0]->m_source).CStr());
                        }
                        else if (pin->m_defaultValue.Size() > 0)
                        {
                            result.Append(pin->m_defaultValue.CStr());
                        }
                        else
                        {
                            result.Append(PTypeDefaultValue(pin->m_type));
                        }
                    }
                    else
                    {
                        result.Append("/* unknown pin: ");
                        result.Append(pinName.CStr());
                        result.Append(" */");
                    }

                    i = end;
                    continue;
                }
            }
            result.Append(s[i]);
        }

        return result;
    }

    static wax::String EmitLifecycleAliases(const Node& node, CodegenContext& ctx)
    {
        wax::String code;
        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            PinId outPin = node.m_outputs[i];
            const Pin* pin = ctx.m_graph->FindPin(outPin);
            if (!pin || pin->m_isExec)
            {
                continue;
            }

            const PType* t = ctx.m_validation->m_resolvedPinTypes.Find(outPin.m_value);
            if (!t)
            {
                continue;
            }

            const char* paramName = nullptr;
            if (pin->m_name == "DeltaTime")
            {
                paramName = "_dt";
            }
            else if (pin->m_name == "Entity")
            {
                paramName = "_entity";
            }

            if (paramName)
            {
                code.Append("        ");
                code.Append(PTypeToCppType(*t));
                code.Append(" ");
                code.Append(VarName(outPin).CStr());
                code.Append(" = ");
                code.Append(paramName);
                code.Append(";\n");
            }
        }
        return code;
    }

    static wax::String EmitGetVariable(const Node& node, CodegenContext& ctx)
    {
        wax::String code;
        if (node.m_outputs.Size() == 0)
        {
            return code;
        }

        PinId outPin = node.m_outputs[0];
        const PType* t = ctx.m_validation->m_resolvedPinTypes.Find(outPin.m_value);
        if (!t)
        {
            return code;
        }

        const Variable* var = ctx.m_graph->FindVariable(node.m_variableRef);
        if (!var)
        {
            hive::LogWarning(LOG_PROPOLIS_COMPILER,
                             "GetVariable node references unknown variable id={}",
                             node.m_variableRef.m_value);
            return code;
        }

        code.Append("        ");
        code.Append(PTypeToCppType(*t));
        code.Append(" ");
        code.Append(VarName(outPin).CStr());
        code.Append(" = _state.");
        code.Append(SanitizeIdentifier(var->m_name).CStr());
        code.Append(";\n");
        return code;
    }

    static wax::String EmitSetVariable(const Node& node, CodegenContext& ctx)
    {
        wax::String code;
        const Variable* var = ctx.m_graph->FindVariable(node.m_variableRef);
        if (!var)
        {
            hive::LogWarning(LOG_PROPOLIS_COMPILER,
                             "SetVariable node references unknown variable id={}",
                             node.m_variableRef.m_value);
            return code;
        }

        PinId valuePin{};
        for (size_t i = 0; i < node.m_inputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node.m_inputs[i]);
            if (p && p->m_type.m_kind != PTypeKind::SIGNAL)
            {
                valuePin = p->m_id;
                break;
            }
        }

        if (!valuePin)
        {
            return code;
        }

        auto edges = ctx.m_graph->EdgesForPin(valuePin);
        code.Append("        _state.");
        code.Append(SanitizeIdentifier(var->m_name).CStr());
        code.Append(" = ");
        if (edges.Size() > 0)
        {
            code.Append(VarName(edges[0]->m_source).CStr());
        }
        else
        {
            code.Append(PTypeDefaultValue(var->m_type));
        }
        code.Append(";\n");
        return code;
    }

    static wax::String EmitGetComponentField(const Node& node, CodegenContext& ctx)
    {
        wax::String code;
        if (node.m_componentRef.Size() == 0 || node.m_outputs.Size() == 0)
        {
            return code;
        }

        char compVar[32];
        std::snprintf(compVar, sizeof(compVar), "_comp%u", node.m_id.m_value);

        code.Append("        auto* ");
        code.Append(compVar);
        code.Append(" = _world.Get<");
        code.Append(node.m_componentRef.CStr());
        code.Append(">(_entity);\n");

        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            PinId outPin = node.m_outputs[i];
            const Pin* pin = ctx.m_graph->FindPin(outPin);
            if (!pin || pin->m_type.m_kind == PTypeKind::SIGNAL)
            {
                continue;
            }

            const PType* t = ctx.m_validation->m_resolvedPinTypes.Find(outPin.m_value);
            if (!t)
            {
                continue;
            }

            const char* cppType = PTypeToCppType(*t);
            code.Append("        ");
            code.Append(cppType);
            code.Append(" ");
            code.Append(VarName(outPin).CStr());
            code.Append(" = ");
            code.Append(compVar);
            code.Append(" ? ");
            code.Append(compVar);
            code.Append("->");
            code.Append(pin->m_name.CStr());
            code.Append(" : ");
            code.Append(cppType);
            code.Append("{};\n");
        }

        return code;
    }

    static wax::String EmitSetComponentField(const Node& node, CodegenContext& ctx)
    {
        wax::String code;
        if (node.m_componentRef.Size() == 0)
        {
            return code;
        }

        char compVar[32];
        std::snprintf(compVar, sizeof(compVar), "_comp%u", node.m_id.m_value);

        code.Append("        auto* ");
        code.Append(compVar);
        code.Append(" = _world.Get<");
        code.Append(node.m_componentRef.CStr());
        code.Append(">(_entity);\n");

        code.Append("        if (");
        code.Append(compVar);
        code.Append(")\n        {\n");

        for (size_t i = 0; i < node.m_inputs.Size(); ++i)
        {
            const Pin* pin = ctx.m_graph->FindPin(node.m_inputs[i]);
            if (!pin || pin->m_type.m_kind == PTypeKind::SIGNAL)
            {
                continue;
            }

            auto edges = ctx.m_graph->EdgesForPin(pin->m_id);
            if (edges.Size() == 0)
            {
                continue;
            }

            code.Append("            ");
            code.Append(compVar);
            code.Append("->");
            code.Append(pin->m_name.CStr());
            code.Append(" = ");
            code.Append(VarName(edges[0]->m_source).CStr());
            code.Append(";\n");
        }

        code.Append("        }\n");
        return code;
    }

    static wax::String EmitStructBreak(const Node& node, CodegenContext& ctx)
    {
        wax::String code;
        if (node.m_inputs.Size() == 0)
        {
            return code;
        }

        const Pin* inPin = ctx.m_graph->FindPin(node.m_inputs[0]);
        if (!inPin)
        {
            return code;
        }

        auto edges = ctx.m_graph->EdgesForPin(inPin->m_id);
        if (edges.Size() == 0)
        {
            return code;
        }

        wax::String srcVar = VarName(edges[0]->m_source);

        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            PinId outPin = node.m_outputs[i];
            const Pin* pin = ctx.m_graph->FindPin(outPin);
            if (!pin)
            {
                continue;
            }

            const PType* t = ctx.m_validation->m_resolvedPinTypes.Find(outPin.m_value);
            if (!t)
            {
                continue;
            }

            code.Append("        ");
            code.Append(PTypeToCppType(*t));
            code.Append(" ");
            code.Append(VarName(outPin).CStr());
            code.Append(" = ");
            code.Append(srcVar.CStr());
            code.Append(".");
            code.Append(pin->m_name.CStr());
            code.Append(";\n");
        }

        return code;
    }

    static wax::String EmitStructMake(const Node& node, CodegenContext& ctx)
    {
        wax::String code;
        if (node.m_outputs.Size() == 0)
        {
            return code;
        }

        PinId outPin = node.m_outputs[0];
        const PType* t = ctx.m_validation->m_resolvedPinTypes.Find(outPin.m_value);
        if (!t)
        {
            return code;
        }

        const char* cppType = PTypeToCppType(*t);

        code.Append("        ");
        code.Append(cppType);
        code.Append(" ");
        code.Append(VarName(outPin).CStr());
        code.Append("{};\n");

        for (size_t i = 0; i < node.m_inputs.Size(); ++i)
        {
            const Pin* pin = ctx.m_graph->FindPin(node.m_inputs[i]);
            if (!pin || pin->m_type.m_kind == PTypeKind::SIGNAL)
            {
                continue;
            }

            auto edges = ctx.m_graph->EdgesForPin(pin->m_id);
            if (edges.Size() == 0)
            {
                continue;
            }

            code.Append("        ");
            code.Append(VarName(outPin).CStr());
            code.Append(".");
            code.Append(pin->m_name.CStr());
            code.Append(" = ");
            code.Append(VarName(edges[0]->m_source).CStr());
            code.Append(";\n");
        }

        return code;
    }

    static wax::String EmitArgList(const Node& node, const wax::Vector<PType>& paramTypes, CodegenContext& ctx)
    {
        wax::String code;
        size_t typeIdx = 0;
        bool first = true;
        // Skip exec pins: void user-fn nodes carry an ExecIn alongside their data params.
        for (size_t i = 0; i < node.m_inputs.Size(); ++i)
        {
            const Pin* pin = ctx.m_graph->FindPin(node.m_inputs[i]);
            if (!pin || pin->m_isExec || pin->m_type.m_kind == PTypeKind::SIGNAL)
            {
                continue;
            }
            if (!first)
            {
                code.Append(", ");
            }
            first = false;

            auto edges = ctx.m_graph->EdgesForPin(pin->m_id);
            if (edges.Size() > 0)
            {
                code.Append(VarName(edges[0]->m_source).CStr());
            }
            else if (pin->m_defaultValue.Size() > 0)
            {
                code.Append(pin->m_defaultValue.CStr());
            }
            else if (typeIdx < paramTypes.Size())
            {
                code.Append(PTypeDefaultValue(paramTypes[typeIdx]));
            }
            else
            {
                code.Append(PTypeDefaultValue(pin->m_type));
            }
            ++typeIdx;
        }
        return code;
    }

    static wax::String EmitUserFunctionCall(const Node& node, CodegenContext& ctx)
    {
        wax::String code;
        const UserFunctionSignature& sig = node.m_userFn;

        wax::String args = EmitArgList(node, sig.m_paramTypes, ctx);

        if (sig.m_returnType.m_kind == PTypeKind::SIGNAL)
        {
            code.Append("        ");
            code.Append(sig.m_qualifiedCppName.CStr());
            code.Append("(");
            code.Append(args.CStr());
            code.Append(");\n");
            return code;
        }

        PinId outPin{};
        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            const Pin* p = ctx.m_graph->FindPin(node.m_outputs[i]);
            if (p && !p->m_isExec && p->m_type.m_kind != PTypeKind::SIGNAL)
            {
                outPin = p->m_id;
                break;
            }
        }
        if (!outPin)
        {
            return code;
        }

        const PType* t = ctx.m_validation->m_resolvedPinTypes.Find(outPin.m_value);
        PType outType = t ? *t : sig.m_returnType;

        code.Append("        ");
        code.Append(PTypeToCppType(outType));
        code.Append(" ");
        code.Append(VarName(outPin).CStr());
        code.Append(" = ");
        code.Append(sig.m_qualifiedCppName.CStr());
        code.Append("(");
        code.Append(args.CStr());
        code.Append(");\n");
        return code;
    }

    static wax::String EmitPrint(const Node& node, CodegenContext& ctx)
    {
        wax::String code;

        PinId valuePin{};
        for (size_t i = 0; i < node.m_inputs.Size(); ++i)
        {
            const Pin* pin = ctx.m_graph->FindPin(node.m_inputs[i]);
            if (pin && pin->m_type.m_kind != PTypeKind::SIGNAL)
            {
                valuePin = pin->m_id;
                break;
            }
        }

        if (!valuePin)
        {
            return code;
        }

        auto edges = ctx.m_graph->EdgesForPin(valuePin);
        if (edges.Size() == 0)
        {
            return code;
        }

        wax::String srcVar = VarName(edges[0]->m_source);

        code.Append("        propolis::ScriptLog(\"{}\", ");
        code.Append(srcVar.CStr());
        code.Append(");\n");

        return code;
    }

    wax::String EmitNode(const Node& node, const NodeDescriptor& desc, CodegenContext& ctx)
    {
        if (HasFlag(desc.m_flags, NodeFlag::LIFECYCLE))   return EmitLifecycleAliases(node, ctx);
        if (HasFlag(desc.m_flags, NodeFlag::VARIABLE_GET))   return EmitGetVariable(node, ctx);
        if (HasFlag(desc.m_flags, NodeFlag::VARIABLE_SET))   return EmitSetVariable(node, ctx);
        if (HasFlag(desc.m_flags, NodeFlag::COMPONENT_GET))  return EmitGetComponentField(node, ctx);
        if (HasFlag(desc.m_flags, NodeFlag::COMPONENT_SET))  return EmitSetComponentField(node, ctx);
        if (HasFlag(desc.m_flags, NodeFlag::STRUCT_BREAK))   return EmitStructBreak(node, ctx);
        if (HasFlag(desc.m_flags, NodeFlag::STRUCT_MAKE))    return EmitStructMake(node, ctx);
        if (HasFlag(desc.m_flags, NodeFlag::PRINT))          return EmitPrint(node, ctx);

        wax::String code;
        for (size_t i = 0; i < node.m_outputs.Size(); ++i)
        {
            PinId outPin = node.m_outputs[i];
            const PType* t = ctx.m_validation->m_resolvedPinTypes.Find(outPin.m_value);
            if (!t)
            {
                continue;
            }

            code.Append("        ");
            code.Append(PTypeToCppType(*t));
            code.Append(" ");
            code.Append(VarName(outPin).CStr());
            code.Append(" = ");
            code.Append(SubstituteTemplate(desc.m_codegenTemplate, node, ctx).CStr());
            code.Append(";\n");
        }

        return code;
    }

    wax::String EmitUserFunctionNode(const Node& node, CodegenContext& ctx)
    {
        return EmitUserFunctionCall(node, ctx);
    }

    wax::Vector<NodeId> ReachableFrom(NodeId root, const Graph& graph)
    {
        wax::Vector<NodeId> result;
        wax::HashSet<uint32_t> visited;
        wax::Vector<NodeId> stack;
        stack.PushBack(root);

        while (stack.Size() > 0)
        {
            NodeId current = stack[stack.Size() - 1];
            stack.PopBack();

            if (visited.Contains(current.m_value))
            {
                continue;
            }
            visited.Insert(current.m_value);
            result.PushBack(current);

            const Node* node = graph.FindNode(current);
            if (!node)
            {
                continue;
            }

            for (size_t oi = 0; oi < node->m_outputs.Size(); ++oi)
            {
                auto edges = graph.EdgesForPin(node->m_outputs[oi]);
                for (size_t ei = 0; ei < edges.Size(); ++ei)
                {
                    const Pin* targetPin = graph.FindPin(edges[ei]->m_target);
                    if (targetPin && !visited.Contains(targetPin->m_owner.m_value))
                    {
                        stack.PushBack(targetPin->m_owner);
                    }
                }
            }
        }

        size_t scanIdx = 0;
        while (scanIdx < result.Size())
        {
            const Node* node = graph.FindNode(result[scanIdx]);
            ++scanIdx;
            if (!node)
            {
                continue;
            }

            for (size_t ii = 0; ii < node->m_inputs.Size(); ++ii)
            {
                auto edges = graph.EdgesForPin(node->m_inputs[ii]);
                for (size_t ei = 0; ei < edges.Size(); ++ei)
                {
                    const Pin* srcPin = graph.FindPin(edges[ei]->m_source);
                    if (srcPin && !visited.Contains(srcPin->m_owner.m_value))
                    {
                        visited.Insert(srcPin->m_owner.m_value);
                        result.PushBack(srcPin->m_owner);
                    }
                }
            }
        }

        return result;
    }

    void EmitDataDeps(NodeId nodeId, CodegenContext& ctx, wax::String& code)
    {
        const Node* node = ctx.m_graph->FindNode(nodeId);
        if (!node)
        {
            return;
        }

        for (size_t i = 0; i < node->m_inputs.Size(); ++i)
        {
            const Pin* pin = ctx.m_graph->FindPin(node->m_inputs[i]);
            if (!pin || pin->m_type.m_kind == PTypeKind::SIGNAL)
            {
                continue;
            }

            auto edges = ctx.m_graph->EdgesForPin(node->m_inputs[i]);
            for (size_t ei = 0; ei < edges.Size(); ++ei)
            {
                const Pin* srcPin = ctx.m_graph->FindPin(edges[ei]->m_source);
                if (!srcPin)
                {
                    continue;
                }

                if (ctx.m_emitted.Contains(srcPin->m_owner.m_value))
                {
                    continue;
                }

                EmitDataDeps(srcPin->m_owner, ctx, code);

                const Node* depNode = ctx.m_graph->FindNode(srcPin->m_owner);
                if (!depNode)
                {
                    continue;
                }

                const NodeDescriptor* depDesc = ctx.m_registry->Find(depNode->m_title.CStr());
                if (!depDesc)
                {
                    if (depNode->m_userFn.m_qualifiedCppName.Size() > 0)
                    {
                        code.Append(EmitUserFunctionNode(*depNode, ctx).CStr());
                        ctx.m_emitted.Insert(srcPin->m_owner.m_value);
                    }
                    continue;
                }

                code.Append(EmitNode(*depNode, *depDesc, ctx).CStr());
                ctx.m_emitted.Insert(srcPin->m_owner.m_value);
            }
        }
    }

    void EmitDataFlowBody(NodeId lifecycleNode, CodegenContext& ctx, wax::String& code)
    {
        auto reachable = ReachableFrom(lifecycleNode, *ctx.m_graph);
        wax::HashSet<uint32_t> reachSet;
        for (size_t i = 0; i < reachable.Size(); ++i)
        {
            reachSet.Insert(reachable[i].m_value);
        }

        auto sorted = ctx.m_graph->TopologicalSort();
        for (size_t i = 0; i < sorted.Size(); ++i)
        {
            if (!reachSet.Contains(sorted[i].m_value))
            {
                continue;
            }
            if (ctx.m_emitted.Contains(sorted[i].m_value))
            {
                continue;
            }

            const Node* node = ctx.m_graph->FindNode(sorted[i]);
            if (!node)
            {
                continue;
            }

            const NodeDescriptor* desc = ctx.m_registry->Find(node->m_title.CStr());
            if (!desc)
            {
                if (node->m_userFn.m_qualifiedCppName.Size() > 0)
                {
                    code.Append(EmitUserFunctionNode(*node, ctx).CStr());
                    ctx.m_emitted.Insert(sorted[i].m_value);
                }
                else
                {
                    hive::LogWarning(LOG_PROPOLIS_COMPILER,
                                     "Unknown node '{}' in graph", node->m_title.CStr());
                }
                continue;
            }

            code.Append(EmitNode(*node, *desc, ctx).CStr());
            ctx.m_emitted.Insert(sorted[i].m_value);
        }
    }
} // namespace propolis
