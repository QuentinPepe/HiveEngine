#pragma once

#include <hive/hive_config.h>

#include <cstdint>

namespace propolis
{
    enum class PTypeKind : uint8_t
    {
        BOOL,
        INT32,
        UINT32,
        FLOAT32,
        FLOAT64,
        ENTITY,
        VEC2,
        VEC3,
        VEC4,
        QUAT,
        MAT4,
        SIGNAL,
        STREAM,
        OPTION,
        COMPONENT,
        ENUM,
        TYPE_VAR,
        STRUCT,
    };

    struct PType
    {
        PTypeKind m_kind{PTypeKind::TYPE_VAR};
        uint32_t m_param{0};

        [[nodiscard]] bool operator==(const PType& other) const noexcept
        {
            return m_kind == other.m_kind && m_param == other.m_param;
        }
        [[nodiscard]] bool operator!=(const PType& other) const noexcept { return !(*this == other); }

        [[nodiscard]] static PType Bool() noexcept    { return {PTypeKind::BOOL, 0}; }
        [[nodiscard]] static PType Int32() noexcept   { return {PTypeKind::INT32, 0}; }
        [[nodiscard]] static PType UInt32() noexcept  { return {PTypeKind::UINT32, 0}; }
        [[nodiscard]] static PType Float32() noexcept { return {PTypeKind::FLOAT32, 0}; }
        [[nodiscard]] static PType Float64() noexcept { return {PTypeKind::FLOAT64, 0}; }
        [[nodiscard]] static PType Entity() noexcept  { return {PTypeKind::ENTITY, 0}; }
        [[nodiscard]] static PType Vec2() noexcept    { return {PTypeKind::VEC2, 0}; }
        [[nodiscard]] static PType Vec3() noexcept    { return {PTypeKind::VEC3, 0}; }
        [[nodiscard]] static PType Vec4() noexcept    { return {PTypeKind::VEC4, 0}; }
        [[nodiscard]] static PType Quat() noexcept    { return {PTypeKind::QUAT, 0}; }
        [[nodiscard]] static PType Mat4() noexcept    { return {PTypeKind::MAT4, 0}; }
        [[nodiscard]] static PType Signal() noexcept  { return {PTypeKind::SIGNAL, 0}; }

        [[nodiscard]] static PType Stream(uint32_t elementTypeVar) noexcept { return {PTypeKind::STREAM, elementTypeVar}; }
        [[nodiscard]] static PType Option(uint32_t innerTypeVar) noexcept   { return {PTypeKind::OPTION, innerTypeVar}; }
        [[nodiscard]] static PType Component(uint32_t typeId) noexcept      { return {PTypeKind::COMPONENT, typeId}; }
        [[nodiscard]] static PType Enum(uint32_t typeId) noexcept           { return {PTypeKind::ENUM, typeId}; }
        [[nodiscard]] static PType Var(uint32_t varId) noexcept             { return {PTypeKind::TYPE_VAR, varId}; }
        [[nodiscard]] static PType Struct(uint32_t typeId) noexcept         { return {PTypeKind::STRUCT, typeId}; }

        [[nodiscard]] bool IsResolved() const noexcept { return m_kind != PTypeKind::TYPE_VAR; }
        [[nodiscard]] HIVE_API bool IsNumeric() const noexcept;
        [[nodiscard]] HIVE_API bool IsOrdered() const noexcept;
        [[nodiscard]] HIVE_API bool IsInterpolable() const noexcept;
    };

    [[nodiscard]] HIVE_API const char* PTypeKindName(PTypeKind kind) noexcept;

} // namespace propolis
