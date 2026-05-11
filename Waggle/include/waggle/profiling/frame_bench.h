#pragma once

#include <hive/hive_config.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>

#include <atomic>
#include <chrono>
#include <cstdint>

namespace waggle
{
    class App;

    // Frame-time + phase-timing harness gated by HIVE_BENCH_DURATION. Drives the bench
    // window: collects samples, periodically logs mean/1%-low/99%-high FPS, appends one
    // row per window to HIVE_BENCH_REPORT, and calls App::RequestStop when the duration
    // elapses so external scripts can chain runs without manual shutdown.
    class FrameBench
    {
    public:
        explicit FrameBench(App& app);

        // Reads env vars; no-op when HIVE_BENCH_DURATION is unset.
        void Configure(const char* defaultLabel);

        [[nodiscard]] bool IsEnabled() const noexcept
        {
            return m_enabled;
        }

        // Game thread: brackets the whole frame including the build phase. EndFrame
        // flushes log windows and triggers shutdown when the duration is reached.
        void BeginFrame() noexcept;
        void EndFrame() noexcept;

        // Game thread: brackets BuildRenderFrame.
        void BeginBuild() noexcept;
        void EndBuild() noexcept;

        // Render thread (or inline render path): brackets ExecuteRenderFrame.
        void BeginExecute() noexcept;
        void EndExecute() noexcept;

    private:
        using Clock = std::chrono::steady_clock;
        using Instant = Clock::time_point;

        static double DurationMs(Instant start, Instant end) noexcept;

        void FlushWindow();
        void EmitFinalReport();

        App& m_app;
        bool m_enabled{false};
        bool m_finalEmitted{false};
        double m_durationSeconds{0.0};
        double m_logIntervalSeconds{2.0};
        wax::String m_label;
        wax::String m_csvPath;

        Instant m_sessionStart{};
        Instant m_lastFlush{};
        Instant m_frameStart{};
        Instant m_buildStart{};
        Instant m_executeStart{};

        std::atomic<int64_t> m_lastExecuteUs{0};

        wax::Vector<float> m_frameMs;
        wax::Vector<float> m_buildMs;
        wax::Vector<float> m_executeMs;

        uint32_t m_flushCount{0};
    };
} // namespace waggle
