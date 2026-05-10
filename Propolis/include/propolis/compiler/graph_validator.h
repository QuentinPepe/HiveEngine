#pragma once

#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>
#include <propolis/types/type_solver.h>

#include <hive/hive_config.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/string.h>
#include <wax/containers/vector.h>

namespace propolis
{
    struct ValidationError
    {
        NodeId m_node;
        PinId m_pin;
        wax::String m_message;
    };

    struct ValidationResult
    {
        bool m_ok{false};
        wax::Vector<ValidationError> m_errors;
        wax::HashMap<uint32_t, PType> m_resolvedPinTypes;
    };

    class HIVE_API GraphValidator
    {
    public:
        [[nodiscard]] ValidationResult Validate(const Graph& graph, const NodeRegistry& registry);

    private:
        void AssignPinTypes(const Graph& graph, const NodeRegistry& registry);
        void UnifyEdges(const Graph& graph);
        void CheckConstraints(const Graph& graph, const NodeRegistry& registry);
        void CheckEntityScriptRules(const Graph& graph, const NodeRegistry& registry);
        void ResolveVariableNodeTypes(const Graph& graph, const NodeRegistry& registry);
        void ResolveAll(const Graph& graph);

        TypeSolver m_solver;
        wax::HashMap<uint32_t, PType> m_pinTypes;
        wax::Vector<ValidationError> m_errors;
    };
} // namespace propolis
