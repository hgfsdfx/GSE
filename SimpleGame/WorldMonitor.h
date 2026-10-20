#pragma once
#include <filesystem>
#include <memory>
class LevelOne;
class World;

// Only immutable JSON crosses the thread boundary; the worker never accesses actors.
class WorldMonitor
{
  public:

    WorldMonitor();
    ~WorldMonitor();
    WorldMonitor(const WorldMonitor&) = delete;
    WorldMonitor& operator=(const WorldMonitor&) = delete;
    void Start(const std::filesystem::path& directory);
    void Tick(const LevelOne& level, const World& world, bool paused);

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
