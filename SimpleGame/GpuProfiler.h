#pragma once
#include "Dependencies/glew.h"
#include <array>
#include <cstdint>
#include <vector>

// No waits or glFinish: a full pool skips a measurement, never the rendering.
class GpuProfiler
{
  public:

    struct Sample
    {
        uint64_t frame = 0, batch = 0;
        const char* stage = "";
        double milliseconds = 0;
    };

    ~GpuProfiler();
    void Initialize();
    bool Begin(uint64_t frame, uint64_t batch, const char* stage);
    void End();
    std::vector<Sample> Collect();

    bool Available() const
    {
        return available_;
    }

    size_t Pending() const;

  private:

    struct Slot
    {
        GLuint query = 0;
        bool pending = false;
        Sample sample;
    };

    std::array<Slot, 128> slots_{};
    int active_ = -1;
    bool available_ = false;
};
