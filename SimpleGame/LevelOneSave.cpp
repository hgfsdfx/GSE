#include "stdafx.h"
#include "LevelOne.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <utility>
#include <Windows.h>

bool LevelOne::Save(const std::filesystem::path& path, const World& world, WorldPoint player) const
{
    if (saveBlocked_)
    {
        return false;
    }
    auto temporary = path;
    temporary += L".tmp";
    std::ofstream file(temporary, std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file << "GSE_LEVEL_ONE 1\n"
         << std::setprecision(17) << seed_ << ' ' << randomState_ << ' ' << nextId_ << '\n'
         << player.x << ' ' << player.y << '\n'
         << level_ << ' ' << experience_ << ' ' << weaponRank_ << ' ' << kills_ << ' '
         << chipsCollected_ << ' ' << health_ << ' ' << bossSpawned_ << ' ' << cleared_ << '\n'
         << enemies_.size() << '\n';
    for (const auto& enemy : enemies_)
    {
        file << enemy.id << ' ' << int(enemy.kind) << ' ' << enemy.position.x << ' '
             << enemy.position.y << ' ' << enemy.health << ' ' << enemy.attackTimer << ' '
             << enemy.pulseTimer << ' ' << enemy.spawnGrace << '\n';
    }
    file << loot_.size() << '\n';
    for (const auto& item : loot_)
    {
        file << int(item.kind) << ' ' << item.position.x << ' ' << item.position.y << ' '
             << item.amount << ' ' << item.attracted << '\n';
    }
    if (!world.WriteChanges(file))
    {
        return false;
    }
    file.flush();
    if (!file)
    {
        return false;
    }
    file.close();
    if (file.fail())
    {
        return false;
    }
    return MoveFileExW(temporary.c_str(),
                       path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
           != 0;
}

bool LevelOne::Load(const std::filesystem::path& path, World& world, WorldPoint& player)
{
    std::ifstream file(path);
    if (!file)
    {
        std::error_code error;
        saveBlocked_ = std::filesystem::exists(path, error) || bool(error);
        if (saveBlocked_)
        {
            Say("LEVEL SAVE UNREADABLE / ORIGINAL FILE PRESERVED");
        }
        return false;
    }
    // Parse into temporary objects so a malformed save cannot partially change the run.
    saveBlocked_ = true;
    Say("LEVEL SAVE INVALID / ORIGINAL FILE PRESERVED");
    LevelOne loaded;
    World loadedWorld;
    WorldPoint position;
    std::string header;
    int version = 0, bossFlag = 0, clearedFlag = 0;
    if (!(file >> header >> version) || header != "GSE_LEVEL_ONE" || version != 1
        || !(file >> loaded.seed_ >> loaded.randomState_ >> loaded.nextId_ >> position.x
             >> position.y >> loaded.level_ >> loaded.experience_ >> loaded.weaponRank_
             >> loaded.kills_ >> loaded.chipsCollected_ >> loaded.health_ >> bossFlag
             >> clearedFlag)
        || !std::isfinite(position.x) || !std::isfinite(position.y)
        || std::abs(position.x) > World::CoordinateLimit
        || std::abs(position.y) > World::CoordinateLimit || loaded.level_ < 1 || loaded.level_ > 30
        || loaded.experience_ < 0 || loaded.experience_ >= loaded.NextExperience()
        || loaded.weaponRank_ < 0 || loaded.weaponRank_ > 10 || loaded.kills_ < 0
        || loaded.kills_ > 1000000 || loaded.chipsCollected_ < 0 || loaded.chipsCollected_ > 1000000
        || !std::isfinite(loaded.health_) || loaded.health_ < 0
        || loaded.health_ > loaded.MaxHealth() || bossFlag < 0 || bossFlag > 1 || clearedFlag < 0
        || clearedFlag > 1 || loaded.nextId_ == 0 || loaded.nextId_ > 1000000000000ULL)
    {
        return false;
    }
    loaded.bossSpawned_ = bossFlag != 0;
    loaded.cleared_ = clearedFlag != 0;
    if (loaded.cleared_ && (!loaded.bossSpawned_ || loaded.Dead()))
    {
        return false;
    }
    size_t count = 0;
    if (!(file >> count) || count > 32)
    {
        return false;
    }
    std::set<uint64_t> ids;
    int bosses = 0;
    for (size_t i = 0; i < count; ++i)
    {
        LevelEnemy enemy;
        int kind = 0;
        if (!(file >> enemy.id >> kind >> enemy.position.x >> enemy.position.y >> enemy.health
              >> enemy.attackTimer >> enemy.pulseTimer >> enemy.spawnGrace)
            || kind < 0 || kind > 2 || enemy.id == 0 || enemy.id >= loaded.nextId_
            || !ids.insert(enemy.id).second || !std::isfinite(enemy.position.x)
            || !std::isfinite(enemy.position.y) || !loaded.InDistrict(enemy.position)
            || !std::isfinite(enemy.health) || !std::isfinite(enemy.attackTimer)
            || !std::isfinite(enemy.pulseTimer) || !std::isfinite(enemy.spawnGrace))
        {
            return false;
        }
        enemy.kind = static_cast<EnemyKind>(kind);
        enemy.maxHealth = enemy.kind == EnemyKind::Boss      ? 800.f
                          : enemy.kind == EnemyKind::Armored ? 60.f
                                                             : 28.f;
        if (enemy.health <= 0 || enemy.health > enemy.maxHealth)
        {
            return false;
        }
        enemy.attackTimer = std::clamp(enemy.attackTimer, 1.5f, 30.f);
        enemy.pulseTimer = std::clamp(enemy.pulseTimer, 2.f, 30.f);
        enemy.spawnGrace = std::clamp(enemy.spawnGrace, 1.5f, 3.f);
        bosses += enemy.kind == EnemyKind::Boss ? 1 : 0;
        loaded.enemies_.push_back(enemy);
    }
    if (bosses != (loaded.bossSpawned_ && !loaded.cleared_ ? 1 : 0)
        || (loaded.cleared_ && !loaded.enemies_.empty()))
    {
        return false;
    }
    if (!(file >> count) || count > 1024)
    {
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        LevelLoot item;
        int kind = 0, attracted = 0;
        if (!(file >> kind >> item.position.x >> item.position.y >> item.amount >> attracted)
            || kind < 0 || kind > 2 || !std::isfinite(item.position.x)
            || !std::isfinite(item.position.y) || !loaded.InDistrict(item.position)
            || item.amount < 1 || item.amount > 1000000 || attracted < 0 || attracted > 1)
        {
            return false;
        }
        item.kind = static_cast<LootKind>(kind);
        item.attracted = attracted != 0;
        loaded.loot_.push_back(item);
    }
    loadedWorld.ConfigureLevelOne(loaded.seed_);
    if (!loadedWorld.ReadChanges(file))
    {
        return false;
    }
    file >> std::ws;
    if (!file.eof())
    {
        return false;
    }
    loadedWorld.Stream(position, 3);
    if (!loadedWorld.CanWalk(position))
    {
        const auto key = loadedWorld.KeyAt(position);
        position = {key.x * World::ChunkSize + 50, key.y * World::ChunkSize + 50};
    }
    // Validate saved actors against the same seeded terrain even when player is outside the district.
    loadedWorld.Stream(loaded.BossArena(), 3);
    for (const auto& enemy : loaded.enemies_)
    {
        if (!loadedWorld.CanWalk(enemy.position))
        {
            return false;
        }
    }
    for (const auto& item : loaded.loot_)
    {
        if (!loadedWorld.CanWalk(item.position))
        {
            return false;
        }
    }
    loadedWorld.Stream(position, 3);
    loaded.BuildFlow(loadedWorld, position);
    loaded.Say(loaded.cleared_ ? "LEVEL 1 CLEARED / PROGRESS RESTORED"
               : loaded.Dead() ? "SIGNAL LOST / PRESS R FOR A NEW RUN"
                               : "LEVEL 1 / PROGRESS RESTORED");
    *this = std::move(loaded);
    world = std::move(loadedWorld);
    player = position;
    return true;
}
