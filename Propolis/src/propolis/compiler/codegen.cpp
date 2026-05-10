#include <propolis/compiler/codegen.h>

#include <propolis/compiler/codegen_data_flow.h>
#include <propolis/compiler/codegen_exec_flow.h>
#include <propolis/compiler/cpp_type_mapper.h>

#include <wax/containers/string_view.h>

namespace propolis
{
    static bool GraphUsesMathTypes(const Graph& graph, const ValidationResult& validation)
    {
        for (size_t i = 0; i < graph.Variables().Size(); ++i)
        {
            switch (graph.Variables()[i].m_type.m_kind)
            {
            case PTypeKind::VEC2:
            case PTypeKind::VEC3:
            case PTypeKind::VEC4:
            case PTypeKind::QUAT:
            case PTypeKind::MAT4:
                return true;
            default:
                break;
            }
        }
        for (const auto& [pinId, type] : validation.m_resolvedPinTypes)
        {
            switch (type.m_kind)
            {
            case PTypeKind::VEC2:
            case PTypeKind::VEC3:
            case PTypeKind::VEC4:
            case PTypeKind::QUAT:
            case PTypeKind::MAT4:
                return true;
            default:
                break;
            }
        }
        return false;
    }

    static bool GraphUsesEntity(const Graph& graph, const ValidationResult& validation)
    {
        for (const auto& [pinId, type] : validation.m_resolvedPinTypes)
        {
            if (type.m_kind == PTypeKind::ENTITY)
            {
                return true;
            }
        }
        // Component nodes always need queen/world
        for (size_t i = 0; i < graph.Nodes().Size(); ++i)
        {
            if (graph.Nodes()[i].m_componentRef.Size() > 0)
            {
                return true;
            }
        }
        return false;
    }

    wax::String Codegen::Generate(const Graph& graph, const ValidationResult& validation,
                                  const NodeRegistry& registry,
                                  const CodegenOptions& options, const FunctionRegistry* functions)
    {
        CodegenContext ctx;
        ctx.m_graph = &graph;
        ctx.m_validation = &validation;
        ctx.m_registry = &registry;
        ctx.m_functionRegistry = functions;
        ctx.m_options = &options;

        if (graph.Mode() == GraphMode::ENTITY_SCRIPT)
        {
            return GenerateEntityScript(ctx);
        }
        return GenerateSystemGraph(ctx);
    }

    static void SplitQualifiedName(const wax::String& qualified,
                                    wax::Vector<wax::String>& namespaces, wax::String& fnName)
    {
        const char* s = qualified.CStr();
        size_t len = qualified.Size();
        size_t start = 0;
        for (size_t i = 0; i + 1 < len; ++i)
        {
            if (s[i] == ':' && s[i + 1] == ':')
            {
                wax::String segment;
                segment.Append(wax::StringView{s + start, i - start});
                namespaces.PushBack(static_cast<wax::String&&>(segment));
                start = i + 2;
                ++i;
            }
        }
        fnName.Append(wax::StringView{s + start, len - start});
    }

    wax::String Codegen::EmitUserFunctionForwardDecls(CodegenContext& ctx)
    {
        wax::String code;
        wax::Vector<wax::String> emitted;

        for (size_t i = 0; i < ctx.m_graph->Nodes().Size(); ++i)
        {
            const Node& n = ctx.m_graph->Nodes()[i];
            const UserFunctionSignature& sig = n.m_userFn;
            if (sig.m_qualifiedCppName.Size() == 0)
            {
                continue;
            }
            bool already = false;
            for (size_t k = 0; k < emitted.Size(); ++k)
            {
                if (emitted[k] == sig.m_qualifiedCppName)
                {
                    already = true;
                    break;
                }
            }
            if (already)
            {
                continue;
            }
            emitted.PushBack(sig.m_qualifiedCppName);

            wax::Vector<wax::String> namespaces;
            wax::String fnName;
            SplitQualifiedName(sig.m_qualifiedCppName, namespaces, fnName);

            for (size_t k = 0; k < namespaces.Size(); ++k)
            {
                code.Append("namespace ");
                code.Append(namespaces[k].CStr());
                code.Append(" { ");
            }
            code.Append(PTypeToCppType(sig.m_returnType));
            code.Append(" ");
            code.Append(fnName.CStr());
            code.Append("(");
            for (size_t k = 0; k < sig.m_paramTypes.Size(); ++k)
            {
                if (k > 0)
                {
                    code.Append(", ");
                }
                code.Append(PTypeToCppType(sig.m_paramTypes[k]));
            }
            code.Append(");");
            for (size_t k = 0; k < namespaces.Size(); ++k)
            {
                code.Append(" }");
            }
            code.Append("\n");
        }
        return code;
    }

