#include "stdafx.h"
#include "LevelOne.h"
#include "GameMath.h"
using GameMath::Distance;

bool LevelOne::SpawnEnemy(const World& world, WorldPoint player, bool nearby)
{
    std::vector<int> candidates;
    std::vector<int> visible;
    const auto enemies = Enemies();
    for (int cell = 0; cell < GridSide * GridSide; ++cell)
    {
        const auto position = navigation_.CellCenter(cell);
        const double distance = Distance(position, player);
        if (!navigation_.Reachable(cell) || distance < (nearby ? 140 : 230)
            || distance > (nearby ? 300 : 470))
        {
            continue;
        }
        bool occupied = false;
        for (const auto& enemy : enemies)
        {
            occupied = occupied || Distance(position, enemy.WorldPosition()) < 45;
        }
        if (!occupied && world.CanWalk(position))
        {
            candidates.push_back(cell);
            if (nearby && navigation_.ClearPath(world, player, position))
            {
                visible.push_back(cell);
            }
        }
    }
    if (candidates.empty())
    {
        return false;
    }
    auto& enemy = scene_.Spawn<EnemyActor>(gameplayRoot_);
    enemy.persistentId = nextId_++;
    const auto& spawnCells = visible.empty() ? candidates : visible;
    enemy.SetWorldPosition(navigation_.CellCenter(spawnCells[Random() % spawnCells.size()]));
    enemy.kind = Kills() >= 5 && Random() % 4 == 0 ? EnemyKind::Armored : EnemyKind::Scout;
    enemy.health = enemy.maxHealth = enemy.kind == EnemyKind::Armored ? 60.f : 28.f;
    enemy.spawnGrace = nearby ? 2.f : 1.5f;

    return true;
}

void LevelOne::PopulateEnemies(const World& world, WorldPoint player)
{
    if (Dead())
    {
        return;
    }
    const size_t minimum = Boss() ? 2 : 4;
    const size_t limit = Boss() ? 7 : 13;
    size_t nearby = 0;
    for (const auto& enemy : Enemies())
    {
        if (enemy.kind != EnemyKind::Boss && Distance(enemy.WorldPosition(), player) <= 650)
        {
            ++nearby;
        }
    }
    while (nearby < minimum && Enemies().size() < limit)
    {
        if (!SpawnEnemy(world, player, true))
        {
            break;
        }
        ++nearby;
    }
}

void LevelOne::SpawnBoss()
{
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
