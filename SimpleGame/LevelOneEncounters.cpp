#include "stdafx.h"
#include "LevelOne.h"
#include "GameMath.h"
using GameMath::Distance;

void LevelOne::PopulateEnemies(const World& world, WorldPoint player)
{
    if (Dead())
    {
        return;
    }
    const bool hasBoss = Boss() != nullptr;
    const size_t minimum = hasBoss ? 3 : 6;
    const size_t limit = hasBoss ? BossEnemyLimit : EnemyLimit;
    std::vector<WorldPoint> occupied;
    size_t nearbyCount = 0;
    size_t regularCount = 0;
    for (auto& enemy : scene_.Actors<EnemyActor>())
    {
        const double distance = Distance(enemy.WorldPosition(), player);
        // Distant survivors must not consume the slots needed along the player's new route.
        if (enemy.kind != EnemyKind::Boss
            && (distance > 470 || Society::InTownSafeZone(enemy.WorldPosition(), 24)
                || regularCount >= limit - (hasBoss ? 1 : 0)))
        {
            enemy.Destroy();
            continue;
        }
        occupied.push_back(enemy.WorldPosition());
        if (enemy.kind != EnemyKind::Boss)
        {
            ++regularCount;
        }
        if (enemy.kind != EnemyKind::Boss && distance <= 300)
        {
            ++nearbyCount;
        }
    }
    if (Society::InTownSafeZone(player) || occupied.size() >= limit)
    {
        return;
    }
    std::vector<int> candidates;
    std::vector<int> nearbyCells;
    // Gather once per replenishment, not once per spawned enemy. Expensive collision
    // checks are only done for sampled cells; no per-cell line-of-sight raycasts.
    for (int cell = 0; cell < GridSide * GridSide; ++cell)
    {
        const auto position = navigation_.CellCenter(cell);
        const double distance = Distance(position, player);
        if (Society::InTownSafeZone(position, 24) || !navigation_.Reachable(cell) || distance < 140
            || distance > 470)
        {
            continue;
        }
        if (distance <= 300)
        {
            nearbyCells.push_back(cell);
        }
        else
        {
            candidates.push_back(cell);
        }
    }
    int spawned = 0;
    for (int attempt = 0; attempt < 64 && spawned < 2 && occupied.size() < limit; ++attempt)
    {
        auto& cells = !nearbyCells.empty() && (nearbyCount < minimum || candidates.empty())
                          ? nearbyCells
                          : candidates;
        if (cells.empty())
        {
            break;
        }
        const size_t index = Random() % cells.size();
        const auto position = navigation_.CellCenter(cells[index]);
        cells[index] = cells.back();
        cells.pop_back();
        bool blocked = false;
        for (const auto& other : occupied)
        {
            if (Distance(position, other) < 45)
            {
                blocked = true;
                break;
            }
        }
        if (blocked || !world.CanWalk(position))
        {
            continue;
        }
        auto& enemy = scene_.Spawn<EnemyActor>(gameplayRoot_);
        enemy.persistentId = nextId_++;
        enemy.SetWorldPosition(position);
        enemy.kind = Kills() >= 5 && Random() % 4 == 0 ? EnemyKind::Armored : EnemyKind::Scout;
        enemy.health = enemy.maxHealth = enemy.kind == EnemyKind::Armored ? 60.f : 28.f;
        enemy.spawnGrace = .5f;
        occupied.push_back(position);
        if (Distance(position, player) <= 300)
        {
            ++nearbyCount;
        }
        ++spawned;
    }
}

void LevelOne::SpawnBoss()
{
    size_t regularCount = 0;
    for (auto& enemy : scene_.Actors<EnemyActor>())
    {
        if (enemy.kind != EnemyKind::Boss && ++regularCount >= BossEnemyLimit)
        {
            enemy.Destroy();
        }
    }
    auto& boss = scene_.Spawn<EnemyActor>(gameplayRoot_);
    boss.persistentId = nextId_++;
    boss.kind = EnemyKind::Boss;
    boss.arenaMinimum = {755, 755};
    boss.arenaMaximum = {1170, 1170};
    boss.SetWorldPosition(BossArena());
    boss.health = boss.maxHealth = 800;
    boss.spawnGrace = 3;
    boss.attackTimer = 3;

    bossSpawned_ = true;
    Say("보스 출현 / 중앙 광장의 감시자-01");
}
