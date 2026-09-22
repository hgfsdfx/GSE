#pragma once
#include "WorldTypes.h"
#include <cmath>

namespace GameMath
{
    inline double Distance(WorldPoint a, WorldPoint b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    inline WorldPoint Direction(WorldPoint from, WorldPoint to)
    {
        const double length = Distance(from, to);
        return length < .001 ? WorldPoint{}
                             : WorldPoint{(to.x - from.x) / length, (to.y - from.y) / length};
    }
} // namespace GameMath
