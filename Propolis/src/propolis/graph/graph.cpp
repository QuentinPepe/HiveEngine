#include <propolis/graph/graph.h>

#include <propolis/core/log.h>

namespace propolis
{
    NodeId Graph::AddNode(const char* title, const char* category)
    {
        NodeId id{m_nextNodeId++};
        size_t index = m_nodes.Size();
        auto& node = m_nodes.EmplaceBack();
        node.m_id = id;
        node.m_title = title;
        node.m_category = category;
        m_nodeIndex.Insert(id.m_value, index);
        return id;
    }

    PinId Graph::AddPin(NodeId nodeId, PinDirection direction, PType type, const char* name)
    {
        Node* node = FindNode(nodeId);
        if (!node)
        {
            return kInvalidPin;
        }

        PinId id{m_nextPinId++};
        size_t index = m_pins.Size();
        auto& pin = m_pins.EmplaceBack();
        pin.m_id = id;
        pin.m_owner = nodeId;
        pin.m_direction = direction;
        pin.m_type = type;
        pin.m_name = name;
        m_pinIndex.Insert(id.m_value, index);

        if (direction == PinDirection::INPUT)
        {
            node->m_inputs.PushBack(id);
        }
        else
        {
            node->m_outputs.PushBack(id);
        }

        return id;
    }

    EdgeId Graph::AddEdge(PinId source, PinId target)
    {
        const Pin* srcPin = FindPin(source);
        const Pin* dstPin = FindPin(target);
        if (!srcPin || !dstPin)
        {
            hive::LogWarning(LOG_PROPOLIS_GRAPH, "AddEdge rejected: invalid pin id ({}->{})",
                             source.m_value, target.m_value);
            return kInvalidEdge;
        }
        if (srcPin->m_direction != PinDirection::OUTPUT || dstPin->m_direction != PinDirection::INPUT)
        {
            hive::LogWarning(LOG_PROPOLIS_GRAPH, "AddEdge rejected: direction mismatch ({}->{})",
                             source.m_value, target.m_value);
            return kInvalidEdge;
        }
        if (srcPin->m_owner == dstPin->m_owner)
        {
            hive::LogWarning(LOG_PROPOLIS_GRAPH, "AddEdge rejected: same-node connection (node {})",
                             srcPin->m_owner.m_value);
            return kInvalidEdge;
        }

        EdgeId id{m_nextEdgeId++};
        size_t index = m_edges.Size();
        auto& edge = m_edges.EmplaceBack();
        edge.m_id = id;
        edge.m_source = source;
        edge.m_target = target;
        m_edgeIndex.Insert(id.m_value, index);
        return id;
    }

    static void EraseAt(wax::Vector<Node>& v, size_t i, wax::HashMap<uint32_t, size_t>& index)
    {
        index.Remove(v[i].m_id.m_value);
        v.Erase(v.Begin() + static_cast<ptrdiff_t>(i));
        for (size_t k = i; k < v.Size(); ++k)
        {
            *index.Find(v[k].m_id.m_value) = k;
        }
    }

    static void EraseAt(wax::Vector<Pin>& v, size_t i, wax::HashMap<uint32_t, size_t>& index)
    {
        index.Remove(v[i].m_id.m_value);
        v.Erase(v.Begin() + static_cast<ptrdiff_t>(i));
        for (size_t k = i; k < v.Size(); ++k)
        {
            *index.Find(v[k].m_id.m_value) = k;
        }
    }

    static void EraseAt(wax::Vector<Edge>& v, size_t i, wax::HashMap<uint32_t, size_t>& index)
    {
        index.Remove(v[i].m_id.m_value);
        v.Erase(v.Begin() + static_cast<ptrdiff_t>(i));
        for (size_t k = i; k < v.Size(); ++k)
        {
            *index.Find(v[k].m_id.m_value) = k;
        }
    }

    static void EraseAt(wax::Vector<Variable>& v, size_t i, wax::HashMap<uint32_t, size_t>& index)
    {
        index.Remove(v[i].m_id.m_value);
        v.Erase(v.Begin() + static_cast<ptrdiff_t>(i));
        for (size_t k = i; k < v.Size(); ++k)
        {
            *index.Find(v[k].m_id.m_value) = k;
        }
    }

