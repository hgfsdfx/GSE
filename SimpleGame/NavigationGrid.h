#pragma once
#include "World.h"
#include <array>

class NavigationGrid
{
  public:

    static constexpr int GridSide = 48;
    static constexpr double CellSize = 40;
    static constexpr double DistrictSize = GridSide * CellSize;
    void BuildFlow(const World& world, WorldPoint player);
    int CellAt(WorldPoint point) const;
    WorldPoint CellCenter(int cell) const;
    bool ClearPath(const World& world, WorldPoint a, WorldPoint b) const;
    WorldPoint FollowFlow(WorldPoint from, WorldPoint player, const World& world) const;
    void MoveActor(WorldPoint& point, WorldPoint target, float distance, const World& world);

    bool Reachable(int cell) const
    {
        return cell >= 0 && cell < GridSide * GridSide && distance_[cell] >= 0;
    }

  private:

    std::array<int, GridSide * GridSide> distance_{};
    std::array<bool, GridSide * GridSide> walkable_{};
    WorldPoint flowOrigin_;
};
