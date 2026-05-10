#pragma once

#include <propolis/compiler/graph_validator.h>
#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>
#include <propolis/runtime/function_registry.h>

#include <hive/hive_config.h>

#include <wax/containers/hash_set.h>
#include <wax/containers/string.h>

namespace propolis
{
    struct CodegenOptions
    {
        wax::String m_systemName{"generated_system"};
        wax::String m_namespaceName{"propolis::generated"};
    };

    struct CodegenContext
    {
        const Graph* m_graph{};
        const ValidationResult* m_validation{};
        const NodeRegistry* m_registry{};
        const FunctionRegistry* m_functionRegistry{};
        const CodegenOptions* m_options{};
        wax::HashSet<uint32_t> m_emitted;
    };

    class HIVE_API Codegen
    {
    public:
        [[nodiscard]] wax::String Generate(const Graph& graph, const ValidationResult& validation,
                                           const NodeRegistry& registry,
                                           const CodegenOptions& options = {},
                                           const FunctionRegistry* functions = nullptr);

    private:
        wax::String GenerateSystemGraph(CodegenContext& ctx);
        wax::String GenerateEntityScript(CodegenContext& ctx);
        wax::String EmitStateStruct(CodegenContext& ctx);
        wax::String EmitRegistrationFunction(CodegenContext& ctx);
        wax::String EmitUserFunctionForwardDecls(CodegenContext& ctx);
    };
} // namespace propolis
