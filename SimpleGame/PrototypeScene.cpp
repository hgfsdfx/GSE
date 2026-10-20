#include "stdafx.h"
#include "Prototype.h"
#include <algorithm>

void Prototype::BindActorRenderers()
{
    actorRenderer_.Register<ModelActor>(
        [this](const ModelActor& actor)
        {
            DrawModelActor(actor);
        });
    actorRenderer_.Register<HouseActor>(
        [this](const HouseActor& house)
        {
            DrawHouse(house);
        });
    actorRenderer_.Register<NpcActor>(
        [this](const NpcActor& npc)
        {
            DrawNpc(npc);
        });
    actorRenderer_.Register<TownActor>(
        [this](const TownActor& town)
        {
            DrawTown(town);
        });
    actorRenderer_.Register<GroundActor>(
        [this](const GroundActor& actor)
        {
            Ground(actor);
        });
    actorRenderer_.Register<BuildingActor>(
        [this](const BuildingActor& actor)
        {
            DrawBuilding(actor.Geometry(), actor.key);
        });
    actorRenderer_.Register<DeviceActor>(
        [this](const DeviceActor& actor)
        {
            DrawDevice(actor.Description());
        });
    actorRenderer_.Register<TrafficActor>(
        [this](const TrafficActor& actor)
        {
            DrawTraffic(actor);
        });
    actorRenderer_.Register<PlayerActor>(
        [this](const PlayerActor& actor)
        {
            DrawPlayer(actor);
        });
    actorRenderer_.Register<SmartphoneActor>(
        [this](const SmartphoneActor& actor)
        {
            DrawSmartphone(actor);
        });
    actorRenderer_.Register<EnemyActor>(
        [this](const EnemyActor& actor)
        {
            DrawEnemyActor(actor);
        });
    actorRenderer_.Register<ProjectileActor>(
        [this](const ProjectileActor& actor)
        {
            DrawProjectileActor(actor);
        });
    actorRenderer_.Register<LootActor>(
        [this](const LootActor& actor)
        {
            DrawLootActor(actor);
        });
    actorRenderer_.Register<PulseActor>(
        [this](const PulseActor& actor)
        {
            DrawPulse(actor);
        });
    actorRenderer_.Register<CombatNumberActor>(
        [this](const CombatNumberActor& actor)
        {
            DrawCombatNumber(actor);
        });
    actorRenderer_.depth = [this](const Actor& actor)
    {
        return ActorDepth(actor);
    };
}

void Prototype::EnsurePresentationActors()
{
    auto& scene = level_.Scene();
    if (!scene.Actors<PresentationRootActor>().empty())
    {
        return;
    }
    const auto root = scene.Spawn<PresentationRootActor>(0).Id();
    scene.Spawn<PresentationActor>(root,
                                   RenderLayer::GroundEffect,
                                   [this]
                                   {
                                       DrawLevelGround();
                                   });
    scene.Spawn<PresentationActor>(root,
                                   RenderLayer::Marker,
                                   [this]
                                   {
                                       DrawMarkers();
                                   });
    scene.Spawn<PresentationActor>(root,
                                   RenderLayer::Hud,
                                   [this]
                                   {
                                       Hud();
                                   });
}

void Prototype::PrepareDepthBuildings()
{
    const CpuProfileScope profile(CpuMetric::DepthPrepare);
    // Snapshot once per Draw: actor transforms and visibility may change between frames.
    // Store values, not actor pointers that could outlive scene destruction.
    depthBuildings_.clear();
    const auto& scene = level_.Scene();
    for (const auto& actor : scene.Actors<BuildingActor>())
    {
        if (!scene.IsActive(actor.Id()) || !scene.IsVisible(actor.Id()))
        {
            continue;
        }
        const auto geometry = actor.Geometry();
        const DepthBuilding building{geometry, actor.Depth()};
        // Include both front-edge correction strips, including neighboring chunks.
        const auto first = world_.KeyAt({geometry.x - 8, geometry.y - 8});
        const auto last =
            world_.KeyAt({geometry.x + geometry.width + 55, geometry.y + geometry.depth + 55});
        for (auto y = first.y; y <= last.y; ++y)
        {
            for (auto x = first.x; x <= last.x; ++x)
            {
                depthBuildings_[{x, y}].push_back(building);
            }
        }
    }
}

double Prototype::ActorDepth(const Actor& actor) const
{
    double depth = actor.Depth();
    if (actor.renderLayer != RenderLayer::World || dynamic_cast<const BuildingActor*>(&actor))
    {
        return depth;
    }
    const auto point = actor.WorldPosition();
    const auto bucket = depthBuildings_.find(world_.KeyAt(point));
    if (bucket == depthBuildings_.end())
    {
        return depth;
    }
    for (const auto& building : bucket->second)
    {
        const auto& b = building.geometry;
        const bool south = point.y >= b.y + b.depth && point.y < b.y + b.depth + 55
                           && point.x >= b.x - 8 && point.x <= b.x + b.width + 8;
        const bool east = point.x >= b.x + b.width && point.x < b.x + b.width + 55
                          && point.y >= b.y - 8 && point.y <= b.y + b.depth + 8;
        if (south || east)
        {
            depth = std::max(depth, building.depth + 1);
        }
    }
    return depth;
}

void Prototype::DrawTraffic(const TrafficActor& actor)
{
    const auto p = actor.WorldPosition();
    auto quad = [&](double x, double y, double width, double depth, Color color)
    {
        r_.Quad(Project(x, y),
                Project(x + width, y),
                Project(x + width, y + depth),
                Project(x, y + depth),
                color);
    };
    quad(p.x, p.y, 24, 39, Color(.17f, .25f, .33f));
    quad(p.x + 2, p.y + 7, 20, 12, Color(.27f, .49f, .58f));
    r_.Line(Project(p.x, p.y + 39),
            Project(p.x + 24, p.y + 39),
            2,
            Color(.82f, .9f, .94f).Emissive(4));
    r_.Line(Project(p.x, p.y), Project(p.x + 24, p.y), 2, Color(.97f, .26f, .59f).Emissive(3));
}
