#include <propolis/compiler/codegen.h>
#include <propolis/compiler/graph_validator.h>
#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>
#include <propolis/runtime/script_registry.h>

#include <larvae/larvae.h>

#include <cstring>

namespace
{
    using namespace propolis;

    static bool Contains(const wax::String& haystack, const char* needle)
    {
        return std::strstr(haystack.CStr(), needle) != nullptr;
    }

    struct TickPins
    {
        NodeId node;
        PinId exec;
        PinId dt;
        PinId entity;
    };

    static TickPins AddOnTick(Graph& g)
    {
        TickPins t;
        t.node = g.AddNode("OnTick");
        t.exec = g.AddPin(t.node, PinDirection::OUTPUT, PType::Signal(), "Exec");
        t.dt = g.AddPin(t.node, PinDirection::OUTPUT, PType::Float32(), "DeltaTime");
        t.entity = g.AddPin(t.node, PinDirection::OUTPUT, PType::Entity(), "Entity");
        return t;
    }

    struct AttachPins
    {
        NodeId node;
        PinId exec;
        PinId entity;
    };

    static AttachPins AddOnAttach(Graph& g)
    {
        AttachPins t;
        t.node = g.AddNode("OnAttach");
        t.exec = g.AddPin(t.node, PinDirection::OUTPUT, PType::Signal(), "Exec");
        t.entity = g.AddPin(t.node, PinDirection::OUTPUT, PType::Entity(), "Entity");
        return t;
    }

    static AttachPins AddOnDetach(Graph& g)
    {
        AttachPins t;
        t.node = g.AddNode("OnDetach");
        t.exec = g.AddPin(t.node, PinDirection::OUTPUT, PType::Signal(), "Exec");
        t.entity = g.AddPin(t.node, PinDirection::OUTPUT, PType::Entity(), "Entity");
        return t;
    }

    struct SetVarPins
    {
        NodeId node;
        PinId execIn;
        PinId value;
        PinId execOut;
    };

    static SetVarPins AddSetVariable(Graph& g, VariableId varRef)
    {
        SetVarPins s;
        s.node = g.AddNode("SetVariable");
        s.execIn = g.AddPin(s.node, PinDirection::INPUT, PType::Signal(), "Exec");
        s.value = g.AddPin(s.node, PinDirection::INPUT, PType{}, "Value");
        s.execOut = g.AddPin(s.node, PinDirection::OUTPUT, PType::Signal(), "Exec");
        g.FindNode(s.node)->m_variableRef = varRef;
        return s;
    }

    // --- Node Registry ---

    auto t1 = larvae::RegisterTest("EntityScript", "LifecycleNodesRegistered", []() {
        NodeRegistry reg;
        larvae::AssertTrue(reg.Find("OnTick") != nullptr);
        larvae::AssertTrue(reg.Find("OnAttach") != nullptr);
        larvae::AssertTrue(reg.Find("OnDetach") != nullptr);
    });

    auto t2 = larvae::RegisterTest("EntityScript", "OnTickHasExecDeltaTimeAndEntity", []() {
        NodeRegistry reg;
        const auto* tick = reg.Find("OnTick");
        larvae::AssertTrue(tick != nullptr);
        larvae::AssertTrue(HasFlag(tick->m_flags, NodeFlag::LIFECYCLE));
        larvae::AssertEqual(tick->m_pins.Size(), size_t{3});
        larvae::AssertTrue(tick->m_pins[0].m_type == PType::Signal());
        larvae::AssertTrue(tick->m_pins[0].m_name == "Exec");
        larvae::AssertTrue(tick->m_pins[1].m_type == PType::Float32());
        larvae::AssertTrue(tick->m_pins[1].m_name == "DeltaTime");
        larvae::AssertTrue(tick->m_pins[2].m_type == PType::Entity());
        larvae::AssertTrue(tick->m_pins[2].m_name == "Entity");
    });

    auto t3 = larvae::RegisterTest("EntityScript", "VariableNodesRegistered", []() {
        NodeRegistry reg;
        const auto* get = reg.Find("GetVariable");
        const auto* set = reg.Find("SetVariable");
        larvae::AssertTrue(get != nullptr);
        larvae::AssertTrue(set != nullptr);
        larvae::AssertTrue(HasFlag(get->m_flags, NodeFlag::VARIABLE_GET));
        larvae::AssertTrue(HasFlag(set->m_flags, NodeFlag::VARIABLE_SET));
    });

    auto t4 = larvae::RegisterTest("EntityScript", "GetVariableHasGenericOutput", []() {
        NodeRegistry reg;
        const auto* get = reg.Find("GetVariable");
        larvae::AssertEqual(get->m_pins.Size(), size_t{1});
        larvae::AssertTrue(get->m_pins[0].m_direction == PinDirection::OUTPUT);
        larvae::AssertTrue(get->m_pins[0].m_isGeneric);
    });

    auto t5 = larvae::RegisterTest("EntityScript", "SetVariableHasExecAndGenericInput", []() {
        NodeRegistry reg;
        const auto* set = reg.Find("SetVariable");
        larvae::AssertEqual(set->m_pins.Size(), size_t{3});
        // Exec in, Value in, Exec out
        larvae::AssertTrue(set->m_pins[0].m_type == PType::Signal());
        larvae::AssertTrue(set->m_pins[1].m_direction == PinDirection::INPUT);
        larvae::AssertTrue(set->m_pins[1].m_isGeneric);
        larvae::AssertTrue(set->m_pins[2].m_type == PType::Signal());
    });

    // --- Validation ---

    auto t6 = larvae::RegisterTest("EntityScript", "LifecycleInSystemGraphIsError", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::SYSTEM_GRAPH);

        auto tick = g.AddNode("OnTick");
        g.AddPin(tick, PinDirection::OUTPUT, PType::Float32(), "DeltaTime");
        g.AddPin(tick, PinDirection::OUTPUT, PType::Entity(), "Entity");

