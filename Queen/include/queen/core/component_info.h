#pragma once

#include <queen/core/type_id.h>

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace queen
{
    // Per-component hint selecting archetype/Table storage (cache-friendly iteration) vs
    // SparseSet storage (cheaper add/remove for volatile components).
    enum class StorageType : uint8_t
    {
        DENSE,
        SPARSE
    };

    namespace detail
    {
        template <typename T, typename = void> struct HasStorageType : std::false_type
        {
        };

        template <typename T> struct HasStorageType<T, std::void_t<decltype(T::storage)>> : std::true_type
        {
        };

        template <typename T> constexpr StorageType DeduceStorage() noexcept
        {
            if constexpr (HasStorageType<T>::value)
            {
                return T::storage;
            }
            else
            {
                return StorageType::DENSE;
            }
        }
    } // namespace detail

    // Compile-time component metadata and lifecycle thunks. Source of truth used to build
    // type-erased ComponentMeta for Column/Table storage.
    template <typename T> struct ComponentInfo
    {
        static constexpr TypeId id = TypeIdOf<T>();
        static constexpr size_t size = sizeof(T);
        static constexpr size_t alignment = alignof(T);
        static constexpr bool isTriviallyCopyable = std::is_trivially_copyable_v<T>;
        static constexpr bool isTriviallyDestructible = std::is_trivially_destructible_v<T>;
        inline static const StorageType storage = detail::DeduceStorage<T>();

        static void Construct(void* ptr)
        {
            new (ptr) T{};
        }

        static void Destruct(void* ptr)
        {
            static_cast<T*>(ptr)->~T();
        }

        static void Move(void* dst, void* src)
        {
            new (dst) T{std::move(*static_cast<T*>(src))};
        }

        static void Copy(void* dst, const void* src)
        {
            new (dst) T{*static_cast<const T*>(src)};
        }
    };

    // Type-erased runtime mirror of ComponentInfo<T> for use in non-template containers.
    // Lifecycle thunks may be null when the corresponding op is trivial.
    // Memory layout:
    // ┌────────────────────────────────────────────────────────────┐
    // │ type_id: TypeId (8 bytes)                                  │
    // │ size: size_t (8 bytes)                                     │
    // │ alignment: size_t (8 bytes)                                │
    // │ storage: StorageType (1 byte) + padding (7 bytes)          │
    // │ construct/destruct/move/copy: function pointers (8 bytes)  │
    // └────────────────────────────────────────────────────────────┘
    struct ComponentMeta
    {
        using ConstructFn = void (*)(void*);
        using DestructFn = void (*)(void*);
        using MoveFn = void (*)(void* dst, void* src);
        using CopyFn = void (*)(void* dst, const void* src);

        TypeId m_typeId = 0;
        size_t m_size = 0;
        size_t m_alignment = 0;
        StorageType m_storage = StorageType::DENSE;
        ConstructFn m_construct = nullptr;
        DestructFn m_destruct = nullptr;
        MoveFn m_move = nullptr;
        CopyFn m_copy = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_typeId != 0 && m_size > 0;
        }

        [[nodiscard]] constexpr bool IsTrivial() const noexcept
        {
            return m_destruct == nullptr;
        }

        template <typename T> [[nodiscard]] static ComponentMeta Of() noexcept
        {
            using Info = ComponentInfo<T>;

            ComponentMeta meta{};
            meta.m_typeId = Info::id;
            meta.m_size = Info::size;
            meta.m_alignment = Info::alignment;
            meta.m_storage = Info::storage;

            if constexpr (std::is_default_constructible_v<T>)
            {
                meta.m_construct = &Info::Construct;
            }

            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                meta.m_destruct = &Info::Destruct;
            }

            if constexpr (std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>)
            {
                meta.m_move = &Info::Move;
            }

            if constexpr (std::is_copy_constructible_v<T>)
            {
                meta.m_copy = &Info::Copy;
            }

            return meta;
        }

        template <typename T> [[nodiscard]] static ComponentMeta OfTag() noexcept
        {
            ComponentMeta meta{};
            meta.m_typeId = TypeIdOf<T>();
            meta.m_size = 0;
            meta.m_alignment = 1;
            return meta;
        }
    };
} // namespace queen
