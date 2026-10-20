#include "stdafx.h"
#include "PerformanceLog.h"
#include "Renderer.h"
#include "Prototype.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

namespace
{
    using Packet = std::array<std::string, 5>;
    constexpr uint64_t FileBudget = 256ull * 1024 * 1024;
    constexpr size_t QueueBudget = 8 * 1024 * 1024;
    constexpr size_t QueuePackets = 512;
    const char* FileNames[] = {"frames.csv", "batches.csv", "gpu.csv", "summary.csv", "events.csv"};

    std::string Escape(const std::string& value)
    {
        std::string result = "\"";
        for (char ch : value)
        {
            if (ch == '"')
            {
                result += '"';
            }
            result += ch;
        }
        return result + '"';
    }

    std::ostringstream Stream()
    {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::fixed << std::setprecision(4);
        return stream;
    }

    double Percentile(std::vector<double> values, double fraction)
    {
        if (values.empty())
        {
            return 0;
        }
        std::sort(values.begin(), values.end());
        const auto index = size_t(std::ceil(values.size() * fraction)) - 1;
        return values[std::min(index, values.size() - 1)];
    }

    double Mean(const std::vector<double>& values)
    {
        double sum = 0;
        for (double value : values)
        {
            sum += value;
        }
        return values.empty() ? 0 : sum / values.size();
    }

    struct CsvRow
    {
        bool names;
        bool first = true;
        std::ostringstream header = Stream(), values = Stream();

        explicit CsvRow(bool includeNames)
            : names(includeNames)
        {
        }

        template <class T> void Add(const std::string& name, T value)
        {
            if (!first)
            {
                values << ',';
                if (names)
                {
                    header << ',';
                }
            }
            first = false;
            if (names)
            {
                header << name;
            }
            values << value;
        }

        std::string Text()
        {
            return (names ? header.str() + "\n" : "") + values.str() + "\n";
        }
    };

    uint64_t ShaderHash(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return 0;
        }
        uint64_t hash = 14695981039346656037ull;
        char ch;
        while (file.get(ch))
        {
            hash = (hash ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
        }
        return file.bad() ? 0 : hash;
    }

    // Owns all CSV streams on the worker after Start. No OpenGL calls on this thread.
    class LogWriter
    {
      public:

        std::filesystem::path directory;
        std::atomic<uint64_t> dropped{0};
        std::atomic<uint64_t> writtenBytes{0};
        std::atomic<bool> failed{false};
        std::atomic<bool> limitReached{false};

        ~LogWriter()
        {
            Stop();
        }

        bool Start(const std::filesystem::path& base)
        {
            SYSTEMTIME utc{};
            GetSystemTime(&utc);
            auto name = Stream();
            name << std::setfill('0') << std::setw(4) << utc.wYear << std::setw(2) << utc.wMonth
                 << std::setw(2) << utc.wDay << 'T' << std::setw(2) << utc.wHour << std::setw(2)
                 << utc.wMinute << std::setw(2) << utc.wSecond << '-' << std::setw(3)
                 << utc.wMilliseconds << 'Z' << '-' << GetCurrentProcessId();
            std::error_code error;
            std::filesystem::create_directories(base, error);
            if (error)
            {
                failed = true;
                return false;
            }
            bool created = false;
            for (int suffix = 0; suffix < 100 && !created; ++suffix)
            {
                directory = base / (name.str() + "-" + std::to_string(suffix));
                created = std::filesystem::create_directory(directory, error);
                if (error)
                {
                    failed = true;
                    return false;
                }
            }
            if (!created)
            {
                failed = true;
                return false;
            }
            for (size_t i = 0; i < streams_.size(); ++i)
            {
                streams_[i].open(directory / FileNames[i], std::ios::binary);
                if (!streams_[i])
                {
                    failed = true;
                    return false;
                }
            }
            running_ = true;
            try
            {
                worker_ = std::thread(
                    [this]
                    {
                        Run();
                    });
            }
            catch (const std::system_error&)
            {
                running_ = false;
                failed = true;
                return false;
            }
            return true;
        }

