#pragma once

#include <queen/core/type_id.h>

namespace queen
{
    // Read-only resource handle used as a system parameter. The wrapped type's
    // TypeId is exposed at compile time so the builder can register the access.
    template <typename T> class Res
    {
    public:
        using ValueType = T;
        static constexpr TypeId typeId = TypeIdOf<T>();
        static constexpr bool isMutable = false;

        explicit constexpr Res(const T* ptr) noexcept
            : m_ptr{ptr}
        {
        }

        [[nodiscard]] constexpr const T& operator*() const noexcept
        {
            return *m_ptr;
        }

        [[nodiscard]] constexpr const T* operator->() const noexcept
        {
            return m_ptr;
        }

        [[nodiscard]] constexpr const T* Get() const noexcept
        {
            return m_ptr;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_ptr != nullptr;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return m_ptr != nullptr;
        }

    private:
        const T* m_ptr;
    };

    // Mutable counterpart to Res<T>. The scheduler treats this as a write, so it
    // conflicts with any other Res<T>/ResMut<T> on the same resource.
    template <typename T> class ResMut
    {
    public:
        using ValueType = T;
        static constexpr TypeId typeId = TypeIdOf<T>();
        static constexpr bool isMutable = true;

        explicit constexpr ResMut(T* ptr) noexcept
            : m_ptr{ptr}
        {
        }

        [[nodiscard]] constexpr T& operator*() const noexcept
        {
            return *m_ptr;
        }

        [[nodiscard]] constexpr T* operator->() const noexcept
        {
            return m_ptr;
        }

        [[nodiscard]] constexpr T* Get() const noexcept
        {
            return m_ptr;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_ptr != nullptr;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return m_ptr != nullptr;
        }

    private:
        T* m_ptr;
    };

    // Type traits for detecting Res/ResMut
    template <typename T> struct IsRes : std::false_type
    {
    };

    template <typename T> struct IsRes<Res<T>> : std::true_type
    {
    };

    template <typename T> inline constexpr bool isResV = IsRes<T>::value;

    template <typename T> struct IsResMut : std::false_type
    {
    };

    template <typename T> struct IsResMut<ResMut<T>> : std::true_type
    {
    };

    template <typename T> inline constexpr bool isResMutV = IsResMut<T>::value;

    template <typename T> inline constexpr bool isResourceParam = isResV<T> || isResMutV<T>;
} // namespace queen
