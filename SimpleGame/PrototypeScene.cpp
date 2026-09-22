#include "stdafx.h"
#include "Prototype.h"
#include <algorithm>

void Prototype::BindActorRenderers()
{
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

double Prototype::ActorDepth(const Actor& actor) const
{
    double depth = actor.Depth();
    if (actor.renderLayer != RenderLayer::World || dynamic_cast<const BuildingActor*>(&actor))
    {
        return depth;
    }
    const auto point = actor.WorldPosition();
    for (const auto& building : depthBuildings_)
    {
        if (!level_.Scene().IsActive(building.Id()) || !level_.Scene().IsVisible(building.Id()))
        {
            continue;
        }
        const auto b = building.Geometry();
        const bool south = point.y >= b.y + b.depth && point.y < b.y + b.depth + 55
                           && point.x >= b.x - 8 && point.x <= b.x + b.width + 8;
        const bool east = point.x >= b.x + b.width && point.x < b.x + b.width + 55
                          && point.y >= b.y - 8 && point.y <= b.y + b.depth + 8;
        if (south || east)
        {
            depth = std::max(depth, building.Depth() + 1);
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
