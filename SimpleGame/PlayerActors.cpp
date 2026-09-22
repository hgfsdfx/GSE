#include "stdafx.h"
#include "GameplayActors.h"
#include "SceneGraph.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
using GameMath::Direction;
using GameMath::Distance;

PlayerActor::PlayerActor()
{
    updatePhase = UpdatePhase::Player;
}

void PlayerActor::Update(float dt, ActorUpdateContext& context)
{
    stats.invulnerability_ = std::max(0.f, stats.invulnerability_ - dt);
    stats.fireTimer_ = std::max(0.f, stats.fireTimer_ - dt);
    const double length = std::hypot(input.x, input.y);
    if (length <= 0 || stats.Dead())
    {
        return;
    }
    const double x = input.x / length, y = input.y / length;
    const double speed = stats.MovementSpeed() * (running ? 1.53 : 1.0);
    auto position = WorldPosition();
    const WorldPoint target{position.x + (x + y) * .70710678 * speed * dt,
                            position.y + (y - x) * .70710678 * speed * dt};
    context.navigation.MoveActor(position,
                                 target,
                                 float(Distance(position, target)),
                                 context.world);
    SetWorldPosition(position);
}

SmartphoneActor::SmartphoneActor()
{
    updatePhase = UpdatePhase::Weapon;
}

void SmartphoneActor::Update(float, ActorUpdateContext& context)
{
    auto owner = dynamic_cast<PlayerActor*>(context.scene.Find(Parent()));
    if (!owner)
    {
        return;
    }
    auto& stats = owner->stats;
    if (stats.fireTimer_ > 0 || stats.Dead())
    {
        return;
    }
    const auto position = WorldPosition();
    const EnemyActor* closest = nullptr;
    double distance = stats.Range();
    for (const auto& enemy : context.scene.Actors<EnemyActor>())
    {
        const double candidate = Distance(position, enemy.WorldPosition());
        if (context.scene.IsActive(enemy.Id()) && enemy.health > 0 && enemy.spawnGrace <= 0
            && candidate <= distance
            && context.navigation.ClearPath(context.world, position, enemy.WorldPosition()))
        {
            closest = &enemy;
            distance = candidate;
        }
    }
    if (closest)
    {
        context.scene.SpawnAt<ProjectileActor>(context.events.GameplayRoot(),
                                               position,
                                               Direction(position, closest->WorldPosition()),
                                               closest->persistentId,
                                               stats.Damage(),
                                               stats.Range(),
                                               false);
        stats.fireTimer_ = stats.FireInterval();
    }
}
