#include <propolis/runtime/propolis_executor.h>
#include <propolis/runtime/propolis_script.h>
#include <propolis/runtime/script_registry.h>

#include <queen/world/world.h>

#include <larvae/larvae.h>

namespace
{
    using namespace propolis;

    static constexpr uint32_t kTestHash = ScriptNameHash("Test");

    static bool g_detachCalled = false;

    struct TestState
    {
        int tickCount{0};
        bool attached{false};
        float lastDt{0.0f};
    };

    static void OnTick(queen::Entity, void* s, queen::World&, float dt)
    {
        auto* state = static_cast<TestState*>(s);
        state->tickCount++;
        state->lastDt = dt;
    }

    static void OnAttach(queen::Entity, void* s, queen::World&)
    {
        static_cast<TestState*>(s)->attached = true;
    }

    static void OnDetach(queen::Entity, void*, queen::World&)
    {
        g_detachCalled = true;
    }

    static ScriptEntry MakeEntry()
    {
        ScriptEntry entry;
        entry.m_name = "Test";
        entry.m_stateSize = sizeof(TestState);
        entry.m_stateAlignment = alignof(TestState);
        entry.m_onTick = OnTick;
        entry.m_onAttach = OnAttach;
        entry.m_onDetach = OnDetach;
        return entry;
    }

    auto e1 = larvae::RegisterTest("PropolisExecutor", "InitAllocatesStateAndCallsAttach", []() {
        queen::World world{};

        ScriptRegistry registry;
        registry.Register(MakeEntry());
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.016f});

        RegisterPropolisExecutor(world);

        auto entity = world.Spawn(PropolisScript{kTestHash, nullptr});
        world.Advance();

        auto* script = world.Get<PropolisScript>(entity);
        larvae::AssertTrue(script != nullptr);
        larvae::AssertTrue(script->m_state != nullptr);

