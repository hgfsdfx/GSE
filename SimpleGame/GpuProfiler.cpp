#include "stdafx.h"
#include "GpuProfiler.h"
#include "ProfileMetrics.h"

GpuProfiler::~GpuProfiler()
{
    for (auto& slot : slots_)
    {
        if (slot.query)
        {
            glDeleteQueries(1, &slot.query);
        }
    }
}

void GpuProfiler::Initialize()
{
    if (!ProfilingEnabled)
    {
        return;
    }
    GLint bits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
    available_ = bits > 0;
    if (!available_)
    {
        return;
    }
    for (auto& slot : slots_)
    {
        glGenQueries(1, &slot.query);
    }
}

bool GpuProfiler::Begin(uint64_t frame, uint64_t batch, const char* stage)
{
    if (!available_ || active_ >= 0)
    {
        return false;
    }
    for (size_t i = 0; i < slots_.size(); ++i)
    {
        auto& slot = slots_[i];
        if (!slot.pending && slot.query)
        {
            slot.sample = {frame, batch, stage, 0};
            active_ = static_cast<int>(i);
            glBeginQuery(GL_TIME_ELAPSED, slot.query);
            return true;
        }
    }
    return false;
}

void GpuProfiler::End()
{
    if (active_ >= 0)
    {
        glEndQuery(GL_TIME_ELAPSED);
        slots_[active_].pending = true;
        active_ = -1;
    }
}

std::vector<GpuProfiler::Sample> GpuProfiler::Collect()
{
    std::vector<Sample> samples;
    if (!available_)
    {
        return samples;
    }
    for (auto& slot : slots_)
    {
        if (!slot.pending)
        {
            continue;
        }
        GLint ready = GL_FALSE;
        glGetQueryObjectiv(slot.query, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready)
        {
            continue;
        }
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(slot.query, GL_QUERY_RESULT, &nanoseconds);
        slot.sample.milliseconds = double(nanoseconds) / 1000000.0;
        samples.push_back(slot.sample);
        slot.pending = false;
    }
    return samples;
}

size_t GpuProfiler::Pending() const
{
    size_t pending = 0;
    for (const auto& slot : slots_)
    {
        pending += slot.pending ? 1 : 0;
    }
    return pending;
}
