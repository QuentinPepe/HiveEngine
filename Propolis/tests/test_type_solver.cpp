#include <propolis/types/type_solver.h>

#include <larvae/larvae.h>

namespace
{
    using namespace propolis;

    auto t1 = larvae::RegisterTest("PropolisTypeSolver", "UnifySameType", []() {
        TypeSolver solver;
        auto result = solver.Unify(PType::Float32(), PType::Float32());
        larvae::AssertTrue(result.m_ok);
        larvae::AssertTrue(result.m_type == PType::Float32());
    });

    auto t2 = larvae::RegisterTest("PropolisTypeSolver", "UnifyIncompatibleFails", []() {
        TypeSolver solver;
        auto result = solver.Unify(PType::Bool(), PType::Float32());
        larvae::AssertFalse(result.m_ok);
        larvae::AssertTrue(result.m_error.Size() > 0);
    });

    auto t3 = larvae::RegisterTest("PropolisTypeSolver", "UnifyVarBindsToConcreteType", []() {
        TypeSolver solver;
        auto var = solver.FreshVar();
        auto result = solver.Unify(var, PType::Vec3());
        larvae::AssertTrue(result.m_ok);
        larvae::AssertTrue(result.m_type == PType::Vec3());

        auto resolved = solver.Resolve(var);
        larvae::AssertTrue(resolved == PType::Vec3());
    });

    auto t4 = larvae::RegisterTest("PropolisTypeSolver", "UnifyTwoVarsChained", []() {
        TypeSolver solver;
        auto a = solver.FreshVar();
        auto b = solver.FreshVar();

        auto r1 = solver.Unify(a, b);
        larvae::AssertTrue(r1.m_ok);

        auto r2 = solver.Unify(b, PType::Int32());
        larvae::AssertTrue(r2.m_ok);

        larvae::AssertTrue(solver.Resolve(a) == PType::Int32());
        larvae::AssertTrue(solver.Resolve(b) == PType::Int32());
    });

    auto t5 = larvae::RegisterTest("PropolisTypeSolver", "PromotionIntToFloat", []() {
        TypeSolver solver;
        auto result = solver.Unify(PType::Int32(), PType::Float32());
        larvae::AssertTrue(result.m_ok);
        larvae::AssertTrue(result.m_type == PType::Float32());
    });

    auto t6 = larvae::RegisterTest("PropolisTypeSolver", "PromotionFloat32ToFloat64", []() {
        TypeSolver solver;
        auto result = solver.Unify(PType::Float32(), PType::Float64());
        larvae::AssertTrue(result.m_ok);
        larvae::AssertTrue(result.m_type == PType::Float64());
    });

    auto t7 = larvae::RegisterTest("PropolisTypeSolver", "PromotionUInt32ToInt32", []() {
        TypeSolver solver;
        auto result = solver.Unify(PType::UInt32(), PType::Int32());
        larvae::AssertTrue(result.m_ok);
        larvae::AssertTrue(result.m_type == PType::Int32());
    });

    auto t8 = larvae::RegisterTest("PropolisTypeSolver", "PromotionUInt32ToFloat32", []() {
        TypeSolver solver;
        auto result = solver.Unify(PType::UInt32(), PType::Float32());
        larvae::AssertTrue(result.m_ok);
        larvae::AssertTrue(result.m_type == PType::Float32());
    });

    auto t9 = larvae::RegisterTest("PropolisTypeSolver", "NoPromotionVec3ToFloat", []() {
        TypeSolver solver;
        auto result = solver.Unify(PType::Vec3(), PType::Float32());
        larvae::AssertFalse(result.m_ok);
    });

    auto t10 = larvae::RegisterTest("PropolisTypeSolver", "NoPromotionBoolToInt", []() {
        TypeSolver solver;
        auto result = solver.Unify(PType::Bool(), PType::Int32());
        larvae::AssertFalse(result.m_ok);
    });

    auto t11 = larvae::RegisterTest("PropolisTypeSolver", "VarPromotionThroughUnify", []() {
        TypeSolver solver;
        auto var = solver.FreshVar();

        auto r1 = solver.Unify(var, PType::Int32());
        larvae::AssertTrue(r1.m_ok);

        auto r2 = solver.Unify(solver.Resolve(var), PType::Float32());
        larvae::AssertTrue(r2.m_ok);
        larvae::AssertTrue(r2.m_type == PType::Float32());
    });

    auto t12 = larvae::RegisterTest("PropolisTypeSolver", "ConstraintNumeric", []() {
        TypeSolver solver;
        larvae::AssertTrue(solver.SatisfiesConstraint(PType::Float32(), Constraint::NUMERIC));
        larvae::AssertTrue(solver.SatisfiesConstraint(PType::Int32(), Constraint::NUMERIC));
        larvae::AssertTrue(solver.SatisfiesConstraint(PType::Vec3(), Constraint::NUMERIC));
        larvae::AssertFalse(solver.SatisfiesConstraint(PType::Bool(), Constraint::NUMERIC));
        larvae::AssertFalse(solver.SatisfiesConstraint(PType::Entity(), Constraint::NUMERIC));
    });

    auto t13 = larvae::RegisterTest("PropolisTypeSolver", "ConstraintInterpolable", []() {
        TypeSolver solver;
        larvae::AssertTrue(solver.SatisfiesConstraint(PType::Float32(), Constraint::INTERPOLABLE));
        larvae::AssertTrue(solver.SatisfiesConstraint(PType::Quat(), Constraint::INTERPOLABLE));
        larvae::AssertFalse(solver.SatisfiesConstraint(PType::Int32(), Constraint::INTERPOLABLE));
    });

    auto t14 = larvae::RegisterTest("PropolisTypeSolver", "ConstraintEquatable", []() {
        TypeSolver solver;
        larvae::AssertTrue(solver.SatisfiesConstraint(PType::Int32(), Constraint::EQUATABLE));
        larvae::AssertTrue(solver.SatisfiesConstraint(PType::Bool(), Constraint::EQUATABLE));
        larvae::AssertTrue(solver.SatisfiesConstraint(PType::Entity(), Constraint::EQUATABLE));
        larvae::AssertFalse(solver.SatisfiesConstraint(PType::Signal(), Constraint::EQUATABLE));
    });

    auto t15 = larvae::RegisterTest("PropolisTypeSolver", "UnresolvedVarSatisfiesAnyConstraint", []() {
        TypeSolver solver;
        auto var = solver.FreshVar();
        larvae::AssertTrue(solver.SatisfiesConstraint(var, Constraint::NUMERIC));
        larvae::AssertTrue(solver.SatisfiesConstraint(var, Constraint::INTERPOLABLE));
    });

    auto t16 = larvae::RegisterTest("PropolisTypeSolver", "Reset", []() {
        TypeSolver solver;
        auto var = solver.FreshVar();
        (void)solver.Unify(var, PType::Float32());
        larvae::AssertTrue(solver.Resolve(var) == PType::Float32());

        solver.Reset();
        auto var2 = solver.FreshVar();
        larvae::AssertEqual(var2.m_param, 1u);
        larvae::AssertFalse(var2.IsResolved());
    });
} // namespace
