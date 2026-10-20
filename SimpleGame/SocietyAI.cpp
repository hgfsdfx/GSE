#include "stdafx.h"
#include "Society.h"
#include "SceneGraph.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
using GameMath::Distance;

void Society::SetDestination(NpcActor& npc, WorldPoint point)
{
    const auto from = npc.WorldPosition();
    const double roadX = std::floor(from.x / World::ChunkSize) * World::ChunkSize + 90;
    const double roadY = std::floor(from.y / World::ChunkSize) * World::ChunkSize + 90;
    const double targetX = std::floor(point.x / World::ChunkSize) * World::ChunkSize + 90;
    npc.destination = point;
    npc.goalAssigned = true;
    // The procedural generator reserves these road lanes in every chunk.
    npc.route = {{{roadX, from.y}, {roadX, roadY}, {targetX, roadY}, {targetX, point.y}, point}};
    npc.routeStep = 0;
    if (std::abs(from.x - point.x) < 1 || std::abs(from.y - point.y) < 1)
    {
        npc.routeStep = 4;
    }
    npc.work = 0;
}

bool Society::Travel(NpcActor& npc, float dt, ActorUpdateContext& context)
{
    auto point = npc.WorldPosition();
    if (npc.againstPlayer)
    {
        const auto target = context.player.WorldPosition();
        const auto goal = context.navigation.FollowFlow(point, target, context.world);
        context.navigation.MoveActor(point, goal, 100.f * dt, context.world);
        npc.SetWorldPosition(point);
        return Distance(point, target) < 65;
    }
    while (npc.routeStep < int(npc.route.size()) && Distance(point, npc.route[npc.routeStep]) < 5)
    {
        ++npc.routeStep;
    }
    if (npc.routeStep >= int(npc.route.size()))
    {
        return true;
    }
    const auto goal = npc.route[npc.routeStep];
    const double distance = Distance(point, goal);
    const double step = std::min(distance, (npc.job == NpcJob::Raider ? 100.0 : 78.0) * dt);
    WorldPoint next = point;
    const WorldPoint proposed{point.x + (goal.x - point.x) * step / distance,
                              point.y + (goal.y - point.y) * step / distance};
    const bool currentLoaded = context.world.Chunks().count(context.world.KeyAt(point)) != 0;
    const bool nextLoaded = context.world.Chunks().count(context.world.KeyAt(proposed)) != 0;
    // Outside loaded chunks, actors continue on the same reserved road network.
    // A loaded chunk always uses actual collision, so changed buildings cannot be crossed.
    if (!currentLoaded || !nextLoaded)
    {
        if (!nextLoaded || context.world.CanWalk(proposed))
        {
            next = proposed;
        }
    }
    else
    {
        context.navigation.MoveActor(next, goal, float(step), context.world);
    }
    npc.SetWorldPosition(next);
    return false;
}

bool Society::TryStartPlayerCombat(NpcActor& npc, ActorUpdateContext& context)
{
    if (npc.againstPlayer || npc.identity == 4 || context.player.stats.Dead()
        || npc.health < npc.MaxHealth() * .4f
        || Distance(npc.WorldPosition(), context.player.WorldPosition()) > PlayerPkDetectionRange)
    {
        return false;
    }
    const bool plunderer = npc.plunderWeight > .7f;
    const bool pkOpponent = pkEnabled && (npc.job == NpcJob::Hunter || npc.job == NpcJob::Raider);
    const bool pursuingGuard = npc.job == NpcJob::Guard && wanted >= 25;
    if (!plunderer && !pkOpponent && !pursuingGuard)
    {
        return false;
    }
    // Player detection is independent of NPC work completion and NPC-vs-NPC combat.
    npc.target = 0;
    npc.againstPlayer = true;
    npc.action = NpcAction::Raid;
    SetDestination(npc, context.player.WorldPosition());
    npc.think = .5f;
    Log(npc.Name() + " / 고스트와 PK 교전 시작");
    context.events.Say(npc.Name() + " 교전 / 스마트폰 자동 반격 · H 수동 공격");
    return true;
}

