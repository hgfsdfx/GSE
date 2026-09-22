#pragma once
#include "World.h"
#include "SceneGraph.h"
#include "GameplayActors.h"
#include "NavigationGrid.h"
#include <string>

// Coordinates one encounter; actors, navigation and progression own their reusable behavior.
class LevelOne : public GameplayEvents
{
  public:

    static constexpr int GridSide = 48;
    static constexpr double CellSize = 40;
    static constexpr double DistrictSize = GridSide * CellSize;
    static constexpr int BossKillsRequired = 20;
    static constexpr int BossLevelRequired = 4;

    LevelOne();
    void Start(World& world);
    void Update(float dt, World& world, int streamRadius);
    bool Save(const std::filesystem::path& path, const World& world) const;
    bool Load(const std::filesystem::path& path, World& world);
    bool SaveBlocked() const;
    bool Dead() const;
    bool Cleared() const;
    bool BossSpawned() const;
    bool InDistrict(WorldPoint point) const;
    int Level() const;
    int Experience() const;
    int NextExperience() const;
    int WeaponRank() const;
    int Kills() const;
    int ChipsCollected() const;
    float Health() const;
    float MaxHealth() const;
    float Damage() const;
    float FireInterval() const;
    float Range() const;
    float MagnetRadius() const;
    float MovementSpeed() const;
    float Invulnerability() const;
    float ShotCooldown() const;
    uint64_t Seed() const;
    WorldPoint BossArena() const;
    const EnemyActor* Boss() const;
    ActorView<const EnemyActor> Enemies() const;
    ActorView<const ProjectileActor> Projectiles() const;
    ActorView<const LootActor> Loot() const;
    ActorView<const PulseActor> Pulses() const;
    ActorView<const CombatNumberActor> Numbers() const;
    std::string TakeMessage();
    SceneGraph& Scene();
    const SceneGraph& Scene() const;
    PlayerActor& Player();
    const PlayerActor& Player() const;
    ActorId GameplayRoot() const override;
    void EnemyDefeated(EnemyActor& enemy) override;
    void Hurt(float damage, WorldPoint position) override;
    void GainExperience(int amount) override;
    void Say(const std::string& message) override;

  private:

    uint64_t Random();
    bool SpawnEnemy(const World& world, WorldPoint player, bool nearby = false);
    void PopulateEnemies(const World& world, WorldPoint player);
    void SpawnBoss();
    void DropLoot(const EnemyActor& enemy);
    void AddLoot(LootKind kind, WorldPoint point, int amount);
    SceneGraph scene_;
    NavigationGrid navigation_;
    ActorId playerId_ = 0;
    ActorId gameplayRoot_ = 0;
    uint64_t seed_ = 1;
    uint64_t randomState_ = 1;
    uint64_t nextId_ = 1;
    float spawnTimer_ = 1;
    float flowTimer_ = 0;
    bool bossSpawned_ = false;
    bool cleared_ = false;
    bool saveBlocked_ = false;
    std::string message_;
};
