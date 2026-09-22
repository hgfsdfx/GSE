#include "stdafx.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
using GameMath::Direction;
using GameMath::Distance;

int NavigationGrid::CellAt(WorldPoint point) const
{
    const double x = point.x - flowOrigin_.x;
    const double y = point.y - flowOrigin_.y;
    if (x < 0 || y < 0 || x >= DistrictSize || y >= DistrictSize)
    {
        return -1;
    }
    return int(y / CellSize) * GridSide + int(x / CellSize);
}

WorldPoint NavigationGrid::CellCenter(int cell) const
{
    return {flowOrigin_.x + (cell % GridSide + .5) * CellSize,
            flowOrigin_.y + (cell / GridSide + .5) * CellSize};
}

bool NavigationGrid::ClearPath(const World& world, WorldPoint a, WorldPoint b) const
{
    const int steps = std::max(1, int(std::ceil(Distance(a, b) / 6)));
    for (int i = 0; i <= steps; ++i)
    {
        const double t = double(i) / steps;
        if (!world.CanWalk({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}))
        {
            return false;
        }
    }
    return true;
}

void NavigationGrid::BuildFlow(const World& world, WorldPoint player)
{
    // Keep a bounded navigation grid around the player throughout the endless city.
    flowOrigin_ = {std::floor(player.x / CellSize) * CellSize - DistrictSize / 2,
                   std::floor(player.y / CellSize) * CellSize - DistrictSize / 2};
    distance_.fill(-1);
    int goal = -1;
    double closest = 90;
    for (int cell = 0; cell < GridSide * GridSide; ++cell)
    {
        const auto position = CellCenter(cell);
        walkable_[cell] = world.CanWalk(position);
        const double distance = Distance(position, player);
        if (walkable_[cell] && distance < closest && ClearPath(world, position, player))
        {
            goal = cell;
            closest = distance;
        }
    }
    if (goal < 0)
    {
        return;
    }
    std::queue<int> queue;
    queue.push(goal);
    distance_[goal] = 0;
    while (!queue.empty())
    {
        const int cell = queue.front();
        queue.pop();
        const int x = cell % GridSide, y = cell / GridSide;
        for (const auto& offset : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
        {
            const int nx = x + offset.first, ny = y + offset.second;
            if (nx < 0 || ny < 0 || nx >= GridSide || ny >= GridSide)
            {
                continue;
            }
            const int next = ny * GridSide + nx;
            if (walkable_[next] && distance_[next] < 0
                && ClearPath(world, CellCenter(cell), CellCenter(next)))
            {
                distance_[next] = distance_[cell] + 1;
                queue.push(next);
            }
        }
    }
}

WorldPoint NavigationGrid::FollowFlow(WorldPoint from, WorldPoint player, const World& world) const
{
    if (Distance(from, player) < 450 && ClearPath(world, from, player))
    {
        return player;
    }
    const int cell = CellAt(from);
    if (cell < 0)
    {
        return from;
    }
    int best = -1;
    int distance = std::numeric_limits<int>::max();
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int nx = cell % GridSide + x, ny = cell / GridSide + y;
            if (nx < 0 || ny < 0 || nx >= GridSide || ny >= GridSide)
            {
                continue;
            }
            const int candidate = ny * GridSide + nx;
            if (distance_[candidate] >= 0 && distance_[candidate] < distance
                && ClearPath(world, from, CellCenter(candidate)))
            {
                distance = distance_[candidate];
                best = candidate;
            }
        }
    }
    return best >= 0 ? CellCenter(best) : from;
}

void NavigationGrid::MoveActor(WorldPoint& point,
                               WorldPoint target,
                               float distance,
                               const World& world)
{
    const auto direction = Direction(point, target);
    distance = std::min(distance, float(Distance(point, target)));
    const int steps = std::max(1, int(std::ceil(distance / 4)));
    for (int i = 0; i < steps; ++i)
    {
        WorldPoint next{point.x + direction.x * distance / steps, point.y};
        if (world.CanWalk(next))
        {
            point = next;
        }
        next = {point.x, point.y + direction.y * distance / steps};
        if (world.CanWalk(next))
        {
            point = next;
        }
    }
}
