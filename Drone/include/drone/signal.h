#pragma once

#include <atomic>
#include <cstdint>

namespace drone
{
    // One-shot wake-up signal. Producer Set()s, consumer Wait()s (then Reset()s, or uses
    // WaitAndReset). Uses C++20 atomic::wait/notify (futex on Linux, WaitOnAddress on Win32)
    // for zero-CPU parking. Same primitive family as drone::Counter, but for "wait until
    // signaled" semantics rather than "wait for N completions to drop the counter to zero".
    //
    // Use cases: signal-driven dedicated threads (render thread, audio mixer) that wake on
    // demand instead of polling a tick interval like drone::ServiceThread.
    class Signal
    {
    public:
        Signal() = default;

        Signal(const Signal&) = delete;
        Signal& operator=(const Signal&) = delete;

        void Set() noexcept
        {
            m_value.store(1, std::memory_order_release);
            m_value.notify_one();
        }

        void Wait() const noexcept
        {
            while (m_value.load(std::memory_order_acquire) == 0)
            {
                m_value.wait(0, std::memory_order_relaxed);
            }
        }

        void Reset() noexcept
        {
            m_value.store(0, std::memory_order_release);
        }

        void WaitAndReset() noexcept
        {
            Wait();
            Reset();
        }

        [[nodiscard]] bool IsSet() const noexcept
        {
            return m_value.load(std::memory_order_acquire) != 0;
        }

    private:
        std::atomic<int32_t> m_value{0};
    };
} // namespace drone
