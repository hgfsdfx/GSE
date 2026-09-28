#pragma once
#include "ProfileMetrics.h"
#include <filesystem>
#include <memory>
#include <string>
class Renderer;
class Prototype;

class PerformanceLog
{
  public:

    PerformanceLog();
    ~PerformanceLog();
    PerformanceLog(const PerformanceLog&) = delete;
    PerformanceLog& operator=(const PerformanceLog&) = delete;
    void Start(const std::filesystem::path& executableDirectory, const Renderer& renderer);
    void RecordUpdate(double milliseconds, double rawDeltaSeconds);
    void RecordFrame(Renderer& renderer,
                     const Prototype& game,
                     ProfileClock::time_point presented,
                     double drawMs,
                     double swapMs);
    void Mark(const std::string& event, const std::string& detail = "");
    void Stop(Renderer* renderer);

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