    wax::String Codegen::EmitStateStruct(CodegenContext& ctx)
    {
        wax::String code;
        wax::String structName = SanitizeIdentifier(ctx.m_options->m_systemName);
        structName.Append("_State");

        code.Append("    struct ");
        code.Append(structName.CStr());
        code.Append("\n    {\n");

        const auto& vars = ctx.m_graph->Variables();
        for (size_t i = 0; i < vars.Size(); ++i)
        {
            const Variable& var = vars[i];
            code.Append("        ");
            code.Append(PTypeToCppType(var.m_type));
            code.Append(" ");
            code.Append(SanitizeIdentifier(var.m_name).CStr());
            code.Append("{");
            code.Append(PTypeDefaultValue(var.m_type));
            code.Append("};\n");
        }

        code.Append("    };\n\n");
        return code;
    }

    wax::String Codegen::EmitRegistrationFunction(CodegenContext& ctx)
    {
        wax::String code;
        wax::String safeName = SanitizeIdentifier(ctx.m_options->m_systemName);
        wax::String structName = safeName;
        structName.Append("_State");

        bool hasTick = false;
        bool hasAttach = false;
        bool hasDetach = false;
        for (size_t i = 0; i < ctx.m_graph->Nodes().Size(); ++i)
        {
            const auto& title = ctx.m_graph->Nodes()[i].m_title;
            if (title == "OnTick")        hasTick = true;
            else if (title == "OnAttach") hasAttach = true;
            else if (title == "OnDetach") hasDetach = true;
        }

        code.Append("    void Register_");
        code.Append(safeName.CStr());
        code.Append("(propolis::ScriptRegistry& registry)\n    {\n");
        code.Append("        propolis::ScriptEntry entry;\n");
        code.Append("        entry.m_name = \"");
        code.Append(ctx.m_options->m_systemName.CStr());
        code.Append("\";\n");

        if (ctx.m_graph->Variables().Size() > 0)
        {
            code.Append("        entry.m_stateSize = sizeof(");
            code.Append(structName.CStr());
            code.Append(");\n");
            code.Append("        entry.m_stateAlignment = alignof(");
            code.Append(structName.CStr());
            code.Append(");\n");
        }

        if (hasTick)
        {
            code.Append("        entry.m_onTick = [](queen::Entity e, void* s, queen::World& w, float dt) {\n");
            code.Append("            ");
            code.Append(safeName.CStr());
            code.Append("_OnTick(e, *static_cast<");
            code.Append(structName.CStr());
            code.Append("*>(s), w, dt);\n");
            code.Append("        };\n");
        }

        if (hasAttach)
        {
            code.Append("        entry.m_onAttach = [](queen::Entity e, void* s, queen::World& w) {\n");
            code.Append("            ");
            code.Append(safeName.CStr());
            code.Append("_OnAttach(e, *static_cast<");
            code.Append(structName.CStr());
            code.Append("*>(s), w);\n");
            code.Append("        };\n");
        }

        if (hasDetach)
        {
            code.Append("        entry.m_onDetach = [](queen::Entity e, void* s, queen::World& w) {\n");
            code.Append("            ");
            code.Append(safeName.CStr());
            code.Append("_OnDetach(e, *static_cast<");
            code.Append(structName.CStr());
            code.Append("*>(s), w);\n");
            code.Append("        };\n");
        }

        code.Append("        registry.Register(static_cast<propolis::ScriptEntry&&>(entry));\n");
        code.Append("    }\n");

        return code;
    }