        bool Submit(Packet packet)
        {
            size_t bytes = 0;
            for (const auto& part : packet)
            {
                bytes += part.size();
            }
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ || failed || queue_.size() >= QueuePackets
                || bytes > QueueBudget - queuedBytes_)
            {
                ++dropped;
                return false;
            }
            queuedBytes_ += bytes;
            queue_.push_back(std::move(packet));
            wake_.notify_one();
            return true;
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                running_ = false;
            }
            wake_.notify_one();
            if (worker_.joinable())
            {
                worker_.join();
            }
            for (auto& stream : streams_)
            {
                if (stream.is_open())
                {
                    stream.close();
                }
            }
        }

      private:

        void Run()
        {
            auto lastFlush = ProfileClock::now();
            for (;;)
            {
                Packet packet;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait_for(lock,
                                   std::chrono::seconds(1),
                                   [this]
                                   {
                                       return !queue_.empty() || !running_;
                                   });
                    if (queue_.empty() && !running_)
                    {
                        break;
                    }
                    if (!queue_.empty())
                    {
                        packet = std::move(queue_.front());
                        queue_.pop_front();
                        for (const auto& part : packet)
                        {
                            queuedBytes_ -= part.size();
                        }
                    }
                }
                size_t bytes = 0;
                for (const auto& part : packet)
                {
                    bytes += part.size();
                }
                if (bytes)
                {
                    if (failed || writtenBytes.load() + bytes > FileBudget)
                    {
                        if (writtenBytes.load() + bytes > FileBudget)
                        {
                            limitReached = true;
                        }
                        failed = true;
                        ++dropped;
                    }
                    else
                    {
                        for (size_t i = 0; i < streams_.size(); ++i)
                        {
                            streams_[i] << packet[i];
                            if (!streams_[i])
                            {
                                failed = true;
                            }
                        }
                        writtenBytes += bytes;
                    }
                }
                if (ProfileClock::now() - lastFlush >= std::chrono::seconds(1))
                {
                    for (auto& stream : streams_)
                    {
                        stream.flush();
                        if (!stream)
                        {
                            failed = true;
                        }
                    }
                    lastFlush = ProfileClock::now();
                }
            }
            for (auto& stream : streams_)
            {
                stream.flush();
                if (!stream)
                {
                    failed = true;
                }
            }
        }

        std::array<std::ofstream, 5> streams_;
        std::deque<Packet> queue_;
        size_t queuedBytes_ = 0;
        bool running_ = false;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::thread worker_;
    };
} // namespace

struct PerformanceLog::Impl
{
    LogWriter writer;
    bool started = false, frameHeader = false, warned = false;
    ProfileClock::time_point start{}, previous{}, windowStart{};
    uint64_t lastFrame = 0, windowFirst = 0, frameUpdates = 0, clampedUpdates = 0;
    uint64_t totalFrames = 0, windowDrawCalls = 0, windowNew = 0, windowLoads = 0;
    uint64_t windowMinDraws = 0, windowMaxDraws = 0, windowLastDraws = 0, windowUpdates = 0;
    double windowUpdateMs = 0;
    double windowSortMs = 0, windowDepthPrepareMs = 0, windowNavigationMaxMs = 0;
    double updateMs = 0, maxUpdateMs = 0, rawDelta = 0, previousLogMs = 0;
    std::vector<double> intervals, draws, swaps;
    std::string configuration;

    double Elapsed() const
    {
        return ProfileMilliseconds(ProfileClock::now() - start);
    }

    std::string Event(uint64_t frame, const std::string& event, const std::string& detail)
    {
        auto line = Stream();
        line << frame << ',' << Elapsed() << ',' << Escape(event) << ',' << Escape(detail) << '\n';
        return line.str();
    }

