#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstddef>

using ProfileClock = std::chrono::steady_clock;
inline bool ProfilingEnabled = true;

enum class CpuMetric
{
    SceneUpdate,
    SceneSort,
    SceneRender,
    Navigation,
    WorldStream,
    CollisionRefresh,
    SaveGame,
    CacheRead,
    CacheWrite,
    CacheUpload,
    DepthPrepare,
    NavigationSnapshot,
    Count
};
inline constexpr std::array<const char*, size_t(CpuMetric::Count)> CpuMetricNames = {
    "scene_update",
    "scene_sort",
    "scene_render",
    "navigation",
    "world_stream",
    "collision_refresh",
    "save_game",
    "cache_read",
    "cache_write",
    "cache_upload",
    "depth_prepare",
    "navigation_snapshot"};

struct CpuProfileCounters
{
    std::array<double, size_t(CpuMetric::Count)> milliseconds{};
    std::array<uint64_t, size_t(CpuMetric::Count)> calls{};
};

inline thread_local CpuProfileCounters CpuProfile;

inline double ProfileMilliseconds(ProfileClock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

inline void RecordCpuMetric(CpuMetric metric, ProfileClock::time_point start)
{
    if (ProfilingEnabled)
    {
        const auto index = size_t(metric);
        CpuProfile.milliseconds[index] += ProfileMilliseconds(ProfileClock::now() - start);
        ++CpuProfile.calls[index];
    }
}

class CpuProfileScope
{
  public:

    explicit CpuProfileScope(CpuMetric metric)
        : metric_(metric),
          start_(ProfilingEnabled ? ProfileClock::now() : ProfileClock::time_point{})
    {
    }

    ~CpuProfileScope()
    {
        RecordCpuMetric(metric_, start_);
    }

    CpuProfileScope(const CpuProfileScope&) = delete;
    CpuProfileScope& operator=(const CpuProfileScope&) = delete;

  private:

    CpuMetric metric_;
    ProfileClock::time_point start_;
};

struct GameProfileState
{
    uint64_t seed = 0;
    double playerX = 0, playerY = 0;
    float zoom = 1;
    size_t actors = 0, chunks = 0, enemies = 0, projectiles = 0, loot = 0;
    bool paused = false, scan = false, boss = false, cleared = false;
    int level = 1;
};
