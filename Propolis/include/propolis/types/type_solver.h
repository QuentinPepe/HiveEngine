#pragma once

#include <propolis/types/ptype.h>

#include <hive/hive_config.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/string.h>

namespace propolis
{
    enum class Constraint : uint8_t
    {
        NONE,
        NUMERIC,
        ORDERED,
        EQUATABLE,
        INTERPOLABLE,
        COMPONENT,
        REFLECTED_ENUM,
    };

    struct UnifyResult
    {
        PType m_type;
        bool m_ok{false};
        wax::String m_error;

        [[nodiscard]] static UnifyResult Ok(PType t) { return {t, true, {}}; }
        [[nodiscard]] static UnifyResult Err(const char* msg) { return {{}, false, wax::String{msg}}; }
    };

    class HIVE_API TypeSolver
    {
    public:
        [[nodiscard]] PType FreshVar() noexcept;

        [[nodiscard]] PType Resolve(PType type) const noexcept;

        [[nodiscard]] UnifyResult Unify(PType a, PType b);

        [[nodiscard]] bool SatisfiesConstraint(PType type, Constraint constraint) const noexcept;

        void Reset() noexcept;

    private:
        UnifyResult Bind(uint32_t varId, PType type);
        [[nodiscard]] static PType Promote(PType a, PType b) noexcept;

        uint32_t m_nextVar{1};
        wax::HashMap<uint32_t, PType> m_substitutions;
    };
} // namespace propolis
