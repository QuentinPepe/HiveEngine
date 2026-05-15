#pragma once

#include <comb/allocator_concepts.h>

#include <wax/containers/vector.h>

#include <queen/core/type_id.h>

namespace queen
{
    enum class WorldAccess : uint8_t
    {
        NONE,      // No direct world access
        READ,      // Read-only world access
        WRITE,     // Read-write world access
        EXCLUSIVE, // Exclusive access (blocks all other systems)
    };

    // Read/write access manifest for a system. The scheduler consults this to
    // decide whether two systems can run concurrently without data races.
    template <comb::Allocator Allocator> class AccessDescriptor
    {
    public:
        explicit AccessDescriptor(Allocator& allocator)
            : m_componentReads{allocator}
            , m_componentWrites{allocator}
            , m_resourceReads{allocator}
            , m_resourceWrites{allocator}
            , m_worldAccess{WorldAccess::NONE}
        {
        }

        template <typename T> void AddComponentRead()
        {
            AddUnique(m_componentReads, TypeIdOf<T>());
        }

        template <typename T> void AddComponentWrite()
        {
            AddUnique(m_componentWrites, TypeIdOf<T>());
        }

        template <typename T> void AddResourceRead()
        {
            AddUnique(m_resourceReads, TypeIdOf<T>());
        }

        template <typename T> void AddResourceWrite()
        {
            AddUnique(m_resourceWrites, TypeIdOf<T>());
        }

        void AddComponentRead(TypeId typeId)
        {
            AddUnique(m_componentReads, typeId);
        }

        void AddComponentWrite(TypeId typeId)
        {
            AddUnique(m_componentWrites, typeId);
        }

        void AddResourceRead(TypeId typeId)
        {
            AddUnique(m_resourceReads, typeId);
        }

        void AddResourceWrite(TypeId typeId)
        {
            AddUnique(m_resourceWrites, typeId);
        }

        void SetWorldAccess(WorldAccess access) noexcept
        {
            m_worldAccess = access;
        }

        [[nodiscard]] WorldAccess GetWorldAccess() const noexcept
        {
            return m_worldAccess;
        }

        [[nodiscard]] const wax::Vector<TypeId>& ComponentReads() const noexcept
        {
            return m_componentReads;
        }

        [[nodiscard]] const wax::Vector<TypeId>& ComponentWrites() const noexcept
        {
            return m_componentWrites;
        }

        [[nodiscard]] const wax::Vector<TypeId>& ResourceReads() const noexcept
        {
            return m_resourceReads;
        }

        [[nodiscard]] const wax::Vector<TypeId>& ResourceWrites() const noexcept
        {
            return m_resourceWrites;
        }

        // Conflict rules: exclusive world access blocks everything; otherwise any
        // write that overlaps the other side's reads or writes is a conflict.
        [[nodiscard]] bool ConflictsWith(const AccessDescriptor& other) const noexcept
        {
            if (m_worldAccess == WorldAccess::EXCLUSIVE || other.m_worldAccess == WorldAccess::EXCLUSIVE)
            {
                return true;
            }

            if (HasOverlap(m_componentWrites, other.m_componentReads) ||
                HasOverlap(m_componentWrites, other.m_componentWrites) ||
                HasOverlap(m_componentReads, other.m_componentWrites))
            {
                return true;
            }

            if (HasOverlap(m_resourceWrites, other.m_resourceReads) ||
                HasOverlap(m_resourceWrites, other.m_resourceWrites) ||
                HasOverlap(m_resourceReads, other.m_resourceWrites))
            {
                return true;
            }

            return false;
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_componentReads.IsEmpty() && m_componentWrites.IsEmpty() && m_resourceReads.IsEmpty() &&
                   m_resourceWrites.IsEmpty() && m_worldAccess == WorldAccess::NONE;
        }

        [[nodiscard]] bool IsPure() const noexcept
        {
            return IsEmpty();
        }

        [[nodiscard]] bool IsExclusive() const noexcept
        {
            return m_worldAccess == WorldAccess::EXCLUSIVE;
        }

    private:
        static void AddUnique(wax::Vector<TypeId>& vec, TypeId typeId)
        {
            for (size_t i = 0; i < vec.Size(); ++i)
            {
                if (vec[i] == typeId)
                {
                    return;
                }
            }
            vec.PushBack(typeId);
        }

        static bool HasOverlap(const wax::Vector<TypeId>& a, const wax::Vector<TypeId>& b) noexcept
        {
            for (size_t i = 0; i < a.Size(); ++i)
            {
                for (size_t j = 0; j < b.Size(); ++j)
                {
                    if (a[i] == b[j])
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        wax::Vector<TypeId> m_componentReads;
        wax::Vector<TypeId> m_componentWrites;
        wax::Vector<TypeId> m_resourceReads;
        wax::Vector<TypeId> m_resourceWrites;
        WorldAccess m_worldAccess;
    };
} // namespace queen