    void Graph::RemoveNode(NodeId nodeId)
    {
        const Node* node = FindNode(nodeId);
        if (!node)
        {
            return;
        }

        wax::Vector<PinId> nodePins;
        nodePins.Reserve(node->m_inputs.Size() + node->m_outputs.Size());
        for (size_t i = 0; i < node->m_inputs.Size(); ++i)
        {
            nodePins.PushBack(node->m_inputs[i]);
        }
        for (size_t i = 0; i < node->m_outputs.Size(); ++i)
        {
            nodePins.PushBack(node->m_outputs[i]);
        }

        for (size_t i = m_edges.Size(); i > 0; --i)
        {
            const Edge& e = m_edges[i - 1];
            bool touches = false;
            for (size_t k = 0; k < nodePins.Size(); ++k)
            {
                if (nodePins[k] == e.m_source || nodePins[k] == e.m_target)
                {
                    touches = true;
                    break;
                }
            }
            if (touches)
            {
                EraseAt(m_edges, i - 1, m_edgeIndex);
            }
        }

        for (size_t i = m_pins.Size(); i > 0; --i)
        {
            if (m_pins[i - 1].m_owner == nodeId)
            {
                EraseAt(m_pins, i - 1, m_pinIndex);
            }
        }

        size_t* idx = m_nodeIndex.Find(nodeId.m_value);
        if (idx)
        {
            EraseAt(m_nodes, *idx, m_nodeIndex);
        }
    }

    void Graph::RemoveEdge(EdgeId edgeId)
    {
        size_t* idx = m_edgeIndex.Find(edgeId.m_value);
        if (idx)
        {
            EraseAt(m_edges, *idx, m_edgeIndex);
        }
    }

    const Node* Graph::FindNode(NodeId id) const noexcept
    {
        const size_t* idx = m_nodeIndex.Find(id.m_value);
        return idx ? &m_nodes[*idx] : nullptr;
    }

    Node* Graph::FindNode(NodeId id) noexcept
    {
        const size_t* idx = m_nodeIndex.Find(id.m_value);
        return idx ? &m_nodes[*idx] : nullptr;
    }

    const Pin* Graph::FindPin(PinId id) const noexcept
    {
        const size_t* idx = m_pinIndex.Find(id.m_value);
        return idx ? &m_pins[*idx] : nullptr;
    }

    Pin* Graph::FindPin(PinId id) noexcept
    {
        const size_t* idx = m_pinIndex.Find(id.m_value);
        return idx ? &m_pins[*idx] : nullptr;
    }

    const Edge* Graph::FindEdge(EdgeId id) const noexcept
    {
        const size_t* idx = m_edgeIndex.Find(id.m_value);
        return idx ? &m_edges[*idx] : nullptr;
    }

    const Pin* Graph::FindPinByName(NodeId nodeId, PinDirection direction, const char* name) const
    {
        const Node* node = FindNode(nodeId);
        if (!node)
        {
            return nullptr;
        }

        const auto& pinIds = (direction == PinDirection::INPUT) ? node->m_inputs : node->m_outputs;
        for (size_t i = 0; i < pinIds.Size(); ++i)
        {
            const Pin* pin = FindPin(pinIds[i]);
            if (pin && pin->m_name == name)
            {
                return pin;
            }
        }
        return nullptr;
    }

    VariableId Graph::AddVariable(const char* name, PType type)
    {
        VariableId id{m_nextVariableId++};
        size_t index = m_variables.Size();
        auto& var = m_variables.EmplaceBack();
        var.m_id = id;
        var.m_name = name;
        var.m_type = type;
        m_variableIndex.Insert(id.m_value, index);
        return id;
    }

    void Graph::RemoveVariable(VariableId varId)
    {
        for (size_t i = m_nodes.Size(); i > 0; --i)
        {
            if (m_nodes[i - 1].m_variableRef == varId)
            {
                RemoveNode(m_nodes[i - 1].m_id);
            }
        }

        size_t* idx = m_variableIndex.Find(varId.m_value);
        if (idx)
        {
            EraseAt(m_variables, *idx, m_variableIndex);
        }
    }

    const Variable* Graph::FindVariable(VariableId id) const noexcept
    {
        const size_t* idx = m_variableIndex.Find(id.m_value);
        return idx ? &m_variables[*idx] : nullptr;
    }

