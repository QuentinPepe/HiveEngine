#include <propolis/compiler/graph_validator.h>

#include <cstdio>

namespace propolis
{
    ValidationResult GraphValidator::Validate(const Graph& graph, const NodeRegistry& registry)
    {
        m_solver.Reset();
        m_pinTypes.Clear();
        m_errors.Clear();

        AssignPinTypes(graph, registry);
        ResolveVariableNodeTypes(graph, registry);
        UnifyEdges(graph);
        CheckConstraints(graph, registry);
        CheckEntityScriptRules(graph, registry);
        ResolveAll(graph);

        ValidationResult result;
        result.m_ok = m_errors.Size() == 0;
        result.m_errors = static_cast<wax::Vector<ValidationError>&&>(m_errors);
        result.m_resolvedPinTypes = static_cast<wax::HashMap<uint32_t, PType>&&>(m_pinTypes);
        return result;
    }

    void GraphValidator::AssignPinTypes(const Graph& graph, const NodeRegistry& registry)
    {
        for (size_t ni = 0; ni < graph.Nodes().Size(); ++ni)
        {
            const Node& node = graph.Nodes()[ni];
            const NodeDescriptor* desc = registry.Find(node.m_title.CStr());

            if (!desc)
            {
                const UserFunctionSignature& sig = node.m_userFn;
                if (sig.m_qualifiedCppName.Size() > 0)
                {
                    size_t paramIdx = 0;
                    for (size_t i = 0; i < node.m_inputs.Size(); ++i)
                    {
                        const Pin* p = graph.FindPin(node.m_inputs[i]);
                        if (!p)
                        {
                            continue;
                        }
                        // Exec pins (SIGNAL or marked) are not params.
                        if (p->m_isExec || p->m_type.m_kind == PTypeKind::SIGNAL)
                        {
                            m_pinTypes.Insert(p->m_id.m_value, PType::Signal());
                            continue;
                        }

                        PType paramT = paramIdx < sig.m_paramTypes.Size()
                                           ? sig.m_paramTypes[paramIdx]
                                           : m_solver.FreshVar();
                        if (p->m_type.IsResolved() && paramIdx < sig.m_paramTypes.Size())
                        {
                            auto u = m_solver.Unify(p->m_type, paramT);
                            if (!u.m_ok)
                            {
                                ValidationError err;
                                err.m_node = node.m_id;
                                err.m_pin = p->m_id;
                                err.m_message = "User function '";
                                err.m_message.Append(sig.m_qualifiedCppName.CStr());
                                err.m_message.Append("' param '");
                                err.m_message.Append(p->m_name.CStr());
                                err.m_message.Append("' type mismatch with C++ signature");
                                m_errors.PushBack(static_cast<ValidationError&&>(err));
                            }
                            m_pinTypes.Insert(p->m_id.m_value, p->m_type);
                        }
                        else
                        {
                            m_pinTypes.Insert(p->m_id.m_value, paramT);
                        }
                        ++paramIdx;
                    }

                    if (paramIdx != sig.m_paramTypes.Size())
                    {
                        ValidationError err;
                        err.m_node = node.m_id;
                        char buf[160];
                        std::snprintf(buf, sizeof(buf),
                                      "User function '%s' arity mismatch: graph has %zu data input(s), C++ signature has %zu",
                                      sig.m_qualifiedCppName.CStr(), paramIdx, sig.m_paramTypes.Size());
                        err.m_message = buf;
                        m_errors.PushBack(static_cast<ValidationError&&>(err));
                    }

                    bool returnAssigned = false;
                    for (size_t i = 0; i < node.m_outputs.Size(); ++i)
                    {
                        const Pin* p = graph.FindPin(node.m_outputs[i]);
                        if (!p)
                        {
                            continue;
                        }
                        if (p->m_isExec || p->m_type.m_kind == PTypeKind::SIGNAL)
                        {
                            m_pinTypes.Insert(p->m_id.m_value, PType::Signal());
                            continue;
                        }
                        PType t;
                        if (p->m_type.IsResolved())
                        {
                            t = p->m_type;
                            if (!returnAssigned && sig.m_returnType.m_kind != PTypeKind::SIGNAL)
                            {
                                auto u = m_solver.Unify(t, sig.m_returnType);
                                if (!u.m_ok)
                                {
                                    ValidationError err;
                                    err.m_node = node.m_id;
                                    err.m_pin = p->m_id;
                                    err.m_message = "User function '";
                                    err.m_message.Append(sig.m_qualifiedCppName.CStr());
                                    err.m_message.Append("' return type mismatch with C++ signature");
                                    m_errors.PushBack(static_cast<ValidationError&&>(err));
                                }
                                returnAssigned = true;
                            }
                        }
                        else if (!returnAssigned && sig.m_returnType.m_kind != PTypeKind::SIGNAL)
                        {
                            t = sig.m_returnType;
                            returnAssigned = true;
                        }
                        else
                        {
                            t = m_solver.FreshVar();
                        }
                        m_pinTypes.Insert(p->m_id.m_value, t);
                    }
                    continue;
                }

                for (size_t i = 0; i < node.m_inputs.Size(); ++i)
                {
                    const Pin* p = graph.FindPin(node.m_inputs[i]);
                    PType t = (p && p->m_type.IsResolved()) ? p->m_type : m_solver.FreshVar();
                    m_pinTypes.Insert(node.m_inputs[i].m_value, t);
                }
                for (size_t i = 0; i < node.m_outputs.Size(); ++i)
                {
                    const Pin* p = graph.FindPin(node.m_outputs[i]);
                    PType t = (p && p->m_type.IsResolved()) ? p->m_type : m_solver.FreshVar();
                    m_pinTypes.Insert(node.m_outputs[i].m_value, t);
                }
                continue;
            }

            PType genericVars[4];
            for (int g = 0; g < 4; ++g)
            {
                genericVars[g] = m_solver.FreshVar();
            }

            size_t inputIdx = 0;
            size_t outputIdx = 0;
            for (size_t pi = 0; pi < desc->m_pins.Size(); ++pi)
            {
                const PinDescriptor& pd = desc->m_pins[pi];
                PinId pinId;

                if (pd.m_direction == PinDirection::INPUT)
                {
                    if (inputIdx < node.m_inputs.Size())
                    {
                        pinId = node.m_inputs[inputIdx++];
                    }
                    else
                    {
                        continue;
                    }
                }
                else
                {
                    if (outputIdx < node.m_outputs.Size())
                    {
                        pinId = node.m_outputs[outputIdx++];
                    }
                    else
                    {
                        continue;
                    }
                }

                const Pin* graphPin = graph.FindPin(pinId);
                if (graphPin && graphPin->m_type.IsResolved())
                {
                    m_pinTypes.Insert(pinId.m_value, graphPin->m_type);
                }
                else if (pd.m_isGeneric)
                {
                    m_pinTypes.Insert(pinId.m_value, genericVars[pd.m_genericGroup % 4]);
                }
                else
                {
                    m_pinTypes.Insert(pinId.m_value, pd.m_type);
                }
            }

            for (; inputIdx < node.m_inputs.Size(); ++inputIdx)
            {
                const Pin* p = graph.FindPin(node.m_inputs[inputIdx]);
                PType t = (p && p->m_type.IsResolved()) ? p->m_type : m_solver.FreshVar();
                m_pinTypes.Insert(node.m_inputs[inputIdx].m_value, t);
            }
            for (; outputIdx < node.m_outputs.Size(); ++outputIdx)
            {
                const Pin* p = graph.FindPin(node.m_outputs[outputIdx]);
                PType t = (p && p->m_type.IsResolved()) ? p->m_type : m_solver.FreshVar();
                m_pinTypes.Insert(node.m_outputs[outputIdx].m_value, t);
            }
        }
    }