    std::string GpuRows(Renderer& renderer)
    {
        auto rows = Stream();
        for (const auto& sample : renderer.CollectGpuSamples())
        {
            rows << sample.frame << ',' << sample.batch << ',' << sample.stage << ','
                 << sample.milliseconds << ',' << lastFrame << ',' << Elapsed() << '\n';
        }
        return rows.str();
    }

    std::string Summary(ProfileClock::time_point end)
    {
        if (intervals.empty())
        {
            return {};
        }
        const double seconds = ProfileMilliseconds(end - windowStart) / 1000;
        const auto count = intervals.size();
        const auto slow33 = std::count_if(intervals.begin(),
                                          intervals.end(),
                                          [](double ms)
                                          {
                                              return ms > 33.3333;
                                          });
        const auto slow50 = std::count_if(intervals.begin(),
                                          intervals.end(),
                                          [](double ms)
                                          {
                                              return ms > 50;
                                          });
        auto row = Stream();
        row << windowFirst << ',' << lastFrame << ',' << count << ',' << seconds << ','
            << (seconds > 0 ? count / seconds : 0) << ',' << Mean(intervals) << ','
            << Percentile(intervals, .5) << ',' << Percentile(intervals, .95) << ','
            << Percentile(intervals, .99) << ','
            << *std::max_element(intervals.begin(), intervals.end()) << ',' << slow33 << ','
            << slow50 << ',' << double(windowDrawCalls) / count << ',' << windowLastDraws << ','
            << windowMinDraws << ',' << windowMaxDraws << ','
            << (windowUpdates ? windowUpdateMs / windowUpdates : 0) << ',' << Mean(draws) << ','
            << Percentile(draws, .95) << ',' << Mean(swaps) << ',' << windowNew << ','
            << windowLoads << ',' << writer.dropped.load() << '\n';
        std::cout << "[Profile] FPS=" << std::fixed << std::setprecision(1)
                  << (seconds > 0 ? count / seconds : 0)
                  << " | FrameMs(avg/p95/p99)=" << Mean(intervals) << '/'
                  << Percentile(intervals, .95) << '/' << Percentile(intervals, .99)
                  << " | DrawCalls(last/avg/min/max)=" << windowLastDraws << '/'
                  << double(windowDrawCalls) / count << '/' << windowMinDraws << '/'
                  << windowMaxDraws
                  << " | UpdateMs(avg)=" << (windowUpdates ? windowUpdateMs / windowUpdates : 0)
                  << " | DrawMs(avg)=" << Mean(draws) << " SwapMs(avg)=" << Mean(swaps)
                  << " | SortMs(avg)=" << windowSortMs / count
                  << " DepthPrepMs(avg)=" << windowDepthPrepareMs / count
                  << " NavMs(maxFrame)=" << windowNavigationMaxMs << " | MeshNew=" << windowNew
                  << " MeshLoad=" << windowLoads << " | LogDropped=" << writer.dropped.load()
                  << '\n';
        intervals.clear();
        draws.clear();
        swaps.clear();
        windowDrawCalls = windowNew = windowLoads = 0;
        windowUpdates = 0;
        windowUpdateMs = 0;
        windowSortMs = windowDepthPrepareMs = windowNavigationMaxMs = 0;
        windowStart = end;
        return row.str();
    }
};

PerformanceLog::PerformanceLog()
    : impl_(std::make_unique<Impl>())
{
}

PerformanceLog::~PerformanceLog()
{
    Stop(nullptr);
}

