#include "stdafx.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>
#include <utility>
#include <Windows.h>

uint64_t World::Hash(uint64_t v) {
    v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ULL;
    v ^= v >> 27; v *= 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}
ChunkKey World::KeyAt(WorldPoint p) const {
    return { static_cast<int64_t>(std::floor(p.x / ChunkSize)), static_cast<int64_t>(std::floor(p.y / ChunkSize)) };
}
Chunk World::Generate(ChunkKey key) const {
    Chunk chunk; chunk.key = key;
    const uint64_t seed = Hash(Seed ^ Hash(static_cast<uint64_t>(key.x)) ^ (Hash(static_cast<uint64_t>(key.y)) << 1));
    for (int row = 0; row < 2; ++row) for (int col = 0; col < 2; ++col) {
        const uint64_t h = Hash(seed + row * 2 + col);
        chunk.buildings.push_back({ key.x * ChunkSize + 145 + col * 235,
            key.y * ChunkSize + 145 + row * 235, 180, 170,
            float(86 + h % 130), unsigned(h % 5), row == 0 && col == 0 });
    }
    return chunk;
}
void World::Stream(WorldPoint player, int radius) {
    const ChunkKey center = KeyAt(player);
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        if (std::abs(it->first.x - center.x) > radius || std::abs(it->first.y - center.y) > radius)
            it = chunks_.erase(it);
        else ++it;
    }
    for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
        const ChunkKey key{ center.x + x, center.y + y };
        if (chunks_.find(key) == chunks_.end()) chunks_.emplace(key, Generate(key));
    }
}
const ChunkChanges& World::Changes(const ChunkKey& key) const {
    static const ChunkChanges untouched;
    const auto it = changes_.find(key);
    return it == changes_.end() ? untouched : it->second;
}
bool World::HasOutage(const ChunkKey& key) const {
    if (key.x == 0 && key.y == 0) return true;
    return Hash(Hash(static_cast<uint64_t>(key.x)) ^ (Hash(static_cast<uint64_t>(key.y)) << 1) ^ Seed) % 7 == 0;
}
bool World::Powered(const ChunkKey& key) const { return !HasOutage(key) || Changes(key).eventSolved; }
bool World::CanWalk(WorldPoint point) const {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > CoordinateLimit || std::abs(point.y) > CoordinateLimit) return false;
    const auto it = chunks_.find(KeyAt(point));
    if (it == chunks_.end()) return false;
    for (const auto& b : it->second.buildings) {
        const bool overlapsBuilding = point.x > b.x - 8 && point.x < b.x + b.width + 8 &&
            point.y > b.y - 8 && point.y < b.y + b.depth + 8;
        if (!overlapsBuilding) continue;
        if (!b.accessRoom || !Changes(it->first).doorOpen) return false;
        // Room walls remain solid; only the south doorway is passable.
        const bool inside = point.x > b.x + 12 && point.x < b.x + b.width - 12 &&
            point.y > b.y + 12 && point.y < b.y + b.depth - 12;
        const bool doorway = std::abs(point.x - (b.x + b.width / 2)) < 23 &&
            point.y >= b.y + b.depth - 16;
        if (!inside && !doorway) return false;
    }
    return true;
}
std::vector<Device> World::Devices(const ChunkKey& key) const {
    const double x = key.x * ChunkSize, y = key.y * ChunkSize;
    return { {key,DeviceType::Power,{x+116,y+118}},
             {key,DeviceType::Camera,{x+346,y+153}},
             {key,DeviceType::Door,{x+235,y+324}} };
}
Device World::NearestDevice(WorldPoint point) const {
    Device best; best.distance = 125;
    for (const auto& entry : chunks_) for (auto device : Devices(entry.first)) {
        device.distance = std::hypot(device.position.x - point.x, device.position.y - point.y);
        if (device.distance < best.distance) { best = device; best.valid = true; }
    }
    return best;
}
int World::Credits() const {
    int64_t credits = 0;
    for (const auto& entry : changes_) credits += (entry.second.eventSolved ? 150 : 0) + (entry.second.dataTaken ? 75 : 0);
    return static_cast<int>(std::min<int64_t>(credits, std::numeric_limits<int>::max()));
}
bool World::Save(const std::filesystem::path& path, WorldPoint player) const {
    // Never write a save larger than this version of the loader can read.
    if(changes_.size()>1000000) return false;
    auto temp = path; temp += L".tmp";
    std::ofstream file(temp, std::ios::trunc);
    if (!file) return false;
    file << "GSE_NIGHT_CITY 1 " << Seed << '\n' << std::setprecision(17)
        << player.x << ' ' << player.y << '\n' << changes_.size() << '\n';
    for (const auto& entry : changes_) file << entry.first.x << ' ' << entry.first.y << ' '
        << entry.second.doorOpen << ' ' << entry.second.cameraOff << ' '
        << entry.second.eventSolved << ' ' << entry.second.dataTaken << '\n';
    file.flush();
    if (!file) return false;
    file.close();
    if (file.fail()) return false;
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
bool World::Load(const std::filesystem::path& path, WorldPoint& player) {
    std::ifstream file(path);
    if (!file) {
        std::error_code error;
        saveWarning_ = std::filesystem::exists(path,error) || bool(error);
        return false;
    }
    saveWarning_ = true;
    std::string header; int version = 0; uint64_t seed = 0; size_t count = 0;
    WorldPoint loaded;
    if (!(file >> header >> version >> seed >> loaded.x >> loaded.y >> count) ||
        header != "GSE_NIGHT_CITY" || version != 1 || seed != Seed || count > 1000000 ||
        !std::isfinite(loaded.x) || !std::isfinite(loaded.y) ||
        std::abs(loaded.x) > CoordinateLimit || std::abs(loaded.y) > CoordinateLimit) return false;
    std::map<ChunkKey, ChunkChanges> parsed;
    for (size_t i = 0; i < count; ++i) {
        ChunkKey key; int door, camera, event, data;
        if (!(file >> key.x >> key.y >> door >> camera >> event >> data) ||
            key.x < -1562500000LL || key.x > 1562500000LL || key.y < -1562500000LL || key.y > 1562500000LL ||
            door < 0 || door > 1 || camera < 0 || camera > 1 || event < 0 || event > 1 || data < 0 || data > 1 ||
            parsed.count(key)) return false;
        parsed[key] = {door != 0,camera != 0,event != 0,data != 0};
    }
    file >> std::ws;
    if (!file.eof()) return false;
    changes_ = std::move(parsed); player = loaded; saveWarning_ = false;
    return true;
}
