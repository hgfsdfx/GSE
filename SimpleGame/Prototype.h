#pragma once
#include "Renderer.h"
#include "World.h"
#include <array>

class Prototype {
public:
    Prototype(Renderer& renderer, const std::filesystem::path& savePath);
    void Update(float dt);
    void Draw();
    void Key(unsigned char key, bool down);
    void ReleaseKeys();
    bool Save();
private:
    Point Project(double x, double y, float height = 0) const;
    void Ground(const Chunk& chunk);
    void DrawBuilding(const Building& building, const ChunkKey& key);
    void DrawDevice(const Device& device);
    void DrawPlayer();
    void Hud();
    void Hack(const Device& target);
    void Notify(const std::string& message);
    int StreamRadius() const;
    Renderer& r_;
    World world_;
    std::filesystem::path savePath_;
    WorldPoint player_{82, 245}, camera_{82,245};
    std::array<bool, 256> keys_{};
    Device target_, hacking_;
    float time_ = 0, zoom_ = 1, hackProgress_ = 0, trace_ = 0, lockdown_ = 0;
    float toastTime_ = 8, saveTime_ = 0;
    bool scan_ = true, paused_ = false, moving_ = false, saveAllowed_ = true;
    std::string toast_ = "GRID FAILURE DETECTED - RESTORE THE RELAY";
};
