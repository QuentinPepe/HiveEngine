#include <propolis/graph/graph.h>

#include <larvae/larvae.h>

namespace
{
    using namespace propolis;

    auto t1 = larvae::RegisterTest("PropolisGraph", "AddNodeReturnsValidId", []() {
        Graph g;
        auto id = g.AddNode("Test");
        larvae::AssertTrue(static_cast<bool>(id));
        larvae::AssertEqual(g.Nodes().Size(), size_t{1});
    });

    auto t2 = larvae::RegisterTest("PropolisGraph", "AddPinToNode", []() {
        Graph g;
        auto node = g.AddNode("Add", "Math");
        auto pinA = g.AddPin(node, PinDirection::INPUT, PType::Float32(), "A");
        auto pinB = g.AddPin(node, PinDirection::INPUT, PType::Float32(), "B");
        auto pinOut = g.AddPin(node, PinDirection::OUTPUT, PType::Float32(), "Result");

        larvae::AssertTrue(static_cast<bool>(pinA));
        larvae::AssertTrue(static_cast<bool>(pinB));
        larvae::AssertTrue(static_cast<bool>(pinOut));

        const auto* n = g.FindNode(node);
        larvae::AssertEqual(n->m_inputs.Size(), size_t{2});
        larvae::AssertEqual(n->m_outputs.Size(), size_t{1});
    });

    auto t3 = larvae::RegisterTest("PropolisGraph", "AddEdgeConnectsPins", []() {
        Graph g;
        auto n1 = g.AddNode("Source");
        auto out = g.AddPin(n1, PinDirection::OUTPUT, PType::Float32(), "Out");

        auto n2 = g.AddNode("Target");
        auto in = g.AddPin(n2, PinDirection::INPUT, PType::Float32(), "In");

        auto edge = g.AddEdge(out, in);
        larvae::AssertTrue(static_cast<bool>(edge));
        larvae::AssertEqual(g.Edges().Size(), size_t{1});
    });

    auto t4 = larvae::RegisterTest("PropolisGraph", "AddEdgeRejectsInputToInput", []() {
        Graph g;
        auto n1 = g.AddNode("A");
        auto in1 = g.AddPin(n1, PinDirection::INPUT, PType::Float32(), "In");

        auto n2 = g.AddNode("B");
        auto in2 = g.AddPin(n2, PinDirection::INPUT, PType::Float32(), "In");

        auto edge = g.AddEdge(in1, in2);
        larvae::AssertFalse(static_cast<bool>(edge));
    });

    auto t5 = larvae::RegisterTest("PropolisGraph", "AddEdgeRejectsSameNode", []() {
        Graph g;
        auto n = g.AddNode("Self");
        auto out = g.AddPin(n, PinDirection::OUTPUT, PType::Float32(), "Out");
        auto in = g.AddPin(n, PinDirection::INPUT, PType::Float32(), "In");

        auto edge = g.AddEdge(out, in);
        larvae::AssertFalse(static_cast<bool>(edge));
    });

    auto t6 = larvae::RegisterTest("PropolisGraph", "RemoveNodeCleansUpEdgesAndPins", []() {
        Graph g;
        auto n1 = g.AddNode("A");
        auto out = g.AddPin(n1, PinDirection::OUTPUT, PType::Float32(), "Out");

        auto n2 = g.AddNode("B");
        auto in = g.AddPin(n2, PinDirection::INPUT, PType::Float32(), "In");

        (void)g.AddEdge(out, in);
        larvae::AssertEqual(g.Edges().Size(), size_t{1});

        g.RemoveNode(n1);
        larvae::AssertEqual(g.Nodes().Size(), size_t{1});
        larvae::AssertEqual(g.Edges().Size(), size_t{0});

        larvae::AssertTrue(g.FindPin(in) != nullptr);
        larvae::AssertTrue(g.FindPin(out) == nullptr);
    });

    auto t7 = larvae::RegisterTest("PropolisGraph", "RemoveEdge", []() {
        Graph g;
        auto n1 = g.AddNode("A");
        auto out = g.AddPin(n1, PinDirection::OUTPUT, PType::Float32(), "Out");
        auto n2 = g.AddNode("B");
        auto in = g.AddPin(n2, PinDirection::INPUT, PType::Float32(), "In");

        auto edge = g.AddEdge(out, in);
        larvae::AssertEqual(g.Edges().Size(), size_t{1});

        g.RemoveEdge(edge);
        larvae::AssertEqual(g.Edges().Size(), size_t{0});
    });

