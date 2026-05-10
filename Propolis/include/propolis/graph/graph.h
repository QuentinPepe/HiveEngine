#pragma once

#include <propolis/graph/graph_types.h>
#include <propolis/types/ptype.h>

#include <hive/hive_config.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/string.h>
#include <wax/containers/vector.h>

#include <cstdint>

namespace propolis
{
    enum class GraphMode : uint8_t
    {
        SYSTEM_GRAPH,
        ENTITY_SCRIPT,
    };

    struct VariableId
    {
        uint32_t m_value{0};
        [[nodiscard]] bool operator==(VariableId other) const noexcept { return m_value == other.m_value; }
        [[nodiscard]] bool operator!=(VariableId other) const noexcept { return m_value != other.m_value; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_value != 0; }
    };

    inline constexpr VariableId kInvalidVariable{0};

    struct Variable
    {
        VariableId m_id;
        wax::String m_name;
        PType m_type;
        bool m_exposed{false};
        wax::String m_category;
    };

    struct Pin
    {
        PinId m_id;
        NodeId m_owner;
        PinDirection m_direction;
        PType m_type;
        wax::String m_name;
        bool m_isExec{false};
        wax::String m_defaultValue;
    };

    // Baked signature for blueprint nodes that reference a user C++ function exposed via
    // HIVE_BLUEPRINT_FUNCTION. The compiler CLI runs without access to the live
    // FunctionRegistry (the gameplay DLL isn't loaded during codegen), so this struct lets
    // it forward-declare and call the function from the generated `.propolis.cpp`.
    // Empty when the node is a built-in (NodeRegistry covers it).
    struct UserFunctionSignature
    {
        wax::String m_qualifiedCppName;
        PType m_returnType;
        wax::Vector<wax::String> m_paramNames;
        wax::Vector<PType> m_paramTypes;
    };

    struct Node
    {
        NodeId m_id;
        wax::String m_title;
        wax::String m_category;
        wax::Vector<PinId> m_inputs;
        wax::Vector<PinId> m_outputs;
        float m_posX{0};
        float m_posY{0};
        VariableId m_variableRef;
        wax::String m_componentRef;
        UserFunctionSignature m_userFn;
    };

    struct Edge
    {
        EdgeId m_id;
        PinId m_source;
        PinId m_target;
    };

    class HIVE_API Graph
    {
    public:
        [[nodiscard]] GraphMode Mode() const noexcept { return m_mode; }
        void SetMode(GraphMode mode) noexcept { m_mode = mode; }

        [[nodiscard]] const wax::String& Name() const noexcept { return m_name; }
        void SetName(const char* name) { m_name = name; }

        [[nodiscard]] NodeId AddNode(const char* title, const char* category = "");
        [[nodiscard]] PinId AddPin(NodeId node, PinDirection direction, PType type, const char* name);
        [[nodiscard]] EdgeId AddEdge(PinId source, PinId target);

        void RemoveNode(NodeId nodeId);
        void RemoveEdge(EdgeId edgeId);

        [[nodiscard]] const Node* FindNode(NodeId id) const noexcept;
        [[nodiscard]] const Pin* FindPin(PinId id) const noexcept;
        [[nodiscard]] const Edge* FindEdge(EdgeId id) const noexcept;
        [[nodiscard]] Node* FindNode(NodeId id) noexcept;
        [[nodiscard]] Pin* FindPin(PinId id) noexcept;

        [[nodiscard]] const Pin* FindPinByName(NodeId node, PinDirection direction, const char* name) const;

        [[nodiscard]] VariableId AddVariable(const char* name, PType type);
        void RemoveVariable(VariableId id);
        [[nodiscard]] const Variable* FindVariable(VariableId id) const noexcept;
        [[nodiscard]] Variable* FindVariable(VariableId id) noexcept;
        [[nodiscard]] const Variable* FindVariableByName(const char* name) const;

        [[nodiscard]] const wax::Vector<Node>& Nodes() const noexcept { return m_nodes; }
        [[nodiscard]] const wax::Vector<Pin>& Pins() const noexcept { return m_pins; }
        [[nodiscard]] const wax::Vector<Edge>& Edges() const noexcept { return m_edges; }
        [[nodiscard]] const wax::Vector<Variable>& Variables() const noexcept { return m_variables; }

        [[nodiscard]] wax::Vector<const Edge*> EdgesForPin(PinId pinId) const;
        [[nodiscard]] wax::Vector<NodeId> TopologicalSort() const;

        [[nodiscard]] bool HasCycle() const;
        void Clear() noexcept;

        [[nodiscard]] wax::String SerializeToJson() const;
        [[nodiscard]] bool DeserializeFromJson(const char* json);

    private:
        void RebuildIndices();

        GraphMode m_mode{GraphMode::ENTITY_SCRIPT};
        wax::String m_name{"Untitled"};

        uint32_t m_nextNodeId{1};
        uint32_t m_nextPinId{1};
        uint32_t m_nextEdgeId{1};
        uint32_t m_nextVariableId{1};

        wax::Vector<Node> m_nodes;
        wax::Vector<Pin> m_pins;
        wax::Vector<Edge> m_edges;
        wax::Vector<Variable> m_variables;

        wax::HashMap<uint32_t, size_t> m_nodeIndex;
        wax::HashMap<uint32_t, size_t> m_pinIndex;
        wax::HashMap<uint32_t, size_t> m_edgeIndex;
        wax::HashMap<uint32_t, size_t> m_variableIndex;
    };
} // namespace propolis
