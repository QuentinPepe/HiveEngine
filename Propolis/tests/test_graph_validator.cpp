#include <propolis/compiler/graph_validator.h>
#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>

#include <larvae/larvae.h>

namespace
{
    using namespace propolis;

    auto t1 = larvae::RegisterTest("PropolisValidator", "ValidAddChainResolvesTypes", []() {
        NodeRegistry reg;
        Graph g;

        auto constNode = g.AddNode("Constant");
        auto constOut = g.AddPin(constNode, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        auto addOut = g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");

        (void)g.AddEdge(constOut, addA);
        (void)g.AddEdge(constOut, addB);

        GraphValidator validator;
        auto result = validator.Validate(g, reg);

        larvae::AssertTrue(result.m_ok);

        const PType* outType = result.m_resolvedPinTypes.Find(addOut.m_value);
        larvae::AssertTrue(outType != nullptr);
        larvae::AssertTrue(*outType == PType::Float32());
    });

    auto t2 = larvae::RegisterTest("PropolisValidator", "TypeMismatchReportsError", []() {
        NodeRegistry reg;
        Graph g;

        auto boolNode = g.AddNode("Constant");
        auto boolOut = g.AddPin(boolNode, PinDirection::OUTPUT, PType::Bool(), "Value");

        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        (void)g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");

        (void)g.AddEdge(boolOut, addA);
        (void)g.AddEdge(boolOut, addB);

        GraphValidator validator;
        auto result = validator.Validate(g, reg);

        larvae::AssertFalse(result.m_ok);
        larvae::AssertTrue(result.m_errors.Size() > 0);
    });

    auto t3 = larvae::RegisterTest("PropolisValidator", "IntToFloatPromotionWorks", []() {
        NodeRegistry reg;
        Graph g;

        auto intNode = g.AddNode("Constant");
        auto intOut = g.AddPin(intNode, PinDirection::OUTPUT, PType::Int32(), "Value");

        auto floatNode = g.AddNode("Constant");
        auto floatOut = g.AddPin(floatNode, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        auto addOut = g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");

        (void)g.AddEdge(intOut, addA);
        (void)g.AddEdge(floatOut, addB);

        GraphValidator validator;
        auto result = validator.Validate(g, reg);

        larvae::AssertTrue(result.m_ok);

        const PType* outType = result.m_resolvedPinTypes.Find(addOut.m_value);
        larvae::AssertTrue(outType != nullptr);
        larvae::AssertTrue(*outType == PType::Float32());
    });

    auto t4 = larvae::RegisterTest("PropolisValidator", "LerpAlphaMustBeFloat", []() {
        NodeRegistry reg;
        Graph g;

        auto f1 = g.AddNode("Constant");
        auto f1Out = g.AddPin(f1, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto f2 = g.AddNode("Constant");
        auto f2Out = g.AddPin(f2, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto alpha = g.AddNode("Constant");
        auto alphaOut = g.AddPin(alpha, PinDirection::OUTPUT, PType::Float32(), "Value");

        auto lerpNode = g.AddNode("Lerp");
        auto lerpA = g.AddPin(lerpNode, PinDirection::INPUT, PType{}, "A");
        auto lerpB = g.AddPin(lerpNode, PinDirection::INPUT, PType{}, "B");
        auto lerpAlpha = g.AddPin(lerpNode, PinDirection::INPUT, PType{}, "Alpha");
        auto lerpOut = g.AddPin(lerpNode, PinDirection::OUTPUT, PType{}, "Result");

        (void)g.AddEdge(f1Out, lerpA);
        (void)g.AddEdge(f2Out, lerpB);
        (void)g.AddEdge(alphaOut, lerpAlpha);

        GraphValidator validator;
        auto result = validator.Validate(g, reg);

        larvae::AssertTrue(result.m_ok);
        const PType* outType = result.m_resolvedPinTypes.Find(lerpOut.m_value);
        larvae::AssertTrue(outType != nullptr);
        larvae::AssertTrue(*outType == PType::Float32());
    });

    auto t5 = larvae::RegisterTest("PropolisValidator", "EmptyGraphIsValid", []() {
        NodeRegistry reg;
        Graph g;

        GraphValidator validator;
        auto result = validator.Validate(g, reg);
        larvae::AssertTrue(result.m_ok);
    });

    auto t6 = larvae::RegisterTest("PropolisValidator", "DisconnectedNodeIsValid", []() {
        NodeRegistry reg;
        Graph g;

        auto addNode = g.AddNode("Add");
        (void)g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        (void)g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        (void)g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");

        GraphValidator validator;
        auto result = validator.Validate(g, reg);

        larvae::AssertTrue(result.m_ok);
    });
} // namespace