    auto t8 = larvae::RegisterTest("PropolisGraph", "FindPinByName", []() {
        Graph g;
        auto n = g.AddNode("Lerp");
        (void)g.AddPin(n, PinDirection::INPUT, PType::Float32(), "A");
        (void)g.AddPin(n, PinDirection::INPUT, PType::Float32(), "B");
        (void)g.AddPin(n, PinDirection::INPUT, PType::Float32(), "Alpha");

        const auto* pin = g.FindPinByName(n, PinDirection::INPUT, "Alpha");
        larvae::AssertTrue(pin != nullptr);
        larvae::AssertTrue(pin->m_name == "Alpha");

        const auto* missing = g.FindPinByName(n, PinDirection::INPUT, "Nope");
        larvae::AssertTrue(missing == nullptr);
    });

    auto t9 = larvae::RegisterTest("PropolisGraph", "EdgesForPin", []() {
        Graph g;
        auto n1 = g.AddNode("A");
        auto out = g.AddPin(n1, PinDirection::OUTPUT, PType::Float32(), "Out");

        auto n2 = g.AddNode("B");
        auto in1 = g.AddPin(n2, PinDirection::INPUT, PType::Float32(), "In");

        auto n3 = g.AddNode("C");
        auto in2 = g.AddPin(n3, PinDirection::INPUT, PType::Float32(), "In");

        (void)g.AddEdge(out, in1);
        (void)g.AddEdge(out, in2);

        auto edges = g.EdgesForPin(out);
        larvae::AssertEqual(edges.Size(), size_t{2});

        auto edgesIn1 = g.EdgesForPin(in1);
        larvae::AssertEqual(edgesIn1.Size(), size_t{1});
    });

    auto t10 = larvae::RegisterTest("PropolisGraph", "TopologicalSortLinearChain", []() {
        Graph g;
        auto a = g.AddNode("A");
        auto aOut = g.AddPin(a, PinDirection::OUTPUT, PType::Float32(), "Out");

        auto b = g.AddNode("B");
        auto bIn = g.AddPin(b, PinDirection::INPUT, PType::Float32(), "In");
        auto bOut = g.AddPin(b, PinDirection::OUTPUT, PType::Float32(), "Out");

        auto c = g.AddNode("C");
        auto cIn = g.AddPin(c, PinDirection::INPUT, PType::Float32(), "In");

        (void)g.AddEdge(aOut, bIn);
        (void)g.AddEdge(bOut, cIn);

        auto sorted = g.TopologicalSort();
        larvae::AssertEqual(sorted.Size(), size_t{3});

        size_t posA = 0, posB = 0, posC = 0;
        for (size_t i = 0; i < sorted.Size(); ++i)
        {
            if (sorted[i] == a) posA = i;
            if (sorted[i] == b) posB = i;
            if (sorted[i] == c) posC = i;
        }
        larvae::AssertTrue(posA < posB);
        larvae::AssertTrue(posB < posC);
    });

    auto t11 = larvae::RegisterTest("PropolisGraph", "HasCycleReturnsFalseForDAG", []() {
        Graph g;
        auto a = g.AddNode("A");
        auto aOut = g.AddPin(a, PinDirection::OUTPUT, PType::Float32(), "Out");
        auto b = g.AddNode("B");
        auto bIn = g.AddPin(b, PinDirection::INPUT, PType::Float32(), "In");
        (void)g.AddEdge(aOut, bIn);

        larvae::AssertFalse(g.HasCycle());
    });

    auto t12 = larvae::RegisterTest("PropolisGraph", "DisconnectedNodesAreInTopoSort", []() {
        Graph g;
        (void)g.AddNode("Isolated1");
        (void)g.AddNode("Isolated2");
        (void)g.AddNode("Isolated3");

        auto sorted = g.TopologicalSort();
        larvae::AssertEqual(sorted.Size(), size_t{3});
        larvae::AssertFalse(g.HasCycle());
    });

    auto t13 = larvae::RegisterTest("PropolisGraph", "Clear", []() {
        Graph g;
        auto n = g.AddNode("Test");
        (void)g.AddPin(n, PinDirection::OUTPUT, PType::Float32(), "Out");

        g.Clear();
        larvae::AssertEqual(g.Nodes().Size(), size_t{0});
        larvae::AssertEqual(g.Pins().Size(), size_t{0});
        larvae::AssertEqual(g.Edges().Size(), size_t{0});
    });

