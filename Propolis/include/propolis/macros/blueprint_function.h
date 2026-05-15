#pragma once

#include <propolis/runtime/function_registry.h>
#include <propolis/types/ptype.h>

#include <hive/math/types.h>

#include <queen/core/entity.h>

#include <cstdint>

// HIVE_BLUEPRINT_FUNCTION_N declares a C++ function callable from blueprints.
// Emits a static FunctionEntry plus a FunctionRegistrar that pushes it into the calling
// module's intrusive list at static init; user body follows the macro inline.
// HiveGameplayRegister must call propolis::RegisterAllBlueprintFunctions(world) at runtime
// to copy this list into the World's FunctionRegistry resource.
// C++26 reflection migration: replace with [[hive::blueprint_function(category="Math")]] —
// the FunctionEntry/FunctionRegistry runtime types stay identical.

namespace propolis::detail
{
    template <> inline PType BPTypeOf<bool>()                  { return PType::Bool(); }
    template <> inline PType BPTypeOf<int32_t>()               { return PType::Int32(); }
    template <> inline PType BPTypeOf<uint32_t>()              { return PType::UInt32(); }
    template <> inline PType BPTypeOf<float>()                 { return PType::Float32(); }
    template <> inline PType BPTypeOf<double>()                { return PType::Float64(); }
    template <> inline PType BPTypeOf<hive::math::Float2>()    { return PType::Vec2(); }
    template <> inline PType BPTypeOf<hive::math::Float3>()    { return PType::Vec3(); }
    template <> inline PType BPTypeOf<hive::math::Float4>()    { return PType::Vec4(); }
    template <> inline PType BPTypeOf<hive::math::Quat>()      { return PType::Quat(); }
    template <> inline PType BPTypeOf<hive::math::Mat4>()      { return PType::Mat4(); }
    template <> inline PType BPTypeOf<queen::Entity>()         { return PType::Entity(); }
    template <> inline PType BPTypeOf<void>()                  { return PType::Signal(); }

    // Strips qualifier prefix ("a::b::Foo" → "Foo") at runtime first call.
    [[nodiscard]] inline const char* ShortName(const char* qualified) noexcept
    {
        const char* last = qualified;
        for (const char* p = qualified; *p; ++p)
        {
            if (p[0] == ':' && p[1] == ':')
            {
                last = p + 2;
            }
        }
        return last;
    }
} // namespace propolis::detail

// NAME-based mangling: two HIVE_BLUEPRINT_FUNCTION_N(SameName, ...) at the same scope
// cause a duplicate-symbol error at link time. Intentional — same name = same blueprint
// node identity.
#define HIVE_BLUEPRINT_FUNCTION_INTERNAL_REG(NAME, CATEGORY, RET, COUNT, ...) \
    namespace _bp_register_helper_##NAME \
    { \
        [[maybe_unused]] static const propolis::ParamInfo s_params[] = __VA_ARGS__; \
        static propolis::FunctionEntry s_entry{ \
            propolis::detail::ShortName(#NAME), \
            CATEGORY, \
            #NAME, \
            propolis::detail::BPTypeOf<RET>(), \
            COUNT == 0 ? nullptr : s_params, \
            COUNT \
        }; \
        [[maybe_unused]] static propolis::detail::FunctionRegistrar s_registrar{&s_entry}; \
    }

#define HIVE_BLUEPRINT_FUNCTION_0(NAME, CATEGORY, RET) \
    RET NAME(); \
    HIVE_BLUEPRINT_FUNCTION_INTERNAL_REG(NAME, CATEGORY, RET, 0, {{nullptr, propolis::PType{}}}) \
    RET NAME()

#define HIVE_BLUEPRINT_FUNCTION_1(NAME, CATEGORY, RET, T0, P0) \
    RET NAME(T0 P0); \
    HIVE_BLUEPRINT_FUNCTION_INTERNAL_REG(NAME, CATEGORY, RET, 1, \
        {{#P0, propolis::detail::BPTypeOf<T0>()}}) \
    RET NAME(T0 P0)

#define HIVE_BLUEPRINT_FUNCTION_2(NAME, CATEGORY, RET, T0, P0, T1, P1) \
    RET NAME(T0 P0, T1 P1); \
    HIVE_BLUEPRINT_FUNCTION_INTERNAL_REG(NAME, CATEGORY, RET, 2, \
        {{#P0, propolis::detail::BPTypeOf<T0>()}, \
         {#P1, propolis::detail::BPTypeOf<T1>()}}) \
    RET NAME(T0 P0, T1 P1)

#define HIVE_BLUEPRINT_FUNCTION_3(NAME, CATEGORY, RET, T0, P0, T1, P1, T2, P2) \
    RET NAME(T0 P0, T1 P1, T2 P2); \
    HIVE_BLUEPRINT_FUNCTION_INTERNAL_REG(NAME, CATEGORY, RET, 3, \
        {{#P0, propolis::detail::BPTypeOf<T0>()}, \
         {#P1, propolis::detail::BPTypeOf<T1>()}, \
         {#P2, propolis::detail::BPTypeOf<T2>()}}) \
    RET NAME(T0 P0, T1 P1, T2 P2)

#define HIVE_BLUEPRINT_FUNCTION_4(NAME, CATEGORY, RET, T0, P0, T1, P1, T2, P2, T3, P3) \
    RET NAME(T0 P0, T1 P1, T2 P2, T3 P3); \
    HIVE_BLUEPRINT_FUNCTION_INTERNAL_REG(NAME, CATEGORY, RET, 4, \
        {{#P0, propolis::detail::BPTypeOf<T0>()}, \
         {#P1, propolis::detail::BPTypeOf<T1>()}, \
         {#P2, propolis::detail::BPTypeOf<T2>()}, \
         {#P3, propolis::detail::BPTypeOf<T3>()}}) \
    RET NAME(T0 P0, T1 P1, T2 P2, T3 P3)

#define HIVE_BLUEPRINT_FUNCTION_5(NAME, CATEGORY, RET, T0, P0, T1, P1, T2, P2, T3, P3, T4, P4) \
    RET NAME(T0 P0, T1 P1, T2 P2, T3 P3, T4 P4); \
    HIVE_BLUEPRINT_FUNCTION_INTERNAL_REG(NAME, CATEGORY, RET, 5, \
        {{#P0, propolis::detail::BPTypeOf<T0>()}, \
         {#P1, propolis::detail::BPTypeOf<T1>()}, \
         {#P2, propolis::detail::BPTypeOf<T2>()}, \
         {#P3, propolis::detail::BPTypeOf<T3>()}, \
         {#P4, propolis::detail::BPTypeOf<T4>()}}) \
    RET NAME(T0 P0, T1 P1, T2 P2, T3 P3, T4 P4)
