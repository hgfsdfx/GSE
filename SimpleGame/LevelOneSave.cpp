#include "stdafx.h"
#include "LevelOne.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <utility>
#include <Windows.h>

bool LevelOne::Save(const std::filesystem::path& path, const World& world) const
{
    if (saveBlocked_)
    {
        return false;
    }
    const auto player = Player().WorldPosition();
    auto temporary = path;
    temporary += L".tmp";
    std::ofstream file(temporary, std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file << "GSE_LEVEL_ONE 2\n"
         << std::setprecision(17) << seed_ << ' ' << randomState_ << ' ' << nextId_ << '\n'
         << player.x << ' ' << player.y << '\n'
         << Player().stats.level_ << ' ' << Player().stats.experience_ << ' '
         << Player().stats.weaponRank_ << ' ' << Player().stats.kills_ << ' '
         << Player().stats.chipsCollected_ << ' ' << Player().stats.health_ << ' ' << bossSpawned_
         << ' ' << cleared_ << '\n'
         << Enemies().size() << '\n';
    for (const auto& enemy : Enemies())
    {
        file << enemy.persistentId << ' ' << int(enemy.kind) << ' ' << enemy.WorldPosition().x
             << ' ' << enemy.WorldPosition().y << ' ' << enemy.health << ' ' << enemy.attackTimer
             << ' ' << enemy.pulseTimer << ' ' << enemy.spawnGrace << '\n';
    }
    file << Loot().size() << '\n';
    for (const auto& item : Loot())
    {
        file << int(item.kind) << ' ' << item.WorldPosition().x << ' ' << item.WorldPosition().y
             << ' ' << item.amount << ' ' << item.attracted << '\n';
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

bool LevelOne::Load(const std::filesystem::path& path, World& world)
{
    std::ifstream file(path);
    if (!file)
    {
        std::error_code error;
        saveBlocked_ = std::filesystem::exists(path, error) || bool(error);
        if (saveBlocked_)
        {
            Say("저장 파일 읽기 실패 / 원본 보존됨");
        }
        return false;
    }
    // Parse into temporary objects so a malformed save cannot partially change the run.
    saveBlocked_ = true;
    Say("저장 파일 오류 / 원본 보존됨");
    LevelOne loaded;
    World loadedWorld;
    WorldPoint position;
    std::string header;
    int version = 0, bossFlag = 0, clearedFlag = 0;
    if (!(file >> header >> version) || header != "GSE_LEVEL_ONE" || (version != 1 && version != 2)
        || !(file >> loaded.seed_ >> loaded.randomState_ >> loaded.nextId_ >> position.x
             >> position.y >> loaded.Player().stats.level_ >> loaded.Player().stats.experience_
             >> loaded.Player().stats.weaponRank_ >> loaded.Player().stats.kills_
             >> loaded.Player().stats.chipsCollected_ >> loaded.Player().stats.health_ >> bossFlag
             >> clearedFlag)
        || !std::isfinite(position.x) || !std::isfinite(position.y)
        || std::abs(position.x) > World::CoordinateLimit
        || std::abs(position.y) > World::CoordinateLimit || loaded.Player().stats.level_ < 1
        || loaded.Player().stats.level_ > 30 || loaded.Player().stats.experience_ < 0
        || loaded.Player().stats.experience_ >= loaded.NextExperience()
        || loaded.Player().stats.weaponRank_ < 0 || loaded.Player().stats.weaponRank_ > 10
        || loaded.Player().stats.kills_ < 0 || loaded.Player().stats.kills_ > 1000000
        || loaded.Player().stats.chipsCollected_ < 0
        || loaded.Player().stats.chipsCollected_ > 1000000
        || !std::isfinite(loaded.Player().stats.health_) || loaded.Player().stats.health_ < 0
        || loaded.Player().stats.health_ > loaded.MaxHealth() || bossFlag < 0 || bossFlag > 1
        || clearedFlag < 0 || clearedFlag > 1 || loaded.nextId_ == 0
        || loaded.nextId_ > 1000000000000ULL)
    {
        return false;
    }
    loaded.bossSpawned_ = bossFlag != 0;
    loaded.cleared_ = clearedFlag != 0;
    if (loaded.cleared_ && !loaded.bossSpawned_)
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
        auto& enemy = loaded.scene_.Spawn<EnemyActor>(loaded.gameplayRoot_);
        WorldPoint enemyPosition;
        int kind = 0;
        if (!(file >> enemy.persistentId >> kind >> enemyPosition.x >> enemyPosition.y
              >> enemy.health >> enemy.attackTimer >> enemy.pulseTimer >> enemy.spawnGrace)
            || kind < 0 || kind > 2 || enemy.persistentId == 0
            || enemy.persistentId >= loaded.nextId_ || !ids.insert(enemy.persistentId).second
            || !std::isfinite(enemyPosition.x) || !std::isfinite(enemyPosition.y)
            || std::abs(enemyPosition.x) > World::CoordinateLimit
            || std::abs(enemyPosition.y) > World::CoordinateLimit
            || (kind == int(EnemyKind::Boss) && !loaded.InDistrict(enemyPosition))
            || !std::isfinite(enemy.health) || !std::isfinite(enemy.attackTimer)
            || !std::isfinite(enemy.pulseTimer) || !std::isfinite(enemy.spawnGrace))
        {
            return false;
        }
        enemy.kind = static_cast<EnemyKind>(kind);
        if (enemy.kind == EnemyKind::Boss)
        {
            enemy.arenaMinimum = {755, 755};
            enemy.arenaMaximum = {1170, 1170};
        }
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
        enemy.SetWorldPosition(enemyPosition);
    }
    if (bosses != (loaded.bossSpawned_ && !loaded.cleared_ ? 1 : 0))
    {
        return false;
    }
    if (!(file >> count) || count > 1024)
    {
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        auto& item = loaded.scene_.Spawn<LootActor>(loaded.gameplayRoot_);
        WorldPoint itemPosition;
        int kind = 0, attracted = 0;
        if (!(file >> kind >> itemPosition.x >> itemPosition.y >> item.amount >> attracted)
            || kind < 0 || kind > 2 || !std::isfinite(itemPosition.x)
            || !std::isfinite(itemPosition.y) || std::abs(itemPosition.x) > World::CoordinateLimit
            || std::abs(itemPosition.y) > World::CoordinateLimit || item.amount < 1
            || item.amount > 1000000 || attracted < 0 || attracted > 1)
        {
            return false;
        }
        item.kind = static_cast<LootKind>(kind);
        item.attracted = attracted != 0;
        item.SetWorldPosition(itemPosition);
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
    // Validate each saved location without retaining an unbounded set of world chunks.
    auto restorePosition = [&](WorldPoint& point)
    {
        loadedWorld.Stream(point, 0);
        if (loadedWorld.CanWalk(point))
        {
            return true;
        }
        const auto key = loadedWorld.KeyAt(point);
        if (version == 1 && loadedWorld.Changes(key).lightsOff)
        {
            const double x = key.x * World::ChunkSize + 145;
            const double y = key.y * World::ChunkSize + 145;
            // Legacy door-open saves could contain actors/loot inside the cutaway room.
            if (point.x > x - 8 && point.x < x + 188 && point.y > y - 8 && point.y < y + 178)
            {
                point = {x + 90, y + 195};
                return loadedWorld.CanWalk(point);
            }
        }
        return false;
    };
    for (auto& enemy : loaded.scene_.Actors<EnemyActor>())
    {
        auto restored = enemy.WorldPosition();
        if (!restorePosition(restored))
        {
            return false;
        }
        enemy.SetWorldPosition(restored);
    }
    for (auto& item : loaded.scene_.Actors<LootActor>())
    {
        auto restored = item.WorldPosition();
        if (!restorePosition(restored))
        {
            return false;
        }
        item.SetWorldPosition(restored);
    }
    loadedWorld.Stream(position, 3);
    loaded.Player().SetWorldPosition(position);
    loaded.navigation_.BuildFlow(loadedWorld, position);
    loaded.PopulateEnemies(loadedWorld, position);
    loaded.spawnTimer_ = 0;
    loaded.Say(loaded.Dead()     ? "신호 끊김 / R 키로 새로 시작"
               : loaded.cleared_ ? "레벨 1 완료 / 도시 순찰 재개"
                                 : "레벨 1 / 진행 상황을 불러왔습니다");
    world.BindScene(nullptr);
    *this = std::move(loaded);
    world = std::move(loadedWorld);
    world.BindScene(&scene_);
    return true;
}
