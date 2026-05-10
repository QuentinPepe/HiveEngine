#include <propolis/types/ptype.h>

#include <larvae/larvae.h>

namespace
{
    using namespace propolis;

    auto t1 = larvae::RegisterTest("PropolisPType", "BasicTypeEquality", []() {
        larvae::AssertTrue(PType::Float32() == PType::Float32());
        larvae::AssertTrue(PType::Int32() != PType::Float32());
        larvae::AssertTrue(PType::Bool() != PType::Int32());
        larvae::AssertTrue(PType::Vec3() == PType::Vec3());
    });

    auto t2 = larvae::RegisterTest("PropolisPType", "TypeVarIsUnresolved", []() {
        auto var = PType::Var(1);
        larvae::AssertFalse(var.IsResolved());
        larvae::AssertTrue(PType::Float32().IsResolved());
        larvae::AssertTrue(PType::Entity().IsResolved());
    });

    auto t3 = larvae::RegisterTest("PropolisPType", "IsNumeric", []() {
        larvae::AssertTrue(PType::Int32().IsNumeric());
        larvae::AssertTrue(PType::UInt32().IsNumeric());
        larvae::AssertTrue(PType::Float32().IsNumeric());
        larvae::AssertTrue(PType::Float64().IsNumeric());
        larvae::AssertTrue(PType::Vec2().IsNumeric());
        larvae::AssertTrue(PType::Vec3().IsNumeric());
        larvae::AssertTrue(PType::Vec4().IsNumeric());

        larvae::AssertFalse(PType::Bool().IsNumeric());
        larvae::AssertFalse(PType::Entity().IsNumeric());
        larvae::AssertFalse(PType::Signal().IsNumeric());
        larvae::AssertFalse(PType::Quat().IsNumeric());
        larvae::AssertFalse(PType::Mat4().IsNumeric());
    });

    auto t4 = larvae::RegisterTest("PropolisPType", "IsOrdered", []() {
        larvae::AssertTrue(PType::Int32().IsOrdered());
        larvae::AssertTrue(PType::Float32().IsOrdered());
        larvae::AssertFalse(PType::Vec3().IsOrdered());
        larvae::AssertFalse(PType::Bool().IsOrdered());
    });

    auto t5 = larvae::RegisterTest("PropolisPType", "IsInterpolable", []() {
        larvae::AssertTrue(PType::Float32().IsInterpolable());
        larvae::AssertTrue(PType::Vec3().IsInterpolable());
        larvae::AssertTrue(PType::Quat().IsInterpolable());
        larvae::AssertFalse(PType::Int32().IsInterpolable());
        larvae::AssertFalse(PType::Bool().IsInterpolable());
        larvae::AssertFalse(PType::Entity().IsInterpolable());
    });

    auto t6 = larvae::RegisterTest("PropolisPType", "ParametricTypes", []() {
        auto stream = PType::Stream(42);
        larvae::AssertTrue(stream.m_kind == PTypeKind::STREAM);
        larvae::AssertEqual(stream.m_param, 42u);

        auto comp = PType::Component(7);
        larvae::AssertTrue(comp.m_kind == PTypeKind::COMPONENT);
        larvae::AssertEqual(comp.m_param, 7u);

        auto opt = PType::Option(3);
        larvae::AssertTrue(opt.m_kind == PTypeKind::OPTION);
    });

    auto t7 = larvae::RegisterTest("PropolisPType", "KindNames", []() {
        larvae::AssertTrue(std::string_view{PTypeKindName(PTypeKind::FLOAT32)} == "Float32");
        larvae::AssertTrue(std::string_view{PTypeKindName(PTypeKind::ENTITY)} == "Entity");
        larvae::AssertTrue(std::string_view{PTypeKindName(PTypeKind::TYPE_VAR)} == "TypeVar");
        larvae::AssertTrue(std::string_view{PTypeKindName(PTypeKind::SIGNAL)} == "Signal");
    });
} // namespace
