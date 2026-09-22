#include "stdafx.h"
#include "GameplayActors.h"
#include "SceneGraph.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
using GameMath::Direction;
using GameMath::Distance;

LootActor::LootActor(LootKind value, int count)
    : kind(value),
      amount(count)
{

    updatePhase = UpdatePhase::Loot;
}

void LootActor::Update(float dt, ActorUpdateContext& context)
{
    const auto player = context.player.WorldPosition();
    auto position = WorldPosition();
    auto& stats = context.player.stats;
    if (Distance(position, player) <= stats.MagnetRadius())
    {
        attracted = true;
    }
    if (attracted)
    {
        const auto goal = context.navigation.FollowFlow(position, player, context.world);
        context.navigation.MoveActor(position,
                                     goal,
                                     (270.f + float(Distance(position, player))) * dt,
                                     context.world);
        SetWorldPosition(position);
    }
    if (Distance(position, player) > 18
        || !context.navigation.ClearPath(context.world, position, player))
    {
        return;
    }
    if (kind == LootKind::Semiconductor)
    {
        stats.chipsCollected_ = std::min(1000000, stats.chipsCollected_ + amount);
        context.events.GainExperience(amount);
    }
    else if (kind == LootKind::Upgrade)
    {
        const int applied = std::min(10 - stats.weaponRank_, amount);
        stats.weaponRank_ += applied;
        context.events.GainExperience((amount - applied) * 12);
        context.events.Say("스마트폰 강화 / 공격력·공격 속도 증가");
    }
    else if (stats.health_ < stats.MaxHealth())
    {
        const int heal = int(std::min(stats.MaxHealth() - stats.health_, float(amount)));
        stats.health_ = std::min(stats.MaxHealth(), stats.health_ + amount);
        context.scene.SpawnAt<CombatNumberActor>(context.events.GameplayRoot(), player, heal, true);
    }
    else
    {
        context.events.GainExperience(3);
    }
    Destroy();
}
