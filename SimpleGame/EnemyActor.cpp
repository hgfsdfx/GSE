#include "stdafx.h"
#include "GameplayActors.h"
#include "SceneGraph.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
using GameMath::Direction;
using GameMath::Distance;

EnemyActor::EnemyActor()
{
    updatePhase = UpdatePhase::Enemy;
}

void EnemyActor::Update(float dt, ActorUpdateContext& context)
{
    hitFlash = std::max(0.f, hitFlash - dt);
    spawnGrace = std::max(0.f, spawnGrace - dt);
    if (health <= 0 || spawnGrace > 0 || context.player.stats.Dead())
    {
        return;
    }
    const auto player = context.player.WorldPosition();
    auto position = WorldPosition();
    if (kind == EnemyKind::Boss)
    {
        const bool enraged = health < maxHealth * .5f;
        const WorldPoint goal{std::clamp(player.x, arenaMinimum.x, arenaMaximum.x),
                              std::clamp(player.y, arenaMinimum.y, arenaMaximum.y)};
        if (Distance(position, player) > 170)
        {
            context.navigation.MoveActor(position,
                                         goal,
                                         (enraged ? 66.f : 48.f) * dt,
                                         context.world);
        }
        attackTimer -= dt;
        pulseTimer -= dt;
        if (attackTimer <= 0 && Distance(position, player) < 650
            && context.navigation.ClearPath(context.world, position, player))
        {
            const double angle = std::atan2(player.y - position.y, player.x - position.x);
            const int spread = enraged ? 3 : 2;
            for (int i = -spread; i <= spread; ++i)
            {
                const double a = angle + i * .20;
                context.scene.SpawnAt<ProjectileActor>(context.events.GameplayRoot(),
                                                       position,
                                                       WorldPoint{std::cos(a), std::sin(a)},
                                                       0,
                                                       enraged ? 16.f : 12.f,
                                                       520.f,
                                                       true);
            }
            attackTimer = enraged ? 2.1f : 3.1f;
        }
        if (pulseTimer <= 0 && Distance(position, player) < 480)
        {
            context.scene.SpawnAt<PulseActor>(context.events.GameplayRoot(),
                                              player,
                                              1.3f,
                                              enraged ? 120.f : 100.f);
            pulseTimer = enraged ? 4.8f : 7.f;
        }
        if (Distance(position, player) < 37)
        {
            context.events.Hurt(20, player);
        }
    }
    else
    {
        const auto goal = context.navigation.FollowFlow(position, player, context.world);
        context.navigation.MoveActor(position,
                                     goal,
                                     (kind == EnemyKind::Armored ? 48.f : 75.f) * dt,
                                     context.world);
        if (Distance(position, player) < 21)
        {
            context.events.Hurt(kind == EnemyKind::Armored ? 14.f : 9.f, player);
        }
    }
    SetWorldPosition(position);
}