    void GraphValidator::UnifyEdges(const Graph& graph)
    {
        for (size_t i = 0; i < graph.Edges().Size(); ++i)
        {
            const Edge& edge = graph.Edges()[i];
            PType* srcType = m_pinTypes.Find(edge.m_source.m_value);
            PType* dstType = m_pinTypes.Find(edge.m_target.m_value);

            if (!srcType || !dstType)
            {
                continue;
            }

            auto result = m_solver.Unify(*srcType, *dstType);
            if (!result.m_ok)
            {
                const Pin* srcPin = graph.FindPin(edge.m_source);
                const Pin* dstPin = graph.FindPin(edge.m_target);

                char buf[256];
                std::snprintf(buf, sizeof(buf), "Type mismatch: %s -> %s: %s",
                              srcPin ? srcPin->m_name.CStr() : "?",
                              dstPin ? dstPin->m_name.CStr() : "?",
                              result.m_error.CStr());

                ValidationError err;
                err.m_node = srcPin ? srcPin->m_owner : NodeId{};
                err.m_pin = edge.m_source;
                err.m_message = buf;
                m_errors.PushBack(static_cast<ValidationError&&>(err));
            }
        }
    }

    void GraphValidator::CheckConstraints(const Graph& graph, const NodeRegistry& registry)
    {
        for (size_t ni = 0; ni < graph.Nodes().Size(); ++ni)
        {
            const Node& node = graph.Nodes()[ni];
            const NodeDescriptor* desc = registry.Find(node.m_title.CStr());
            if (!desc)
            {
                continue;
            }

            size_t inputIdx = 0;
            size_t outputIdx = 0;
            for (size_t pi = 0; pi < desc->m_pins.Size(); ++pi)
            {
                const PinDescriptor& pd = desc->m_pins[pi];
                PinId pinId;

                if (pd.m_direction == PinDirection::INPUT)
                {
                    if (inputIdx < node.m_inputs.Size())
                    {
                        pinId = node.m_inputs[inputIdx++];
                    }
                    else
                    {
                        continue;
                    }
                }
                else
                {
                    if (outputIdx < node.m_outputs.Size())
                    {
                        pinId = node.m_outputs[outputIdx++];
                    }
                    else
                    {
                        continue;
                    }
                }

                if (pd.m_constraint == Constraint::NONE)
                {
                    continue;
                }

                PType* t = m_pinTypes.Find(pinId.m_value);
                if (!t)
                {
                    continue;
                }

                PType resolved = m_solver.Resolve(*t);
                if (!m_solver.SatisfiesConstraint(resolved, pd.m_constraint))
                {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "Pin '%s' on node '%s': %s does not satisfy constraint",
                                  pd.m_name.CStr(), node.m_title.CStr(), PTypeKindName(resolved.m_kind));

                    ValidationError err;
                    err.m_node = node.m_id;
                    err.m_pin = pinId;
                    err.m_message = buf;
                    m_errors.PushBack(static_cast<ValidationError&&>(err));
                }
            }
        }
    }

    void GraphValidator::ResolveVariableNodeTypes(const Graph& graph, const NodeRegistry& registry)
    {
        for (size_t ni = 0; ni < graph.Nodes().Size(); ++ni)
        {
            const Node& node = graph.Nodes()[ni];
            const NodeDescriptor* desc = registry.Find(node.m_title.CStr());
            if (!desc)
            {
                continue;
            }

            if (HasFlag(desc->m_flags, NodeFlag::VARIABLE_GET))
            {
                const Variable* var = graph.FindVariable(node.m_variableRef);
                if (var && node.m_outputs.Size() > 0)
                {
                    PType* t = m_pinTypes.Find(node.m_outputs[0].m_value);
                    if (t)
                    {
                        (void)m_solver.Unify(*t, var->m_type);
                    }
                }
            }
            else if (HasFlag(desc->m_flags, NodeFlag::VARIABLE_SET))
            {
                const Variable* var = graph.FindVariable(node.m_variableRef);
                if (var && node.m_inputs.Size() > 0)
                {
                    PType* t = m_pinTypes.Find(node.m_inputs[0].m_value);
                    if (t)
                    {
                        (void)m_solver.Unify(*t, var->m_type);
                    }
                }
            }
        }
    }

    void GraphValidator::CheckEntityScriptRules(const Graph& graph, const NodeRegistry& registry)
    {
        uint32_t onTickCount = 0;
        uint32_t onAttachCount = 0;
        uint32_t onDetachCount = 0;

        for (size_t ni = 0; ni < graph.Nodes().Size(); ++ni)
        {
            const Node& node = graph.Nodes()[ni];
            const NodeDescriptor* desc = registry.Find(node.m_title.CStr());
            if (!desc)
            {
                continue;
            }

            if (HasFlag(desc->m_flags, NodeFlag::LIFECYCLE))
            {
                if (graph.Mode() != GraphMode::ENTITY_SCRIPT)
                {
                    ValidationError err;
                    err.m_node = node.m_id;
                    err.m_message = "Lifecycle node '";
                    err.m_message.Append(node.m_title.CStr());
                    err.m_message.Append("' is only valid in ENTITY_SCRIPT mode");
                    m_errors.PushBack(static_cast<ValidationError&&>(err));
                    continue;
                }

                if (node.m_title == "OnTick")        ++onTickCount;
                else if (node.m_title == "OnAttach") ++onAttachCount;
                else if (node.m_title == "OnDetach") ++onDetachCount;
            }

            if (HasFlag(desc->m_flags, NodeFlag::VARIABLE_GET) ||
                HasFlag(desc->m_flags, NodeFlag::VARIABLE_SET))
            {
                if (!node.m_variableRef || !graph.FindVariable(node.m_variableRef))
                {
                    ValidationError err;
                    err.m_node = node.m_id;
                    err.m_message = "Variable node '";
                    err.m_message.Append(node.m_title.CStr());
                    err.m_message.Append("' references invalid variable");
                    m_errors.PushBack(static_cast<ValidationError&&>(err));
                }
            }

            if (HasFlag(desc->m_flags, NodeFlag::COMPONENT_GET) ||
                HasFlag(desc->m_flags, NodeFlag::COMPONENT_SET))
            {
                if (node.m_componentRef.Size() == 0)
                {
                    ValidationError err;
                    err.m_node = node.m_id;
                    err.m_message = "Component node '";
                    err.m_message.Append(node.m_title.CStr());
                    err.m_message.Append("' missing component reference");
                    m_errors.PushBack(static_cast<ValidationError&&>(err));
                }
            }
        }

        auto emitDuplicate = [&](const char* name, uint32_t count) {
            if (count > 1)
            {
                ValidationError err;
                err.m_message = "Multiple '";
                err.m_message.Append(name);
                err.m_message.Append("' nodes (max 1 per graph)");
                m_errors.PushBack(static_cast<ValidationError&&>(err));
            }
        };
        emitDuplicate("OnTick", onTickCount);
        emitDuplicate("OnAttach", onAttachCount);
        emitDuplicate("OnDetach", onDetachCount);
    }

    void GraphValidator::ResolveAll(const Graph& graph)
    {
        for (size_t i = 0; i < graph.Pins().Size(); ++i)
        {
            const Pin& pin = graph.Pins()[i];
            PType* t = m_pinTypes.Find(pin.m_id.m_value);
            if (t)
            {
                *t = m_solver.Resolve(*t);
            }
        }
    }
} // namespace propolis
