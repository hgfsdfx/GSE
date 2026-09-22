#include "stdafx.h"
#include "LevelOne.h"
#include "GameMath.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>
using GameMath::Distance;

LevelOne::LevelOne()
{
    gameplayRoot_ = scene_.Spawn<GroupActor>(0).Id();
    auto& player = scene_.Spawn<PlayerActor>(gameplayRoot_);
    playerId_ = player.Id();
    scene_.Spawn<SmartphoneActor>(playerId_);
}

SceneGraph& LevelOne::Scene()
{
    return scene_;
}

const SceneGraph& LevelOne::Scene() const
{
    return scene_;
}

PlayerActor& LevelOne::Player()
{
    auto player = dynamic_cast<PlayerActor*>(scene_.Find(playerId_));
    if (!player)
    {
        throw std::logic_error("The level player actor no longer exists");
    }
    return *player;
}

const PlayerActor& LevelOne::Player() const
{
    auto player = dynamic_cast<const PlayerActor*>(scene_.Find(playerId_));
    if (!player)
    {
        throw std::logic_error("The level player actor no longer exists");
    }
    return *player;
}

ActorId LevelOne::GameplayRoot() const
{
    return gameplayRoot_;
}

void LevelOne::Start(World& world)
{
    world.BindScene(nullptr);
    *this = LevelOne();
    seed_ =
        World::Hash(uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    randomState_ = seed_;
    world.ConfigureLevelOne(seed_);
    Player().SetWorldPosition({90, 350});
    world.BindScene(&scene_);
    world.Stream(Player().WorldPosition(), 3);
    navigation_.BuildFlow(world, Player().WorldPosition());
    PopulateEnemies(world, Player().WorldPosition());
    Say("레벨 1 / 드론에게 다가가면 스마트폰이 자동 발사합니다");
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

bool LevelOne::Cleared() const
{
    return cleared_;
}

bool LevelOne::BossSpawned() const
{
    return bossSpawned_;
}

uint64_t LevelOne::Seed() const
{
    return seed_;
}

WorldPoint LevelOne::BossArena() const
{
    return {960, 960};
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

bool LevelOne::Dead() const
{
    return Player().stats.Dead();
}

int LevelOne::Level() const
{
    return Player().stats.Level();
}

int LevelOne::Experience() const
{
    return Player().stats.Experience();
}

int LevelOne::NextExperience() const
{
    return Player().stats.NextExperience();
}

int LevelOne::WeaponRank() const
{
    return Player().stats.WeaponRank();
}

int LevelOne::Kills() const
{
    return Player().stats.Kills();
}

int LevelOne::ChipsCollected() const
{
    return Player().stats.ChipsCollected();
}

float LevelOne::Health() const
{
    return Player().stats.Health();
}

float LevelOne::MaxHealth() const
{
    return Player().stats.MaxHealth();
}

float LevelOne::Damage() const
{
    return Player().stats.Damage();
}

float LevelOne::FireInterval() const
{
    return Player().stats.FireInterval();
}

float LevelOne::Range() const
{
    return Player().stats.Range();
}

float LevelOne::MagnetRadius() const
{
    return Player().stats.MagnetRadius();
}

float LevelOne::MovementSpeed() const
{
    return Player().stats.MovementSpeed();
}

float LevelOne::Invulnerability() const
{
    return Player().stats.Invulnerability();
}

float LevelOne::ShotCooldown() const
{
    return Player().stats.ShotCooldown();
}

ActorView<const EnemyActor> LevelOne::Enemies() const
{
    return scene_.Actors<EnemyActor>();
}

ActorView<const ProjectileActor> LevelOne::Projectiles() const
{
    return scene_.Actors<ProjectileActor>();
}

ActorView<const LootActor> LevelOne::Loot() const
{
    return scene_.Actors<LootActor>();
}

ActorView<const PulseActor> LevelOne::Pulses() const
{
    return scene_.Actors<PulseActor>();
}

ActorView<const CombatNumberActor> LevelOne::Numbers() const
{
    return scene_.Actors<CombatNumberActor>();
}

const EnemyActor* LevelOne::Boss() const
{
    for (const auto& enemy : Enemies())
    {
        if (enemy.kind == EnemyKind::Boss && enemy.health > 0)
        {
            return &enemy;
        }
    }
    return nullptr;
}

void LevelOne::Update(float dt, World& world, int streamRadius)
{
    ActorUpdateContext context{scene_, world, navigation_, *this, Player()};
    scene_.Update(UpdatePhase::Effect, dt, context);
    scene_.FlushDestroyed();
    if (Dead())
    {
        return;
    }
    world.RefreshColliders();
    scene_.Update(UpdatePhase::Player, dt, context);
    const auto player = Player().WorldPosition();
    world.Stream(player, streamRadius);
    scene_.FlushDestroyed();
    flowTimer_ -= dt;
    if (flowTimer_ <= 0)
    {
        navigation_.BuildFlow(world, player);
        flowTimer_ = .4f;
    }
    for (auto& enemy : scene_.Actors<EnemyActor>())
    {
        if (enemy.kind != EnemyKind::Boss && Distance(enemy.WorldPosition(), player) > 750)
        {
            enemy.Destroy();
        }
    }
    spawnTimer_ -= dt;
    const size_t limit = Boss() ? 7 : 13;
    if (spawnTimer_ <= 0 && Enemies().size() < limit)
    {
        PopulateEnemies(world, player);
        spawnTimer_ = Enemies().size() < limit && SpawnEnemy(world, player)
                          ? (Kills() < 3 ? 2.8f : 1.9f)
                          : .5f;
    }
    if (!bossSpawned_ && InDistrict(player) && Kills() >= BossKillsRequired
        && Level() >= BossLevelRequired && navigation_.Reachable(navigation_.CellAt(BossArena())))
    {
        SpawnBoss();
    }
    scene_.Update(UpdatePhase::Weapon, dt, context);
    scene_.Update(UpdatePhase::Enemy, dt, context);
    scene_.Update(UpdatePhase::Pulse, dt, context);
    if (!Dead())
    {
        scene_.Update(UpdatePhase::Projectile, dt, context);
    }
    if (!Dead())
    {
        scene_.Update(UpdatePhase::Loot, dt, context);
    }
    scene_.Update(UpdatePhase::Decoration, dt, context);
    scene_.FlushDestroyed();
}
