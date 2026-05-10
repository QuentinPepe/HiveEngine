#include <propolis/macros/blueprint_function.h>
#include <propolis/runtime/function_registry.h>

#include <larvae/larvae.h>

// Each macro arity below contributes one FunctionEntry to the test executable's
// BlueprintFunctionHead linked list at static-init time. The single test at the
// bottom walks that list and asserts the signatures came through correctly.
//
// Names are scoped with TestArity_ to avoid colliding with other test files'
// HIVE_BLUEPRINT_FUNCTION usages (the macro creates a NAME-mangled helper
// namespace).

HIVE_BLUEPRINT_FUNCTION_0(TestArity_Zero, "ArityTest", float)
{
    return 7.0f;
}

HIVE_BLUEPRINT_FUNCTION_1(TestArity_One, "ArityTest", int32_t,
    int32_t, x)
{
    return x + 1;
}

HIVE_BLUEPRINT_FUNCTION_2(TestArity_Two, "ArityTest", float,
    float, a,
    float, b)
{
    return a + b;
}

HIVE_BLUEPRINT_FUNCTION_4(TestArity_Four, "ArityTest", double,
    double, a,
    double, b,
    double, c,
    double, d)
{
    return a + b + c + d;
}

HIVE_BLUEPRINT_FUNCTION_5(TestArity_Five, "ArityTest", uint32_t,
    uint32_t, a,
    uint32_t, b,
    uint32_t, c,
    uint32_t, d,
    uint32_t, e)
{
    return a + b + c + d + e;
}

// Void-returning at arity 1 — exercises the SIGNAL return path.
HIVE_BLUEPRINT_FUNCTION_1(TestArity_VoidOne, "ArityTest", void,
    bool, flag)
{
    (void)flag;
}

namespace
{
    using namespace propolis;

    static const FunctionEntry* FindInHead(const char* name)
    {
        for (FunctionEntry* node = detail::BlueprintFunctionHead(); node; node = node->m_next)
        {
            if (std::strcmp(node->m_name, name) == 0)
            {
                return node;
            }
        }
        return nullptr;
    }

    auto tArities = larvae::RegisterTest("PropolisFunctionRegistry", "MacroArities_AllRegistered", []() {
        const FunctionEntry* zero = FindInHead("TestArity_Zero");
        larvae::AssertTrue(zero != nullptr);
        larvae::AssertEqual<size_t, size_t>(zero->m_paramCount, 0);
        larvae::AssertTrue(zero->m_returnType.m_kind == PTypeKind::FLOAT32);
        larvae::AssertTrue(zero->m_params == nullptr);
        larvae::AssertTrue(TestArity_Zero() == 7.0f);

        const FunctionEntry* one = FindInHead("TestArity_One");
        larvae::AssertTrue(one != nullptr);
        larvae::AssertEqual<size_t, size_t>(one->m_paramCount, 1);
        larvae::AssertTrue(one->m_returnType.m_kind == PTypeKind::INT32);
        larvae::AssertTrue(one->m_params[0].m_type.m_kind == PTypeKind::INT32);
        larvae::AssertTrue(std::strcmp(one->m_params[0].m_name, "x") == 0);
        larvae::AssertTrue(TestArity_One(41) == 42);

        const FunctionEntry* two = FindInHead("TestArity_Two");
        larvae::AssertTrue(two != nullptr);
        larvae::AssertEqual<size_t, size_t>(two->m_paramCount, 2);
        larvae::AssertTrue(two->m_returnType.m_kind == PTypeKind::FLOAT32);
        larvae::AssertTrue(two->m_params[0].m_type.m_kind == PTypeKind::FLOAT32);
        larvae::AssertTrue(two->m_params[1].m_type.m_kind == PTypeKind::FLOAT32);
        larvae::AssertTrue(std::strcmp(two->m_params[1].m_name, "b") == 0);
        larvae::AssertTrue(TestArity_Two(1.0f, 2.0f) == 3.0f);

        const FunctionEntry* four = FindInHead("TestArity_Four");
        larvae::AssertTrue(four != nullptr);
        larvae::AssertEqual<size_t, size_t>(four->m_paramCount, 4);
        larvae::AssertTrue(four->m_returnType.m_kind == PTypeKind::FLOAT64);
        for (size_t i = 0; i < 4; ++i)
        {
            larvae::AssertTrue(four->m_params[i].m_type.m_kind == PTypeKind::FLOAT64);
        }
        larvae::AssertTrue(TestArity_Four(1.0, 2.0, 3.0, 4.0) == 10.0);

        const FunctionEntry* five = FindInHead("TestArity_Five");
        larvae::AssertTrue(five != nullptr);
        larvae::AssertEqual<size_t, size_t>(five->m_paramCount, 5);
        larvae::AssertTrue(five->m_returnType.m_kind == PTypeKind::UINT32);
        for (size_t i = 0; i < 5; ++i)
        {
            larvae::AssertTrue(five->m_params[i].m_type.m_kind == PTypeKind::UINT32);
        }
        larvae::AssertTrue(TestArity_Five(1u, 2u, 3u, 4u, 5u) == 15u);

        const FunctionEntry* voidOne = FindInHead("TestArity_VoidOne");
        larvae::AssertTrue(voidOne != nullptr);
        larvae::AssertTrue(voidOne->m_returnType.m_kind == PTypeKind::SIGNAL);
        larvae::AssertEqual<size_t, size_t>(voidOne->m_paramCount, 1);
        larvae::AssertTrue(voidOne->m_params[0].m_type.m_kind == PTypeKind::BOOL);
    });

    auto tShortName = larvae::RegisterTest("PropolisFunctionRegistry", "MacroArities_ShortNameStrips", []() {
        // ShortName helper should yield the unqualified short name from a qualified one.
        larvae::AssertTrue(std::strcmp(detail::ShortName("foo::bar::Baz"), "Baz") == 0);
        larvae::AssertTrue(std::strcmp(detail::ShortName("Plain"), "Plain") == 0);
        larvae::AssertTrue(std::strcmp(detail::ShortName(""), "") == 0);
    });
} // namespace
