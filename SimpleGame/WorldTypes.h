#pragma once
#include <cstdint>
#include <vector>

struct WorldPoint
{
    double x = 0, y = 0;
};

struct ChunkKey
{
    int64_t x = 0, y = 0;

    bool operator<(const ChunkKey& other) const
    {
        return x != other.x ? x < other.x : y < other.y;
    }

    bool operator==(const ChunkKey& other) const
    {
        return x == other.x && y == other.y;
    }
};

struct Building
{
    double x, y;
    float width, depth, height;
    unsigned style;
    bool hackable;
};

struct Chunk
{
    ChunkKey key;
    std::vector<Building> buildings;
    uint64_t actorRoot = 0;
    std::vector<uint64_t> buildingActors;
    std::vector<uint64_t> deviceActors;
};

struct ChunkChanges
{
    bool lightsOff = false;
    bool cameraOff = false;
    bool eventSolved = false;
    bool dataTaken = false;
};
enum class DeviceType
{
    Power,
    Camera,
    Building
};

struct Device
{
    ChunkKey key;
    DeviceType type = DeviceType::Power;
    WorldPoint position;
    double distance = 0;
    bool valid = false;
};
