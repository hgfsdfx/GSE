#pragma once
#include "WorldTypes.h"
#include <filesystem>
#include <iosfwd>
#include <map>
#include <vector>

class SceneGraph;

class World
{
  public:

    static constexpr double ChunkSize = 640.0;
    static constexpr double CoordinateLimit = 1.0e12;
    static constexpr uint64_t Seed = 0x4e494748544c494eULL;
    void ConfigureLevelOne(uint64_t seed);
    void BindScene(SceneGraph* scene);
    void RefreshColliders();
    bool WriteChanges(std::ostream& stream) const;
    bool ReadChanges(std::istream& stream);
    ChunkKey KeyAt(WorldPoint point) const;
    void Stream(WorldPoint player, int radius);
    bool CanWalk(WorldPoint point) const;
    Device NearestDevice(WorldPoint point) const;
    std::vector<Device> Devices(const ChunkKey& key) const;
    const ChunkChanges& Changes(const ChunkKey& key) const;

    ChunkChanges& Change(const ChunkKey& key)
    {
        return changes_[key];
    }

    const std::map<ChunkKey, Chunk>& Chunks() const
    {
        return chunks_;
    }

    bool Powered(const ChunkKey& key) const;
    bool HasOutage(const ChunkKey& key) const;
    int Credits() const;
    bool Save(const std::filesystem::path& path, WorldPoint player) const;
    bool Load(const std::filesystem::path& path, WorldPoint& player);

    bool HasSaveWarning() const
    {
        return saveWarning_;
    }

    static uint64_t Hash(uint64_t value);

  private:

    Chunk Generate(ChunkKey key) const;
    void SpawnChunkActors(Chunk& chunk);
    SceneGraph* scene_ = nullptr;
    std::map<ChunkKey, std::vector<uint64_t>> colliders_;
    std::map<ChunkKey, Chunk> chunks_;
    std::map<ChunkKey, ChunkChanges> changes_;
    bool saveWarning_ = false;
    uint64_t generationSeed_ = Seed;
    bool levelOne_ = false;
};
