#include "stdafx.h"
#include "Renderer.h"
#include <algorithm>
#include <cmath>

bool Renderer::DrawModel(const ModelAppearance& appearance,
                         Point origin,
                         ModelMath::Vec3 scale,
                         float yaw,
                         const std::string& clip,
                         float seconds,
                         bool loop,
                         float emissionScale,
                         Color tint)
{
    if (recording_ || appearance.parts.empty() || !IsInitialized())
    {
        return false;
    }
    for (float value : scale)
    {
        if (!std::isfinite(value) || value <= 0)
        {
            return false;
        }
    }
    if (!std::isfinite(yaw) || !std::isfinite(origin.x) || !std::isfinite(origin.y)
        || !std::isfinite(emissionScale))
    {
        return false;
    }
    size_t instances = 0, triangles = 0;
    for (const auto& part : appearance.parts)
    {
        for (const auto& surface : part.asset->surfaces)
        {
            ++instances;
            triangles += surface.vertices.size() / 3;
        }
    }
    if (instances > maxInstances_ || triangles > MaxBatchTriangles)
    {
        return false;
    }
    const auto texels = models_.RequiredTexels(appearance);
    if (instances_.size() + instances > maxInstances_
        || triangles_.size() + triangles > MaxBatchTriangles || !models_.Fits(texels))
    {
        Flush("model_batch_limit");
    }
    if (!models_.Fits(texels))
    {
        return false;
    }
    auto& surfaces = modelSurfaces_;
    surfaces.clear();
    surfaces.reserve(instances);
    if (!models_.Prepare(appearance, clip, seconds, loop, surfaces))
    {
        return false;
    }
    const float depth = float(instances_.size());
    float radius = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        radius +=
            (std::max(std::abs(appearance.minimum[axis]), std::abs(appearance.maximum[axis])) + 2.f)
            * scale[axis];
    }
    for (const auto& surface : surfaces)
    {
        const auto index = static_cast<uint32_t>(instances_.size());
        instances_.push_back({origin.x,
                              origin.y,
                              std::max(radius, .001f),
                              .25f,
                              scale[0] / scale[1],
                              scale[2] / scale[1],
                              depth,
                              float(surface.palette + 1),
                              tint.r,
                              tint.g,
                              tint.b,
                              tint.a,
                              std::max(0.f, emissionScale),
                              scale[1],
                              std::cos(yaw),
                              std::sin(yaw)});
        ++instanceCount_;
        const auto offset = triangles_.size();
        const auto count = surface.count / 3;
        triangles_.resize(offset + count);
        for (uint32_t triangle = 0; triangle < count; ++triangle)
        {
            triangles_[offset + triangle] = {surface.first + triangle * 3, index};
        }
    }
    return true;
}
