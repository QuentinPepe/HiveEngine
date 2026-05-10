#include <propolis/compiler/codegen.h>
#include <propolis/compiler/graph_validator.h>
#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>

#include <larvae/larvae.h>

#include <cstring>

namespace
{
    using namespace propolis;

    static bool Contains(const wax::String& haystack, const char* needle)
    {
        return std::strstr(haystack.CStr(), needle) != nullptr;
    }

    auto t1 = larvae::RegisterTest("PropolisCodegen", "SimpleAddGeneratesCode", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        auto constA = g.AddNode("Constant");
        auto constAOut = g.AddPin(constA, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto constB = g.AddNode("Constant");
        auto constBOut = g.AddPin(constB, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        (void)g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");

        (void)g.AddEdge(constAOut, addA);
        (void)g.AddEdge(constBOut, addB);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "test_add";
        opts.m_namespaceName = "test";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(code.Size() > 0);
        larvae::AssertTrue(Contains(code, "void test_add()"));
        larvae::AssertTrue(Contains(code, "namespace test"));
        larvae::AssertTrue(Contains(code, "float"));
        larvae::AssertTrue(Contains(code, "+"));
    });

    auto t2 = larvae::RegisterTest("PropolisCodegen", "ChainedOpsGenerateTopoOrder", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        auto c1 = g.AddNode("Constant");
        auto c1Out = g.AddPin(c1, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto c2 = g.AddNode("Constant");
        auto c2Out = g.AddPin(c2, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        auto addOut = g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");

        (void)g.AddEdge(c1Out, addA);
        (void)g.AddEdge(c2Out, addB);

        auto negNode = g.AddNode("Negate");
        auto negA = g.AddPin(negNode, PinDirection::INPUT, PType{}, "A");
        (void)g.AddPin(negNode, PinDirection::OUTPUT, PType{}, "Result");

        (void)g.AddEdge(addOut, negA);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        auto code = codegen.Generate(g, validation, reg);

        const char* addPos = std::strstr(code.CStr(), " + ");
        const char* negPos = std::strstr(code.CStr(), " = -");
        larvae::AssertTrue(addPos != nullptr);
        larvae::AssertTrue(negPos != nullptr);
        larvae::AssertTrue(addPos < negPos);
    });

    auto t3 = larvae::RegisterTest("PropolisCodegen", "HeaderIncludesPresent", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);

        Codegen codegen;
        auto code = codegen.Generate(g, validation, reg);

        larvae::AssertTrue(Contains(code, "#include <cstdint>"));
        larvae::AssertTrue(Contains(code, "#include <cmath>"));
        larvae::AssertTrue(Contains(code, "DO NOT EDIT"));
    });

    auto t4 = larvae::RegisterTest("PropolisCodegen", "UnknownNodeEmitsComment", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        (void)g.AddNode("MyCustomNode");

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);

        Codegen codegen;
        auto code = codegen.Generate(g, validation, reg);

        larvae::AssertTrue(Contains(code, "Unknown node: MyCustomNode"));
    });

    auto t5 = larvae::RegisterTest("PropolisCodegen", "EntityScriptIncludesQueenAndMath", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        (void)g.AddVariable("Position", PType::Vec3());

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);

        Codegen codegen;
        auto code = codegen.Generate(g, validation, reg);

        larvae::AssertTrue(Contains(code, "#include <queen/world/world.h>"));
        larvae::AssertTrue(Contains(code, "#include <queen/core/entity.h>"));
        larvae::AssertTrue(Contains(code, "#include <hive/math/types.h>"));
    });
} // namespace
