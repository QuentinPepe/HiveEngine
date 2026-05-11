#include <waggle/profiling/frame_bench.h>

#include <hive/core/env.h>
#include <hive/core/log.h>

#include <comb/default_allocator.h>

#include <waggle/app.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const hive::LogCategory LOG_BENCH{"Waggle.FrameBench"};

namespace waggle
{
    namespace
    {
        void ComputeStats(wax::Vector<float>& samples, float& outMean, float& outMin, float& outMax,
                          float& outP1, float& outP99)
        {
            outMean = 0.f;
            outMin = 0.f;
            outMax = 0.f;
            outP1 = 0.f;
            outP99 = 0.f;
            if (samples.IsEmpty())
            {
                return;
            }
            double sum = 0.0;
            for (size_t i = 0; i < samples.Size(); ++i)
            {
                sum += static_cast<double>(samples[i]);
            }
            outMean = static_cast<float>(sum / static_cast<double>(samples.Size()));
            std::sort(samples.Data(), samples.Data() + samples.Size());
            outMin = samples[0];
            outMax = samples[samples.Size() - 1];
            const size_t i1 = static_cast<size_t>(0.01 * static_cast<double>(samples.Size() - 1));
            const size_t i99 = static_cast<size_t>(0.99 * static_cast<double>(samples.Size() - 1));
            outP1 = samples[i1];
            outP99 = samples[i99];
        }
    } // namespace

    FrameBench::FrameBench(App& app)
        : m_app{app}
        , m_label{comb::GetDefaultMemoryResource()}
        , m_csvPath{comb::GetDefaultMemoryResource()}
        , m_frameMs{comb::GetDefaultMemoryResource()}
        , m_buildMs{comb::GetDefaultMemoryResource()}
        , m_executeMs{comb::GetDefaultMemoryResource()}
    {
    }

    void FrameBench::Configure(const char* defaultLabel)
    {
        char buf[260]{};
        if (!hive::core::ReadEnvVar("HIVE_BENCH_DURATION", buf, sizeof(buf)))
        {
            return;
        }
        const double duration = std::atof(buf);
        if (duration <= 0.0)
        {
            return;
        }
        m_durationSeconds = duration;
        m_enabled = true;

        if (hive::core::ReadEnvVar("HIVE_BENCH_LOG_INTERVAL", buf, sizeof(buf)))
        {
            const double interval = std::atof(buf);
            if (interval > 0.0)
            {
                m_logIntervalSeconds = interval;
            }
        }

        m_label.Clear();
        if (hive::core::ReadEnvVar("HIVE_BENCH_LABEL", buf, sizeof(buf)))
        {
            m_label.Append(buf, std::strlen(buf));
        }
        else if (defaultLabel != nullptr)
        {
            m_label.Append(defaultLabel, std::strlen(defaultLabel));
        }

        m_csvPath.Clear();
        if (hive::core::ReadEnvVar("HIVE_BENCH_REPORT", buf, sizeof(buf)))
        {
            m_csvPath.Append(buf, std::strlen(buf));
        }

        m_frameMs.Reserve(1024);
        m_buildMs.Reserve(1024);
        m_executeMs.Reserve(1024);

        m_sessionStart = Clock::now();
        m_lastFlush = m_sessionStart;

        hive::LogInfo(LOG_BENCH, "Bench enabled: label='{}' duration={}s interval={}s csv='{}'",
                      m_label.CStr(), m_durationSeconds, m_logIntervalSeconds, m_csvPath.CStr());
    }

    void FrameBench::BeginFrame() noexcept
    {
        if (!m_enabled)
        {
            return;
        }
        m_frameStart = Clock::now();
    }

    void FrameBench::EndFrame() noexcept
    {
        if (!m_enabled)
        {
            return;
        }
        const Instant now = Clock::now();
        m_frameMs.PushBack(static_cast<float>(DurationMs(m_frameStart, now)));

        const int64_t execUs = m_lastExecuteUs.load(std::memory_order_acquire);
        if (execUs > 0)
        {
            m_executeMs.PushBack(static_cast<float>(execUs) / 1000.f);
        }

        const double elapsedWindow = DurationMs(m_lastFlush, now) / 1000.0;
        if (elapsedWindow >= m_logIntervalSeconds)
        {
            FlushWindow();
            m_lastFlush = now;
        }

        const double elapsedSession = DurationMs(m_sessionStart, now) / 1000.0;
        if (elapsedSession >= m_durationSeconds && !m_finalEmitted)
        {
            EmitFinalReport();
            m_finalEmitted = true;
            m_app.RequestStop();
        }
    }

