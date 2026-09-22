#pragma once
#include "Actor.h"
#include "PlayerStats.h"
#include <string>
class World;
class NavigationGrid;
class PlayerActor;
class EnemyActor;

// Levels supply rewards/progression policy without actors depending on LevelOne.
class GameplayEvents
{
  public:

    virtual ~GameplayEvents() = default;
    virtual ActorId GameplayRoot() const = 0;
    virtual void EnemyDefeated(EnemyActor& enemy) = 0;
    virtual void Hurt(float damage, WorldPoint position) = 0;
    virtual void GainExperience(int amount) = 0;
    virtual void Say(const std::string& message) = 0;
};

struct ActorUpdateContext
{
    SceneGraph& scene;
    World& world;
    NavigationGrid& navigation;
    GameplayEvents& events;
    PlayerActor& player;
};
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

class PlayerActor : public Actor
{
  public:

    PlayerActor();
    void Update(float dt, ActorUpdateContext& context) override;
    PlayerStats stats;
    WorldPoint input;
    bool running = false;
};

class SmartphoneActor : public Actor
{
  public:

    SmartphoneActor();
    void Update(float dt, ActorUpdateContext& context) override;
};

class EnemyActor : public Actor
{
  public:

    EnemyActor();
    void Update(float dt, ActorUpdateContext& context) override;
    uint64_t persistentId = 0;
    WorldPoint arenaMinimum;
    WorldPoint arenaMaximum;
    EnemyKind kind = EnemyKind::Scout;
    float health = 28, maxHealth = 28, attackTimer = 1, pulseTimer = 5, spawnGrace = 1,
          hitFlash = 0;
};

class ProjectileActor : public Actor
{
  public:

    ProjectileActor(WorldPoint direction, uint64_t target, float damage, float range, bool hostile);
    void Update(float dt, ActorUpdateContext& context) override;
    WorldPoint direction;
    uint64_t targetId;
    float damage, distanceLeft;
    bool hostile;
};

class LootActor : public Actor
{
  public:

    LootActor(LootKind kind = LootKind::Semiconductor, int amount = 1);
    void Update(float dt, ActorUpdateContext& context) override;
    LootKind kind;
    int amount;
    bool attracted = false;
};

class PulseActor : public Actor
{
  public:

    PulseActor(float remaining = 1.3f, float radius = 105);
    void Update(float dt, ActorUpdateContext& context) override;
    float remaining, radius;
};

class CombatNumberActor : public Actor
{
  public:

    CombatNumberActor(int value, bool healing = false);
    void Update(float dt, ActorUpdateContext& context) override;
    int value;
    float remaining = .8f;
    bool healing;
};
