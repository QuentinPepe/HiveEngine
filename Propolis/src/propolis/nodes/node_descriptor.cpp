#include <propolis/nodes/node_descriptor.h>

namespace propolis
{
    static PinDescriptor In(const char* name, PType type)
    {
        return {PinDirection::INPUT, type, Constraint::NONE, wax::String{name}, false};
    }

    static PinDescriptor Out(const char* name, PType type)
    {
        return {PinDirection::OUTPUT, type, Constraint::NONE, wax::String{name}, false};
    }

    static PinDescriptor InGeneric(const char* name, Constraint c)
    {
        return {PinDirection::INPUT, PType{}, c, wax::String{name}, true};
    }

    static PinDescriptor OutGeneric(const char* name, Constraint c)
    {
        return {PinDirection::OUTPUT, PType{}, c, wax::String{name}, true};
    }

    static PinDescriptor ExecIn(const char* name = "Exec")
    {
        PinDescriptor pd;
        pd.m_direction = PinDirection::INPUT;
        pd.m_type = PType::Signal();
        pd.m_name = name;
        pd.m_isGeneric = false;
        return pd;
    }

    static PinDescriptor ExecOut(const char* name = "Exec")
    {
        PinDescriptor pd;
        pd.m_direction = PinDirection::OUTPUT;
        pd.m_type = PType::Signal();
        pd.m_name = name;
        pd.m_isGeneric = false;
        return pd;
    }

    void NodeRegistry::Add(NodeDescriptor desc)
    {
        m_descriptors.PushBack(static_cast<NodeDescriptor&&>(desc));
    }

    const NodeDescriptor* NodeRegistry::Find(const char* name) const
    {
        for (size_t i = 0; i < m_descriptors.Size(); ++i)
        {
            if (m_descriptors[i].m_name == name)
                return &m_descriptors[i];
        }
        return nullptr;
    }

    NodeRegistry::NodeRegistry()
    {
        RegisterMathNodes();
        RegisterLogicNodes();
        RegisterConversionNodes();
        RegisterEventNodes();
        RegisterVariableNodes();
        RegisterComponentNodes();
        RegisterDebugNodes();
    }