void Society::Decide(NpcActor& npc, ActorUpdateContext& context)
{
    ++npc.decisions;
    npc.target = 0;
    npc.againstPlayer = false;
    npc.destinationTown = npc.home;
    const float deficit = 1 - npc.health / npc.MaxHealth();
    NpcAction choice = NpcAction::Hunt;
    const float financialNeed = npc.credits < 40 ? 1.6f : npc.credits < 100 ? 1.f : .6f;
    float best = .6f * npc.wealthWeight * financialNeed + (npc.job == NpcJob::Hunter ? .9f : 0);
    auto consider = [&](NpcAction action, float utility)
    {
        if (utility > best)
        {
            choice = action;
            best = utility;
        }
    };
    consider(NpcAction::Rest, deficit * (2 + npc.survivalWeight));
    consider(NpcAction::Study, npc.insightWeight * .7f + (npc.job == NpcJob::Seeker ? .8f : 0));
    consider(NpcAction::Hack, npc.insightWeight * .5f + (npc.job == NpcJob::Hacker ? 1.1f : 0));
    if (npc.cargo > 0 || npc.job == NpcJob::Merchant)
    {
        consider(NpcAction::Trade,
                 npc.wealthWeight * financialNeed + (npc.job == NpcJob::Merchant ? 1.1f : .4f)
                     + std::min(2.f, npc.cargo * .25f));
    }
    const NpcActor* victim = nullptr;
    double value = 0;
    for (const auto& other : context.scene.Actors<NpcActor>())
    {
        if (other.Id() == npc.Id() || other.health <= 0 || !context.scene.IsActive(other.Id()))
        {
            continue;
        }
        const double distance = Distance(npc.WorldPosition(), other.WorldPosition());
        if (distance > 900)
        {
            continue;
        }
        const double score = npc.job == NpcJob::Guard
                                 ? (other.notoriety > 10 && distance < 450 ? other.notoriety : 0)
                                 : (other.credits + other.cargo * 8.0) / (1 + distance / 150);
        if (score > value)
        {
            value = score;
            victim = &other;
        }
    }
    if (victim)
    {
        consider(NpcAction::Raid, npc.plunderWeight * npc.wealthWeight * financialNeed * 2.4f);
        if (npc.job == NpcJob::Guard)
        {
            choice = NpcAction::Raid;
        }
    }
    if (npc.health < npc.MaxHealth() * .3f)
    {
        choice = NpcAction::Rest;
    }
    if (npc.identity == 4)
    {
        choice = npc.health < npc.MaxHealth() * .8f ? NpcAction::Rest : NpcAction::Trade;
    }
    if (choice == NpcAction::Trade && npc.job == NpcJob::Merchant && npc.identity != 4
        && npc.cargo == 0 && npc.credits < BuyPrice(0))
    {
        choice = NpcAction::Hunt;
    }
    npc.action = choice;
    auto destination = npc.HomePosition();
    if (choice == NpcAction::Raid)
    {
        npc.target = victim ? victim->Id() : 0;
        destination = npc.againstPlayer ? context.player.WorldPosition() : victim->WorldPosition();
        Log(npc.Name() + " 추격 시작 / "
            + (npc.againstPlayer ? std::string("고스트") : victim->Name()));
    }
    else if (choice == NpcAction::Hunt && npc.job != NpcJob::Guard)
    {
        destination = TownCenter(npc.home);
        destination.y += TownSafeRadius + 180 + 140 * ((npc.decisions + npc.identity) % 3);
        destination.x += npc.identity > LegacyPopulation ? 24 : -24;
    }
    else if (choice == NpcAction::Hunt && npc.job == NpcJob::Guard)
    {
        const WorldPoint patrol[] = {{-520, 0}, {0, -520}, {520, 0}, {0, 520}};
        const auto offset = patrol[(npc.decisions + npc.identity) % 4];
        destination = TownCenter(npc.home);
        destination.x += offset.x;
        destination.y += offset.y;
    }
    else if (choice == NpcAction::Trade && npc.job == NpcJob::Merchant && npc.identity != 4)
    {
        const int currentTown = NearbyTown(npc.WorldPosition());
        npc.destinationTown = npc.cargo > 0
                                  ? NeighborTown(currentTown >= 0 ? currentTown : npc.home,
                                                 npc.decisions + npc.identity)
                                  : npc.home;
        destination = TownCenter(npc.destinationTown);
        destination.x += npc.HomePosition().x - TownCenter(npc.home).x;
        destination.y += npc.HomePosition().y - TownCenter(npc.home).y;
    }
    else if (choice == NpcAction::Hack)
    {
        npc.destinationTown = int((npc.home + npc.decisions / 2) % TownCount);
        destination = TownCenter(npc.destinationTown);
        destination.x += npc.identity > LegacyPopulation ? 24 : -24;
        destination.y += TownSafeRadius + 140 + 140 * (npc.decisions % 2);
    }
    else if (choice == NpcAction::Study)
    {
        npc.destinationTown = int((npc.home + npc.decisions) % TownCount);
        destination = TownCenter(npc.destinationTown);
        destination.x += (npc.decisions % 2 ? 160 : -160);
    }
    if (npc.identity == 4 && choice == NpcAction::Trade)
    {
        destination = TownCenter(0);
        destination.x += 24;
        destination.y += 24;
    }
    SetDestination(npc, destination);
    npc.think = 1;
    TryStartPlayerCombat(npc, context);
}