    void FrameBench::BeginBuild() noexcept
    {
        if (!m_enabled)
        {
            return;
        }
        m_buildStart = Clock::now();
    }

    void FrameBench::EndBuild() noexcept
    {
        if (!m_enabled)
        {
            return;
        }
        const Instant now = Clock::now();
        m_buildMs.PushBack(static_cast<float>(DurationMs(m_buildStart, now)));
    }

    void FrameBench::BeginExecute() noexcept
    {
        if (!m_enabled)
        {
            return;
        }
        m_executeStart = Clock::now();
    }

    void FrameBench::EndExecute() noexcept
    {
        if (!m_enabled)
        {
            return;
        }
        const Instant now = Clock::now();
        const int64_t us =
            std::chrono::duration_cast<std::chrono::microseconds>(now - m_executeStart).count();
        m_lastExecuteUs.store(us, std::memory_order_release);
    }

    double FrameBench::DurationMs(Instant start, Instant end) noexcept
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    void FrameBench::FlushWindow()
    {
        float frameMean = 0.f, frameMin = 0.f, frameMax = 0.f, frameP1 = 0.f, frameP99 = 0.f;
        ComputeStats(m_frameMs, frameMean, frameMin, frameMax, frameP1, frameP99);
        float buildMean = 0.f, buildMin = 0.f, buildMax = 0.f, buildP1 = 0.f, buildP99 = 0.f;
        ComputeStats(m_buildMs, buildMean, buildMin, buildMax, buildP1, buildP99);
        float execMean = 0.f, execMin = 0.f, execMax = 0.f, execP1 = 0.f, execP99 = 0.f;
        ComputeStats(m_executeMs, execMean, execMin, execMax, execP1, execP99);

        const float fpsMean = frameMean > 0.f ? 1000.f / frameMean : 0.f;
        // 1% low FPS comes from the worst-case (largest) frame times.
        const float fpsLow1 = frameP99 > 0.f ? 1000.f / frameP99 : 0.f;
        const float fpsHigh99 = frameP1 > 0.f ? 1000.f / frameP1 : 0.f;

        ++m_flushCount;
        hive::LogInfo(LOG_BENCH,
                      "[{}] #{} n={} FPS mean={:.1f} 1%low={:.1f} 99%high={:.1f} | "
                      "frame_ms={:.2f}/{:.2f}/{:.2f} build_ms={:.3f} execute_ms={:.3f}",
                      m_label.CStr(), m_flushCount, m_frameMs.Size(), fpsMean, fpsLow1, fpsHigh99,
                      frameMean, frameP1, frameP99, buildMean, execMean);

        if (!m_csvPath.IsEmpty())
        {
            const bool first = (m_flushCount == 1);
            FILE* file = nullptr;
            const char* mode = first ? "wb" : "ab";
#ifdef _MSC_VER
            fopen_s(&file, m_csvPath.CStr(), mode);
#else
            file = fopen(m_csvPath.CStr(), mode);
#endif
            if (file != nullptr)
            {
                if (first)
                {
                    fputs("label,window,samples,fps_mean,fps_1pct_low,fps_99pct_high,"
                          "frame_ms_mean,frame_ms_min,frame_ms_max,frame_ms_p1,frame_ms_p99,"
                          "build_ms_mean,execute_ms_mean\n",
                          file);
                }
                fprintf(file, "%s,%u,%zu,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                        m_label.CStr(), m_flushCount, m_frameMs.Size(), fpsMean, fpsLow1,
                        fpsHigh99, frameMean, frameMin, frameMax, frameP1, frameP99, buildMean,
                        execMean);
                fclose(file);
            }
        }

        m_frameMs.Clear();
        m_buildMs.Clear();
        m_executeMs.Clear();
    }

    void FrameBench::EmitFinalReport()
    {
        if (!m_frameMs.IsEmpty())
        {
            FlushWindow();
        }
        hive::LogInfo(LOG_BENCH, "[{}] bench complete after {}s, {} windows", m_label.CStr(),
                      m_durationSeconds, m_flushCount);
    }
} // namespace waggle