    Variable* Graph::FindVariable(VariableId id) noexcept
    {
        const size_t* idx = m_variableIndex.Find(id.m_value);
        return idx ? &m_variables[*idx] : nullptr;
    }

    const Variable* Graph::FindVariableByName(const char* name) const
    {
        for (size_t i = 0; i < m_variables.Size(); ++i)
        {
            if (m_variables[i].m_name == name)
            {
                return &m_variables[i];
            }
        }
        return nullptr;
    }

    wax::Vector<const Edge*> Graph::EdgesForPin(PinId pinId) const
    {
        wax::Vector<const Edge*> result;
        for (size_t i = 0; i < m_edges.Size(); ++i)
        {
            if (m_edges[i].m_source == pinId || m_edges[i].m_target == pinId)
            {
                result.PushBack(&m_edges[i]);
            }
        }
        return result;
    }

    wax::Vector<NodeId> Graph::TopologicalSort() const
    {
        wax::HashMap<uint32_t, wax::Vector<uint32_t>> adj;
        wax::HashMap<uint32_t, int> inDegree;

        for (size_t i = 0; i < m_nodes.Size(); ++i)
        {
            uint32_t nid = m_nodes[i].m_id.m_value;
            if (!adj.Contains(nid))
            {
                adj.Insert(nid, {});
            }
            inDegree.Insert(nid, 0);
        }

        for (size_t i = 0; i < m_edges.Size(); ++i)
        {
            const Pin* src = FindPin(m_edges[i].m_source);
            const Pin* dst = FindPin(m_edges[i].m_target);
            if (!src || !dst)
            {
                continue;
            }

            uint32_t from = src->m_owner.m_value;
            uint32_t to = dst->m_owner.m_value;
            if (from != to)
            {
                adj[from].PushBack(to);
                ++inDegree[to];
            }
        }

        wax::Vector<uint32_t> ready;
        for (size_t i = 0; i < m_nodes.Size(); ++i)
        {
            uint32_t nid = m_nodes[i].m_id.m_value;
            const int* deg = inDegree.Find(nid);
            if (deg && *deg == 0)
            {
                ready.PushBack(nid);
            }
        }

        wax::Vector<NodeId> result;
        result.Reserve(m_nodes.Size());

        while (ready.Size() > 0)
        {
            uint32_t curr = ready[ready.Size() - 1];
            ready.Erase(ready.Begin() + static_cast<ptrdiff_t>(ready.Size() - 1));
            result.PushBack(NodeId{curr});

            const auto* neighbors = adj.Find(curr);
            if (neighbors)
            {
                for (size_t i = 0; i < neighbors->Size(); ++i)
                {
                    uint32_t next = (*neighbors)[i];
                    int* deg = inDegree.Find(next);
                    if (deg && --(*deg) == 0)
                    {
                        ready.PushBack(next);
                    }
                }
            }
        }

        return result;
    }

    bool Graph::HasCycle() const
    {
        return TopologicalSort().Size() != m_nodes.Size();
    }

    void Graph::Clear() noexcept
    {
        m_nodes.Clear();
        m_pins.Clear();
        m_edges.Clear();
        m_variables.Clear();
        m_nodeIndex.Clear();
        m_pinIndex.Clear();
        m_edgeIndex.Clear();
        m_variableIndex.Clear();
        m_nextNodeId = 1;
        m_nextPinId = 1;
        m_nextEdgeId = 1;
        m_nextVariableId = 1;
    }

    void Graph::RebuildIndices()
    {
        m_nodeIndex.Clear();
        m_pinIndex.Clear();
        m_edgeIndex.Clear();
        m_variableIndex.Clear();
        for (size_t i = 0; i < m_nodes.Size(); ++i)
        {
            m_nodeIndex.Insert(m_nodes[i].m_id.m_value, i);
        }
        for (size_t i = 0; i < m_pins.Size(); ++i)
        {
            m_pinIndex.Insert(m_pins[i].m_id.m_value, i);
        }
        for (size_t i = 0; i < m_edges.Size(); ++i)
        {
            m_edgeIndex.Insert(m_edges[i].m_id.m_value, i);
        }
        for (size_t i = 0; i < m_variables.Size(); ++i)
        {
            m_variableIndex.Insert(m_variables[i].m_id.m_value, i);
        }
    }
} // namespace propolis
