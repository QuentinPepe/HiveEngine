#pragma once

#include <hive/core/assert.h>

#include <comb/allocator_concepts.h>

#include <wax/containers/hash_map.h>
#include <wax/containers/vector.h>

#include <queen/event/event.h>
#include <queen/event/event_queue.h>
#include <queen/event/event_reader.h>
#include <queen/event/event_writer.h>

#include <type_traits>
#include <utility>

namespace queen
{
    // World-owned registry of type-erased event queues, created lazily per event type.
    // entries_ owns the queues and holds the type-specific function pointers (swap, clear,
    // dtor) so SwapBuffers/ClearAll can fan out without templates. queues_ stores indices
    // into entries_ rather than pointers, so the vector can grow without invalidating lookups.
    template <comb::Allocator Allocator> class Events
    {
    public:
        explicit Events(Allocator& allocator)
            : m_allocator{&allocator}
            , m_queues{allocator, 32}
            , m_entries{allocator}
        {
        }

        ~Events()
        {
            // Call destructors on all queue entries
            for (size_t i = 0; i < m_entries.Size(); ++i)
            {
                if (m_entries[i].m_destructor != nullptr)
                {
                    m_entries[i].m_destructor(m_entries[i].m_queue);
                }
                m_allocator->Deallocate(m_entries[i].m_queue);
            }
        }

        Events(const Events&) = delete;
        Events& operator=(const Events&) = delete;
        Events(Events&&) = default;
        Events& operator=(Events&&) = default;

        template <Event T> [[nodiscard]] EventWriter<T, Allocator> Writer()
        {
            return EventWriter<T, Allocator>{this->template GetOrCreateQueue<T>()};
        }

        template <Event T> [[nodiscard]] EventReader<T, Allocator> Reader()
        {
            return EventReader<T, Allocator>{this->template GetOrCreateQueue<T>()};
        }

        template <Event T> void Send(const T& event)
        {
            this->template GetOrCreateQueue<T>().Push(event);
        }

        template <Event T> void Send(T&& event)
        {
            using EventType = std::remove_cvref_t<T>;
            this->template GetOrCreateQueue<EventType>().Push(std::forward<T>(event));
        }

        // Called once per frame after all systems have run; rotates each queue's double buffer
        // so frame-N events stay readable through frame N+1.
        void SwapBuffers()
        {
            for (size_t i = 0; i < m_entries.Size(); ++i)
            {
                if (m_entries[i].m_swapFn != nullptr)
                {
                    m_entries[i].m_swapFn(m_entries[i].m_queue);
                }
            }
        }

        void ClearAll()
        {
            for (size_t i = 0; i < m_entries.Size(); ++i)
            {
                if (m_entries[i].m_clearFn != nullptr)
                {
                    m_entries[i].m_clearFn(m_entries[i].m_queue);
                }
            }
        }

        template <Event T> [[nodiscard]] bool HasQueue() const
        {
            TypeId id = TypeIdOf<T>();
            return m_queues.Find(id) != nullptr;
        }

        [[nodiscard]] size_t QueueCount() const noexcept
        {
            return m_entries.Size();
        }

    private:
        struct QueueEntry
        {
            void* m_queue;
            void (*m_swapFn)(void*);
            void (*m_clearFn)(void*);
            void (*m_destructor)(void*);
            TypeId m_typeId;
        };

        template <Event T> EventQueue<T, Allocator>& GetOrCreateQueue()
        {
            TypeId id = TypeIdOf<T>();

            if (auto* index = m_queues.Find(id))
            {
                return *static_cast<EventQueue<T, Allocator>*>(m_entries[*index].m_queue);
            }

            void* memory = m_allocator->Allocate(sizeof(EventQueue<T, Allocator>), alignof(EventQueue<T, Allocator>));
            hive::Assert(memory != nullptr, "Failed to allocate EventQueue");

            auto* queue = new (memory) EventQueue<T, Allocator>(*m_allocator);

            QueueEntry entry{};
            entry.m_queue = queue;
            entry.m_swapFn = [](void* q) {
                static_cast<EventQueue<T, Allocator>*>(q)->Swap();
            };
            entry.m_clearFn = [](void* q) {
                static_cast<EventQueue<T, Allocator>*>(q)->Clear();
            };
            entry.m_destructor = [](void* q) {
                static_cast<EventQueue<T, Allocator>*>(q)->~EventQueue();
            };
            entry.m_typeId = id;

            size_t newIndex = m_entries.Size();
            m_entries.PushBack(entry);
            m_queues.Insert(id, newIndex);

            return *queue;
        }

        Allocator* m_allocator;
        wax::HashMap<TypeId, size_t> m_queues;
        wax::Vector<QueueEntry> m_entries;
    };
} // namespace queen
