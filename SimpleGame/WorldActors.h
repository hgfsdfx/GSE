#pragma once
#include "Actor.h"

class ChunkActor : public GroupActor
{
  public:

    explicit ChunkActor(ChunkKey key);
    ChunkKey key;
};

class GroundActor : public Actor
{
  public:

    explicit GroundActor(ChunkKey key);
    ChunkKey key;
};

class BuildingActor : public Actor
{
  public:

    BuildingActor(Building building, ChunkKey key);
    Building Geometry() const;
    double Depth() const override;
    bool Contains(WorldPoint point) const;
    Building shape;
    ChunkKey key;
};

class DeviceActor : public Actor
{
  public:

    explicit DeviceActor(Device device);
    Device Description() const;
    Device device;
};

class TrafficActor : public Actor
{
  public:

    explicit TrafficActor(ChunkKey key);
    void Update(float dt, ActorUpdateContext& context) override;

  private:

    float travel_ = 0;
};
