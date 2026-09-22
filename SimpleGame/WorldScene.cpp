#include "stdafx.h"
#include "World.h"
#include "SceneGraph.h"
#include "WorldActors.h"

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
