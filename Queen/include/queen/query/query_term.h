#pragma once

#include <queen/core/type_id.h>

#include <cstdint>

namespace queen
{
    // Match mode for a query term: required presence, exclusion, or optional access.
    enum class TermOperator : uint8_t
    {
        WITH,    // Must have component (default)
        WITHOUT, // Must not have component
        Optional // May have component (access via pointer, nullptr if absent)
    };

    // Access mode for a query term; drives parallel-scheduling dependency analysis.
    enum class TermAccess : uint8_t
    {
        READ,  // Immutable access (const T&)
        WRITE, // Mutable access (T&)
        NONE   // No data access, just filter (With<Tag>, Without<Tag>)
    };

    // Runtime representation of one component condition in a query.
    // Type-erased counterpart of the compile-time Read/Write/With/Without wrappers.
    struct Term
    {
        TypeId m_typeId = 0;
        TermOperator m_op = TermOperator::WITH;
        TermAccess m_access = TermAccess::READ;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_typeId != 0;
        }

        [[nodiscard]] constexpr bool IsRequired() const noexcept
        {
            return m_op == TermOperator::WITH;
        }

        [[nodiscard]] constexpr bool IsExcluded() const noexcept
        {
            return m_op == TermOperator::WITHOUT;
        }

        [[nodiscard]] constexpr bool IsOptional() const noexcept
        {
            return m_op == TermOperator::Optional;
        }

        [[nodiscard]] constexpr bool IsReadOnly() const noexcept
        {
            return m_access == TermAccess::READ;
        }

        [[nodiscard]] constexpr bool IsWritable() const noexcept
        {
            return m_access == TermAccess::WRITE;
        }

        [[nodiscard]] constexpr bool HasDataAccess() const noexcept
        {
            return m_access != TermAccess::NONE;
        }

        template <typename T>
        [[nodiscard]] static constexpr Term Create(TermOperator op = TermOperator::WITH,
                                                   TermAccess access = TermAccess::READ) noexcept
        {
            return Term{TypeIdOf<T>(), op, access};
        }
    };

    // Query Term Wrappers (Compile-Time DSL)

    // Require component T with read-only access; contributes a read dependency to the scheduler.
    template <typename T> struct Read
    {
        using ComponentType = T;
        static constexpr TermOperator op = TermOperator::WITH;
        static constexpr TermAccess access = TermAccess::READ;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, op, access};
        }
    };

    // Require component T with mutable access; emits a write dependency for parallel scheduling.
    template <typename T> struct Write
    {
        using ComponentType = T;
        static constexpr TermOperator op = TermOperator::WITH;
        static constexpr TermAccess access = TermAccess::WRITE;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, op, access};
        }
    };

    // Presence filter: entity must have T but its data is not bound into the callback.
    // Use for tag components or pure existence checks to avoid spurious access dependencies.
    template <typename T> struct With
    {
        using ComponentType = T;
        static constexpr TermOperator op = TermOperator::WITH;
        static constexpr TermAccess access = TermAccess::NONE;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, op, access};
        }
    };

    // Exclusion filter: archetype must not contain T.
    template <typename T> struct Without
    {
        using ComponentType = T;
        static constexpr TermOperator op = TermOperator::WITHOUT;
        static constexpr TermAccess access = TermAccess::NONE;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, op, access};
        }
    };

    // Optional read access: callback receives a pointer that is null when the component is absent.
    // Lets a single query span archetypes that differ only by this component.
    template <typename T> struct Maybe
    {
        using ComponentType = T;
        static constexpr TermOperator op = TermOperator::Optional;
        static constexpr TermAccess access = TermAccess::READ;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, op, access};
        }
    };

    // Optional mutable access; same semantics as Maybe but contributes a write dependency.
    template <typename T> struct MaybeWrite
    {
        using ComponentType = T;
        static constexpr TermOperator op = TermOperator::Optional;
        static constexpr TermAccess access = TermAccess::WRITE;
        static constexpr TypeId typeId = TypeIdOf<T>();

        [[nodiscard]] static constexpr Term ToTerm() noexcept
        {
            return Term{typeId, op, access};
        }
    };

    // Term Type Traits

    namespace detail
    {
        template <typename T> struct IsQueryTerm : std::false_type
        {
        };

        template <typename T> struct IsQueryTerm<Read<T>> : std::true_type
        {
        };

        template <typename T> struct IsQueryTerm<Write<T>> : std::true_type
        {
        };

        template <typename T> struct IsQueryTerm<With<T>> : std::true_type
        {
        };

        template <typename T> struct IsQueryTerm<Without<T>> : std::true_type
        {
        };

        template <typename T> struct IsQueryTerm<Maybe<T>> : std::true_type
        {
        };

        template <typename T> struct IsQueryTerm<MaybeWrite<T>> : std::true_type
        {
        };

        template <typename T> constexpr bool isQueryTermV = IsQueryTerm<T>::value;

        template <typename T> struct HasDataAccess : std::bool_constant<T::access != TermAccess::NONE>
        {
        };

        template <typename T> constexpr bool hasDataAccessV = HasDataAccess<T>::value;

        template <typename T> struct IsOptionalTerm : std::bool_constant<T::op == TermOperator::Optional>
        {
        };

        template <typename T> constexpr bool isOptionalTermV = IsOptionalTerm<T>::value;
    } // namespace detail
} // namespace queen
