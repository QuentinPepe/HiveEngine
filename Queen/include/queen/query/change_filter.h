#pragma once

#include <queen/core/tick.h>
#include <queen/core/type_id.h>
#include <queen/query/query_term.h>

namespace queen
{
    enum class ChangeFilterMode : uint8_t
    {
        ADDED,           // Only entities where component was added since last_run
        CHANGED,         // Only entities where component was modified since last_run
        ADDED_OR_CHANGED // Either added or changed
    };

    // Runtime change-detection filter applied per-row during iteration rather than during
    // archetype matching, so it can react to value changes without splitting archetypes.
    struct ChangeFilterTerm
    {
        TypeId m_typeId = 0;
        ChangeFilterMode m_mode = ChangeFilterMode::CHANGED;
        TermAccess m_access = TermAccess::READ;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_typeId != 0;
        }

        [[nodiscard]] constexpr bool Matches(ComponentTicks ticks, Tick lastRun) const noexcept
        {
            switch (m_mode)
            {
                case ChangeFilterMode::ADDED:
                    return ticks.WasAdded(lastRun);
                case ChangeFilterMode::CHANGED:
                    return ticks.WasChanged(lastRun);
                case ChangeFilterMode::ADDED_OR_CHANGED:
                    return ticks.WasAddedOrChanged(lastRun);
            }
            return false;
        }

        template <typename T>
        [[nodiscard]] static constexpr ChangeFilterTerm Create(ChangeFilterMode mode,
                                                               TermAccess access = TermAccess::READ) noexcept
        {
            return ChangeFilterTerm{TypeIdOf<T>(), mode, access};
        }
    };

    // Change Filter Wrappers (Compile-Time DSL)

    // Matches rows where T was added since the system's last run tick.
    template <typename T> struct Added
    {
        using ComponentType = T;
        static constexpr ChangeFilterMode mode = ChangeFilterMode::ADDED;
        static constexpr TermAccess access = TermAccess::READ;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr ChangeFilterTerm ToChangeFilter() noexcept
        {
            return ChangeFilterTerm{typeId, mode, access};
        }

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, TermOperator::WITH, access};
        }
    };

    // Matches rows where T was modified since the system's last run tick; relies on Mut<T>
    // or explicit MarkChanged to populate the change tick.
    template <typename T> struct Changed
    {
        using ComponentType = T;
        static constexpr ChangeFilterMode mode = ChangeFilterMode::CHANGED;
        static constexpr TermAccess access = TermAccess::READ;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr ChangeFilterTerm ToChangeFilter() noexcept
        {
            return ChangeFilterTerm{typeId, mode, access};
        }

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, TermOperator::WITH, access};
        }
    };

    // Union of Added<T> and Changed<T>; matches when T is either newly inserted or mutated.
    template <typename T> struct AddedOrChanged
    {
        using ComponentType = T;
        static constexpr ChangeFilterMode mode = ChangeFilterMode::ADDED_OR_CHANGED;
        static constexpr TermAccess access = TermAccess::READ;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr ChangeFilterTerm ToChangeFilter() noexcept
        {
            return ChangeFilterTerm{typeId, mode, access};
        }

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, TermOperator::WITH, access};
        }
    };

    // Change Filter Type Traits

    namespace detail
    {
        template <typename T> struct IsChangeFilter : std::false_type
        {
        };

        template <typename T> struct IsChangeFilter<Added<T>> : std::true_type
        {
        };

        template <typename T> struct IsChangeFilter<Changed<T>> : std::true_type
        {
        };

        template <typename T> struct IsChangeFilter<AddedOrChanged<T>> : std::true_type
        {
        };

        template <typename T> constexpr bool isChangeFilterV = IsChangeFilter<T>::value;
    } // namespace detail
} // namespace queen
