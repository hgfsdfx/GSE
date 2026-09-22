#include "stdafx.h"
#include "WorldActors.h"
#include "World.h"
#include <cmath>

ChunkActor::ChunkActor(ChunkKey value)
    : key(value)
{
    SetLocalPosition({key.x * World::ChunkSize, key.y * World::ChunkSize});
}

GroundActor::GroundActor(ChunkKey value)
    : key(value)
{
    renderLayer = RenderLayer::Ground;
}

BuildingActor::BuildingActor(Building building, ChunkKey chunk)
    : shape(building),
      key(chunk)
{
    SetLocalPosition(
        {building.x - key.x * World::ChunkSize, building.y - key.y * World::ChunkSize});
}

Building BuildingActor::Geometry() const
{
    auto geometry = shape;
    const auto position = WorldPosition();
    geometry.x = position.x;
    geometry.y = position.y;
    return geometry;
}

double BuildingActor::Depth() const
{
    return Actor::Depth() + shape.width + shape.depth;
}

bool BuildingActor::Contains(WorldPoint point) const
{
    const auto position = WorldPosition();
    return point.x > position.x - 8 && point.x < position.x + shape.width + 8
           && point.y > position.y - 8 && point.y < position.y + shape.depth + 8;
}

DeviceActor::DeviceActor(Device value)
    : device(value)
{
    SetLocalPosition({value.position.x - value.key.x * World::ChunkSize,
                      value.position.y - value.key.y * World::ChunkSize});
}

Device DeviceActor::Description() const
{
    auto value = device;
    value.position = WorldPosition();
    return value;
}

TrafficActor::TrafficActor(ChunkKey key)
{
    renderLayer = RenderLayer::GroundEffect;
    travel_ =
        float(World::Hash(static_cast<uint64_t>(key.x) ^ World::Hash(static_cast<uint64_t>(key.y)))
              % 600);
    SetLocalPosition({18, travel_});
}

void TrafficActor::Update(float dt, ActorUpdateContext&)
{
    travel_ = std::fmod(travel_ + dt * 65, 620.f);
    SetLocalPosition({18, travel_});
}