    auto t14 = larvae::RegisterTest("PropolisGraph", "AddPinToInvalidNodeFails", []() {
        Graph g;
        auto pin = g.AddPin(kInvalidNode, PinDirection::INPUT, PType::Float32(), "X");
        larvae::AssertFalse(static_cast<bool>(pin));
    });

    auto t15 = larvae::RegisterTest("PropolisGraph", "AddEdgeToInvalidPinFails", []() {
        Graph g;
        auto edge = g.AddEdge(kInvalidPin, kInvalidPin);
        larvae::AssertFalse(static_cast<bool>(edge));
    });

    auto t16 = larvae::RegisterTest("PropolisGraph", "SerializeDeserializeRoundTrip", []() {
        Graph g;
        auto n1 = g.AddNode("Add", "Math");
        auto n1In = g.AddPin(n1, PinDirection::INPUT, PType::Float32(), "A");
        auto n1Out = g.AddPin(n1, PinDirection::OUTPUT, PType::Float32(), "Result");

        auto n2 = g.AddNode("Negate", "Math");
        auto n2In = g.AddPin(n2, PinDirection::INPUT, PType::Float32(), "A");
        (void)g.AddPin(n2, PinDirection::OUTPUT, PType::Float32(), "Result");

        (void)g.AddEdge(n1Out, n2In);

        auto* node1 = g.FindNode(n1);
        node1->m_posX = 100.0f;
        node1->m_posY = 200.0f;

        auto json = g.SerializeToJson();
        larvae::AssertTrue(json.Size() > 0);

        Graph g2;
        bool ok = g2.DeserializeFromJson(json.CStr());
        larvae::AssertTrue(ok);

        larvae::AssertEqual(g2.Nodes().Size(), size_t{2});
        larvae::AssertEqual(g2.Pins().Size(), size_t{4});
        larvae::AssertEqual(g2.Edges().Size(), size_t{1});

        const Node* restored = g2.FindNode(n1);
        larvae::AssertTrue(restored != nullptr);
        larvae::AssertTrue(restored->m_title == "Add");
        larvae::AssertTrue(restored->m_category == "Math");
        larvae::AssertTrue(restored->m_posX > 99.0f && restored->m_posX < 101.0f);

        const Pin* restoredPin = g2.FindPin(n1In);
        larvae::AssertTrue(restoredPin != nullptr);
        larvae::AssertTrue(restoredPin->m_type == PType::Float32());
        larvae::AssertTrue(restoredPin->m_name == "A");

        auto edges = g2.EdgesForPin(n1Out);
        larvae::AssertEqual(edges.Size(), size_t{1});

        auto newNode = g2.AddNode("New");
        larvae::AssertTrue(newNode.m_value > n2.m_value);
    });

    auto t17 = larvae::RegisterTest("PropolisGraph", "DeserializeInvalidJsonReturnsFalse", []() {
        Graph g;
        larvae::AssertFalse(g.DeserializeFromJson(nullptr));
        larvae::AssertFalse(g.DeserializeFromJson("not json"));
        larvae::AssertFalse(g.DeserializeFromJson("{}"));
    });

    auto t18 = larvae::RegisterTest("PropolisGraph", "AddVariableAndFind", []() {
        Graph g;
        auto id = g.AddVariable("Health", PType::Float32());
        larvae::AssertTrue(static_cast<bool>(id));
        larvae::AssertEqual(g.Variables().Size(), size_t{1});

        const auto* var = g.FindVariable(id);
        larvae::AssertTrue(var != nullptr);
        larvae::AssertTrue(var->m_name == "Health");
        larvae::AssertTrue(var->m_type == PType::Float32());
        larvae::AssertFalse(var->m_exposed);
    });

    auto t19 = larvae::RegisterTest("PropolisGraph", "FindVariableByName", []() {
        Graph g;
        (void)g.AddVariable("Speed", PType::Float32());
        (void)g.AddVariable("IsAlive", PType::Bool());

        larvae::AssertTrue(g.FindVariableByName("Speed") != nullptr);
        larvae::AssertTrue(g.FindVariableByName("IsAlive") != nullptr);
        larvae::AssertTrue(g.FindVariableByName("Nope") == nullptr);
    });

