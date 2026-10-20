#pragma once
#include "GameplayActors.h"
#include "WorldActors.h"
#include <array>
#include <deque>
#include <iosfwd>

enum class NpcJob
{
    Hunter,
    Raider,
    Hacker,
    Merchant,
    Seeker,
    Guard
};
enum class NpcAction
{
    Rest,
    Hunt,
    Raid,
    Trade,
    Hack,
    Study,
    Recover,
    Relocate
};

struct TownReport
{
    uint64_t revision = 0;
    double timestamp = 0;
    float prosperity = 0, safety = 0, learning = 0;
    int population = 0;
};

class HouseActor : public BuildingActor
{
  public:

    HouseActor(int town, int slot);
    int town, slot;
    int owner = 0;
};

class TownActor : public Actor
{
  public:

    explicit TownActor(int index);
    int index;
    int treasury = 3000;
    int stock = 150;
    std::array<TownReport, 7> reports{};
};

class NpcActor : public Actor
{
  public:

    NpcActor(int identity, int home, NpcJob job);
    void Update(float dt, ActorUpdateContext& context) override;
    float MaxHealth() const;
    WorldPoint HomePosition() const;
    std::string Name() const;
    int identity, home;
    int houseSlot = 0;
    NpcJob job;
    NpcAction action = NpcAction::Rest;
    float health = 90, survivalWeight = 1, wealthWeight = 1, insightWeight = 1, plunderWeight = 0;
    int credits = 80, cargo = 0, insight = 0;
    float notoriety = 0, recovery = 0, work = 0, attackCooldown = 0, flash = 0;
    float think = 0;
    float playerScan = 0;
    float migrationCooldown = 30, communicationTimer = 0;
    int lastVisitedTown = -1;
    std::array<TownReport, 7> reports{};
    uint64_t decisions = 0;
    ActorId target = 0;
    bool againstPlayer = false;
    bool goalAssigned = false;
    int destinationTown = 0;
    WorldPoint destination;
    std::array<WorldPoint, 5> route{};
    int routeStep = 5;
};

// Bounded, persistent society. Actors belong to SceneGraph, never to streamed chunks.
class Society
{
  public:

    static constexpr int TownCount = 7;
    static constexpr int LegacyPopulation = 18;
    static constexpr int ResidentsPerTown = 20;
    static constexpr int HousesPerTown = 24;
    static constexpr int Population = TownCount * ResidentsPerTown;
    static constexpr double TownSafeRadius = 600;
    static constexpr double TownBuildingClearRadius = 760;
    static constexpr double TownSpacing = 4480;
    static constexpr float PlayerPkDetectionRange = 320;
    static constexpr float PlayerPkAttackRange = 160;
    static int HomeTown(int identity);
    static bool InTownSafeZone(WorldPoint point, double padding = 0);
    static bool BlocksMonsterTravel(WorldPoint from, WorldPoint to, double padding = 0);
    static WorldPoint TownCenter(int index);
    static WorldPoint HouseCenter(int town, int slot);
    static WorldPoint HouseDoor(int town, int slot);
    static Building HouseGeometry(int town, int slot);
    static int NeighborTown(int town, uint64_t choice);
    static const char* TownName(int index);
    static const char* JobName(NpcJob job);
    static const char* ActionName(NpcAction action);
    static int BuyPrice(int town);
    static int SellPrice(int town);
    void Initialize(SceneGraph& scene, ActorId root);
    void Tick(float dt, ActorUpdateContext& context);
    void UpdateNpc(NpcActor& npc, float dt, ActorUpdateContext& context);
    void NpcDefeated(NpcActor& npc, ActorUpdateContext& context);
    void Command(unsigned char key, ActorUpdateContext& context);
    bool AttackNpc(ActorUpdateContext& context, bool manual);
    void OnHack(const Device& device, ActorUpdateContext& context);
    bool Write(std::ostream& stream, const SceneGraph& scene) const;
    bool Read(std::istream& stream, SceneGraph& scene, ActorId root);
    HouseActor* House(SceneGraph& scene, int town, int slot) const;
    const HouseActor* House(const SceneGraph& scene, int town, int slot) const;
    TownActor* Town(SceneGraph& scene, int index) const;
    const TownActor* Town(const SceneGraph& scene, int index) const;
    int NearbyTown(WorldPoint point, double range = TownSafeRadius) const;
    const NpcActor* NearestNpc(const SceneGraph& scene, WorldPoint point, double range) const;
    std::string Objective() const;
    const std::deque<std::string>& News() const;
    void Log(const std::string& message);
    int credits = 100, cargo = 0, insight = 0;
    float wanted = 0;
    bool pkEnabled = false;
    int story = 0;
    float escapeTime = 0;
    uint64_t trades = 0, raids = 0, hunts = 0;
    uint64_t lettersSent = 0, lettersDelivered = 0, rumorVisits = 0, migrations = 0;
    size_t LettersInTransit() const;
    double SimulationTime() const;

  private:

    void Decide(NpcActor& npc, ActorUpdateContext& context);
    bool TryStartPlayerCombat(NpcActor& npc, ActorUpdateContext& context);
    void InitializeHouses(SceneGraph& scene, ActorId root);
    void RefreshTownReports(SceneGraph& scene);
    void UpdateCommunication(float dt, ActorUpdateContext& context);
    void Communicate(NpcActor& npc, ActorUpdateContext& context);
    bool TryRelocate(NpcActor& npc, ActorUpdateContext& context);
    void CompleteRelocation(NpcActor& npc, ActorUpdateContext& context);
    void Respawn(NpcActor& npc, SceneGraph& scene);
    void SetDestination(NpcActor& npc, WorldPoint point);
    bool Travel(NpcActor& npc, float dt, ActorUpdateContext& context);
    void Strike(NpcActor& attacker, NpcActor& victim, ActorUpdateContext& context);
    void Defeat(NpcActor& victim,
                NpcActor* attacker,
                ActorUpdateContext& context,
                bool playerKill = false,
                bool selfDefense = false);
    std::array<ActorId, TownCount> towns_{};
    std::array<std::array<ActorId, HousesPerTown>, TownCount> houses_{};

    struct Letter
    {
        int from = 0, to = 0;
        float remaining = 0;
        std::array<TownReport, TownCount> reports{};
    };

    std::deque<Letter> letters_;
    double societyTime_ = 0;
    float letterTimer_ = 0;
    uint64_t nextReport_ = 1;
    std::deque<std::string> news_;
};
