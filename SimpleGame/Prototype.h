#pragma once
#include "Renderer.h"
#include "World.h"
#include "LevelOne.h"
#include "WorldActors.h"
#include <array>

class Prototype
{
  public:

    Prototype(Renderer& renderer, const std::filesystem::path& savePath);
    void Update(float dt);
    void Draw();
    void Key(unsigned char key, bool down);
    void ReleaseKeys();
    bool Save();

  private:

    Point Project(double x, double y, float height = 0) const;
    void Ground(const GroundActor& actor);
    void GroundMesh(const Chunk& chunk);
    void DrawBuilding(const Building& building, const ChunkKey& key);
    void DrawBuildingMesh(const Building& building, bool illuminated);
    void DrawDevice(const Device& device);
    void DrawPlayer(const PlayerActor& actor);
    void Hud();
    void BindActorRenderers();
    void EnsurePresentationActors();
    double ActorDepth(const Actor& actor) const;
    void DrawMarkers();
    void DrawTraffic(const TrafficActor& actor);
    void DrawSmartphone(const SmartphoneActor& actor);
    void DrawPulse(const PulseActor& pulse);
    void DrawLevelGround();
    void DrawEnemyActor(const EnemyActor& enemy);
    void DrawLootActor(const LootActor& item);
    void DrawProjectileActor(const ProjectileActor& shot);
    void DrawCombatNumber(const CombatNumberActor& number);
    void DrawLevelHud();
    void WorldRing(WorldPoint center, float radius, Color color, float width = 1);
    void Hack(const Device& target);
    void Notify(const std::string& message);
    int StreamRadius() const;
    Renderer& r_;
    World world_;
    LevelOne level_;
    std::filesystem::path savePath_;
    WorldPoint camera_{82, 245};
    ActorRenderContext actorRenderer_;
    ActorView<const BuildingActor> depthBuildings_;
    std::array<bool, 256> keys_{};
    Device target_, hacking_;
    float time_ = 0, zoom_ = 1, hackProgress_ = 0, trace_ = 0, lockdown_ = 0;
    float toastTime_ = 8, saveTime_ = 0;
    bool scan_ = true, paused_ = false, moving_ = false, saveAllowed_ = true;
    std::string toast_ = "정전 감지 / 중계기를 복구하세요";
};
