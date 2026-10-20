#include "stdafx.h"
#include "GameplayActors.h"
#include "SceneGraph.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include "Society.h"
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
    if (Society::InTownSafeZone(WorldPosition(), 24))
    {
        // Remove legacy saved enemies inside a town without granting combat rewards.
        Destroy();
        return;
    }
    hitFlash = std::max(0.f, hitFlash - dt);
    spawnGrace = std::max(0.f, spawnGrace - dt);
    if (health <= 0 || spawnGrace > 0 || context.player.stats.Dead())
    {
        return;
    }
    const auto player = context.player.WorldPosition();
    auto position = WorldPosition();
    auto moveOutsideTown = [&](WorldPoint goal, float distance)
    {
        auto next = position;
        context.navigation.MoveActor(next, goal, distance, context.world);
        if (!Society::BlocksMonsterTravel(position, next, 24))
        {
            position = next;
        }
    };
    if (kind != EnemyKind::Boss && npcThreat)
    {
        const auto npc = dynamic_cast<NpcActor*>(context.scene.Find(npcThreat));
        if (npc && npc->health > 0 && !Society::InTownSafeZone(npc->WorldPosition())
            && Distance(position, npc->WorldPosition()) < 450)
        {
            moveOutsideTown(npc->WorldPosition(), 65.f * dt);
            attackTimer -= dt;
            if (Distance(position, npc->WorldPosition()) < 24 && attackTimer <= 0)
            {
                npc->health = std::max(0.f, npc->health - 10);
                npc->flash = .15f;
                attackTimer = 1;
                if (npc->health <= 0)
                {
                    context.society.NpcDefeated(*npc, context);
                }
            }
            SetWorldPosition(position);
            return;
        }
        npcThreat = 0;
    }
    if (Society::InTownSafeZone(player))
    {
        return;
    }
    if (kind == EnemyKind::Boss)
    {
        const bool enraged = health < maxHealth * .5f;
        const WorldPoint goal{std::clamp(player.x, arenaMinimum.x, arenaMaximum.x),
                              std::clamp(player.y, arenaMinimum.y, arenaMaximum.y)};
        if (Distance(position, player) > 170)
        {
            moveOutsideTown(goal, (enraged ? 66.f : 48.f) * dt);
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
        moveOutsideTown(goal, (kind == EnemyKind::Armored ? 48.f : 75.f) * dt);
        if (Distance(position, player) < 21)
        {
            context.events.Hurt(kind == EnemyKind::Armored ? 14.f : 9.f, player);
        }
    }
    SetWorldPosition(position);
}
