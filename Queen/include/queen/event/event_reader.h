#pragma once

#include <comb/allocator_concepts.h>

#include <queen/event/event.h>
#include <queen/event/event_queue.h>

namespace queen
{
    // Per-reader cursor over an EventQueue's double buffer. Multiple readers can share the
    // same queue without interfering because each one tracks its own consumed position. The
    // cursor must be advanced explicitly via Read()/MarkRead() to avoid re-processing.
    template <Event T, comb::Allocator Allocator> class EventReader
    {
    public:
        using EventType = T;
        using Iterator = typename EventQueue<T, Allocator>::EventIterator;

        explicit EventReader(EventQueue<T, Allocator>& queue) noexcept
            : m_queue{&queue}
            , m_cursor{0}
        {
        }

        [[nodiscard]] Iterator Begin() const
        {
            return Iterator{m_queue, m_cursor};
        }

        [[nodiscard]] Iterator End() const
        {
            return Iterator{m_queue, m_queue->TotalCount()};
        }

        // Preferred entry point: visits unread events and advances the cursor so the same
        // events are not delivered twice on the next call.
        template <typename Func> void Read(Func&& func)
        {
            size_t total = m_queue->TotalCount();
            size_t prevSize = m_queue->PreviousCount();

            for (size_t i = m_cursor; i < total; ++i)
            {
                if (i < prevSize)
                {
                    func(m_queue->PreviousBuffer()[i]);
                }
                else
                {
                    func(m_queue->CurrentBuffer()[i - prevSize]);
                }
            }

            m_cursor = total;
        }

        [[nodiscard]] size_t Count() const noexcept
        {
            size_t total = m_queue->TotalCount();
            return total > m_cursor ? total - m_cursor : 0;
        }

        [[nodiscard]] size_t TotalCount() const noexcept
        {
            return m_queue->TotalCount();
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_cursor >= m_queue->TotalCount();
        }

        // Skips remaining events without invoking the callback; useful to drain a queue.
        void MarkRead() noexcept
        {
            m_cursor = m_queue->TotalCount();
        }

        // Rewinds the cursor so the next Read() replays both buffers from the start.
        void Reset() noexcept
        {
            m_cursor = 0;
        }

        void Clear() noexcept
        {
            MarkRead();
        }

    private:
        EventQueue<T, Allocator>* m_queue;
        size_t m_cursor;
    };
} // namespace queen
