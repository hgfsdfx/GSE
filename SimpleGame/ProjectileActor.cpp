#include "stdafx.h"
#include "GameplayActors.h"
#include "SceneGraph.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
using GameMath::Direction;
using GameMath::Distance;

ProjectileActor::ProjectileActor(WorldPoint directionValue,
                                 uint64_t target,
                                 float damageValue,
                                 float range,
                                 bool hostileValue)
    : direction(directionValue),
      targetId(target),
      damage(damageValue),
      distanceLeft(range),
      hostile(hostileValue)
{

    updatePhase = UpdatePhase::Projectile;
}

void ProjectileActor::Update(float dt, ActorUpdateContext& context)
{
    if (context.player.stats.Dead())
    {
        return;
    }
    auto position = WorldPosition();
    if (!hostile)
    {
        for (const auto& enemy : context.scene.Actors<EnemyActor>())
        {
            if (enemy.persistentId == targetId && enemy.health > 0)
            {
                direction = Direction(position, enemy.WorldPosition());
                break;
            }
        }
    }
    const float travel = std::min(distanceLeft, (hostile ? 185.f : 620.f) * dt);
    const int steps = std::max(1, int(std::ceil(travel / 4)));
    for (int i = 0; i < steps && distanceLeft > 0 && !PendingDestroy(); ++i)
    {
        position.x += direction.x * travel / steps;
        position.y += direction.y * travel / steps;
        distanceLeft -= travel / steps;
        if (!context.world.CanWalk(position))
        {
            distanceLeft = 0;
            break;
        }
        if (hostile)
        {
            if (Distance(position, context.player.WorldPosition()) < 12)
            {
                context.events.Hurt(damage, context.player.WorldPosition());
                distanceLeft = 0;
            }
        }
        else
        {
            for (auto& enemy : context.scene.Actors<EnemyActor>())
            {
                if (context.scene.IsActive(enemy.Id()) && enemy.health > 0 && enemy.spawnGrace <= 0
                    && Distance(position, enemy.WorldPosition())
                           < (enemy.kind == EnemyKind::Boss ? 29 : 15))
                {
                    enemy.health -= damage;
                    enemy.hitFlash = .12f;
                    context.scene.SpawnAt<CombatNumberActor>(context.events.GameplayRoot(),
                                                             enemy.WorldPosition(),
                                                             int(damage));
                    if (enemy.health <= 0)
                    {
                        context.events.EnemyDefeated(enemy);
                    }
                    distanceLeft = 0;
                    break;
                }
            }
        }
    }
    SetWorldPosition(position);
    if (distanceLeft <= .001f)
    {
        Destroy();
    }
}
