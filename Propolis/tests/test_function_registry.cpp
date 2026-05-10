#include <propolis/runtime/function_registry.h>
#include <propolis/types/ptype.h>

#include <queen/world/world.h>

#include <larvae/larvae.h>

namespace
{
    using namespace propolis;

    auto t1 = larvae::RegisterTest("PropolisFunctionRegistry", "RegisterFunction_AddsToRegistry", []() {
        ParamInfo params[] = {
            {"a", PType::Float32()},
            {"b", PType::Float32()},
        };
        FunctionEntry entry{"Add", "Math", "ns::Add", PType::Float32(), params, 2};

        FunctionRegistry registry;
        registry.Register(entry);
        larvae::AssertEqual<size_t, size_t>(registry.Count(), 1);
    });

    auto t2 = larvae::RegisterTest("PropolisFunctionRegistry", "Find_ByName_ReturnsEntry", []() {
        ParamInfo params[] = {{"x", PType::Float32()}};
        FunctionEntry entry{"Square", "Math", "ns::Square", PType::Float32(), params, 1};

        FunctionRegistry registry;
        registry.Register(entry);

        const FunctionEntry* found = registry.Find("Square");
        larvae::AssertTrue(found != nullptr);
        larvae::AssertTrue(found->m_returnType.m_kind == PTypeKind::FLOAT32);
        larvae::AssertEqual<size_t, size_t>(found->m_paramCount, 1);
    });

    auto t3 = larvae::RegisterTest("PropolisFunctionRegistry", "Find_Missing_ReturnsNull", []() {
        FunctionRegistry registry;
        larvae::AssertTrue(registry.Find("NotRegistered") == nullptr);
    });

    auto t4 = larvae::RegisterTest("PropolisFunctionRegistry", "Clear_EmptiesRegistry", []() {
        FunctionEntry entry{"Foo", "Test", "Foo", PType::Signal(), nullptr, 0};
        FunctionRegistry registry;
        registry.Register(entry);
        registry.Clear();
        larvae::AssertEqual<size_t, size_t>(registry.Count(), 0);
        larvae::AssertTrue(registry.Find("Foo") == nullptr);
    });

    auto t5 = larvae::RegisterTest("PropolisFunctionRegistry", "MultipleFunctions_AllRegistered", []() {
        FunctionEntry e1{"A", "Math", "A", PType::Float32(), nullptr, 0};
        FunctionEntry e2{"B", "Math", "B", PType::Int32(), nullptr, 0};
        FunctionEntry e3{"C", "Logic", "C", PType::Bool(), nullptr, 0};

        FunctionRegistry registry;
        registry.Register(e1);
        registry.Register(e2);
        registry.Register(e3);
        larvae::AssertEqual<size_t, size_t>(registry.Count(), 3);
        larvae::AssertTrue(registry.Find("A") != nullptr);
        larvae::AssertTrue(registry.Find("B") != nullptr);
        larvae::AssertTrue(registry.Find("C") != nullptr);
    });

    auto tNext = larvae::RegisterTest("PropolisFunctionRegistry", "Register_NullsIntrusiveNextPointer", []() {
        FunctionEntry source{"Linked", "Test", "Linked", PType::Float32(), nullptr, 0};
        // Simulate a stale linked-list pointer.
        FunctionEntry sentinel{"Sentinel", "Test", "Sentinel", PType::Float32(), nullptr, 0};
        source.m_next = &sentinel;

        FunctionRegistry registry;
        registry.Register(source);

        const FunctionEntry* stored = registry.Find("Linked");
        larvae::AssertTrue(stored != nullptr);
        larvae::AssertTrue(stored->m_next == nullptr);
    });

    auto t6 = larvae::RegisterTest("PropolisFunctionRegistry", "RegisterIntoWorld_InsertsResource", []() {
        queen::World world;
        FunctionRegistry registry;
        registry.Register(FunctionEntry{"X", "Math", "X", PType::Float32(), nullptr, 0});
        world.InsertResource(static_cast<FunctionRegistry&&>(registry));

        auto* res = world.Resource<FunctionRegistry>();
        larvae::AssertTrue(res != nullptr);
        larvae::AssertEqual<size_t, size_t>(res->Count(), 1);
    });
} // namespace
