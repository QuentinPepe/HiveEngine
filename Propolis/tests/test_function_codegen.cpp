#include <propolis/compiler/codegen.h>
#include <propolis/compiler/graph_validator.h>
#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>
#include <propolis/runtime/function_registry.h>

#include <larvae/larvae.h>

#include <cstring>

namespace
{
    using namespace propolis;

    static bool Contains(const wax::String& haystack, const char* needle)
    {
        return std::strstr(haystack.CStr(), needle) != nullptr;
    }

    // Helper: build a system graph that contains a single user-fn node.
    static void StampUserFnSignature(Graph& g, NodeId nodeId, const char* qualifiedName, PType ret,
                                      const ParamInfo* params, size_t paramCount)
    {
        Node* node = g.FindNode(nodeId);
        node->m_userFn.m_qualifiedCppName = qualifiedName;
        node->m_userFn.m_returnType = ret;
        for (size_t i = 0; i < paramCount; ++i)
        {
            node->m_userFn.m_paramNames.PushBack(wax::String{params[i].m_name});
            node->m_userFn.m_paramTypes.PushBack(params[i].m_type);
        }
    }

    auto t1 = larvae::RegisterTest("PropolisCodegen", "UserFunction_EmitsQualifiedCall", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        // Add a user-fn node "Damp" with two float inputs and one float output.
        NodeId damp = g.AddNode("Damp");
        (void)g.AddPin(damp, PinDirection::INPUT, PType::Float32(), "current");
        (void)g.AddPin(damp, PinDirection::INPUT, PType::Float32(), "target");
        (void)g.AddPin(damp, PinDirection::OUTPUT, PType::Float32(), "Result");

        ParamInfo params[] = {
            {"current", PType::Float32()},
            {"target", PType::Float32()},
        };
        StampUserFnSignature(g, damp, "mygame::Damp", PType::Float32(), params, 2);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "test_user_fn";
        opts.m_namespaceName = "test";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "mygame::Damp"));
        larvae::AssertTrue(Contains(code, "namespace mygame { float Damp(float, float); }"));
    });

    auto t2 = larvae::RegisterTest("PropolisCodegen", "UserFunction_VoidReturn_NoBinding", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        NodeId logn = g.AddNode("Log");
        (void)g.AddPin(logn, PinDirection::INPUT, PType::Float32(), "value");

        ParamInfo params[] = {{"value", PType::Float32()}};
        StampUserFnSignature(g, logn, "mygame::Log", PType::Signal(), params, 1);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "test_void";
        opts.m_namespaceName = "test";
        auto code = codegen.Generate(g, validation, reg, opts);

        // Forward declaration uses void for return.
        larvae::AssertTrue(Contains(code, "namespace mygame { void Log("));
        // Invocation with no `auto x = ...` binding.
        larvae::AssertTrue(Contains(code, "mygame::Log(0.0f);"));
    });

    auto t3 = larvae::RegisterTest("PropolisCodegen", "UserFunction_DefaultValuesUsed", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        NodeId damp = g.AddNode("Damp");
        PinId p0 = g.AddPin(damp, PinDirection::INPUT, PType::Float32(), "a");
        PinId p1 = g.AddPin(damp, PinDirection::INPUT, PType::Float32(), "b");
        (void)g.AddPin(damp, PinDirection::OUTPUT, PType::Float32(), "Result");

        // Set a default value on p0; p1 falls back to PTypeDefaultValue.
        g.FindPin(p0)->m_defaultValue = "1.5f";
        (void)p1;

        ParamInfo params[] = {
            {"a", PType::Float32()},
            {"b", PType::Float32()},
        };
        StampUserFnSignature(g, damp, "mygame::Damp", PType::Float32(), params, 2);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "test_default";
        opts.m_namespaceName = "test";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "mygame::Damp(1.5f, 0.0f)"));
    });

    auto tArity = larvae::RegisterTest("PropolisCodegen", "UserFunction_ArityMismatch_RaisesError", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        // Graph claims 1 input but signature claims 2 params.
        NodeId fn = g.AddNode("Bad");
        (void)g.AddPin(fn, PinDirection::INPUT, PType::Float32(), "a");
        (void)g.AddPin(fn, PinDirection::OUTPUT, PType::Float32(), "Result");

        ParamInfo params[] = {
            {"a", PType::Float32()},
            {"b", PType::Float32()},
        };
        StampUserFnSignature(g, fn, "ns::Bad", PType::Float32(), params, 2);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(!validation.m_ok);
        larvae::AssertTrue(validation.m_errors.Size() > 0);
    });

    auto tTypeMismatch = larvae::RegisterTest("PropolisCodegen", "UserFunction_TypeMismatch_RaisesError", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        // Pin says Bool but signature says Float32 — incompatible (no numeric promotion).
        NodeId fn = g.AddNode("Stale");
        (void)g.AddPin(fn, PinDirection::INPUT, PType::Bool(), "x");
        (void)g.AddPin(fn, PinDirection::OUTPUT, PType::Float32(), "Result");

        ParamInfo params[] = {{"x", PType::Float32()}};
        StampUserFnSignature(g, fn, "ns::Stale", PType::Float32(), params, 1);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(!validation.m_ok);
        larvae::AssertTrue(validation.m_errors.Size() > 0);
    });

    auto tVoidExec = larvae::RegisterTest("PropolisCodegen", "UserFunction_Void_HasExecPins_EmitsInChain", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        NodeId logn = g.AddNode("Log");
        PinId execIn = g.AddPin(logn, PinDirection::INPUT, PType::Signal(), "Exec");
        if (auto* p = g.FindPin(execIn)) p->m_isExec = true;
        (void)g.AddPin(logn, PinDirection::INPUT, PType::Float32(), "value");
        PinId execOut = g.AddPin(logn, PinDirection::OUTPUT, PType::Signal(), "Exec");
        if (auto* p = g.FindPin(execOut)) p->m_isExec = true;

        ParamInfo params[] = {{"value", PType::Float32()}};
        StampUserFnSignature(g, logn, "ns::Log", PType::Signal(), params, 1);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "test_void_exec";
        opts.m_namespaceName = "test";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "ns::Log("));
    });

    auto t4 = larvae::RegisterTest("PropolisCodegen", "UserFunction_RoundTripsThroughJson", []() {
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        NodeId damp = g.AddNode("Damp");
        (void)g.AddPin(damp, PinDirection::INPUT, PType::Float32(), "current");
        (void)g.AddPin(damp, PinDirection::OUTPUT, PType::Float32(), "Result");

        ParamInfo params[] = {{"current", PType::Float32()}};
        StampUserFnSignature(g, damp, "mygame::Damp", PType::Float32(), params, 1);

        wax::String json = g.SerializeToJson();

        Graph reloaded;
        bool ok = reloaded.DeserializeFromJson(json.CStr());
        larvae::AssertTrue(ok);

        const Node* node = nullptr;
        for (size_t i = 0; i < reloaded.Nodes().Size(); ++i)
        {
            if (reloaded.Nodes()[i].m_title == "Damp")
            {
                node = &reloaded.Nodes()[i];
                break;
            }
        }
        larvae::AssertTrue(node != nullptr);
        larvae::AssertTrue(node->m_userFn.m_qualifiedCppName == "mygame::Damp");
        larvae::AssertTrue(node->m_userFn.m_returnType.m_kind == PTypeKind::FLOAT32);
        larvae::AssertEqual<size_t, size_t>(node->m_userFn.m_paramTypes.Size(), 1);
        larvae::AssertTrue(node->m_userFn.m_paramTypes[0].m_kind == PTypeKind::FLOAT32);
        larvae::AssertTrue(node->m_userFn.m_paramNames[0] == "current");
    });
} // namespace
