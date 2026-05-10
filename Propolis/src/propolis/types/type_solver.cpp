#include <propolis/types/type_solver.h>

#include <cstdio>

namespace propolis
{
    PType TypeSolver::FreshVar() noexcept
    {
        return PType::Var(m_nextVar++);
    }

    PType TypeSolver::Resolve(PType type) const noexcept
    {
        while (type.m_kind == PTypeKind::TYPE_VAR)
        {
            const PType* found = m_substitutions.Find(type.m_param);
            if (!found)
                break;
            type = *found;
        }
        return type;
    }

    UnifyResult TypeSolver::Bind(uint32_t varId, PType type)
    {
        if (type.m_kind == PTypeKind::TYPE_VAR && type.m_param == varId)
            return UnifyResult::Ok(type);

        PType* existing = m_substitutions.Find(varId);
        if (existing)
        {
            auto result = Unify(*existing, type);
            if (result.m_ok)
                *existing = result.m_type;
            return result;
        }

        m_substitutions.Insert(varId, type);
        return UnifyResult::Ok(type);
    }

    PType TypeSolver::Promote(PType a, PType b) noexcept
    {
        if (a.m_kind == PTypeKind::INT32 && b.m_kind == PTypeKind::FLOAT32)
            return b;
        if (a.m_kind == PTypeKind::FLOAT32 && b.m_kind == PTypeKind::INT32)
            return a;

        if (a.m_kind == PTypeKind::INT32 && b.m_kind == PTypeKind::FLOAT64)
            return b;
        if (a.m_kind == PTypeKind::FLOAT64 && b.m_kind == PTypeKind::INT32)
            return a;

        if (a.m_kind == PTypeKind::UINT32 && b.m_kind == PTypeKind::INT32)
            return b;
        if (a.m_kind == PTypeKind::INT32 && b.m_kind == PTypeKind::UINT32)
            return a;

        if (a.m_kind == PTypeKind::FLOAT32 && b.m_kind == PTypeKind::FLOAT64)
            return b;
        if (a.m_kind == PTypeKind::FLOAT64 && b.m_kind == PTypeKind::FLOAT32)
            return a;

        if (a.m_kind == PTypeKind::UINT32 && b.m_kind == PTypeKind::FLOAT32)
            return b;
        if (a.m_kind == PTypeKind::FLOAT32 && b.m_kind == PTypeKind::UINT32)
            return a;

        if (a.m_kind == PTypeKind::UINT32 && b.m_kind == PTypeKind::FLOAT64)
            return b;
        if (a.m_kind == PTypeKind::FLOAT64 && b.m_kind == PTypeKind::UINT32)
            return a;

        return PType{};
    }

    UnifyResult TypeSolver::Unify(PType a, PType b)
    {
        PType origA = a;
        PType origB = b;
        a = Resolve(a);
        b = Resolve(b);

        if (a.m_kind == PTypeKind::TYPE_VAR)
            return Bind(a.m_param, b);
        if (b.m_kind == PTypeKind::TYPE_VAR)
            return Bind(b.m_param, a);

        if (a == b)
            return UnifyResult::Ok(a);

        PType promoted = Promote(a, b);
        if (promoted.m_kind != PTypeKind::TYPE_VAR)
        {
            if (origA.m_kind == PTypeKind::TYPE_VAR)
            {
                PType* sub = m_substitutions.Find(origA.m_param);
                if (sub)
                    *sub = promoted;
            }
            if (origB.m_kind == PTypeKind::TYPE_VAR)
            {
                PType* sub = m_substitutions.Find(origB.m_param);
                if (sub)
                    *sub = promoted;
            }
            return UnifyResult::Ok(promoted);
        }

        if (a.m_kind == PTypeKind::STREAM && b.m_kind == PTypeKind::STREAM)
        {
            auto inner = Unify(PType::Var(a.m_param), PType::Var(b.m_param));
            if (!inner.m_ok)
                return inner;
            return UnifyResult::Ok(PType::Stream(a.m_param));
        }

        if (a.m_kind == PTypeKind::OPTION && b.m_kind == PTypeKind::OPTION)
        {
            auto inner = Unify(PType::Var(a.m_param), PType::Var(b.m_param));
            if (!inner.m_ok)
                return inner;
            return UnifyResult::Ok(PType::Option(a.m_param));
        }

        char buf[128];
        std::snprintf(buf, sizeof(buf), "Cannot unify %s with %s",
                      PTypeKindName(a.m_kind), PTypeKindName(b.m_kind));
        return UnifyResult::Err(buf);
    }

    bool TypeSolver::SatisfiesConstraint(PType type, Constraint constraint) const noexcept
    {
        type = Resolve(type);

        if (type.m_kind == PTypeKind::TYPE_VAR)
            return true;

        switch (constraint)
        {
        case Constraint::NONE:
            return true;
        case Constraint::NUMERIC:
            return type.IsNumeric();
        case Constraint::ORDERED:
            return type.IsOrdered();
        case Constraint::EQUATABLE:
            return type.IsNumeric() || type.m_kind == PTypeKind::BOOL ||
                   type.m_kind == PTypeKind::ENTITY || type.m_kind == PTypeKind::ENUM;
        case Constraint::INTERPOLABLE:
            return type.IsInterpolable();
        case Constraint::COMPONENT:
            return type.m_kind == PTypeKind::COMPONENT;
        case Constraint::REFLECTED_ENUM:
            return type.m_kind == PTypeKind::ENUM;
        }
        return false;
    }

    void TypeSolver::Reset() noexcept
    {
        m_substitutions.Clear();
        m_nextVar = 1;
    }
} // namespace propolis