void PerformanceLog::Start(const std::filesystem::path& base, const Renderer& renderer)
{
    if (!ProfilingEnabled || impl_->started)
    {
        return;
    }
    auto& p = *impl_;
    p.started = true;
    p.start = p.previous = p.windowStart = ProfileClock::now();
    CpuProfile = {};
    if (p.writer.Start(base / L"PerformanceLogs"))
    {
        std::ofstream session(p.writer.directory / L"session.txt", std::ios::binary);
        auto glText = [](GLenum name)
        {
            const auto value = glGetString(name);
            return value ? reinterpret_cast<const char*>(value) : "unavailable";
        };
        SYSTEM_INFO system{};
        GetNativeSystemInfo(&system);
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        GlobalMemoryStatusEx(&memory);
        session << "schema=1\nrenderer=atlas-v3\ntelemetry=1\nbuild=" << __DATE__ << ' ' << __TIME__
                << "\ncompiler_msvc=" << _MSC_VER << "\npointer_bits=" << sizeof(void*) * 8
#ifdef _DEBUG
                << "\nconfiguration=Debug"
#else
                << "\nconfiguration=Release"
#endif
                << "\ngl_vendor=" << glText(GL_VENDOR) << "\ngl_renderer=" << glText(GL_RENDERER)
                << "\ngl_version=" << glText(GL_VERSION)
                << "\nglsl=" << glText(GL_SHADING_LANGUAGE_VERSION)
                << "\ngpu_timing_available=" << renderer.GpuTimingAvailable()
                << "\nmesh_limit_bytes=" << renderer.MeshLimitBytes()
                << "\ninstance_limit=" << renderer.InstanceLimit()
                << "\ntriangle_limit=" << renderer.TriangleLimit()
                << "\nfile_budget_bytes=" << FileBudget << "\nqueue_budget_bytes=" << QueueBudget
                << "\ntimer_interval_ms=16\nraw_game_dt_clamp_seconds=0.05"
                << "\nprofile_env=GSE_PROFILE (0 disables)"
                << "\nsource_revision=not_embedded\n";
        session << "logical_processors=" << system.dwNumberOfProcessors
                << "\nphysical_memory_bytes=" << memory.ullTotalPhys
                << "\nswap_interval=not_queried\n";
        char label[257] = {};
        const DWORD labelLength =
            GetEnvironmentVariableA("GSE_PROFILE_LABEL", label, sizeof(label));
        if (labelLength && labelLength < sizeof(label))
        {
            session << "label=" << Escape(label) << '\n';
        }
        for (const auto* shader : {"SolidRect.vs",
                                   "SolidRect.fs",
                                   "Fullscreen.vs",
                                   "Downsample.fs",
                                   "GaussianBlur.fs",
                                   "Composite.fs"})
        {
            session << "shader_hash." << shader << '=' << std::hex
                    << ShaderHash(base / L"Shaders" / shader) << std::dec << '\n';
        }
        Packet headers;
        headers[1] = "frame,batch,stage,reason,instances,triangles,upload_bytes,instance_fill_"
                     "ratio,triangle_fill_ratio,submit_cpu_ms\n";
        headers[2] = "frame,batch,stage,gpu_ms,collected_at_frame,collected_at_elapsed_ms\n";
        headers[3] =
            "first_frame,last_frame,samples,window_seconds,fps,frame_ms_avg,frame_ms_p50,frame_ms_"
            "p95,frame_ms_p99,frame_ms_max,over_33ms,over_50ms,draw_calls_avg,draw_calls_last,draw_"
            "calls_min,draw_calls_max,update_ms_per_tick_avg,draw_cpu_ms_avg,draw_"
            "cpu_ms_p95,swap_ms_avg,mesh_new,mesh_loaded,log_packets_dropped_total\n";
        headers[4] = "frame,elapsed_ms,event,detail\n";
        headers[4] += "0,0,session_start,schema=1\n";
        p.writer.Submit(std::move(headers));
        std::cout << "분석 로그: " << p.writer.directory.u8string() << " | M: 비교 지점 표시\n";
    }
    else
    {
        std::cerr << "분석 로그 폴더를 열 수 없습니다. 콘솔 요약만 출력합니다.\n";
    }
    // Exclude metadata and file setup from the first frame interval.
    p.start = p.previous = p.windowStart = ProfileClock::now();
}

