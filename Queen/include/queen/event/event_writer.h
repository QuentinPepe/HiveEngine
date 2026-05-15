#pragma once

#include <comb/allocator_concepts.h>

#include <queen/event/event.h>
#include <queen/event/event_queue.h>

namespace queen
{
    // Write-only view over an EventQueue. Exposes Send/Emplace without surfacing the queue's
    // read or buffer-management APIs, so systems can declare their intent in the signature.
    // Not thread-safe; serialise writes externally or use one writer per worker.
    template <Event T, comb::Allocator Allocator> class EventWriter
    {
    public:
        using EventType = T;

        explicit EventWriter(EventQueue<T, Allocator>& queue) noexcept
            : m_queue{&queue}
        {
        }

        void Send(const T& event)
        {
            m_queue->Push(event);
        }

        void Send(T&& event)
        {
            m_queue->Push(std::move(event));
        }

        template <typename... Args> T& Emplace(Args&&... args)
        {
            return m_queue->Emplace(std::forward<Args>(args)...);
        }

        template <typename InputIt> void SendBatch(InputIt first, InputIt last)
        {
            for (; first != last; ++first)
            {
                m_queue->Push(*first);
            }
        }

        [[nodiscard]] size_t Count() const noexcept
        {
            return m_queue->CurrentCount();
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_queue->IsCurrentEmpty();
        }

    private:
        EventQueue<T, Allocator>* m_queue;
    };
} // namespace queen