        GraphValidator validator;
        auto result = validator.Validate(g, reg);
        larvae::AssertFalse(result.m_ok);
        larvae::AssertTrue(result.m_errors.Size() > 0);
        larvae::AssertTrue(Contains(result.m_errors[0].m_message, "ENTITY_SCRIPT"));
    });

    auto t7 = larvae::RegisterTest("EntityScript", "DuplicateOnTickIsError", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto tick1 = g.AddNode("OnTick");
        g.AddPin(tick1, PinDirection::OUTPUT, PType::Float32(), "DeltaTime");
        g.AddPin(tick1, PinDirection::OUTPUT, PType::Entity(), "Entity");

        auto tick2 = g.AddNode("OnTick");
        g.AddPin(tick2, PinDirection::OUTPUT, PType::Float32(), "DeltaTime");
        g.AddPin(tick2, PinDirection::OUTPUT, PType::Entity(), "Entity");

        GraphValidator validator;
        auto result = validator.Validate(g, reg);
        larvae::AssertFalse(result.m_ok);
    });

    auto t8 = larvae::RegisterTest("EntityScript", "InvalidVariableRefIsError", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto getNode = g.AddNode("GetVariable");
        g.AddPin(getNode, PinDirection::OUTPUT, PType{}, "Value");
        // variableRef is 0 (invalid) by default

        GraphValidator validator;
        auto result = validator.Validate(g, reg);
        larvae::AssertFalse(result.m_ok);
        larvae::AssertTrue(Contains(result.m_errors[0].m_message, "invalid variable"));
    });

    auto t9 = larvae::RegisterTest("EntityScript", "ValidEntityScriptPasses", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto speedVar = g.AddVariable("speed", PType::Float32());

        AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto getOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = speedVar;

        GraphValidator validator;
        auto result = validator.Validate(g, reg);
        larvae::AssertTrue(result.m_ok);

        const PType* typeIt = result.m_resolvedPinTypes.Find(getOut.m_value);
        larvae::AssertTrue(typeIt != nullptr);
        larvae::AssertTrue(*typeIt == PType::Float32());
    });

    auto t10 = larvae::RegisterTest("EntityScript", "VariableTypeFlowsThroughEdges", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto speedVar = g.AddVariable("speed", PType::Float32());
        auto resultVar = g.AddVariable("result", PType::Float32());

        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto speedOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = speedVar;

        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        auto mulOut = g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");

        g.AddEdge(speedOut, mulA);
        g.AddEdge(tick.dt, mulB);

        auto setResult = AddSetVariable(g, resultVar);

        g.AddEdge(mulOut, setResult.value);

        GraphValidator validator;
        auto result = validator.Validate(g, reg);
        larvae::AssertTrue(result.m_ok);
    });

    // --- Codegen ---

    auto t11 = larvae::RegisterTest("EntityScript", "CodegenEmitsStateStruct", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("speed", PType::Float32());
        g.AddVariable("health", PType::Int32());

        AddOnTick(g);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Player";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "struct Player_State"));
        larvae::AssertTrue(Contains(code, "float speed{0.0f}"));
        larvae::AssertTrue(Contains(code, "int32_t health{0}"));
    });

    auto t12 = larvae::RegisterTest("EntityScript", "CodegenEmitsOnTickFunction", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("speed", PType::Float32());

        AddOnTick(g);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Player";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "void Player_OnTick("));
        larvae::AssertTrue(Contains(code, "queen::Entity _entity"));
        larvae::AssertTrue(Contains(code, "Player_State& _state"));
        larvae::AssertTrue(Contains(code, "queen::World& _world"));
        larvae::AssertTrue(Contains(code, "float _dt"));
    });

    auto t13 = larvae::RegisterTest("EntityScript", "CodegenEmitsOnAttachWithoutDt", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("speed", PType::Float32());

        AddOnAttach(g);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Player";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "void Player_OnAttach("));
        larvae::AssertFalse(Contains(code, "Player_OnAttach") && Contains(code, "_dt"));
    });

    auto t14 = larvae::RegisterTest("EntityScript", "CodegenGetVariableReadsState", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto speedVar = g.AddVariable("speed", PType::Float32());

        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto getOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = speedVar;

        // Connect tick to get via an Add so getSpeed is reachable from OnTick
        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");

        g.AddEdge(getOut, addA);

        // Need to connect OnTick's DeltaTime to addB to make add reachable from OnTick
        g.AddEdge(tick.dt, addB);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Test";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "_state.speed"));
    });

    auto t15 = larvae::RegisterTest("EntityScript", "CodegenSetVariableWritesState", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto speedVar = g.AddVariable("speed", PType::Float32());
        auto resultVar = g.AddVariable("result", PType::Float32());

        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto getOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = speedVar;

        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        auto mulOut = g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");

        g.AddEdge(getOut, mulA);
        g.AddEdge(tick.dt, mulB);

        auto setResult = AddSetVariable(g, resultVar);

        g.AddEdge(mulOut, setResult.value);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Move";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "_state.result = "));
        larvae::AssertTrue(Contains(code, "_state.speed"));
        larvae::AssertTrue(Contains(code, " * "));
    });

    auto t16 = larvae::RegisterTest("EntityScript", "CodegenSkipsUnreachableNodes", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("x", PType::Float32());

        AddOnTick(g);

        // Disconnected node — not reachable from OnTick
        auto orphan = g.AddNode("Add");
        g.AddPin(orphan, PinDirection::INPUT, PType::Float32(), "A");
        g.AddPin(orphan, PinDirection::INPUT, PType::Float32(), "B");
        g.AddPin(orphan, PinDirection::OUTPUT, PType::Float32(), "Result");

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Test";
        auto code = codegen.Generate(g, validation, reg, opts);

        // The orphan Add node should not appear in OnTick body
        larvae::AssertFalse(Contains(code, " + "));
    });

    auto t17 = larvae::RegisterTest("EntityScript", "SystemGraphModeUnchanged", []() {
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
        g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");

        g.AddEdge(constAOut, addA);
        g.AddEdge(constBOut, addB);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "test_add";
        opts.m_namespaceName = "test";
        auto code = codegen.Generate(g, validation, reg, opts);

        // Should use old system graph format
        larvae::AssertTrue(Contains(code, "void test_add()"));
        larvae::AssertFalse(Contains(code, "_State"));
        larvae::AssertFalse(Contains(code, "_entity"));
    });

    auto t18 = larvae::RegisterTest("EntityScript", "OnAttachValidInEntityScript", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        AddOnAttach(g);
        AddOnDetach(g);

        GraphValidator validator;
        auto result = validator.Validate(g, reg);
        larvae::AssertTrue(result.m_ok);
    });

    auto t19 = larvae::RegisterTest("EntityScript", "CodegenFullPipeline", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto speedVar = g.AddVariable("speed", PType::Float32());
        auto posVar = g.AddVariable("pos_x", PType::Float32());

        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto speedOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = speedVar;

        auto getPosX = g.AddNode("GetVariable");
        auto posOut = g.AddPin(getPosX, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getPosX)->m_variableRef = posVar;

        // delta = speed * dt
        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        auto mulOut = g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(speedOut, mulA);
        g.AddEdge(tick.dt, mulB);

        // new_pos = pos_x + delta
        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        auto addOut = g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(posOut, addA);
        g.AddEdge(mulOut, addB);

        // pos_x = new_pos
        auto setPos = AddSetVariable(g, posVar);
        g.AddEdge(addOut, setPos.value);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Mover";
        opts.m_namespaceName = "game";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "namespace game"));
        larvae::AssertTrue(Contains(code, "struct Mover_State"));
        larvae::AssertTrue(Contains(code, "float speed{0.0f}"));
        larvae::AssertTrue(Contains(code, "float pos_x{0.0f}"));
        larvae::AssertTrue(Contains(code, "void Mover_OnTick("));
        larvae::AssertTrue(Contains(code, "_state.speed"));
        larvae::AssertTrue(Contains(code, "_state.pos_x"));
        larvae::AssertTrue(Contains(code, " * "));
        larvae::AssertTrue(Contains(code, " + "));
    });
    // --- Exec flow ---

    auto t20 = larvae::RegisterTest("EntityScript", "BranchAndSequenceRegistered", []() {
        NodeRegistry reg;
        const auto* branch = reg.Find("Branch");
        const auto* seq = reg.Find("Sequence");
        larvae::AssertTrue(branch != nullptr);
        larvae::AssertTrue(seq != nullptr);
        larvae::AssertTrue(HasFlag(branch->m_flags, NodeFlag::BRANCH));
        larvae::AssertTrue(HasFlag(seq->m_flags, NodeFlag::SEQUENCE));
    });

    auto t21 = larvae::RegisterTest("EntityScript", "BranchHasExecAndCondition", []() {
        NodeRegistry reg;
        const auto* branch = reg.Find("Branch");
        // ExecIn, Condition(Bool), True(ExecOut), False(ExecOut)
        larvae::AssertEqual(branch->m_pins.Size(), size_t{4});
        larvae::AssertTrue(branch->m_pins[0].m_type == PType::Signal());
        larvae::AssertTrue(branch->m_pins[1].m_type == PType::Bool());
        larvae::AssertTrue(branch->m_pins[2].m_type == PType::Signal());
        larvae::AssertTrue(branch->m_pins[2].m_name == "True");
        larvae::AssertTrue(branch->m_pins[3].m_type == PType::Signal());
        larvae::AssertTrue(branch->m_pins[3].m_name == "False");
    });

    auto t22 = larvae::RegisterTest("EntityScript", "LifecycleParamMapping", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("speed", PType::Float32());

        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto speedOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = g.Variables()[0].m_id;

        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");

        g.AddEdge(speedOut, mulA);
        g.AddEdge(tick.dt, mulB);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Test";
        auto code = codegen.Generate(g, validation, reg, opts);

        // DeltaTime pin should be mapped to _dt parameter
        larvae::AssertTrue(Contains(code, " = _dt;"));
    });

    auto t23 = larvae::RegisterTest("EntityScript", "BranchCodegenEmitsIfElse", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto flagVar = g.AddVariable("flag", PType::Bool());
        auto aVar = g.AddVariable("a", PType::Float32());
        auto bVar = g.AddVariable("b", PType::Float32());

        // OnTick
        auto tick = AddOnTick(g);

        // GetVariable("flag")
        auto getFlag = g.AddNode("GetVariable");
        auto flagOut = g.AddPin(getFlag, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getFlag)->m_variableRef = flagVar;

        // Branch
        auto branch = g.AddNode("Branch");
        auto branchExecIn = g.AddPin(branch, PinDirection::INPUT, PType::Signal(), "Exec");
        auto branchCond = g.AddPin(branch, PinDirection::INPUT, PType::Bool(), "Condition");
        auto branchTrue = g.AddPin(branch, PinDirection::OUTPUT, PType::Signal(), "True");
        auto branchFalse = g.AddPin(branch, PinDirection::OUTPUT, PType::Signal(), "False");

        g.AddEdge(tick.exec, branchExecIn);
        g.AddEdge(flagOut, branchCond);

        // SetVariable("a") on True branch
        auto setA = AddSetVariable(g, aVar);

        g.AddEdge(branchTrue, setA.execIn);

        // SetVariable("b") on False branch
        auto setB = AddSetVariable(g, bVar);

        g.AddEdge(branchFalse, setB.execIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "BranchTest";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "if ("));
        larvae::AssertTrue(Contains(code, "_state.flag"));
        larvae::AssertTrue(Contains(code, "else"));
        larvae::AssertTrue(Contains(code, "_state.a"));
        larvae::AssertTrue(Contains(code, "_state.b"));
    });

    auto t24 = larvae::RegisterTest("EntityScript", "SequenceCodegenEmitsInOrder", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto aVar = g.AddVariable("a", PType::Float32());
        auto bVar = g.AddVariable("b", PType::Float32());

        // OnTick
        auto tick = AddOnTick(g);

        // Sequence
        auto seq = g.AddNode("Sequence");
        auto seqExecIn = g.AddPin(seq, PinDirection::INPUT, PType::Signal(), "Exec");
        auto seqThen0 = g.AddPin(seq, PinDirection::OUTPUT, PType::Signal(), "Then0");
        auto seqThen1 = g.AddPin(seq, PinDirection::OUTPUT, PType::Signal(), "Then1");

        g.AddEdge(tick.exec, seqExecIn);

        // SetVariable("a") on Then0
        auto setA = AddSetVariable(g, aVar);
        g.AddEdge(seqThen0, setA.execIn);

        // SetVariable("b") on Then1
        auto setB = AddSetVariable(g, bVar);
        g.AddEdge(seqThen1, setB.execIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "SeqTest";
        auto code = codegen.Generate(g, validation, reg, opts);

        // Both variables should be set, a before b
        const char* posA = std::strstr(code.CStr(), "_state.a");
        const char* posB = std::strstr(code.CStr(), "_state.b");
        larvae::AssertTrue(posA != nullptr);
        larvae::AssertTrue(posB != nullptr);
        larvae::AssertTrue(posA < posB);
    });
    // --- ScriptRegistry ---

    auto t25 = larvae::RegisterTest("EntityScript", "ScriptRegistryRegisterAndFind", []() {
        propolis::ScriptRegistry registry;
        larvae::AssertEqual(registry.Count(), size_t{0});

        propolis::ScriptEntry entry;
        entry.m_name = "TestScript";
        entry.m_stateSize = 16;
        entry.m_stateAlignment = 4;
        registry.Register(static_cast<propolis::ScriptEntry&&>(entry));

        larvae::AssertEqual(registry.Count(), size_t{1});
        const auto* found = registry.Find("TestScript");
        larvae::AssertTrue(found != nullptr);
        larvae::AssertEqual(found->m_stateSize, uint32_t{16});
        larvae::AssertTrue(registry.Find("NonExistent") == nullptr);
    });

    auto t26 = larvae::RegisterTest("EntityScript", "CodegenEmitsRegistrationFunction", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("speed", PType::Float32());

        AddOnTick(g);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Player";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "void Register_Player("));
        larvae::AssertTrue(Contains(code, "propolis::ScriptRegistry&"));
        larvae::AssertTrue(Contains(code, "entry.m_name = \"Player\""));
        larvae::AssertTrue(Contains(code, "sizeof(Player_State)"));
        larvae::AssertTrue(Contains(code, "entry.m_onTick"));
        larvae::AssertTrue(Contains(code, "#include <propolis/runtime/script_registry.h>"));
    });

    auto t27 = larvae::RegisterTest("EntityScript", "RegistrationSkipsMissingLifecycles", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("x", PType::Float32());

        AddOnTick(g);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Simple";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "entry.m_onTick"));
        larvae::AssertFalse(Contains(code, "entry.m_onAttach"));
        larvae::AssertFalse(Contains(code, "entry.m_onDetach"));
    });

    auto t28 = larvae::RegisterTest("EntityScript", "CodegenWorldParamInAllLifecycles", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("x", PType::Float32());

        AddOnTick(g);
        AddOnAttach(g);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Test";
        auto code = codegen.Generate(g, validation, reg, opts);

        // World& should appear in both OnTick and OnAttach signatures
        const char* first = std::strstr(code.CStr(), "queen::World& _world");
        larvae::AssertTrue(first != nullptr);
        const char* second = std::strstr(first + 1, "queen::World& _world");
        larvae::AssertTrue(second != nullptr);
    });

    // --- Component access (struct-based) ---

    auto t29 = larvae::RegisterTest("EntityScript", "ComponentNodesRegistered", []() {
        NodeRegistry reg;
        larvae::AssertTrue(reg.Find("GetComponent") != nullptr);
        larvae::AssertTrue(reg.Find("SetComponent") != nullptr);
        larvae::AssertTrue(HasFlag(reg.Find("GetComponent")->m_flags, NodeFlag::COMPONENT_GET));
        larvae::AssertTrue(HasFlag(reg.Find("SetComponent")->m_flags, NodeFlag::COMPONENT_SET));
    });

    auto t30 = larvae::RegisterTest("EntityScript", "ComponentNodeMissingRefIsError", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        g.AddNode("GetComponent");

        GraphValidator validator;
        auto result = validator.Validate(g, reg);
        larvae::AssertFalse(result.m_ok);
        larvae::AssertTrue(Contains(result.m_errors[0].m_message, "missing component"));
    });

    auto t31 = larvae::RegisterTest("EntityScript", "GetComponentMultiFieldCodegen", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("dummy", PType::Float32());

        auto tick = AddOnTick(g);

        // GetComponent("Position") with output pins x, y
        auto getComp = g.AddNode("GetComponent");
        auto xOut = g.AddPin(getComp, PinDirection::OUTPUT, PType::Float32(), "x");
        auto yOut = g.AddPin(getComp, PinDirection::OUTPUT, PType::Float32(), "y");
        g.FindNode(getComp)->m_componentRef = "Position";

        // Connect x+y via Add, and connect DeltaTime to make reachable from OnTick
        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        auto addOut = g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(xOut, addA);
        g.AddEdge(yOut, addB);

        // Connect to a Multiply with DeltaTime to ensure reachability from OnTick
        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(addOut, mulA);
        g.AddEdge(tick.dt, mulB);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Test";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "_world.Get<Position>(_entity)"));
        larvae::AssertTrue(Contains(code, "->x"));
        larvae::AssertTrue(Contains(code, "->y"));
    });

    auto t32 = larvae::RegisterTest("EntityScript", "SetComponentMultiFieldCodegen", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("speed", PType::Float32());

        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto speedOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = g.Variables()[0].m_id;

        // SetComponent("Velocity") with input pins dx, dy
        auto setComp = g.AddNode("SetComponent");
        auto setExecIn = g.AddPin(setComp, PinDirection::INPUT, PType::Signal(), "Exec");
        auto dxIn = g.AddPin(setComp, PinDirection::INPUT, PType::Float32(), "dx");
        g.AddPin(setComp, PinDirection::INPUT, PType::Float32(), "dy");
        g.AddPin(setComp, PinDirection::OUTPUT, PType::Signal(), "Exec");
        g.FindNode(setComp)->m_componentRef = "Velocity";

        g.AddEdge(tick.exec, setExecIn);
        g.AddEdge(speedOut, dxIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Mover";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "_world.Get<Velocity>(_entity)"));
        larvae::AssertTrue(Contains(code, "->dx = "));
        // dy not connected — should not appear in codegen
        larvae::AssertFalse(Contains(code, "->dy"));
    });

    auto t33 = larvae::RegisterTest("EntityScript", "FullPipelineWithComponentAccess", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        auto speedVar = g.AddVariable("speed", PType::Float32());
        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto speedOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = speedVar;

        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        auto mulOut = g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(speedOut, mulA);
        g.AddEdge(tick.dt, mulB);

        // GetComponent("Position") → x pin
        auto getPos = g.AddNode("GetComponent");
        auto posXOut = g.AddPin(getPos, PinDirection::OUTPUT, PType::Float32(), "x");
        g.FindNode(getPos)->m_componentRef = "Position";

        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        auto addOut = g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(posXOut, addA);
        g.AddEdge(mulOut, addB);

        // SetComponent("Position") ← x pin
        auto setPos = g.AddNode("SetComponent");
        auto setPosExecIn = g.AddPin(setPos, PinDirection::INPUT, PType::Signal(), "Exec");
        auto setPosXIn = g.AddPin(setPos, PinDirection::INPUT, PType::Float32(), "x");
        g.AddPin(setPos, PinDirection::OUTPUT, PType::Signal(), "Exec");
        g.FindNode(setPos)->m_componentRef = "Position";

        g.AddEdge(tick.exec, setPosExecIn);
        g.AddEdge(addOut, setPosXIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Movement";
        opts.m_namespaceName = "game";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "_state.speed"));
        larvae::AssertTrue(Contains(code, "_world.Get<Position>(_entity)"));
        larvae::AssertTrue(Contains(code, "->x"));
        larvae::AssertTrue(Contains(code, " * "));
        larvae::AssertTrue(Contains(code, " + "));
        larvae::AssertTrue(Contains(code, "->x = "));
    });

    // --- Break node ---

    auto t34 = larvae::RegisterTest("EntityScript", "BreakNodeRegistered", []() {
        NodeRegistry reg;
        const auto* brk = reg.Find("Break");
        larvae::AssertTrue(brk != nullptr);
        larvae::AssertTrue(HasFlag(brk->m_flags, NodeFlag::STRUCT_BREAK));
        larvae::AssertEqual(brk->m_pins.Size(), size_t{1});
        larvae::AssertTrue(brk->m_pins[0].m_direction == PinDirection::INPUT);
    });

    auto t35 = larvae::RegisterTest("EntityScript", "BreakVec3Codegen", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("dummy", PType::Float32());

        auto tick = AddOnTick(g);

        // Simulate GetComponent outputting a Vec3 pin
        auto getComp = g.AddNode("GetComponent");
        auto posOut = g.AddPin(getComp, PinDirection::OUTPUT, PType::Vec3(), "m_position");
        g.FindNode(getComp)->m_componentRef = "Transform";

        // Break node: input = Vec3, outputs = m_x, m_y, m_z
        auto brk = g.AddNode("Break");
        auto brkIn = g.AddPin(brk, PinDirection::INPUT, PType::Vec3(), "Value");
        auto brkX = g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_x");
        auto brkY = g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_y");
        auto brkZ = g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_z");

        g.AddEdge(posOut, brkIn);

        // Use m_x in an Add with DeltaTime to make reachable from OnTick
        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(brkX, addA);
        g.AddEdge(tick.dt, addB);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Test";
        auto code = codegen.Generate(g, validation, reg, opts);

        // GetComponent emits the struct
        larvae::AssertTrue(Contains(code, "_world.Get<Transform>(_entity)"));
        larvae::AssertTrue(Contains(code, "hive::math::Float3"));
        larvae::AssertTrue(Contains(code, "->m_position"));

        // Break emits field access
        larvae::AssertTrue(Contains(code, ".m_x"));
        larvae::AssertTrue(Contains(code, ".m_y"));
        larvae::AssertTrue(Contains(code, ".m_z"));
    });

    auto t36 = larvae::RegisterTest("EntityScript", "BreakQuatCodegen", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("dummy", PType::Float32());

        auto tick = AddOnTick(g);

        auto getComp = g.AddNode("GetComponent");
        auto rotOut = g.AddPin(getComp, PinDirection::OUTPUT, PType::Quat(), "m_rotation");
        g.FindNode(getComp)->m_componentRef = "Transform";

        auto brk = g.AddNode("Break");
        auto brkIn = g.AddPin(brk, PinDirection::INPUT, PType::Quat(), "Value");
        auto brkW = g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_w");

        g.AddEdge(rotOut, brkIn);

        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(brkW, mulA);
        g.AddEdge(tick.dt, mulB);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "RotTest";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "hive::math::Quat"));
        larvae::AssertTrue(Contains(code, ".m_w"));
    });

    auto t37 = larvae::RegisterTest("EntityScript", "CodegenEngineTypeNames", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);

        g.AddVariable("v2", PType::Vec2());
        g.AddVariable("v3", PType::Vec3());
        g.AddVariable("v4", PType::Vec4());
        g.AddVariable("q", PType::Quat());

        AddOnTick(g);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Types";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "hive::math::Float2 v2"));
        larvae::AssertTrue(Contains(code, "hive::math::Float3 v3"));
        larvae::AssertTrue(Contains(code, "hive::math::Float4 v4"));
        larvae::AssertTrue(Contains(code, "hive::math::Quat q"));
    });

    auto t38 = larvae::RegisterTest("EntityScript", "SetComponentOnlyWritesConnectedPins", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("val", PType::Float32());

        auto tick = AddOnTick(g);

        auto getVal = g.AddNode("GetVariable");
        auto valOut = g.AddPin(getVal, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getVal)->m_variableRef = g.Variables()[0].m_id;

        // SetComponent with 3 input pins but only 1 connected
        auto setComp = g.AddNode("SetComponent");
        auto setExecIn = g.AddPin(setComp, PinDirection::INPUT, PType::Signal(), "Exec");
        auto dxIn = g.AddPin(setComp, PinDirection::INPUT, PType::Float32(), "dx");
        g.AddPin(setComp, PinDirection::INPUT, PType::Float32(), "dy");
        g.AddPin(setComp, PinDirection::INPUT, PType::Float32(), "dz");
        g.AddPin(setComp, PinDirection::OUTPUT, PType::Signal(), "Exec");
        g.FindNode(setComp)->m_componentRef = "Velocity";

        g.AddEdge(tick.exec, setExecIn);
        g.AddEdge(valOut, dxIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Selective";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "->dx = "));
        larvae::AssertFalse(Contains(code, "->dy"));
        larvae::AssertFalse(Contains(code, "->dz"));
    });

    auto t39 = larvae::RegisterTest("EntityScript", "BreakUnconnectedEmitsNothing", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("dummy", PType::Float32());

        auto tick = AddOnTick(g);

        // Break node not connected to any source
        auto brk = g.AddNode("Break");
        g.AddPin(brk, PinDirection::INPUT, PType::Vec3(), "Value");
        g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_x");

        // Make sure OnTick has something to emit
        auto getVar = g.AddNode("GetVariable");
        auto varOut = g.AddPin(getVar, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getVar)->m_variableRef = g.Variables()[0].m_id;

        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(varOut, mulA);
        g.AddEdge(tick.dt, mulB);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "NoBreak";
        auto code = codegen.Generate(g, validation, reg, opts);

        // Break node is disconnected — should NOT emit .m_x
        larvae::AssertFalse(Contains(code, ".m_x"));
    });

    auto t40 = larvae::RegisterTest("EntityScript", "FullPipelineGetBreakSetComponent", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("speed", PType::Float32());

        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto speedOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = g.Variables()[0].m_id;

        // GetComponent(Transform) → position (Vec3)
        auto getComp = g.AddNode("GetComponent");
        auto posOut = g.AddPin(getComp, PinDirection::OUTPUT, PType::Vec3(), "m_position");
        g.FindNode(getComp)->m_componentRef = "Transform";

        // Break position → m_x
        auto brk = g.AddNode("Break");
        auto brkIn = g.AddPin(brk, PinDirection::INPUT, PType::Vec3(), "Value");
        auto brkX = g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_x");
        g.AddEdge(posOut, brkIn);

        // pos.x + speed * dt
        auto mulNode = g.AddNode("Multiply");
        auto mulA = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "A");
        auto mulB = g.AddPin(mulNode, PinDirection::INPUT, PType{}, "B");
        auto mulOut = g.AddPin(mulNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(speedOut, mulA);
        g.AddEdge(tick.dt, mulB);

        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        auto addOut = g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(brkX, addA);
        g.AddEdge(mulOut, addB);

        // SetComponent(Transform).m_position_x = result
        auto setComp = g.AddNode("SetComponent");
        auto setExecIn = g.AddPin(setComp, PinDirection::INPUT, PType::Signal(), "Exec");
        auto setXIn = g.AddPin(setComp, PinDirection::INPUT, PType::Float32(), "x");
        g.AddPin(setComp, PinDirection::OUTPUT, PType::Signal(), "Exec");
        g.FindNode(setComp)->m_componentRef = "Transform";

        g.AddEdge(tick.exec, setExecIn);
        g.AddEdge(addOut, setXIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Movement";
        opts.m_namespaceName = "game";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "namespace game"));
        larvae::AssertTrue(Contains(code, "_world.Get<Transform>(_entity)"));
        larvae::AssertTrue(Contains(code, "hive::math::Float3"));
        larvae::AssertTrue(Contains(code, ".m_x"));
        larvae::AssertTrue(Contains(code, "_state.speed"));
        larvae::AssertTrue(Contains(code, " * "));
        larvae::AssertTrue(Contains(code, " + "));
        larvae::AssertTrue(Contains(code, "->x = "));
    });

    // --- Make node ---

    auto t41 = larvae::RegisterTest("EntityScript", "MakeNodeRegistered", []() {
        NodeRegistry reg;
        const auto* mk = reg.Find("Make");
        larvae::AssertTrue(mk != nullptr);
        larvae::AssertTrue(HasFlag(mk->m_flags, NodeFlag::STRUCT_MAKE));
        larvae::AssertEqual(mk->m_pins.Size(), size_t{1});
        larvae::AssertTrue(mk->m_pins[0].m_direction == PinDirection::OUTPUT);
    });

    auto t42 = larvae::RegisterTest("EntityScript", "MakeVec3Codegen", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("dummy", PType::Float32());

        auto tick = AddOnTick(g);

        // Make Vec3 from individual floats
        auto mk = g.AddNode("Make");
        auto mkXIn = g.AddPin(mk, PinDirection::INPUT, PType::Float32(), "m_x");
        auto mkYIn = g.AddPin(mk, PinDirection::INPUT, PType::Float32(), "m_y");
        g.AddPin(mk, PinDirection::INPUT, PType::Float32(), "m_z");
        auto mkOut = g.AddPin(mk, PinDirection::OUTPUT, PType::Vec3(), "Value");

        g.AddEdge(tick.dt, mkXIn);
        g.AddEdge(tick.dt, mkYIn);

        // Connect to a SetComponent to make reachable via exec
        auto setComp = g.AddNode("SetComponent");
        auto setExecIn = g.AddPin(setComp, PinDirection::INPUT, PType::Signal(), "Exec");
        auto setPosIn = g.AddPin(setComp, PinDirection::INPUT, PType::Vec3(), "m_position");
        g.AddPin(setComp, PinDirection::OUTPUT, PType::Signal(), "Exec");
        g.FindNode(setComp)->m_componentRef = "Transform";

        g.AddEdge(tick.exec, setExecIn);
        g.AddEdge(mkOut, setPosIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "MakeTest";
        auto code = codegen.Generate(g, validation, reg, opts);

        // Make creates the struct and assigns connected fields
        larvae::AssertTrue(Contains(code, "hive::math::Float3"));
        larvae::AssertTrue(Contains(code, ".m_x = "));
        larvae::AssertTrue(Contains(code, ".m_y = "));
        // m_z is not connected — should not appear
        larvae::AssertFalse(Contains(code, ".m_z = "));
    });

    auto t43 = larvae::RegisterTest("EntityScript", "FullPipelineBreakMakeRoundtrip", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("offset", PType::Float32());

        auto tick = AddOnTick(g);

        auto getOff = g.AddNode("GetVariable");
        auto offOut = g.AddPin(getOff, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getOff)->m_variableRef = g.Variables()[0].m_id;

        // Get Transform.position (Vec3)
        auto getComp = g.AddNode("GetComponent");
        auto posOut = g.AddPin(getComp, PinDirection::OUTPUT, PType::Vec3(), "m_position");
        g.FindNode(getComp)->m_componentRef = "Transform";

        // Break → m_x
        auto brk = g.AddNode("Break");
        auto brkIn = g.AddPin(brk, PinDirection::INPUT, PType::Vec3(), "Value");
        auto brkX = g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_x");
        auto brkY = g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_y");
        auto brkZ = g.AddPin(brk, PinDirection::OUTPUT, PType::Float32(), "m_z");
        g.AddEdge(posOut, brkIn);

        // m_x + offset
        auto addNode = g.AddNode("Add");
        auto addA = g.AddPin(addNode, PinDirection::INPUT, PType{}, "A");
        auto addB = g.AddPin(addNode, PinDirection::INPUT, PType{}, "B");
        auto addOut = g.AddPin(addNode, PinDirection::OUTPUT, PType{}, "Result");
        g.AddEdge(brkX, addA);
        g.AddEdge(offOut, addB);

        // Make Vec3 with modified x, original y and z
        auto mk = g.AddNode("Make");
        auto mkXIn = g.AddPin(mk, PinDirection::INPUT, PType::Float32(), "m_x");
        auto mkYIn = g.AddPin(mk, PinDirection::INPUT, PType::Float32(), "m_y");
        auto mkZIn = g.AddPin(mk, PinDirection::INPUT, PType::Float32(), "m_z");
        auto mkOut = g.AddPin(mk, PinDirection::OUTPUT, PType::Vec3(), "Value");
        g.AddEdge(addOut, mkXIn);
        g.AddEdge(brkY, mkYIn);
        g.AddEdge(brkZ, mkZIn);

        // SetComponent Transform.m_position = made Vec3
        auto setComp = g.AddNode("SetComponent");
        auto setExecIn = g.AddPin(setComp, PinDirection::INPUT, PType::Signal(), "Exec");
        auto setPosIn = g.AddPin(setComp, PinDirection::INPUT, PType::Vec3(), "m_position");
        g.AddPin(setComp, PinDirection::OUTPUT, PType::Signal(), "Exec");
        g.FindNode(setComp)->m_componentRef = "Transform";

        g.AddEdge(tick.exec, setExecIn);
        g.AddEdge(mkOut, setPosIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "Roundtrip";
        auto code = codegen.Generate(g, validation, reg, opts);

        // Full roundtrip: Get → Break → Add → Make → Set
        larvae::AssertTrue(Contains(code, "_world.Get<Transform>(_entity)"));
        larvae::AssertTrue(Contains(code, ".m_x;"));
        larvae::AssertTrue(Contains(code, ".m_y;"));
        larvae::AssertTrue(Contains(code, ".m_z;"));
        larvae::AssertTrue(Contains(code, " + "));
        larvae::AssertTrue(Contains(code, "hive::math::Float3"));
        larvae::AssertTrue(Contains(code, "->m_position = "));
    });

    // --- Print + ForEach ---

    auto t44 = larvae::RegisterTest("EntityScript", "PrintNodeRegistered", []() {
        NodeRegistry reg;
        const auto* p = reg.Find("Print");
        larvae::AssertTrue(p != nullptr);
        larvae::AssertTrue(HasFlag(p->m_flags, NodeFlag::PRINT));
    });

    auto t45 = larvae::RegisterTest("EntityScript", "ForEachNodeRegistered", []() {
        NodeRegistry reg;
        const auto* fe = reg.Find("ForEach");
        larvae::AssertTrue(fe != nullptr);
        larvae::AssertTrue(HasFlag(fe->m_flags, NodeFlag::FOR_EACH));
        // ExecIn, Count, Body(ExecOut), Index, Done(ExecOut)
        larvae::AssertEqual(fe->m_pins.Size(), size_t{5});
    });

    auto t46 = larvae::RegisterTest("EntityScript", "PrintCodegenUsesLogger", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("speed", PType::Float32());

        auto tick = AddOnTick(g);

        auto getSpeed = g.AddNode("GetVariable");
        auto speedOut = g.AddPin(getSpeed, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getSpeed)->m_variableRef = g.Variables()[0].m_id;

        auto printNode = g.AddNode("Print");
        auto printExecIn = g.AddPin(printNode, PinDirection::INPUT, PType::Signal(), "Exec");
        auto printValueIn = g.AddPin(printNode, PinDirection::INPUT, PType{}, "Value");
        g.AddPin(printNode, PinDirection::OUTPUT, PType::Signal(), "Exec");

        g.AddEdge(tick.exec, printExecIn);
        g.AddEdge(speedOut, printValueIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "PrintTest";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "propolis::ScriptLog"));
        larvae::AssertTrue(Contains(code, "_state.speed"));
        larvae::AssertTrue(Contains(code, "script_log.h"));
    });

    auto t47 = larvae::RegisterTest("EntityScript", "ForEachCodegenEmitsForLoop", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("count", PType::Int32());
        g.AddVariable("sum", PType::Float32());

        auto tick = AddOnTick(g);

        auto getCount = g.AddNode("GetVariable");
        auto countOut = g.AddPin(getCount, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getCount)->m_variableRef = g.Variables()[0].m_id;

        // ForEach node
        auto forEach = g.AddNode("ForEach");
        auto feExecIn = g.AddPin(forEach, PinDirection::INPUT, PType::Signal(), "Exec");
        auto feCountIn = g.AddPin(forEach, PinDirection::INPUT, PType::Int32(), "Count");
        auto feBody = g.AddPin(forEach, PinDirection::OUTPUT, PType::Signal(), "Body");
        auto feIndex = g.AddPin(forEach, PinDirection::OUTPUT, PType::Int32(), "Index");
        auto feDone = g.AddPin(forEach, PinDirection::OUTPUT, PType::Signal(), "Done");

        g.AddEdge(tick.exec, feExecIn);
        g.AddEdge(countOut, feCountIn);

        // Inside the loop: print the index
        auto printNode = g.AddNode("Print");
        auto printExecIn = g.AddPin(printNode, PinDirection::INPUT, PType::Signal(), "Exec");
        auto printValueIn = g.AddPin(printNode, PinDirection::INPUT, PType::Int32(), "Value");
        g.AddPin(printNode, PinDirection::OUTPUT, PType::Signal(), "Exec");

        g.AddEdge(feBody, printExecIn);
        g.AddEdge(feIndex, printValueIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "LoopTest";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "for (int32_t"));
        larvae::AssertTrue(Contains(code, "< "));
        larvae::AssertTrue(Contains(code, "propolis::ScriptLog"));
    });

    // --- While ---

    auto t48 = larvae::RegisterTest("EntityScript", "WhileNodeRegistered", []() {
        NodeRegistry reg;
        const auto* w = reg.Find("While");
        larvae::AssertTrue(w != nullptr);
        larvae::AssertTrue(HasFlag(w->m_flags, NodeFlag::WHILE_LOOP));
        larvae::AssertEqual(w->m_pins.Size(), size_t{4});
    });

    auto t49 = larvae::RegisterTest("EntityScript", "WhileCodegenEmitsWhileLoop", []() {
        NodeRegistry reg;
        Graph g;
        g.SetMode(GraphMode::ENTITY_SCRIPT);
        g.AddVariable("running", PType::Bool());
        g.AddVariable("counter", PType::Float32());

        auto tick = AddOnTick(g);

        auto getRunning = g.AddNode("GetVariable");
        auto runOut = g.AddPin(getRunning, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getRunning)->m_variableRef = g.Variables()[0].m_id;

        auto whileNode = g.AddNode("While");
        auto whExecIn = g.AddPin(whileNode, PinDirection::INPUT, PType::Signal(), "Exec");
        auto whCondIn = g.AddPin(whileNode, PinDirection::INPUT, PType::Bool(), "Condition");
        auto whBody = g.AddPin(whileNode, PinDirection::OUTPUT, PType::Signal(), "Body");
        auto whDone = g.AddPin(whileNode, PinDirection::OUTPUT, PType::Signal(), "Done");

        g.AddEdge(tick.exec, whExecIn);
        g.AddEdge(runOut, whCondIn);

        auto getCounter = g.AddNode("GetVariable");
        auto ctrOut = g.AddPin(getCounter, PinDirection::OUTPUT, PType{}, "Value");
        g.FindNode(getCounter)->m_variableRef = g.Variables()[1].m_id;

        auto printNode = g.AddNode("Print");
        auto prExecIn = g.AddPin(printNode, PinDirection::INPUT, PType::Signal(), "Exec");
        auto prValueIn = g.AddPin(printNode, PinDirection::INPUT, PType{}, "Value");
        g.AddPin(printNode, PinDirection::OUTPUT, PType::Signal(), "Exec");

        g.AddEdge(whBody, prExecIn);
        g.AddEdge(ctrOut, prValueIn);

        GraphValidator validator;
        auto validation = validator.Validate(g, reg);
        larvae::AssertTrue(validation.m_ok);

        Codegen codegen;
        CodegenOptions opts;
        opts.m_systemName = "WhileTest";
        auto code = codegen.Generate(g, validation, reg, opts);

        larvae::AssertTrue(Contains(code, "while ("));
        larvae::AssertTrue(Contains(code, "_state.running"));
        larvae::AssertTrue(Contains(code, "propolis::ScriptLog"));
    });
} // namespace
