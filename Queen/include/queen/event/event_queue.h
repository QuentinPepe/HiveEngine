#pragma once

#include <comb/allocator_concepts.h>

#include <wax/containers/vector.h>

#include <queen/event/event.h>

#include <cstddef>

namespace queen
{
    // Double-buffered queue that keeps events readable across exactly two frames so consumer
    // systems are insensitive to execution order. Writers never invalidate live readers
    // because Swap() only clears the about-to-become-current buffer.
    //
    // Frame N:
    //   buffers_[current_]      buffers_[1 - current_]
    //   [E1][E2][E3]  <- writes [E0]  <- from frame N-1
    //
    // Swap() flips current_ and clears the new current buffer; events from N-1 are dropped,
    // events from N become "previous", new writes go into the freshly cleared current.
    template <Event T, comb::Allocator Allocator> class EventQueue
    {
    public:
        using EventType = T;
        using Iterator = const T*;

        explicit EventQueue(Allocator& allocator)
            : m_buffers{wax::Vector<T>{allocator}, wax::Vector<T>{allocator}}
            , m_current{0}
        {
        }

        ~EventQueue() = default;

        EventQueue(const EventQueue&) = delete;
        EventQueue& operator=(const EventQueue&) = delete;
        EventQueue(EventQueue&&) = default;
        EventQueue& operator=(EventQueue&&) = default;

        void Push(const T& event)
        {
            m_buffers[m_current].PushBack(event);
        }

        void Push(T&& event)
        {
            m_buffers[m_current].PushBack(std::move(event));
        }

        template <typename... Args> T& Emplace(Args&&... args)
        {
            return m_buffers[m_current].EmplaceBack(std::forward<Args>(args)...);
        }

        // Rotates the buffers and drops the oldest frame's events. Call once per frame after
        // all readers have run; the just-flipped buffer is the new write target.
        void Swap()
        {
            // Flip to the other buffer (which contains old events)
            m_current = 1 - m_current;
            // Clear the new current buffer (was previous, now will be current)
            m_buffers[m_current].Clear();
        }

        void Clear()
        {
            m_buffers[0].Clear();
            m_buffers[1].Clear();
        }

        [[nodiscard]] size_t CurrentCount() const noexcept
        {
            return m_buffers[m_current].Size();
        }

        [[nodiscard]] size_t PreviousCount() const noexcept
        {
            return m_buffers[1 - m_current].Size();
        }

        [[nodiscard]] size_t TotalCount() const noexcept
        {
            return m_buffers[0].Size() + m_buffers[1].Size();
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_buffers[0].IsEmpty() && m_buffers[1].IsEmpty();
        }

        [[nodiscard]] bool IsCurrentEmpty() const noexcept
        {
            return m_buffers[m_current].IsEmpty();
        }

        // Walks previous-frame events first, then current-frame events, so consumers see
        // them in chronological order regardless of the underlying buffer index.
        class EventIterator
        {
        public:
            EventIterator(const EventQueue* queue, size_t index)
                : m_queue{queue}
                , m_index{index}
            {
            }

            const T& operator*() const
            {
                size_t prevSize = m_queue->PreviousCount();
                if (m_index < prevSize)
                {
                    return m_queue->m_buffers[1 - m_queue->m_current][m_index];
                }
                return m_queue->m_buffers[m_queue->m_current][m_index - prevSize];
            }

            const T* operator->() const
            {
                return &(**this);
            }

            EventIterator& operator++()
            {
                ++m_index;
                return *this;
            }

            bool operator==(const EventIterator& other) const
            {
                return m_index == other.m_index;
            }

            bool operator!=(const EventIterator& other) const
            {
                return m_index != other.m_index;
            }

        private:
            const EventQueue* m_queue;
            size_t m_index;
        };

        [[nodiscard]] EventIterator Begin() const
        {
            return EventIterator{this, 0};
        }

        [[nodiscard]] EventIterator End() const
        {
            return EventIterator{this, TotalCount()};
        }

        [[nodiscard]] const wax::Vector<T>& CurrentBuffer() const noexcept
        {
            return m_buffers[m_current];
        }

        [[nodiscard]] const wax::Vector<T>& PreviousBuffer() const noexcept
        {
            return m_buffers[1 - m_current];
        }

    private:
        wax::Vector<T> m_buffers[2];
        uint8_t m_current;
    };
} // namespace queen