    wax::String Codegen::GenerateSystemGraph(CodegenContext& ctx)
    {
        wax::String code;

        code.Append("// Auto-generated by Propolis compiler -- DO NOT EDIT\n");
        code.Append("#include <cstdint>\n");
        code.Append("#include <cmath>\n");
        code.Append("#include <algorithm>\n");
        if (GraphUsesMathTypes(*ctx.m_graph, *ctx.m_validation))
        {
            code.Append("#include <hive/math/types.h>\n");
        }
        if (GraphUsesEntity(*ctx.m_graph, *ctx.m_validation))
        {
            code.Append("#include <queen/core/entity.h>\n");
            code.Append("#include <queen/world/world.h>\n");
        }
        code.Append("\n");

        wax::String userFnDecls = EmitUserFunctionForwardDecls(ctx);
        if (userFnDecls.Size() > 0)
        {
            code.Append(userFnDecls.CStr());
            code.Append("\n");
        }

        code.Append("namespace ");
        code.Append(ctx.m_options->m_namespaceName.CStr());
        code.Append("\n{\n");
        code.Append("    void ");
        code.Append(ctx.m_options->m_systemName.CStr());
        code.Append("()\n    {\n");

        auto sorted = ctx.m_graph->TopologicalSort();
        for (size_t i = 0; i < sorted.Size(); ++i)
        {
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
                }
                else
                {
                    code.Append("        // Unknown node: ");
                    code.Append(node->m_title.CStr());
                    code.Append("\n");
                }
                continue;
            }

            code.Append(EmitNode(*node, *desc, ctx).CStr());
        }

        code.Append("    }\n");
        code.Append("} // namespace ");
        code.Append(ctx.m_options->m_namespaceName.CStr());
        code.Append("\n");

        return code;
    }

    wax::String Codegen::GenerateEntityScript(CodegenContext& ctx)
    {
        wax::String code;

        code.Append("// Auto-generated by Propolis compiler -- DO NOT EDIT\n");
        code.Append("#include <cstdint>\n");
        code.Append("#include <cmath>\n");
        code.Append("#include <algorithm>\n");
        code.Append("#include <propolis/runtime/script_registry.h>\n");
        code.Append("#include <propolis/runtime/script_log.h>\n");
        code.Append("#include <queen/core/entity.h>\n");
        code.Append("#include <queen/world/world.h>\n");
        if (GraphUsesMathTypes(*ctx.m_graph, *ctx.m_validation))
        {
            code.Append("#include <hive/math/types.h>\n");
        }
        code.Append("\n");

        wax::String userFnDecls = EmitUserFunctionForwardDecls(ctx);
        if (userFnDecls.Size() > 0)
        {
            code.Append(userFnDecls.CStr());
            code.Append("\n");
        }

        code.Append("namespace ");
        code.Append(ctx.m_options->m_namespaceName.CStr());
        code.Append("\n{\n");

        if (ctx.m_graph->Variables().Size() > 0)
        {
            code.Append(EmitStateStruct(ctx).CStr());
        }

        ctx.m_emitted.Clear();
        code.Append(EmitLifecycleFunction("OnAttach", ctx).CStr());
        ctx.m_emitted.Clear();
        code.Append(EmitLifecycleFunction("OnTick", ctx).CStr());
        ctx.m_emitted.Clear();
        code.Append(EmitLifecycleFunction("OnDetach", ctx).CStr());

        code.Append(EmitRegistrationFunction(ctx).CStr());

        code.Append("\n} // namespace ");
        code.Append(ctx.m_options->m_namespaceName.CStr());
        code.Append("\n");

        return code;
    }
} // namespace propolis