    auto t20 = larvae::RegisterTest("PropolisGraph", "RemoveVariableCleansUpRefNodes", []() {
        Graph g;
        auto varId = g.AddVariable("Temp", PType::Int32());

        auto nodeId = g.AddNode("GetVariable");
        auto* node = g.FindNode(nodeId);
        node->m_variableRef = varId;
        (void)g.AddPin(nodeId, PinDirection::OUTPUT, PType::Int32(), "Value");

        larvae::AssertEqual(g.Nodes().Size(), size_t{1});

        g.RemoveVariable(varId);
        larvae::AssertEqual(g.Variables().Size(), size_t{0});
        larvae::AssertEqual(g.Nodes().Size(), size_t{0});
    });

    auto t21 = larvae::RegisterTest("PropolisGraph", "VariableExposedToggle", []() {
        Graph g;
        auto id = g.AddVariable("HP", PType::Float32());
        auto* var = g.FindVariable(id);
        larvae::AssertFalse(var->m_exposed);

        var->m_exposed = true;
        larvae::AssertTrue(g.FindVariable(id)->m_exposed);
    });

    auto t22 = larvae::RegisterTest("PropolisGraph", "GraphModeAndName", []() {
        Graph g;
        larvae::AssertTrue(g.Mode() == GraphMode::ENTITY_SCRIPT);
        larvae::AssertTrue(g.Name() == "Untitled");

        g.SetMode(GraphMode::SYSTEM_GRAPH);
        g.SetName("GravitySystem");

        larvae::AssertTrue(g.Mode() == GraphMode::SYSTEM_GRAPH);
        larvae::AssertTrue(g.Name() == "GravitySystem");
    });

    auto t23 = larvae::RegisterTest("PropolisGraph", "VariablesSerializeRoundTrip", []() {
        Graph g;
        g.SetName("TestGraph");
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        auto v1 = g.AddVariable("Health", PType::Float32());
        auto* var1 = g.FindVariable(v1);
        var1->m_exposed = true;
        var1->m_category = "Stats";

        (void)g.AddVariable("Name", PType{PTypeKind::STRUCT, 0});

        auto json = g.SerializeToJson();

        Graph g2;
        larvae::AssertTrue(g2.DeserializeFromJson(json.CStr()));
        larvae::AssertTrue(g2.Name() == "TestGraph");
        larvae::AssertTrue(g2.Mode() == GraphMode::SYSTEM_GRAPH);
        larvae::AssertEqual(g2.Variables().Size(), size_t{2});

        const auto* restored = g2.FindVariable(v1);
        larvae::AssertTrue(restored != nullptr);
        larvae::AssertTrue(restored->m_name == "Health");
        larvae::AssertTrue(restored->m_type == PType::Float32());
        larvae::AssertTrue(restored->m_exposed);
        larvae::AssertTrue(restored->m_category == "Stats");
    });

    auto t24 = larvae::RegisterTest("PropolisGraph", "JsonRoundTripWithSpecialChars", []() {
        Graph g;
        g.SetName("Hello \"World\"\n\\Path");
        auto v = g.AddVariable("My \"Var\"", PType::Float32());
        auto* var = g.FindVariable(v);
        var->m_category = "Tab\there";

        auto json = g.SerializeToJson();
        Graph g2;
        larvae::AssertTrue(g2.DeserializeFromJson(json.CStr()));
        larvae::AssertTrue(g2.Name() == "Hello \"World\"\n\\Path");
        const auto* restored = g2.FindVariableByName("My \"Var\"");
        larvae::AssertTrue(restored != nullptr);
        larvae::AssertTrue(restored->m_category == "Tab\there");
    });

    auto t25 = larvae::RegisterTest("PropolisGraph", "DeserializeRejectsOrphanPin", []() {
        // Pin references node id 999 that doesn't exist
        const char* badJson =
            "{\"version\":1,\"name\":\"Bad\",\"mode\":1,\"variables\":[],\"nodes\":[],"
            "\"pins\":[{\"id\":1,\"node\":999,\"dir\":0,\"type\":3,\"param\":0,\"exec\":false,\"name\":\"x\"}],"
            "\"edges\":[]}";
        Graph g;
        larvae::AssertFalse(g.DeserializeFromJson(badJson));
    });

    auto t26 = larvae::RegisterTest("PropolisGraph", "DeserializeRejectsWrongVersion", []() {
        const char* wrongVersion =
            "{\"version\":99,\"name\":\"X\",\"mode\":1,\"variables\":[],\"nodes\":[],\"pins\":[],\"edges\":[]}";
        Graph g;
        larvae::AssertFalse(g.DeserializeFromJson(wrongVersion));
    });
} // namespace