void Society::Respawn(NpcActor& npc, SceneGraph& scene)
{
    // Reuse the persistent actor: identity, job, accumulated insight and remaining wealth survive.
    npc.health = npc.MaxHealth();
    npc.recovery = 0;
    npc.notoriety = 0;
    npc.flash = 0;
    npc.attackCooldown = 1;
    npc.target = 0;
    npc.againstPlayer = false;
    npc.action = NpcAction::Rest;
    npc.destinationTown = npc.home;
    npc.SetWorldPosition(npc.HomePosition());
    SetDestination(npc, npc.HomePosition());
    npc.think = .1f * (npc.identity % 20);
    npc.goalAssigned = false;
    npc.playerScan = .5f;
    // End the old encounter instead of dragging enemies to the revived resident's home.
    for (auto& other : scene.Actors<NpcActor>())
    {
        if (other.target == npc.Id())
        {
            other.target = 0;
            if (!other.againstPlayer)
            {
                other.action = NpcAction::Rest;
                other.destinationTown = other.home;
                SetDestination(other, other.HomePosition());
            }
        }
    }
    for (auto& enemy : scene.Actors<EnemyActor>())
    {
        if (enemy.npcThreat == npc.Id())
        {
            enemy.npcThreat = 0;
        }
    }
    Log(npc.Name() + " / 거점에서 즉시 부활 · 체력 회복");
}

void Society::NpcDefeated(NpcActor& npc, ActorUpdateContext& context)
{
    Defeat(npc, nullptr, context);
}

void Society::Defeat(NpcActor& victim,
                     NpcActor* attacker,
                     ActorUpdateContext& context,
                     bool playerKill,
                     bool selfDefense)
{
    if (victim.health > 0)
    {
        return;
    }
    const int stolen = victim.credits / 2;
    const int goods = victim.cargo;
    victim.credits -= stolen;
    victim.cargo = 0;
    if (attacker)
    {
        attacker->credits = std::min(1000000, attacker->credits + stolen);
        attacker->cargo = std::min(10000, attacker->cargo + goods);
        attacker->insight = std::min(1000000, attacker->insight + 2);
        attacker->think = 0;
        ++raids;
        Log(attacker->Name() + " → " + victim.Name() + " / 약탈 " + std::to_string(stolen));
    }
    else if (playerKill)
    {
        credits = std::min(1000000, credits + stolen);
        cargo = std::min(10000, cargo + goods);
        ++raids;
        context.events.Say(std::string(selfDefense ? "반격 / " : "PK / ") + victim.Name()
                           + "에게서 " + std::to_string(stolen) + " 크레딧 약탈");
        Log("고스트가 " + victim.Name()
            + (selfDefense ? "을 제압 / 정당방위" : "을 제압 / 마을 경비가 수색합니다"));
    }
    else
    {
        Log(victim.Name() + " / 드론에게 제압당함");
    }
    Respawn(victim, context.scene);
}

void Society::Strike(NpcActor& attacker, NpcActor& victim, ActorUpdateContext& context)
{
    if (attacker.attackCooldown > 0 || victim.health <= 0)
    {
        return;
    }
    const float damage = 11 + std::min(20.f, attacker.insight * .15f);
    victim.health = std::max(0.f, victim.health - damage);
    victim.flash = .2f;
    attacker.attackCooldown = 1;
    if (attacker.job != NpcJob::Guard)
    {
        attacker.notoriety = std::min(100.f, attacker.notoriety + 6);
    }
    victim.target = attacker.Id();
    victim.againstPlayer = false;
    victim.action = NpcAction::Raid;
    victim.think = 0;
    if (victim.health <= 0)
    {
        Defeat(victim, &attacker, context);
    }
}