    void NodeRegistry::RegisterMathNodes()
    {
        struct BinaryOp
        {
            const char* name;
            const char* codegen;
        };
        BinaryOp binaryOps[] = {
            {"Add", "{A} + {B}"},
            {"Subtract", "{A} - {B}"},
            {"Multiply", "{A} * {B}"},
            {"Divide", "{A} / {B}"},
        };

        for (const auto& op : binaryOps)
        {
            NodeDescriptor desc;
            desc.m_name = op.name;
            desc.m_category = "Math";
            desc.m_codegenTemplate = op.codegen;
            desc.m_pins.PushBack(InGeneric("A", Constraint::NUMERIC));
            desc.m_pins.PushBack(InGeneric("B", Constraint::NUMERIC));
            desc.m_pins.PushBack(OutGeneric("Result", Constraint::NUMERIC));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Negate";
            desc.m_category = "Math";
            desc.m_codegenTemplate = "-{A}";
            desc.m_pins.PushBack(InGeneric("A", Constraint::NUMERIC));
            desc.m_pins.PushBack(OutGeneric("Result", Constraint::NUMERIC));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Abs";
            desc.m_category = "Math";
            desc.m_codegenTemplate = "std::abs({A})";
            desc.m_pins.PushBack(InGeneric("A", Constraint::NUMERIC));
            desc.m_pins.PushBack(OutGeneric("Result", Constraint::NUMERIC));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        struct FloatUnaryOp
        {
            const char* name;
            const char* codegen;
        };
        FloatUnaryOp floatOps[] = {
            {"Sin", "std::sin({A})"},
            {"Cos", "std::cos({A})"},
            {"Sqrt", "std::sqrt({A})"},
            {"Floor", "std::floor({A})"},
            {"Ceil", "std::ceil({A})"},
        };

        for (const auto& op : floatOps)
        {
            NodeDescriptor desc;
            desc.m_name = op.name;
            desc.m_category = "Math";
            desc.m_codegenTemplate = op.codegen;
            desc.m_pins.PushBack(In("A", PType::Float32()));
            desc.m_pins.PushBack(Out("Result", PType::Float32()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Clamp";
            desc.m_category = "Math";
            desc.m_codegenTemplate = "std::clamp({Value}, {Min}, {Max})";
            desc.m_pins.PushBack(InGeneric("Value", Constraint::ORDERED));
            desc.m_pins.PushBack(InGeneric("Min", Constraint::ORDERED));
            desc.m_pins.PushBack(InGeneric("Max", Constraint::ORDERED));
            desc.m_pins.PushBack(OutGeneric("Result", Constraint::ORDERED));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Lerp";
            desc.m_category = "Math";
            desc.m_codegenTemplate = "{A} + ({B} - {A}) * {Alpha}";
            desc.m_pins.PushBack(InGeneric("A", Constraint::INTERPOLABLE));
            desc.m_pins.PushBack(InGeneric("B", Constraint::INTERPOLABLE));
            desc.m_pins.PushBack(In("Alpha", PType::Float32()));
            desc.m_pins.PushBack(OutGeneric("Result", Constraint::INTERPOLABLE));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Min";
            desc.m_category = "Math";
            desc.m_codegenTemplate = "std::min({A}, {B})";
            desc.m_pins.PushBack(InGeneric("A", Constraint::ORDERED));
            desc.m_pins.PushBack(InGeneric("B", Constraint::ORDERED));
            desc.m_pins.PushBack(OutGeneric("Result", Constraint::ORDERED));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Max";
            desc.m_category = "Math";
            desc.m_codegenTemplate = "std::max({A}, {B})";
            desc.m_pins.PushBack(InGeneric("A", Constraint::ORDERED));
            desc.m_pins.PushBack(InGeneric("B", Constraint::ORDERED));
            desc.m_pins.PushBack(OutGeneric("Result", Constraint::ORDERED));
            Add(static_cast<NodeDescriptor&&>(desc));
        }
    }

    void NodeRegistry::RegisterLogicNodes()
    {
        struct CmpOp
        {
            const char* name;
            const char* codegen;
            Constraint constraint;
        };
        CmpOp cmpOps[] = {
            {"Equal", "{A} == {B}", Constraint::EQUATABLE},
            {"NotEqual", "{A} != {B}", Constraint::EQUATABLE},
            {"Less", "{A} < {B}", Constraint::ORDERED},
            {"Greater", "{A} > {B}", Constraint::ORDERED},
            {"LessEqual", "{A} <= {B}", Constraint::ORDERED},
            {"GreaterEqual", "{A} >= {B}", Constraint::ORDERED},
        };

        for (const auto& op : cmpOps)
        {
            NodeDescriptor desc;
            desc.m_name = op.name;
            desc.m_category = "Logic";
            desc.m_codegenTemplate = op.codegen;
            desc.m_pins.PushBack(InGeneric("A", op.constraint));
            desc.m_pins.PushBack(InGeneric("B", op.constraint));
            desc.m_pins.PushBack(Out("Result", PType::Bool()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "And";
            desc.m_category = "Logic";
            desc.m_codegenTemplate = "{A} && {B}";
            desc.m_pins.PushBack(In("A", PType::Bool()));
            desc.m_pins.PushBack(In("B", PType::Bool()));
            desc.m_pins.PushBack(Out("Result", PType::Bool()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Or";
            desc.m_category = "Logic";
            desc.m_codegenTemplate = "{A} || {B}";
            desc.m_pins.PushBack(In("A", PType::Bool()));
            desc.m_pins.PushBack(In("B", PType::Bool()));
            desc.m_pins.PushBack(Out("Result", PType::Bool()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Not";
            desc.m_category = "Logic";
            desc.m_codegenTemplate = "!{A}";
            desc.m_pins.PushBack(In("A", PType::Bool()));
            desc.m_pins.PushBack(Out("Result", PType::Bool()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Select";
            desc.m_category = "Logic";
            desc.m_codegenTemplate = "{Condition} ? {IfTrue} : {IfFalse}";
            desc.m_pins.PushBack(In("Condition", PType::Bool()));
            desc.m_pins.PushBack(InGeneric("IfTrue", Constraint::NONE));
            desc.m_pins.PushBack(InGeneric("IfFalse", Constraint::NONE));
            desc.m_pins.PushBack(OutGeneric("Result", Constraint::NONE));
            Add(static_cast<NodeDescriptor&&>(desc));
        }
    }

    void NodeRegistry::RegisterConversionNodes()
    {
        {
            NodeDescriptor desc;
            desc.m_name = "IntToFloat";
            desc.m_category = "Conversion";
            desc.m_codegenTemplate = "static_cast<float>({Value})";
            desc.m_pins.PushBack(In("Value", PType::Int32()));
            desc.m_pins.PushBack(Out("Result", PType::Float32()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "FloatToInt";
            desc.m_category = "Conversion";
            desc.m_codegenTemplate = "static_cast<int32_t>({Value})";
            desc.m_pins.PushBack(In("Value", PType::Float32()));
            desc.m_pins.PushBack(Out("Result", PType::Int32()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "BoolToFloat";
            desc.m_category = "Conversion";
            desc.m_codegenTemplate = "{Value} ? 1.0f : 0.0f";
            desc.m_pins.PushBack(In("Value", PType::Bool()));
            desc.m_pins.PushBack(Out("Result", PType::Float32()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }
    }

    void NodeRegistry::RegisterEventNodes()
    {
        {
            NodeDescriptor desc;
            desc.m_name = "OnTick";
            desc.m_category = "Event";
            desc.m_flags = NodeFlag::LIFECYCLE;
            desc.m_pins.PushBack(ExecOut());
            desc.m_pins.PushBack(Out("DeltaTime", PType::Float32()));
            desc.m_pins.PushBack(Out("Entity", PType::Entity()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "OnAttach";
            desc.m_category = "Event";
            desc.m_flags = NodeFlag::LIFECYCLE;
            desc.m_pins.PushBack(ExecOut());
            desc.m_pins.PushBack(Out("Entity", PType::Entity()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "OnDetach";
            desc.m_category = "Event";
            desc.m_flags = NodeFlag::LIFECYCLE;
            desc.m_pins.PushBack(ExecOut());
            desc.m_pins.PushBack(Out("Entity", PType::Entity()));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Branch";
            desc.m_category = "Flow";
            desc.m_flags = NodeFlag::BRANCH;
            desc.m_pins.PushBack(ExecIn());
            desc.m_pins.PushBack(In("Condition", PType::Bool()));
            desc.m_pins.PushBack(ExecOut("True"));
            desc.m_pins.PushBack(ExecOut("False"));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Sequence";
            desc.m_category = "Flow";
            desc.m_flags = NodeFlag::SEQUENCE;
            desc.m_pins.PushBack(ExecIn());
            desc.m_pins.PushBack(ExecOut("Then0"));
            desc.m_pins.PushBack(ExecOut("Then1"));
            Add(static_cast<NodeDescriptor&&>(desc));
        }
    }

    void NodeRegistry::RegisterVariableNodes()
    {
        {
            NodeDescriptor desc;
            desc.m_name = "GetVariable";
            desc.m_category = "Variable";
            desc.m_flags = NodeFlag::VARIABLE_GET;
            desc.m_pins.PushBack(OutGeneric("Value", Constraint::NONE));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "SetVariable";
            desc.m_category = "Variable";
            desc.m_flags = NodeFlag::VARIABLE_SET;
            desc.m_pins.PushBack(ExecIn());
            desc.m_pins.PushBack(InGeneric("Value", Constraint::NONE));
            desc.m_pins.PushBack(ExecOut());
            Add(static_cast<NodeDescriptor&&>(desc));
        }
    }

    void NodeRegistry::RegisterComponentNodes()
    {
        {
            NodeDescriptor desc;
            desc.m_name = "GetComponent";
            desc.m_category = "Component";
            desc.m_flags = NodeFlag::COMPONENT_GET;
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "SetComponent";
            desc.m_category = "Component";
            desc.m_flags = NodeFlag::COMPONENT_SET;
            desc.m_pins.PushBack(ExecIn());
            desc.m_pins.PushBack(ExecOut());
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Break";
            desc.m_category = "Utility";
            desc.m_flags = NodeFlag::STRUCT_BREAK;
            desc.m_pins.PushBack(InGeneric("Value", Constraint::NONE));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "Make";
            desc.m_category = "Utility";
            desc.m_flags = NodeFlag::STRUCT_MAKE;
            desc.m_pins.PushBack(OutGeneric("Value", Constraint::NONE));
            Add(static_cast<NodeDescriptor&&>(desc));
        }
    }

    void NodeRegistry::RegisterDebugNodes()
    {
        {
            NodeDescriptor desc;
            desc.m_name = "Print";
            desc.m_category = "Debug";
            desc.m_flags = NodeFlag::PRINT;
            desc.m_pins.PushBack(ExecIn());
            desc.m_pins.PushBack(InGeneric("Value", Constraint::NONE));
            desc.m_pins.PushBack(ExecOut());
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "ForEach";
            desc.m_category = "Flow";
            desc.m_flags = NodeFlag::FOR_EACH;
            desc.m_pins.PushBack(ExecIn());
            desc.m_pins.PushBack(In("Count", PType::Int32()));
            desc.m_pins.PushBack(ExecOut("Body"));
            desc.m_pins.PushBack(Out("Index", PType::Int32()));
            desc.m_pins.PushBack(ExecOut("Done"));
            Add(static_cast<NodeDescriptor&&>(desc));
        }

        {
            NodeDescriptor desc;
            desc.m_name = "While";
            desc.m_category = "Flow";
            desc.m_flags = NodeFlag::WHILE_LOOP;
            desc.m_pins.PushBack(ExecIn());
            desc.m_pins.PushBack(In("Condition", PType::Bool()));
            desc.m_pins.PushBack(ExecOut("Body"));
            desc.m_pins.PushBack(ExecOut("Done"));
            Add(static_cast<NodeDescriptor&&>(desc));
        }
    }
} // namespace propolis
