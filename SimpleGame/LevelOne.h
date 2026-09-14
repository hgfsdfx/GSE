#pragma once
#include "World.h"
#include <array>
#include <string>

enum class EnemyKind
{
    Scout,
    Armored,
    Boss
};

enum class LootKind
{
    Semiconductor,
    Upgrade,
    Medkit
};

struct LevelEnemy
{
    uint64_t id = 0;
    EnemyKind kind = EnemyKind::Scout;
    WorldPoint position;
    float health = 26;
    float maxHealth = 26;
    float attackTimer = 1;
    float pulseTimer = 5;
    float spawnGrace = 1;
    float hitFlash = 0;
};

struct LevelProjectile
{
    WorldPoint position;
    WorldPoint direction;
    uint64_t targetId = 0;
    float damage = 0;
    float distanceLeft = 0;
    bool hostile = false;
};

struct LevelLoot
{
    LootKind kind = LootKind::Semiconductor;
    WorldPoint position;
    int amount = 1;
    bool attracted = false;
};

struct LevelPulse
{
    WorldPoint position;
    float remaining = 1.3f;
    float radius = 105;
};

struct CombatNumber
{
    WorldPoint position;
    int value = 0;
    float remaining = .8f;
    bool healing = false;
};

// Level-one combat and progression. Rendering stays in Prototype.
class LevelOne
{
  public:

    static constexpr int GridSide = 48;
    static constexpr double CellSize = 40;
    static constexpr double DistrictSize = GridSide * CellSize;
    static constexpr int BossKillsRequired = 20;
    static constexpr int BossLevelRequired = 4;

    void Start(World& world, WorldPoint& player);
    void Update(float dt, World& world, WorldPoint player);
    bool Save(const std::filesystem::path& path, const World& world, WorldPoint player) const;
    bool Load(const std::filesystem::path& path, World& world, WorldPoint& player);
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
    const LevelEnemy* Boss() const;
    const std::vector<LevelEnemy>& Enemies() const;
    const std::vector<LevelProjectile>& Projectiles() const;
    const std::vector<LevelLoot>& Loot() const;
    const std::vector<LevelPulse>& Pulses() const;
    const std::vector<CombatNumber>& Numbers() const;
    std::string TakeMessage();

  private:

    uint64_t Random();
    void BuildFlow(const World& world, WorldPoint player);
    int CellAt(WorldPoint point) const;
    WorldPoint CellCenter(int cell) const;
    bool ClearPath(const World& world, WorldPoint a, WorldPoint b) const;
    WorldPoint FollowFlow(WorldPoint from, WorldPoint player, const World& world) const;
    void MoveActor(WorldPoint& point, WorldPoint target, float distance, const World& world);
    bool SpawnEnemy(const World& world, WorldPoint player);
    void SpawnBoss();
    void UpdateEnemies(float dt, World& world, WorldPoint player);
    void UpdateProjectiles(float dt, const World& world, WorldPoint player);
    void UpdateLoot(float dt, const World& world, WorldPoint player);
    void AutoFire(const World& world, WorldPoint player);
    void DropLoot(const LevelEnemy& enemy);
    void AddLoot(LootKind kind, WorldPoint point, int amount);
    void GainExperience(int amount);
    void Hurt(float damage, WorldPoint position);
    void Say(const std::string& message);
    std::array<int, GridSide * GridSide> distance_{};
    std::array<bool, GridSide * GridSide> walkable_{};
    std::vector<LevelEnemy> enemies_;
    std::vector<LevelProjectile> projectiles_;
    std::vector<LevelLoot> loot_;
    std::vector<LevelPulse> pulses_;
    std::vector<CombatNumber> numbers_;
    uint64_t seed_ = 1;
    uint64_t randomState_ = 1;
    uint64_t nextId_ = 1;
    int level_ = 1;
    int experience_ = 0;
    int weaponRank_ = 0;
    int kills_ = 0;
    int chipsCollected_ = 0;
    float health_ = 100;
    float invulnerability_ = 1.5f;
    float fireTimer_ = 0;
    float spawnTimer_ = 1;
    float flowTimer_ = 0;
    bool bossSpawned_ = false;
    bool cleared_ = false;
    bool saveBlocked_ = false;
    std::string message_;
};
