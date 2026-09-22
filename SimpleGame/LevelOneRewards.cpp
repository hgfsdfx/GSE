#include "stdafx.h"
#include "LevelOne.h"
#include <algorithm>

void LevelOne::Hurt(float damage, WorldPoint position)
{
    auto& stats = Player().stats;
    if (stats.invulnerability_ > 0 || stats.Dead())
    {
        return;
    }
    stats.health_ = std::max(0.f, stats.health_ - damage);
    stats.invulnerability_ = .8f;
    scene_.SpawnAt<CombatNumberActor>(gameplayRoot_, position, int(damage));
    if (stats.Dead())
    {
        Say("신호 끊김 / R 키로 새로 시작");
    }
}

void LevelOne::EnemyDefeated(EnemyActor& enemy)
{
    if (enemy.PendingDestroy())
    {
        return;
    }
    DropLoot(enemy);
    enemy.Destroy();
    if (enemy.kind == EnemyKind::Boss)
    {
        Player().stats.invulnerability_ = std::max(Player().stats.invulnerability_, 3.f);
        for (auto& other : scene_.Actors<EnemyActor>())
        {
            other.Destroy();
        }
        for (auto& shot : scene_.Actors<ProjectileActor>())
        {
            shot.Destroy();
        }
        for (auto& pulse : scene_.Actors<PulseActor>())
        {
            pulse.Destroy();
        }
        spawnTimer_ = 6;
    }
}

void LevelOne::AddLoot(LootKind kind, WorldPoint point, int amount)
{
    if (Loot().size() >= 512)
    {
        for (auto& item : scene_.Actors<LootActor>())
        {
            if (item.kind == kind)
            {
                item.amount = std::min(1000000, item.amount + amount);
                return;
            }
        }
    }
    scene_.SpawnAt<LootActor>(gameplayRoot_, point, kind, amount);
}

void LevelOne::GainExperience(int amount)
{
    if (Player().stats.GainExperience(amount))
    {
        Say("레벨 상승 / 공격력·공격 속도·체력·자석 범위 증가");
    }
}

void LevelOne::DropLoot(const EnemyActor& enemy)
{
    if (enemy.kind == EnemyKind::Boss)
    {
        cleared_ = true;
        AddLoot(LootKind::Semiconductor, enemy.WorldPosition(), 150);
        AddLoot(LootKind::Upgrade, enemy.WorldPosition(), 2);
        AddLoot(LootKind::Medkit, enemy.WorldPosition(), 100);
        Say("레벨 1 완료 / 감시자-01 무력화");
        return;
    }
    Player().stats.kills_ = std::min(1000000, Player().stats.kills_ + 1);
    AddLoot(LootKind::Semiconductor,
            enemy.WorldPosition(),
            enemy.kind == EnemyKind::Armored ? 14 : 8);
    if (Player().stats.kills_ == 3 || Player().stats.kills_ % 5 == 0)
    {
        AddLoot(LootKind::Upgrade, enemy.WorldPosition(), 1);
    }
    if (Player().stats.kills_ == 5 || Player().stats.kills_ % 7 == 0 || Random() % 12 == 0)
    {
        AddLoot(LootKind::Medkit, enemy.WorldPosition(), 32);
    }
}
