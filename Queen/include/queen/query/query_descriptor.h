#pragma once

#include <comb/allocator_concepts.h>

#include <wax/containers/vector.h>

#include <queen/core/type_id.h>
#include <queen/query/query_term.h>
#include <queen/storage/archetype.h>
#include <queen/storage/component_index.h>

namespace queen
{
    // Runtime descriptor that partitions a query's Terms into required/excluded/optional
    // buckets so archetype matching is a pair of linear scans rather than a re-walk of the
    // original term list. Immutable after Finalize(); not thread-safe.
    //
    // Bucket layout populated by Finalize():
    //   terms_        all query terms (input)
    //   required_     With terms     (archetype must have)
    //   excluded_     Without terms  (archetype must not have)
    //   optional_     Maybe terms    (fetched if present)
    //   data_access_  terms with Read/Write access (for callback binding)
    template <comb::Allocator Allocator> class QueryDescriptor
    {
    public:
        explicit QueryDescriptor(Allocator& allocator)
            : m_allocator{&allocator}
            , m_terms{allocator}
            , m_required{allocator}
            , m_excluded{allocator}
            , m_optional{allocator}
            , m_dataAccess{allocator}
        {
        }

        ~QueryDescriptor() = default;

        QueryDescriptor(const QueryDescriptor&) = delete;
        QueryDescriptor& operator=(const QueryDescriptor&) = delete;
        QueryDescriptor(QueryDescriptor&&) = default;
        QueryDescriptor& operator=(QueryDescriptor&&) = default;

        void AddTerm(const Term& term)
        {
            m_terms.PushBack(term);
        }

        template <typename TermWrapper> void AddTerm()
        {
            AddTerm(TermWrapper::ToTerm());
        }

        void Finalize()
        {
            m_required.Clear();
            m_excluded.Clear();
            m_optional.Clear();
            m_dataAccess.Clear();

            for (size_t i = 0; i < m_terms.Size(); ++i)
            {
                const Term& term = m_terms[i];

                switch (term.m_op)
                {
                    case TermOperator::WITH:
                        m_required.PushBack(term.m_typeId);
                        break;
                    case TermOperator::WITHOUT:
                        m_excluded.PushBack(term.m_typeId);
                        break;
                    case TermOperator::Optional:
                        m_optional.PushBack(term.m_typeId);
                        break;
                }

                if (term.HasDataAccess())
                {
                    m_dataAccess.PushBack(term);
                }
            }
        }

        template <typename... TermWrappers> static QueryDescriptor FromTerms(Allocator& allocator)
        {
            QueryDescriptor desc{allocator};
            (desc.template AddTerm<TermWrappers>(), ...);
            desc.Finalize();
            return desc;
        }

        [[nodiscard]] bool MatchesArchetype(const Archetype<Allocator>& archetype) const noexcept
        {
            for (size_t i = 0; i < m_required.Size(); ++i)
            {
                if (!archetype.HasComponent(m_required[i]))
                {
                    return false;
                }
            }

            for (size_t i = 0; i < m_excluded.Size(); ++i)
            {
                if (archetype.HasComponent(m_excluded[i]))
                {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] wax::Vector<Archetype<Allocator>*>
        FindMatchingArchetypes(const ComponentIndex<Allocator>& index) const
        {
            wax::Vector<Archetype<Allocator>*> result{*m_allocator};

            if (m_required.IsEmpty())
            {
                return result;
            }

            size_t smallestIdx = 0;
            size_t smallestSize = SIZE_MAX;

            for (size_t i = 0; i < m_required.Size(); ++i)
            {
                const auto* list = index.GetArchetypesWith(m_required[i]);
                if (list == nullptr)
                {
                    return result;
                }
                if (list->Size() < smallestSize)
                {
                    smallestSize = list->Size();
                    smallestIdx = i;
                }
            }

            const auto* candidates = index.GetArchetypesWith(m_required[smallestIdx]);
            if (candidates == nullptr)
            {
                return result;
            }

            for (size_t i = 0; i < candidates->Size(); ++i)
            {
                Archetype<Allocator>* arch = (*candidates)[i];
                if (MatchesArchetype(*arch))
                {
                    result.PushBack(arch);
                }
            }

            return result;
        }

        [[nodiscard]] const wax::Vector<Term>& GetTerms() const noexcept
        {
            return m_terms;
        }

        [[nodiscard]] const wax::Vector<TypeId>& GetRequired() const noexcept
        {
            return m_required;
        }

        [[nodiscard]] const wax::Vector<TypeId>& GetExcluded() const noexcept
        {
            return m_excluded;
        }

        [[nodiscard]] const wax::Vector<TypeId>& GetOptional() const noexcept
        {
            return m_optional;
        }

        [[nodiscard]] const wax::Vector<Term>& GetDataAccessTerms() const noexcept
        {
            return m_dataAccess;
        }

        [[nodiscard]] size_t TermCount() const noexcept
        {
            return m_terms.Size();
        }
        [[nodiscard]] size_t RequiredCount() const noexcept
        {
            return m_required.Size();
        }
        [[nodiscard]] size_t ExcludedCount() const noexcept
        {
            return m_excluded.Size();
        }
        [[nodiscard]] size_t OptionalCount() const noexcept
        {
            return m_optional.Size();
        }
        [[nodiscard]] size_t DataAccessCount() const noexcept
        {
            return m_dataAccess.Size();
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_terms.IsEmpty();
        }
        [[nodiscard]] bool HasRequired() const noexcept
        {
            return !m_required.IsEmpty();
        }
        [[nodiscard]] bool HasExcluded() const noexcept
        {
            return !m_excluded.IsEmpty();
        }
        [[nodiscard]] bool HasOptional() const noexcept
        {
            return !m_optional.IsEmpty();
        }

    private:
        Allocator* m_allocator;
        wax::Vector<Term> m_terms;
        wax::Vector<TypeId> m_required;
        wax::Vector<TypeId> m_excluded;
        wax::Vector<TypeId> m_optional;
        wax::Vector<Term> m_dataAccess;
    };
} // namespace queen
