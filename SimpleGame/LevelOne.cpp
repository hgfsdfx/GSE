#include "stdafx.h"
#include "LevelOne.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace
{
    double Distance(WorldPoint a, WorldPoint b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    WorldPoint Direction(WorldPoint from, WorldPoint to)
    {
        const double length = Distance(from, to);
        if (length < .001)
        {
            return {};
        }
        return {(to.x - from.x) / length, (to.y - from.y) / length};
    }
} // namespace

void LevelOne::Start(World& world, WorldPoint& player)
{
    *this = LevelOne();
    seed_ =
        World::Hash(uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    randomState_ = seed_;
    world.ConfigureLevelOne(seed_);
    player = {90, 350};
    world.Stream(player, 3);
    BuildFlow(world, player);
    Say("LEVEL 1 / APPROACH A DRONE - YOUR PHONE FIRES AUTOMATICALLY");
}

uint64_t LevelOne::Random()
{
    randomState_ += 0x9e3779b97f4a7c15ULL;
    return World::Hash(randomState_);
}

bool LevelOne::SaveBlocked() const
{
    return saveBlocked_;
}

bool LevelOne::Dead() const
{
    return health_ <= 0;
}

bool LevelOne::Cleared() const
{
    return cleared_;
}

bool LevelOne::BossSpawned() const
{
    return bossSpawned_;
}

int LevelOne::Level() const
{
    return level_;
}

int LevelOne::Experience() const
{
    return experience_;
}

int LevelOne::NextExperience() const
{
    return 20 + (level_ - 1) * 12;
}

int LevelOne::WeaponRank() const
{
    return weaponRank_;
}

int LevelOne::Kills() const
{
    return kills_;
}

int LevelOne::ChipsCollected() const
{
    return chipsCollected_;
}

float LevelOne::Health() const
{
    return health_;
}

float LevelOne::MaxHealth() const
{
    return 100.f + 18.f * (level_ - 1);
}

float LevelOne::Damage() const
{
    return 12.f + 3.f * (level_ - 1) + 4.f * weaponRank_;
}

float LevelOne::FireInterval() const
{
    return std::max(.22f, .85f / (1.f + .10f * (level_ - 1) + .05f * weaponRank_));
}

float LevelOne::Range() const
{
    return std::min(420.f, 300.f + 8.f * (level_ - 1) + 5.f * weaponRank_);
}

float LevelOne::MagnetRadius() const
{
    return std::min(300.f, 110.f + 8.f * (level_ - 1) + 6.f * weaponRank_);
}

float LevelOne::MovementSpeed() const
{
    return 150.f + std::min(24.f, 2.f * (level_ - 1));
}

float LevelOne::Invulnerability() const
{
    return invulnerability_;
}

float LevelOne::ShotCooldown() const
{
    return fireTimer_;
}

uint64_t LevelOne::Seed() const
{
    return seed_;
}

WorldPoint LevelOne::BossArena() const
{
    return {960, 960};
}

const std::vector<LevelEnemy>& LevelOne::Enemies() const
{
    return enemies_;
}

const std::vector<LevelProjectile>& LevelOne::Projectiles() const
{
    return projectiles_;
}

const std::vector<LevelLoot>& LevelOne::Loot() const
{
    return loot_;
}

const std::vector<LevelPulse>& LevelOne::Pulses() const
{
    return pulses_;
}

const std::vector<CombatNumber>& LevelOne::Numbers() const
{
    return numbers_;
}

const LevelEnemy* LevelOne::Boss() const
{
    for (const auto& enemy : enemies_)
    {
        if (enemy.kind == EnemyKind::Boss && enemy.health > 0)
        {
            return &enemy;
        }
    }
    return nullptr;
}

bool LevelOne::InDistrict(WorldPoint point) const
{
    return point.x >= 0 && point.y >= 0 && point.x < DistrictSize && point.y < DistrictSize;
}

void LevelOne::Say(const std::string& message)
{
    message_ = message;
}

std::string LevelOne::TakeMessage()
{
    std::string result;
    result.swap(message_);
    return result;
}

int LevelOne::CellAt(WorldPoint point) const
{
    if (!InDistrict(point))
    {
        return -1;
    }
    return int(point.y / CellSize) * GridSide + int(point.x / CellSize);
}

WorldPoint LevelOne::CellCenter(int cell) const
{
    return {(cell % GridSide + .5) * CellSize, (cell / GridSide + .5) * CellSize};
}

bool LevelOne::ClearPath(const World& world, WorldPoint a, WorldPoint b) const
{
    const int steps = std::max(1, int(std::ceil(Distance(a, b) / 6)));
    for (int i = 0; i <= steps; ++i)
    {
        const double t = double(i) / steps;
        if (!world.CanWalk({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}))
        {
            return false;
        }
    }
    return true;
}

void LevelOne::BuildFlow(const World& world, WorldPoint player)
{
    distance_.fill(-1);
    int goal = -1;
    double closest = 90;
    for (int cell = 0; cell < GridSide * GridSide; ++cell)
    {
        const auto position = CellCenter(cell);
        walkable_[cell] = world.CanWalk(position);
        const double distance = Distance(position, player);
        if (walkable_[cell] && distance < closest && ClearPath(world, position, player))
        {
            goal = cell;
            closest = distance;
        }
    }
    if (goal < 0)
    {
        return;
    }
    std::queue<int> queue;
    queue.push(goal);
    distance_[goal] = 0;
    while (!queue.empty())
    {
        const int cell = queue.front();
        queue.pop();
        const int x = cell % GridSide, y = cell / GridSide;
        for (const auto& offset : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
        {
            const int nx = x + offset.first, ny = y + offset.second;
            if (nx < 0 || ny < 0 || nx >= GridSide || ny >= GridSide)
            {
                continue;
            }
            const int next = ny * GridSide + nx;
            if (walkable_[next] && distance_[next] < 0
                && ClearPath(world, CellCenter(cell), CellCenter(next)))
            {
                distance_[next] = distance_[cell] + 1;
                queue.push(next);
            }
        }
    }
}

WorldPoint LevelOne::FollowFlow(WorldPoint from, WorldPoint player, const World& world) const
{
    if (Distance(from, player) < 450 && ClearPath(world, from, player))
    {
        return player;
    }
    const int cell = CellAt(from);
    if (cell < 0)
    {
        return from;
    }
    int best = -1;
    int distance = std::numeric_limits<int>::max();
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int nx = cell % GridSide + x, ny = cell / GridSide + y;
            if (nx < 0 || ny < 0 || nx >= GridSide || ny >= GridSide)
            {
                continue;
            }
            const int candidate = ny * GridSide + nx;
            if (distance_[candidate] >= 0 && distance_[candidate] < distance
                && ClearPath(world, from, CellCenter(candidate)))
            {
                distance = distance_[candidate];
                best = candidate;
            }
        }
    }
    return best >= 0 ? CellCenter(best) : from;
}

void LevelOne::MoveActor(WorldPoint& point, WorldPoint target, float distance, const World& world)
{
    const auto direction = Direction(point, target);
    distance = std::min(distance, float(Distance(point, target)));
    const int steps = std::max(1, int(std::ceil(distance / 4)));
    for (int i = 0; i < steps; ++i)
    {
        WorldPoint next{point.x + direction.x * distance / steps, point.y};
        if (world.CanWalk(next))
        {
            point = next;
        }
        next = {point.x, point.y + direction.y * distance / steps};
        if (world.CanWalk(next))
        {
            point = next;
        }
    }
}

bool LevelOne::SpawnEnemy(const World& world, WorldPoint player)
{
    std::vector<int> candidates;
    for (int cell = 0; cell < GridSide * GridSide; ++cell)
    {
        const auto position = CellCenter(cell);
        const double distance = Distance(position, player);
        if (distance_[cell] < 0 || distance < 230 || distance > 470)
        {
            continue;
        }
        bool occupied = false;
        for (const auto& enemy : enemies_)
        {
            occupied = occupied || Distance(position, enemy.position) < 45;
        }
        if (!occupied && world.CanWalk(position))
        {
            candidates.push_back(cell);
        }
    }
    if (candidates.empty())
    {
        return false;
    }
    LevelEnemy enemy;
    enemy.id = nextId_++;
    enemy.position = CellCenter(candidates[Random() % candidates.size()]);
    enemy.kind = kills_ >= 5 && Random() % 4 == 0 ? EnemyKind::Armored : EnemyKind::Scout;
    enemy.health = enemy.maxHealth = enemy.kind == EnemyKind::Armored ? 60.f : 28.f;
    enemies_.push_back(enemy);
    return true;
}

void LevelOne::SpawnBoss()
{
    LevelEnemy boss;
    boss.id = nextId_++;
    boss.kind = EnemyKind::Boss;
    boss.position = BossArena();
    boss.health = boss.maxHealth = 800;
    boss.spawnGrace = 3;
    boss.attackTimer = 3;
    enemies_.push_back(boss);
    bossSpawned_ = true;
    Say("BOSS DETECTED / WARDEN-01 / CENTRAL PLAZA");
}

void LevelOne::Hurt(float damage, WorldPoint position)
{
    if (invulnerability_ > 0 || Dead() || cleared_)
    {
        return;
    }
    health_ = std::max(0.f, health_ - damage);
    invulnerability_ = .8f;
    numbers_.push_back({position, int(damage), .8f, false});
    if (Dead())
    {
        Say("SIGNAL LOST / PRESS R TO START A NEW RUN");
    }
}

void LevelOne::UpdateEnemies(float dt, World& world, WorldPoint player)
{
    for (auto& enemy : enemies_)
    {
        enemy.hitFlash = std::max(0.f, enemy.hitFlash - dt);
        enemy.spawnGrace = std::max(0.f, enemy.spawnGrace - dt);
        if (enemy.health <= 0 || enemy.spawnGrace > 0)
        {
            continue;
        }
        if (enemy.kind == EnemyKind::Boss)
        {
            const bool enraged = enemy.health < enemy.maxHealth * .5f;
            const WorldPoint goal{std::clamp(player.x, 755.0, 1170.0),
                                  std::clamp(player.y, 755.0, 1170.0)};
            if (Distance(enemy.position, player) > 170)
            {
                MoveActor(enemy.position, goal, (enraged ? 66.f : 48.f) * dt, world);
            }
            enemy.attackTimer -= dt;
            enemy.pulseTimer -= dt;
            if (enemy.attackTimer <= 0 && Distance(enemy.position, player) < 650
                && ClearPath(world, enemy.position, player))
            {
                const double angle =
                    std::atan2(player.y - enemy.position.y, player.x - enemy.position.x);
                const int spread = enraged ? 3 : 2;
                for (int i = -spread; i <= spread; ++i)
                {
                    const double a = angle + i * .20;
                    projectiles_.push_back({enemy.position,
                                            {std::cos(a), std::sin(a)},
                                            0,
                                            enraged ? 16.f : 12.f,
                                            520,
                                            true});
                }
                enemy.attackTimer = enraged ? 2.1f : 3.1f;
            }
            if (enemy.pulseTimer <= 0 && Distance(enemy.position, player) < 480)
            {
                pulses_.push_back({player, 1.3f, enraged ? 120.f : 100.f});
                enemy.pulseTimer = enraged ? 4.8f : 7.f;
            }
            if (Distance(enemy.position, player) < 37)
            {
                Hurt(20, player);
            }
        }
        else
        {
            const auto goal = FollowFlow(enemy.position, player, world);
            MoveActor(enemy.position,
                      goal,
                      (enemy.kind == EnemyKind::Armored ? 48.f : 75.f) * dt,
                      world);
            if (Distance(enemy.position, player) < 21)
            {
                Hurt(enemy.kind == EnemyKind::Armored ? 14.f : 9.f, player);
            }
        }
    }
    for (auto& pulse : pulses_)
    {
        pulse.remaining -= dt;
        if (pulse.remaining <= 0 && Distance(player, pulse.position) < pulse.radius)
        {
            Hurt(24, player);
        }
    }
    pulses_.erase(std::remove_if(pulses_.begin(),
                                 pulses_.end(),
                                 [](const LevelPulse& pulse)
                                 {
                                     return pulse.remaining <= 0;
                                 }),
                  pulses_.end());
}

void LevelOne::AutoFire(const World& world, WorldPoint player)
{
    if (fireTimer_ > 0)
    {
        return;
    }
    const LevelEnemy* closest = nullptr;
    double distance = Range();
    for (const auto& enemy : enemies_)
    {
        const double candidate = Distance(player, enemy.position);
        if (enemy.health > 0 && enemy.spawnGrace <= 0 && candidate <= distance
            && ClearPath(world, player, enemy.position))
        {
            closest = &enemy;
            distance = candidate;
        }
    }
    if (closest)
    {
        projectiles_.push_back(
            {player, Direction(player, closest->position), closest->id, Damage(), Range(), false});
        fireTimer_ = FireInterval();
    }
}

void LevelOne::UpdateProjectiles(float dt, const World& world, WorldPoint player)
{
    for (auto& shot : projectiles_)
    {
        if (!shot.hostile)
        {
            for (const auto& enemy : enemies_)
            {
                if (enemy.id == shot.targetId && enemy.health > 0)
                {
                    shot.direction = Direction(shot.position, enemy.position);
                    break;
                }
            }
        }
        const float travel = std::min(shot.distanceLeft, (shot.hostile ? 185.f : 620.f) * dt);
        const int steps = std::max(1, int(std::ceil(travel / 4)));
        for (int i = 0; i < steps && shot.distanceLeft > 0; ++i)
        {
            shot.position.x += shot.direction.x * travel / steps;
            shot.position.y += shot.direction.y * travel / steps;
            shot.distanceLeft -= travel / steps;
            if (!world.CanWalk(shot.position))
            {
                shot.distanceLeft = 0;
                break;
            }
            if (shot.hostile)
            {
                if (Distance(shot.position, player) < 12)
                {
                    Hurt(shot.damage, player);
                    shot.distanceLeft = 0;
                }
            }
            else
            {
                for (auto& enemy : enemies_)
                {
                    if (enemy.health > 0 && enemy.spawnGrace <= 0
                        && Distance(shot.position, enemy.position)
                               < (enemy.kind == EnemyKind::Boss ? 29 : 15))
                    {
                        if (Dead())
                        {
                            break;
                        }
                        enemy.health -= shot.damage;
                        if (enemy.kind == EnemyKind::Boss && enemy.health <= 0)
                        {
                            // A killing shot resolves the encounter before later hostile shots.
                            cleared_ = true;
                        }
                        enemy.hitFlash = .12f;
                        numbers_.push_back({enemy.position, int(shot.damage), .8f, false});
                        shot.distanceLeft = 0;
                        break;
                    }
                }
            }
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(),
                                      projectiles_.end(),
                                      [](const LevelProjectile& shot)
                                      {
                                          return shot.distanceLeft <= .001f;
                                      }),
                       projectiles_.end());
    for (const auto& enemy : enemies_)
    {
        if (enemy.health <= 0)
        {
            DropLoot(enemy);
        }
    }
    enemies_.erase(std::remove_if(enemies_.begin(),
                                  enemies_.end(),
                                  [](const LevelEnemy& enemy)
                                  {
                                      return enemy.health <= 0;
                                  }),
                   enemies_.end());
    if (cleared_)
    {
        enemies_.clear();
        projectiles_.clear();
        pulses_.clear();
    }
}

void LevelOne::AddLoot(LootKind kind, WorldPoint point, int amount)
{
    if (loot_.size() >= 512)
    {
        for (auto& item : loot_)
        {
            if (item.kind == kind)
            {
                item.amount = std::min(1000000, item.amount + amount);
                return;
            }
        }
    }
    loot_.push_back({kind, point, amount, false});
}

void LevelOne::DropLoot(const LevelEnemy& enemy)
{
    if (enemy.kind == EnemyKind::Boss)
    {
        cleared_ = true;
        AddLoot(LootKind::Semiconductor, enemy.position, 150);
        AddLoot(LootKind::Upgrade, enemy.position, 2);
        AddLoot(LootKind::Medkit, enemy.position, 100);
        Say("LEVEL 1 CLEAR / WARDEN-01 DISCONNECTED");
        return;
    }
    kills_ = std::min(1000000, kills_ + 1);
    AddLoot(LootKind::Semiconductor, enemy.position, enemy.kind == EnemyKind::Armored ? 14 : 8);
    if (kills_ == 3 || kills_ % 5 == 0)
    {
        AddLoot(LootKind::Upgrade, enemy.position, 1);
    }
    if (kills_ == 5 || kills_ % 7 == 0 || Random() % 12 == 0)
    {
        AddLoot(LootKind::Medkit, enemy.position, 32);
    }
}

void LevelOne::GainExperience(int amount)
{
    experience_ = std::min(1000000, experience_ + amount);
    while (level_ < 30 && experience_ >= NextExperience())
    {
        experience_ -= NextExperience();
        ++level_;
        health_ = std::min(MaxHealth(), health_ + 30);
        Say("LEVEL UP / DAMAGE - FIRE RATE - HEALTH - MAGNET IMPROVED");
    }
    if (level_ == 30)
    {
        experience_ = 0;
    }
}

void LevelOne::UpdateLoot(float dt, const World& world, WorldPoint player)
{
    for (auto& item : loot_)
    {
        if (Distance(item.position, player) <= MagnetRadius())
        {
            item.attracted = true;
        }
        if (item.attracted)
        {
            const auto goal = FollowFlow(item.position, player, world);
            MoveActor(item.position,
                      goal,
                      (270.f + float(Distance(item.position, player))) * dt,
                      world);
        }
        if (Distance(item.position, player) > 18 || !ClearPath(world, item.position, player))
        {
            continue;
        }
        if (item.kind == LootKind::Semiconductor)
        {
            chipsCollected_ = std::min(1000000, chipsCollected_ + item.amount);
            GainExperience(item.amount);
        }
        else if (item.kind == LootKind::Upgrade)
        {
            const int applied = std::min(10 - weaponRank_, item.amount);
            weaponRank_ += applied;
            GainExperience((item.amount - applied) * 12);
            Say("SMARTPHONE UPGRADED / DMG AND FIRE RATE INCREASED");
        }
        else if (health_ < MaxHealth())
        {
            const int heal = int(std::min(MaxHealth() - health_, float(item.amount)));
            health_ = std::min(MaxHealth(), health_ + item.amount);
            numbers_.push_back({player, heal, .8f, true});
        }
        else
        {
            GainExperience(3);
        }
        item.amount = 0;
    }
    loot_.erase(std::remove_if(loot_.begin(),
                               loot_.end(),
                               [](const LevelLoot& item)
                               {
                                   return item.amount == 0;
                               }),
                loot_.end());
}

void LevelOne::Update(float dt, World& world, WorldPoint player)
{
    for (auto& number : numbers_)
    {
        number.remaining -= dt;
    }
    numbers_.erase(std::remove_if(numbers_.begin(),
                                  numbers_.end(),
                                  [](const CombatNumber& number)
                                  {
                                      return number.remaining <= 0;
                                  }),
                   numbers_.end());
    if (Dead() || !InDistrict(player))
    {
        return;
    }
    invulnerability_ = std::max(0.f, invulnerability_ - dt);
    fireTimer_ = std::max(0.f, fireTimer_ - dt);
    flowTimer_ -= dt;
    if (flowTimer_ <= 0)
    {
        BuildFlow(world, player);
        flowTimer_ = .4f;
    }
    if (!cleared_)
    {
        // Distant ordinary encounters may respawn nearer the player, without rewards.
        enemies_.erase(std::remove_if(enemies_.begin(),
                                      enemies_.end(),
                                      [&](const LevelEnemy& enemy)
                                      {
                                          return enemy.kind != EnemyKind::Boss
                                                 && Distance(enemy.position, player) > 1050;
                                      }),
                       enemies_.end());
        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0 && enemies_.size() < (bossSpawned_ ? 7U : 13U))
        {
            SpawnEnemy(world, player);
            spawnTimer_ = kills_ < 3 ? 2.8f : 1.9f;
        }
        if (!bossSpawned_ && kills_ >= BossKillsRequired && level_ >= BossLevelRequired)
        {
            const int arenaCell = CellAt(BossArena());
            if (arenaCell >= 0 && distance_[arenaCell] >= 0)
            {
                SpawnBoss();
            }
        }
        AutoFire(world, player);
        UpdateEnemies(dt, world, player);
        if (Dead())
        {
            return;
        }
        UpdateProjectiles(dt, world, player);
    }
    if (!Dead())
    {
        UpdateLoot(dt, world, player);
    }
}