        auto* state = static_cast<TestState*>(script->m_state);
        larvae::AssertTrue(state->attached);
    });

    auto e2 = larvae::RegisterTest("PropolisExecutor", "TickCallsOnTick", []() {
        queen::World world{};

        ScriptRegistry registry;
        registry.Register(MakeEntry());
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.016f});

        RegisterPropolisExecutor(world);

        auto entity = world.Spawn(PropolisScript{kTestHash, nullptr});
        world.Advance();

        auto* script = world.Get<PropolisScript>(entity);
        auto* state = static_cast<TestState*>(script->m_state);
        larvae::AssertEqual(state->tickCount, 1);
        larvae::AssertFloatEqual(state->lastDt, 0.016f);
    });

    auto e3 = larvae::RegisterTest("PropolisExecutor", "MultipleTicksAccumulate", []() {
        queen::World world{};

        ScriptRegistry registry;
        registry.Register(MakeEntry());
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.016f});

        RegisterPropolisExecutor(world);

        auto entity = world.Spawn(PropolisScript{kTestHash, nullptr});
        world.Advance();
        world.Advance();
        world.Advance();

        auto* script = world.Get<PropolisScript>(entity);
        auto* state = static_cast<TestState*>(script->m_state);
        larvae::AssertEqual(state->tickCount, 3);
    });

    auto e4 = larvae::RegisterTest("PropolisExecutor", "RemoveCallsDetach", []() {
        queen::World world{};

        ScriptRegistry registry;
        registry.Register(MakeEntry());
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.016f});

        RegisterPropolisExecutor(world);

        auto entity = world.Spawn(PropolisScript{kTestHash, nullptr});
        world.Advance();

        g_detachCalled = false;
        world.Remove<PropolisScript>(entity);
        larvae::AssertTrue(g_detachCalled);
    });

    auto e5 = larvae::RegisterTest("PropolisExecutor", "UnknownHashSkipped", []() {
        queen::World world{};

        ScriptRegistry registry;
        registry.Register(MakeEntry());
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.016f});

        RegisterPropolisExecutor(world);

        auto entity = world.Spawn(PropolisScript{0xDEADBEEF, nullptr});
        world.Advance();

        auto* script = world.Get<PropolisScript>(entity);
        larvae::AssertTrue(script->m_state == nullptr);
    });

    auto e6 = larvae::RegisterTest("PropolisExecutor", "MultipleEntitiesSameScript", []() {
        queen::World world{};

        ScriptRegistry registry;
        registry.Register(MakeEntry());
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.033f});

        RegisterPropolisExecutor(world);

        auto ent1 = world.Spawn(PropolisScript{kTestHash, nullptr});
        auto ent2 = world.Spawn(PropolisScript{kTestHash, nullptr});
        world.Advance();

        auto* s1 = static_cast<TestState*>(world.Get<PropolisScript>(ent1)->m_state);
        auto* s2 = static_cast<TestState*>(world.Get<PropolisScript>(ent2)->m_state);

        larvae::AssertTrue(s1 != s2);
        larvae::AssertTrue(s1->attached);
        larvae::AssertTrue(s2->attached);
        larvae::AssertEqual(s1->tickCount, 1);
        larvae::AssertEqual(s2->tickCount, 1);
    });

    auto e7 = larvae::RegisterTest("PropolisExecutor", "PrepareReloadDetachesAndFreesState", []() {
        queen::World world{};

        ScriptRegistry registry;
        registry.Register(MakeEntry());
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.016f});

        RegisterPropolisExecutor(world);

        auto entity = world.Spawn(PropolisScript{kTestHash, nullptr});
        world.Advance();

        auto* script = world.Get<PropolisScript>(entity);
        larvae::AssertTrue(script->m_state != nullptr);

        g_detachCalled = false;
        PrepareScriptReload(world);

        script = world.Get<PropolisScript>(entity);
        larvae::AssertTrue(script->m_state == nullptr);
        larvae::AssertTrue(g_detachCalled);
        larvae::AssertEqual(script->m_nameHash, kTestHash);

        auto* reg = world.Resource<ScriptRegistry>();
        larvae::AssertEqual(reg->Count(), size_t{0});
    });

    auto e8 = larvae::RegisterTest("PropolisExecutor", "FullReloadCycleReInitializes", []() {
        queen::World world{};

        ScriptRegistry registry;
        registry.Register(MakeEntry());
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.016f});

        RegisterPropolisExecutor(world);

        auto entity = world.Spawn(PropolisScript{kTestHash, nullptr});
        world.Advance();
        world.Advance();

        auto* state1 = static_cast<TestState*>(world.Get<PropolisScript>(entity)->m_state);
        larvae::AssertEqual(state1->tickCount, 2);

        PrepareScriptReload(world);

        auto* reg = world.Resource<ScriptRegistry>();
        reg->Register(MakeEntry());

        world.Advance();

        auto* script = world.Get<PropolisScript>(entity);
        larvae::AssertTrue(script->m_state != nullptr);

        auto* state2 = static_cast<TestState*>(script->m_state);
        larvae::AssertTrue(state2->attached);
        larvae::AssertEqual(state2->tickCount, 1);
    });

    auto e9 = larvae::RegisterTest("PropolisExecutor", "ReloadWithScriptReorderWorks", []() {
        queen::World world{};

        ScriptRegistry registry;

        ScriptEntry entryA = MakeEntry();
        entryA.m_name = "ScriptA";
        ScriptEntry entryB = MakeEntry();
        entryB.m_name = "ScriptB";

        registry.Register(static_cast<ScriptEntry&&>(entryA));
        registry.Register(static_cast<ScriptEntry&&>(entryB));
        world.InsertResource(static_cast<ScriptRegistry&&>(registry));
        world.InsertResource(ScriptTime{0.016f});

        RegisterPropolisExecutor(world);

        uint32_t hashA = ScriptNameHash("ScriptA");
        uint32_t hashB = ScriptNameHash("ScriptB");

        auto ent1 = world.Spawn(PropolisScript{hashA, nullptr});
        auto ent2 = world.Spawn(PropolisScript{hashB, nullptr});
        world.Advance();

        larvae::AssertTrue(world.Get<PropolisScript>(ent1)->m_state != nullptr);
        larvae::AssertTrue(world.Get<PropolisScript>(ent2)->m_state != nullptr);

        PrepareScriptReload(world);

        auto* reg = world.Resource<ScriptRegistry>();
        ScriptEntry newB = MakeEntry();
        newB.m_name = "ScriptB";
        ScriptEntry newA = MakeEntry();
        newA.m_name = "ScriptA";
        reg->Register(static_cast<ScriptEntry&&>(newB));
        reg->Register(static_cast<ScriptEntry&&>(newA));

        world.Advance();

        larvae::AssertTrue(world.Get<PropolisScript>(ent1)->m_state != nullptr);
        larvae::AssertTrue(world.Get<PropolisScript>(ent2)->m_state != nullptr);
        larvae::AssertEqual(world.Get<PropolisScript>(ent1)->m_nameHash, hashA);
        larvae::AssertEqual(world.Get<PropolisScript>(ent2)->m_nameHash, hashB);
    });
} // namespace
