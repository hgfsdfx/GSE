#include "stdafx.h"
#include "ProfileMetrics.h"
#include "World.h"
#include "SceneGraph.h"
#include "WorldActors.h"
#include <cmath>

World::WalkabilitySnapshot World::CaptureWalkability() const
{
    const CpuProfileScope profile(CpuMetric::NavigationSnapshot);
    WalkabilitySnapshot snapshot;
    for (const auto& entry : chunks_)
    {
        auto& buildings = snapshot.chunks_[entry.first];
        if (!scene_)
        {
            buildings = entry.second.buildings;
            continue;
        }
        const auto bucket = colliders_.find(entry.first);
        if (bucket == colliders_.end())
        {
            continue;
        }
        buildings.reserve(bucket->second.size());
        for (auto id : bucket->second)
        {
            const auto actor = dynamic_cast<const BuildingActor*>(scene_->Find(id));
            if (actor && scene_->IsActive(id))
            {
                buildings.push_back(actor->Geometry());
            }
        }
    }
    return snapshot;
}

bool World::WalkabilitySnapshot::CanWalk(WorldPoint point) const
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > CoordinateLimit
        || std::abs(point.y) > CoordinateLimit)
    {
        return false;
    }
    const ChunkKey key{static_cast<int64_t>(std::floor(point.x / ChunkSize)),
                       static_cast<int64_t>(std::floor(point.y / ChunkSize))};
    const auto chunk = chunks_.find(key);
    if (chunk == chunks_.end())
    {
        return false;
    }
    for (const auto& b : chunk->second)
    {
        if (point.x > b.x - 8 && point.x < b.x + b.width + 8 && point.y > b.y - 8
            && point.y < b.y + b.depth + 8)
        {
            return false;
        }
    }
    return true;
}

void World::BindScene(SceneGraph* scene)
{
    if (scene_ == scene)
    {
        return;
    }
    for (auto& entry : chunks_)
    {
        if (scene_)
        {
            scene_->Destroy(entry.second.actorRoot);
        }
        entry.second.actorRoot = 0;
        entry.second.buildingActors.clear();
        entry.second.deviceActors.clear();
    }
    scene_ = scene;
    if (scene_)
    {
        for (auto& entry : chunks_)
        {
            SpawnChunkActors(entry.second);
        }
    }
    RefreshColliders();
}

void World::SpawnChunkActors(Chunk& chunk)
{
    // Capture descriptors before actor-backed device lookup becomes active.
    const auto devices = Devices(chunk.key);
    auto& root = scene_->Spawn<ChunkActor>(0, chunk.key);
    chunk.actorRoot = root.Id();
    scene_->Spawn<GroundActor>(root.Id(), chunk.key);
    for (const auto& building : chunk.buildings)
    {
        chunk.buildingActors.push_back(
            scene_->Spawn<BuildingActor>(root.Id(), building, chunk.key).Id());
    }
    for (const auto& device : devices)
    {
        chunk.deviceActors.push_back(scene_->Spawn<DeviceActor>(root.Id(), device).Id());
    }
    scene_->Spawn<TrafficActor>(root.Id(), chunk.key);
}

void World::RefreshColliders()
{
    const CpuProfileScope profile(CpuMetric::CollisionRefresh);
    colliders_.clear();
    if (!scene_)
    {
        return;
    }
    for (const auto& actor : scene_->Actors<BuildingActor>())
    {
        if (!scene_->IsActive(actor.Id()))
        {
            continue;
        }
        const auto geometry = actor.Geometry();
        const auto first = KeyAt({geometry.x - 8, geometry.y - 8});
        const auto last = KeyAt({geometry.x + geometry.width + 8, geometry.y + geometry.depth + 8});
        for (auto y = first.y; y <= last.y; ++y)
        {
            for (auto x = first.x; x <= last.x; ++x)
            {
                colliders_[{x, y}].push_back(actor.Id());
            }
        }
    }
}
