#pragma once

#include <propolis/graph/graph_types.h>
#include <propolis/types/ptype.h>
#include <propolis/types/type_solver.h>

#include <hive/hive_config.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>

namespace propolis
{
    struct PinDescriptor
    {
        PinDirection m_direction;
        PType m_type;
        Constraint m_constraint{Constraint::NONE};
        wax::String m_name;
        bool m_isGeneric{false};
        uint8_t m_genericGroup{0};
    };

    enum class NodeFlag : uint16_t
    {
        NONE           = 0,
        LIFECYCLE      = 1 << 0,
        VARIABLE_GET   = 1 << 1,
        VARIABLE_SET   = 1 << 2,
        BRANCH         = 1 << 3,
        SEQUENCE       = 1 << 4,
        COMPONENT_GET  = 1 << 5,
        COMPONENT_SET  = 1 << 6,
        STRUCT_BREAK   = 1 << 7,
        STRUCT_MAKE    = 1 << 8,
        PRINT          = 1 << 9,
        FOR_EACH       = 1 << 10,
        WHILE_LOOP     = 1 << 11,
    };

    [[nodiscard]] constexpr NodeFlag operator|(NodeFlag a, NodeFlag b) noexcept
    {
        return static_cast<NodeFlag>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
    }

    [[nodiscard]] constexpr bool HasFlag(NodeFlag flags, NodeFlag test) noexcept
    {
        return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(test)) != 0;
    }

    struct NodeDescriptor
    {
        wax::String m_name;
        wax::String m_category;
        wax::Vector<PinDescriptor> m_pins;
        wax::String m_codegenTemplate;
        NodeFlag m_flags{NodeFlag::NONE};
    };

    class HIVE_API NodeRegistry
    {
    public:
        NodeRegistry();

        [[nodiscard]] const NodeDescriptor* Find(const char* name) const;
        [[nodiscard]] const wax::Vector<NodeDescriptor>& All() const noexcept { return m_descriptors; }

    private:
        void Add(NodeDescriptor desc);

        void RegisterMathNodes();
        void RegisterLogicNodes();
        void RegisterConversionNodes();
        void RegisterEventNodes();
        void RegisterVariableNodes();
        void RegisterComponentNodes();
        void RegisterDebugNodes();

        wax::Vector<NodeDescriptor> m_descriptors;
    };
} // namespace propolis