void PerformanceLog::RecordUpdate(double milliseconds, double rawDeltaSeconds)
{
    if (!impl_->started)
    {
        return;
    }
    impl_->updateMs += milliseconds;
    impl_->maxUpdateMs = std::max(impl_->maxUpdateMs, milliseconds);
    impl_->rawDelta = rawDeltaSeconds;
    ++impl_->frameUpdates;
    impl_->clampedUpdates += rawDeltaSeconds > .05 ? 1 : 0;
}

void PerformanceLog::RecordFrame(Renderer& renderer,
                                 const Prototype& game,
                                 ProfileClock::time_point presented,
                                 double drawMs,
                                 double swapMs)
{
    if (!impl_->started)
    {
        return;
    }
    const auto logStart = ProfileClock::now();
    auto& p = *impl_;
    const auto state = game.ProfileState();
    const auto& f = renderer.FrameStats();
    const auto& c = renderer.CacheStats();
    const auto& fx = renderer.PostEffects();
    const auto& passes = renderer.PostProcessorStats().PassCounts();
    const double interval = ProfileMilliseconds(presented - p.previous);
    p.previous = presented;
    p.lastFrame = f.frame;
    ++p.totalFrames;
    if (p.intervals.empty())
    {
        p.windowFirst = f.frame;
        p.windowMinDraws = p.windowMaxDraws = renderer.DrawCalls();
    }
    p.intervals.push_back(interval);
    p.draws.push_back(drawMs);
    p.swaps.push_back(swapMs);
    p.windowDrawCalls += renderer.DrawCalls();
    p.windowLastDraws = renderer.DrawCalls();
    p.windowMinDraws = std::min(p.windowMinDraws, renderer.DrawCalls());
    p.windowMaxDraws = std::max(p.windowMaxDraws, renderer.DrawCalls());
    p.windowUpdates += p.frameUpdates;
    p.windowUpdateMs += p.updateMs;
    p.windowSortMs += CpuProfile.milliseconds[size_t(CpuMetric::SceneSort)];
    p.windowDepthPrepareMs += CpuProfile.milliseconds[size_t(CpuMetric::DepthPrepare)];
    p.windowNavigationMaxMs =
        std::max(p.windowNavigationMaxMs, CpuProfile.milliseconds[size_t(CpuMetric::Navigation)]);
    p.windowNew += c.generated;
    p.windowLoads += c.diskLoaded;
    CsvRow row(!p.frameHeader);
    row.Add("frame", f.frame);
    row.Add("elapsed_ms", ProfileMilliseconds(presented - p.start));
    row.Add("frame_ms", interval);
    row.Add("updates", p.frameUpdates);
    row.Add("update_ms_total", p.updateMs);
    row.Add("update_ms_max", p.maxUpdateMs);
    row.Add("raw_update_dt_s", p.rawDelta);
    row.Add("dt_clamped_updates", p.clampedUpdates);
    row.Add("draw_cpu_ms", drawMs);
    row.Add("swap_ms", swapMs);
    row.Add("previous_log_cpu_ms", p.previousLogMs);
    row.Add("draw_calls", renderer.DrawCalls());
    row.Add("world_draws", f.worldDraws);
    row.Add("hud_draws", f.hudDraws);
    const char* passNames[] = {"bloom_extract_draws",
                               "bloom_blur_draws",
                               "edge_downsample_draws",
                               "edge_blur_draws",
                               "composite_draws"};
    for (size_t i = 0; i < passes.size(); ++i)
    {
        row.Add(passNames[i], passes[i]);
    }
    row.Add("mesh_instances", renderer.InstanceCount());
    row.Add("triangle_instances", f.triangles);
    row.Add("instance_upload_bytes", renderer.InstanceUploadBytes());
    row.Add("mesh_upload_bytes", c.uploadedBytes);
    row.Add("mesh_count", renderer.MeshCount());
    row.Add("mesh_live_bytes", renderer.MeshBytes());
    row.Add("mesh_capacity_bytes", renderer.MeshCapacityBytes());
    row.Add("mesh_limit_bytes", renderer.MeshLimitBytes());
    row.Add("disk_cache_enabled", renderer.DiskCacheEnabled());
    row.Add("mesh_new", c.generated);
    row.Add("mesh_loaded", c.diskLoaded);
    row.Add("cache_hits", c.hits);
    row.Add("cache_misses", c.misses);
    row.Add("file_missing", c.fileMissing);
    row.Add("file_invalid", c.fileInvalid);
    row.Add("file_errors", c.fileErrors);
    row.Add("file_saved", c.fileSaved);
    row.Add("file_skipped", c.fileSkipped);
    row.Add("file_read_bytes", c.readBytes);
    row.Add("file_written_bytes", c.writtenBytes);
    row.Add("mesh_evicted", c.evicted);
    row.Add("atlas_compactions", c.compactions);
    row.Add("atlas_growth", c.atlasGrowth);
    row.Add("upload_rejected", c.uploadRejected);
    row.Add("gpu_available", renderer.GpuTimingAvailable());
    row.Add("gpu_skipped", f.gpuSkipped);
    row.Add("gpu_pending", renderer.PendingGpuSamples());
    DWORD foregroundProcess = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
    row.Add("foreground", foregroundProcess == GetCurrentProcessId());
    row.Add("width", renderer.Width());
    row.Add("height", renderer.Height());
    row.Add("post_available", renderer.PostEffectsAvailable());
    row.Add("post_enabled", fx.enabled);
    row.Add("bloom", fx.bloom);
    row.Add("edge_blur", fx.edgeBlur);
    row.Add("vignette", fx.vignette);
    row.Add("exposure", fx.exposure);
    row.Add("bloom_strength", fx.bloomStrength);
    row.Add("bloom_radius", fx.bloomRadius);
    row.Add("bloom_threshold", fx.bloomThreshold);
    row.Add("bloom_knee", fx.bloomKnee);
    row.Add("edge_strength", fx.edgeBlurStrength);
    row.Add("edge_radius", fx.edgeBlurRadius);
    row.Add("edge_start", fx.edgeBlurStart);
    row.Add("edge_end", fx.edgeBlurEnd);
    row.Add("vignette_strength", fx.vignetteStrength);
    row.Add("vignette_start", fx.vignetteStart);
    row.Add("vignette_end", fx.vignetteEnd);
    row.Add("seed", state.seed);
    row.Add("player_x", state.playerX);
    row.Add("player_y", state.playerY);
    row.Add("zoom", state.zoom);
    row.Add("chunks", state.chunks);
    row.Add("actors", state.actors);
    row.Add("enemies", state.enemies);
    row.Add("projectiles", state.projectiles);
    row.Add("loot", state.loot);
    row.Add("paused", state.paused);
    row.Add("scan", state.scan);
    row.Add("level", state.level);
    row.Add("boss", state.boss);
    row.Add("cleared", state.cleared);
    row.Add("npcs", state.npcs);
    row.Add("wallet", state.wallet);
    row.Add("cargo", state.cargo);
    row.Add("insight", state.insight);
    row.Add("story", state.story);
    row.Add("wanted", state.wanted);
    row.Add("society_trades_total", state.trades);
    row.Add("society_raids_total", state.raids);
    row.Add("society_hunts_total", state.hunts);
    for (size_t i = 0; i < CpuMetricNames.size(); ++i)
    {
        row.Add(std::string(CpuMetricNames[i]) + "_ms", CpuProfile.milliseconds[i]);
        row.Add(std::string(CpuMetricNames[i]) + "_calls", CpuProfile.calls[i]);
    }
    row.Add("log_packets_dropped_total", p.writer.dropped.load());
    row.Add("log_file_failed", p.writer.failed.load());
    row.Add("log_limit_reached", p.writer.limitReached.load());
    Packet packet;
    packet[0] = row.Text();
    auto batchRows = Stream();
    for (const auto& batch : renderer.Batches())
    {
        batchRows << batch.frame << ',' << batch.batch << ',' << batch.stage << ',' << batch.reason
                  << ',' << batch.instances << ',' << batch.triangles << ',' << batch.bytes << ','
                  << double(batch.instances) / renderer.InstanceLimit() << ','
                  << double(batch.triangles) / renderer.TriangleLimit() << ',' << batch.submitMs
                  << '\n';
    }
    packet[1] = batchRows.str();
    packet[2] = p.GpuRows(renderer);
    auto config = Stream();
    config << renderer.Width() << 'x' << renderer.Height() << ";seed=" << state.seed
           << ";zoom=" << state.zoom << ";paused=" << state.paused << ";scan=" << state.scan
           << ";post=" << fx.enabled << ";bloom=" << fx.bloom << ";edge=" << fx.edgeBlur
           << ";vignette=" << fx.vignette << ";exposure=" << fx.exposure
           << ";bloom_strength=" << fx.bloomStrength << ";edge_strength=" << fx.edgeBlurStrength
           << ";vignette_strength=" << fx.vignetteStrength;
    if (config.str() != p.configuration)
    {
        p.configuration = config.str();
        packet[4] += p.Event(f.frame, "configuration", p.configuration);
    }
    if (interval > 50)
    {
        packet[4] += p.Event(f.frame, "slow_frame", "frame_ms=" + std::to_string(interval));
    }
    if (c.uploadRejected || c.fileErrors || c.fileInvalid)
    {
        packet[4] += p.Event(f.frame,
                             "cache_issue",
                             "upload_rejected=" + std::to_string(c.uploadRejected)
                                 + ";file_errors=" + std::to_string(c.fileErrors)
                                 + ";file_invalid=" + std::to_string(c.fileInvalid));
    }
    if (presented - p.windowStart >= std::chrono::seconds(1))
    {
        packet[3] = p.Summary(presented);
    }
    if (p.writer.Submit(std::move(packet)))
    {
        p.frameHeader = true;
    }
    if (p.writer.failed && !p.warned)
    {
        p.warned = true;
        std::cerr << "분석 파일 기록 중단: 파일 예산 초과 또는 I/O 오류. 콘솔 요약은 계속됩니다.\n";
    }
    p.updateMs = p.maxUpdateMs = 0;
    p.frameUpdates = p.clampedUpdates = 0;
    CpuProfile = {};
    p.previousLogMs = ProfileMilliseconds(ProfileClock::now() - logStart);
}

