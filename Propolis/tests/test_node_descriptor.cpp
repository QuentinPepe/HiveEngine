#include <propolis/nodes/node_descriptor.h>

#include <larvae/larvae.h>

namespace
{
    using namespace propolis;

    auto t1 = larvae::RegisterTest("PropolisNodeRegistry", "FindAdd", []() {
        NodeRegistry reg;
        const auto* add = reg.Find("Add");
        larvae::AssertTrue(add != nullptr);
        larvae::AssertTrue(add->m_name == "Add");
        larvae::AssertTrue(add->m_category == "Math");
        larvae::AssertTrue(add->m_pins.Size() == 3);
    });

    auto t2 = larvae::RegisterTest("PropolisNodeRegistry", "FindLerp", []() {
        NodeRegistry reg;
        const auto* lerp = reg.Find("Lerp");
        larvae::AssertTrue(lerp != nullptr);
        larvae::AssertTrue(lerp->m_pins.Size() == 4);

        larvae::AssertFalse(lerp->m_pins[2].m_isGeneric);
        larvae::AssertTrue(lerp->m_pins[2].m_type == PType::Float32());
    });

    auto t3 = larvae::RegisterTest("PropolisNodeRegistry", "FindNotReturnsNull", []() {
        NodeRegistry reg;
        larvae::AssertTrue(reg.Find("NonExistent") == nullptr);
    });

    auto t4 = larvae::RegisterTest("PropolisNodeRegistry", "AllNonSpecialNodesHaveCodegenTemplate", []() {
        NodeRegistry reg;
        for (size_t i = 0; i < reg.All().Size(); ++i)
        {
            const auto& desc = reg.All()[i];
            if (desc.m_flags != NodeFlag::NONE)
            {
                continue;
            }
            larvae::AssertTrue(desc.m_codegenTemplate.Size() > 0);
        }
    });

    auto t5 = larvae::RegisterTest("PropolisNodeRegistry", "GenericPinsHaveConstraints", []() {
        NodeRegistry reg;
        const auto* add = reg.Find("Add");
        larvae::AssertTrue(add->m_pins[0].m_isGeneric);
        larvae::AssertTrue(add->m_pins[0].m_constraint == Constraint::NUMERIC);
    });

    auto t6 = larvae::RegisterTest("PropolisNodeRegistry", "ComparisonOutputsBool", []() {
        NodeRegistry reg;
        const auto* less = reg.Find("Less");
        larvae::AssertTrue(less != nullptr);
        const auto& outPin = less->m_pins[less->m_pins.Size() - 1];
        larvae::AssertTrue(outPin.m_direction == PinDirection::OUTPUT);
        larvae::AssertTrue(outPin.m_type == PType::Bool());
        larvae::AssertFalse(outPin.m_isGeneric);
    });

    auto t7 = larvae::RegisterTest("PropolisNodeRegistry", "SelectHasGenericInOut", []() {
        NodeRegistry reg;
        const auto* sel = reg.Find("Select");
        larvae::AssertTrue(sel != nullptr);
        larvae::AssertFalse(sel->m_pins[0].m_isGeneric);
        larvae::AssertTrue(sel->m_pins[1].m_isGeneric);
        larvae::AssertTrue(sel->m_pins[2].m_isGeneric);
        larvae::AssertTrue(sel->m_pins[3].m_isGeneric);
    });
} // namespace
