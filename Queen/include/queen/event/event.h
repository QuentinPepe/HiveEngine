#pragma once

#include <queen/core/type_id.h>

#include <cstdint>
#include <type_traits>

namespace queen
{
    // Constrains event payloads to trivially copyable/destructible non-empty types so the
    // queue can rely on memcpy semantics and skip destructor calls on Clear/Swap.
    template <typename T>
    concept Event = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> && !std::is_empty_v<T>;

    // Strongly-typed TypeId wrapper used for event-queue lookup; the distinct type prevents
    // accidentally mixing event identifiers with arbitrary TypeIds at call sites.
    class EventId
    {
    public:
        constexpr EventId() noexcept
            : m_value{0}
        {
        }
        constexpr explicit EventId(TypeId id) noexcept
            : m_value{id}
        {
        }

        [[nodiscard]] constexpr TypeId Value() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] constexpr bool operator==(const EventId& other) const noexcept
        {
            return m_value == other.m_value;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_value != 0;
        }

    private:
        TypeId m_value;
    };

    template <Event T> [[nodiscard]] constexpr EventId EventIdOf() noexcept
    {
        return EventId{TypeIdOf<T>()};
    }

    // Erased size/alignment metadata so the event registry can manage queue memory without
    // instantiating per-type machinery.
    struct EventMeta
    {
        EventId m_id;
        size_t m_size;
        size_t m_alignment;

        template <Event T> [[nodiscard]] static constexpr EventMeta Of() noexcept
        {
            return EventMeta{EventIdOf<T>(), sizeof(T), alignof(T)};
        }
    };
} // namespace queen