void PerformanceLog::Mark(const std::string& event, const std::string& detail)
{
    if (!impl_->started)
    {
        return;
    }
    Packet packet;
    packet[4] = impl_->Event(impl_->lastFrame, event, detail);
    impl_->writer.Submit(std::move(packet));
}

void PerformanceLog::Stop(Renderer* renderer)
{
    if (!impl_ || !impl_->started)
    {
        return;
    }
    auto& p = *impl_;
    Packet packet;
    if (renderer)
    {
        packet[2] = p.GpuRows(*renderer);
    }
    packet[3] = p.Summary(p.previous);
    const auto pending = renderer ? renderer->PendingGpuSamples() : 0;
    packet[4] =
        p.Event(p.lastFrame, "session_end", "uncollected_gpu_queries=" + std::to_string(pending));
    p.writer.Submit(std::move(packet));
    p.writer.Stop();
    if (!p.writer.directory.empty())
    {
        std::ofstream session(p.writer.directory / L"session.txt",
                              std::ios::binary | std::ios::app);
        session << "\ncompleted_frames=" << p.totalFrames
                << "\nlog_packets_dropped=" << p.writer.dropped.load()
                << "\nlog_file_failed=" << p.writer.failed.load()
                << "\nlog_limit_reached=" << p.writer.limitReached.load()
                << "\nlog_bytes_written=" << p.writer.writtenBytes.load()
                << "\ngpu_queries_uncollected=" << pending << "\n";
    }
    p.started = false;
}
