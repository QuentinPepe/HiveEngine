// Placement new/delete helpers that respect a comb::Allocator. Replace raw
// new/delete so allocation routing stays explicit and alignment is correct.

#pragma once

#include <comb/allocator_concepts.h>

#include <new>         // For placement new
#include <type_traits> // For is_trivially_destructible_v
#include <utility>     // For std::forward

namespace comb
{

    template <typename T, Allocator Alloc, typename... Args> [[nodiscard]] T* New(Alloc& allocator, Args&&... args)
    {
        void* memory = allocator.Allocate(sizeof(T), alignof(T));

        if (memory == nullptr)
        {
            return nullptr;
        }

        return new (memory) T(std::forward<Args>(args)...);
    }

    // Allocator must match the one passed to New. nullptr is a no-op.
    template <typename T, Allocator Alloc> void Delete(Alloc& allocator, T* ptr)
    {
        if (ptr == nullptr)
        {
            return;
        }

        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            ptr->~T();
        }

        allocator.Deallocate(ptr);
    }

    template <typename T, Allocator Alloc> [[nodiscard]] T* NewArray(Alloc& allocator, size_t count)
    {
        if (count == 0)
        {
            return nullptr;
        }

        void* memory = allocator.Allocate(sizeof(T) * count, alignof(T));

        if (memory == nullptr)
        {
            return nullptr;
        }

        T* array = static_cast<T*>(memory);
        for (size_t i = 0; i < count; ++i)
        {
            new (&array[i]) T();
        }

        return array;
    }

    // count must match the value passed to NewArray. nullptr is a no-op.
    template <typename T, Allocator Alloc> void DeleteArray(Alloc& allocator, T* ptr, size_t count)
    {
        if (ptr == nullptr)
        {
            return;
        }

        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            for (size_t i = count; i > 0; --i)
            {
                ptr[i - 1].~T();
            }
        }

        allocator.Deallocate(ptr);
    }

} // namespace comb