void Society::UpdateNpc(NpcActor& npc, float dt, ActorUpdateContext& context)
{
    npc.attackCooldown = std::max(0.f, npc.attackCooldown - dt);
    npc.flash = std::max(0.f, npc.flash - dt);
    npc.notoriety = std::max(0.f, npc.notoriety - dt * .2f);
    npc.think -= dt;
    if (npc.health <= 0)
    {
        if (npc.recovery > 0 || npc.action == NpcAction::Recover)
        {
            // Legacy corpses already paid their death penalty.
            Respawn(npc, context.scene);
        }
        else
        {
            Defeat(npc, nullptr, context);
        }
        return;
    }
    npc.playerScan -= dt;
    npc.migrationCooldown = std::max(0.f, npc.migrationCooldown - dt);
    npc.communicationTimer -= dt;
    if (npc.communicationTimer <= 0)
    {
        npc.communicationTimer = 5;
        Communicate(npc, context);
    }
    if (npc.playerScan <= 0)
    {
        npc.playerScan = .5f;
        TryStartPlayerCombat(npc, context);
    }
    if (npc.action == NpcAction::Raid)
    {
        auto victim = dynamic_cast<NpcActor*>(context.scene.Find(npc.target));
        const bool hasVictim = npc.againstPlayer ? !context.player.stats.Dead()
                                                 : victim && victim->health > 0
                                                       && context.scene.IsActive(victim->Id());
        if (!hasVictim || npc.health < npc.MaxHealth() * .25f
            || (npc.againstPlayer && npc.job == NpcJob::Guard && wanted < 25
                && Distance(npc.WorldPosition(), context.player.WorldPosition()) > 250)
            || (npc.againstPlayer
                && Distance(npc.WorldPosition(), context.player.WorldPosition()) > 1400))
        {
            npc.againstPlayer = false;
            npc.target = 0;
            npc.action = NpcAction::Rest;
            npc.destinationTown = npc.home;
            SetDestination(npc, npc.HomePosition());
            return;
        }
        const auto target =
            npc.againstPlayer ? context.player.WorldPosition() : victim->WorldPosition();
        const bool loaded = context.world.Chunks().count(context.world.KeyAt(target)) != 0;
        const bool canStrike =
            Distance(npc.WorldPosition(), target) < (npc.againstPlayer ? PlayerPkAttackRange : 65.f)
            && (!loaded
                || context.navigation.ClearPath(context.world, npc.WorldPosition(), target));
        if (canStrike)
        {
            if (npc.againstPlayer && npc.attackCooldown <= 0)
            {
                const float previousHealth = context.player.stats.Health();
                context.events.Hurt(10, target);
                npc.attackCooldown = 1;
                if (npc.job != NpcJob::Guard)
                {
                    npc.notoriety = std::min(100.f, npc.notoriety + 6);
                }
                const int loss =
                    context.player.stats.Health() < previousHealth ? std::min(credits, 3) : 0;
                credits -= loss;
                npc.credits = std::min(1000000, npc.credits + loss);
            }
            else if (!npc.againstPlayer)
            {
                Strike(npc, *victim, context);
            }
        }
        else
        {
            if (npc.think <= 0 && (npc.routeStep >= 5 || Distance(npc.destination, target) > 100))
            {
                SetDestination(npc, target);
                npc.think = 1.5f;
            }
            Travel(npc, dt, context);
        }
        return;
    }
    if (npc.action == NpcAction::Relocate)
    {
        if (Travel(npc, dt, context))
        {
            CompleteRelocation(npc, context);
        }
        return;
    }
    if (npc.migrationCooldown <= 0)
    {
        npc.migrationCooldown = 30.f + npc.identity % 20;
        if (TryRelocate(npc, context))
        {
            return;
        }
    }
    if (!npc.goalAssigned && npc.think <= 0)
    {
        Decide(npc, context);
    }
    if (npc.action == NpcAction::Hunt && npc.attackCooldown <= 0)
    {
        for (auto& enemy : context.scene.Actors<EnemyActor>())
        {
            if (enemy.kind == EnemyKind::Boss || enemy.health <= 0 || enemy.spawnGrace > 0
                || Distance(npc.WorldPosition(), enemy.WorldPosition()) > 180)
            {
                continue;
            }
            if (!context.navigation.ClearPath(context.world,
                                              npc.WorldPosition(),
                                              enemy.WorldPosition()))
            {
                continue;
            }
            enemy.health -= 18 + std::min(20.f, npc.insight * .1f);
            enemy.hitFlash = .15f;
            enemy.npcThreat = npc.Id();
            npc.attackCooldown = 1;
            if (enemy.health <= 0)
            {
                enemy.Destroy();
                npc.cargo = std::min(10000, npc.cargo + 3);
                npc.insight = std::min(1000000, npc.insight + 2);
                ++hunts;
                Log(npc.Name() + " 드론 사냥 성공 / 부품 +3");
            }
            break;
        }
    }
    if (!Travel(npc, dt, context))
    {
        return;
    }
    npc.work += dt;
    const float workDuration = npc.action == NpcAction::Rest || npc.identity == 4 ? 6.f : 3.f;
    if (npc.work < workDuration)
    {
        return;
    }
    npc.work = 0;
    auto town = Town(context.scene, npc.destinationTown);
    if (!town)
    {
        return;
    }
    switch (npc.action)
    {
        case NpcAction::Rest:
            if (npc.health < npc.MaxHealth() && npc.credits >= 4 && town->stock > 0
                && town->treasury <= 999996)
            {
                npc.credits -= 4;
                town->treasury += 4;
                --town->stock;
                npc.health = std::min(npc.MaxHealth(), npc.health + 28);
            }
            else
            {
                npc.health = std::min(npc.MaxHealth(), npc.health + 8);
            }
            break;
        case NpcAction::Hunt:
            if (npc.job == NpcJob::Guard)
            {
                npc.insight = std::min(1000000, npc.insight + 1);
                break;
            }
            // Repeatable wilderness salvage is also simulated beyond streamed chunks.
            npc.cargo = std::min(10000, npc.cargo + 1);
            npc.insight = std::min(1000000, npc.insight + 1);
            npc.health = std::max(1.f, npc.health - 3);
            break;
        case NpcAction::Trade:
            if (npc.cargo > 0)
            {
                const int count =
                    std::min({npc.cargo,
                              4,
                              town->treasury / SellPrice(npc.destinationTown),
                              10000 - town->stock,
                              (1000000 - npc.credits) / SellPrice(npc.destinationTown)});
                if (count > 0)
                {
                    const int amount = count * SellPrice(npc.destinationTown);
                    npc.cargo -= count;
                    npc.credits += amount;
                    town->treasury -= amount;
                    town->stock += count;
                    ++trades;
                    Log(npc.Name() + " / " + TownName(npc.destinationTown) + " 부품 판매 "
                        + std::to_string(amount));
                }
            }
            else if (npc.job == NpcJob::Merchant && npc.identity != 4)
            {
                const int count =
                    std::min({4,
                              town->stock,
                              npc.credits / BuyPrice(npc.destinationTown),
                              (1000000 - town->treasury) / BuyPrice(npc.destinationTown)});
                if (count > 0)
                {
                    const int amount = count * BuyPrice(npc.destinationTown);
                    npc.credits -= amount;
                    npc.cargo += count;
                    town->treasury += amount;
                    town->stock -= count;
                    ++trades;
                }
            }
            break;
        case NpcAction::Hack:
            if (town->treasury >= 8 && npc.credits <= 999992)
            {
                town->treasury -= 8;
                npc.credits += 8;
                npc.insight = std::min(1000000, npc.insight + 2);
                const auto key = context.world.KeyAt(npc.destination);
                if (!(key == ChunkKey{0, 0}) || story >= 4)
                {
                    context.world.Change(key).cameraOff = true;
                }
                Log(npc.Name() + " / 루멘 감시망 교란 의뢰 수행");
            }
            break;
        case NpcAction::Study:
            if (npc.credits >= 2 && town->treasury <= 999998)
            {
                npc.credits -= 2;
                town->treasury += 2;
                npc.insight = std::min(1000000, npc.insight + 3);
            }
            else
            {
                npc.insight = std::min(1000000, npc.insight + 1);
            }
            break;
        default:
            break;
    }
    // A staggered work cycle selects goals from health, wealth, insight and raid weights.
    Decide(npc, context);
}
