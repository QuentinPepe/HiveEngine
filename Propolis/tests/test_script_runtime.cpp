#include <propolis/runtime/script_instance.h>
#include <propolis/runtime/script_registry.h>

#include <queen/core/entity.h>

#include <larvae/larvae.h>

namespace
{
    using namespace propolis;

    struct TestState
    {
        float value{0.0f};
        int tickCount{0};
        bool attached{false};
        bool detached{false};
        float lastDt{0.0f};
    };

    static void TestOnTick(queen::Entity, void* s, queen::World&, float dt)
    {
        auto* state = static_cast<TestState*>(s);
        state->tickCount++;
        state->lastDt = dt;
    }

    static void TestOnAttach(queen::Entity, void* s, queen::World&)
    {
        static_cast<TestState*>(s)->attached = true;
    }

    static void TestOnDetach(queen::Entity, void* s, queen::World&)
    {
        static_cast<TestState*>(s)->detached = true;
    }

    static ScriptEntry MakeTestEntry()
    {
        ScriptEntry entry;
        entry.m_name = "TestScript";
        entry.m_stateSize = sizeof(TestState);
        entry.m_stateAlignment = alignof(TestState);
        entry.m_onTick = TestOnTick;
        entry.m_onAttach = TestOnAttach;
        entry.m_onDetach = TestOnDetach;
        return entry;
    }

    auto r1 = larvae::RegisterTest("ScriptRuntime", "RegistryEmpty", []() {
        ScriptRegistry registry;
        larvae::AssertEqual(registry.Count(), size_t{0});
        larvae::AssertTrue(registry.Find("anything") == nullptr);
    });

    auto r2 = larvae::RegisterTest("ScriptRuntime", "RegistryRegisterAndFind", []() {
        ScriptRegistry registry;
        registry.Register(MakeTestEntry());

        larvae::AssertEqual(registry.Count(), size_t{1});
        const auto* found = registry.Find("TestScript");
        larvae::AssertTrue(found != nullptr);
        larvae::AssertEqual(found->m_stateSize, static_cast<uint32_t>(sizeof(TestState)));
        larvae::AssertTrue(found->m_onTick == TestOnTick);
    });

    auto r3 = larvae::RegisterTest("ScriptRuntime", "RegistryFindMissing", []() {
        ScriptRegistry registry;
        registry.Register(MakeTestEntry());
        larvae::AssertTrue(registry.Find("NonExistent") == nullptr);
    });

    auto r4 = larvae::RegisterTest("ScriptRuntime", "RegistryMultipleEntries", []() {
        ScriptRegistry registry;

        ScriptEntry a;
        a.m_name = "ScriptA";
        registry.Register(static_cast<ScriptEntry&&>(a));

        ScriptEntry b;
        b.m_name = "ScriptB";
        registry.Register(static_cast<ScriptEntry&&>(b));

        larvae::AssertEqual(registry.Count(), size_t{2});
        larvae::AssertTrue(registry.Find("ScriptA") != nullptr);
        larvae::AssertTrue(registry.Find("ScriptB") != nullptr);
    });

    auto r5 = larvae::RegisterTest("ScriptRuntime", "TickCallsCallback", []() {
        ScriptEntry entry = MakeTestEntry();
        TestState state;
        ScriptInstance inst{&entry, &state};
        queen::Entity entity{0, 1};

        alignas(16) char worldBuf[256]{};
        auto& world = reinterpret_cast<queen::World&>(worldBuf);
        TickScript(inst, entity, world, 0.016f);

        larvae::AssertEqual(state.tickCount, 1);
        larvae::AssertFloatEqual(state.lastDt, 0.016f);
    });

    auto r6 = larvae::RegisterTest("ScriptRuntime", "AttachCallsCallback", []() {
        ScriptEntry entry = MakeTestEntry();
        TestState state;
        ScriptInstance inst{&entry, &state};
        queen::Entity entity{0, 1};

        alignas(16) char worldBuf[256]{};
        auto& world = reinterpret_cast<queen::World&>(worldBuf);
        AttachScript(inst, entity, world);

        larvae::AssertTrue(state.attached);
    });

    auto r7 = larvae::RegisterTest("ScriptRuntime", "DetachCallsCallback", []() {
        ScriptEntry entry = MakeTestEntry();
        TestState state;
        ScriptInstance inst{&entry, &state};
        queen::Entity entity{0, 1};

        alignas(16) char worldBuf[256]{};
        auto& world = reinterpret_cast<queen::World&>(worldBuf);
        DetachScript(inst, entity, world);

        larvae::AssertTrue(state.detached);
    });

    auto r8 = larvae::RegisterTest("ScriptRuntime", "NullEntrySafe", []() {
        ScriptInstance inst{nullptr, nullptr};
        queen::Entity entity{0, 1};
        alignas(16) char worldBuf[256]{};
        auto& world = reinterpret_cast<queen::World&>(worldBuf);

        TickScript(inst, entity, world, 0.016f);
        AttachScript(inst, entity, world);
        DetachScript(inst, entity, world);
    });

    auto r9 = larvae::RegisterTest("ScriptRuntime", "NullCallbackSafe", []() {
        ScriptEntry entry;
        entry.m_name = "Empty";

        TestState state;
        ScriptInstance inst{&entry, &state};
        queen::Entity entity{0, 1};
        alignas(16) char worldBuf[256]{};
        auto& world = reinterpret_cast<queen::World&>(worldBuf);

        TickScript(inst, entity, world, 0.016f);
        AttachScript(inst, entity, world);
        DetachScript(inst, entity, world);
    });

    auto r10 = larvae::RegisterTest("ScriptRuntime", "MultipleTicksAccumulate", []() {
        ScriptEntry entry = MakeTestEntry();
        TestState state;
        ScriptInstance inst{&entry, &state};
        queen::Entity entity{0, 1};
        alignas(16) char worldBuf[256]{};
        auto& world = reinterpret_cast<queen::World&>(worldBuf);

        for (int i = 0; i < 10; ++i)
        {
            TickScript(inst, entity, world, 0.016f);
        }

        larvae::AssertEqual(state.tickCount, 10);
    });
} // namespace
