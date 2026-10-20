#include "stdafx.h"
#include "WorldMonitor.h"
#include "LevelOne.h"
#include <Windows.h>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace
{
    using Clock = std::chrono::steady_clock;

    long long Timestamp()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    std::string JsonString(const std::string& value)
    {
        std::ostringstream out;
        out << '"';
        for (unsigned char c : value)
        {
            switch (c)
            {
                case '"':
                    out << "\\\"";
                    break;
                case '\\':
                    out << "\\\\";
                    break;
                case '\n':
                    out << "\\n";
                    break;
                case '\r':
                    out << "\\r";
                    break;
                case '\t':
                    out << "\\t";
                    break;
                default:
                    if (c < 32)
                    {
                        out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << int(c)
                            << std::dec;
                    }
                    else
                    {
                        out << char(c);
                    }
                    break;
            }
        }
        out << '"';
        return out.str();
    }

    double Number(double value)
    {
        return std::isfinite(value) ? value : 0;
    }

    void PointJson(std::ostream& out, WorldPoint point)
    {
        out << "{\"x\":" << Number(point.x) << ",\"y\":" << Number(point.y) << '}';
    }

    std::string Snapshot(const LevelOne& level,
                         const World& world,
                         bool paused,
                         const std::string& session,
                         uint64_t sequence)
    {
        const auto& society = level.Citizens();
        const auto residents = level.Scene().Actors<NpcActor>();
        const auto enemies = level.Enemies();
        size_t alive = 0, bosses = 0;
        for (const auto& npc : residents)
        {
            alive += npc.health > 0 ? 1 : 0;
        }
        for (const auto& enemy : enemies)
        {
            bosses += enemy.kind == EnemyKind::Boss ? 1 : 0;
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(10) << std::boolalpha
            << "{\"schemaVersion\":1,\"session\":" << JsonString(session)
            << ",\"sequence\":" << sequence << ",\"timestampMs\":" << Timestamp()
            << ",\"paused\":" << paused << ",\"counts\":{\"npcs\":" << residents.size()
            << ",\"aliveNpcs\":" << alive << ",\"expectedNpcs\":" << Society::Population
            << ",\"towns\":" << Society::TownCount
            << ",\"houses\":" << Society::TownCount * Society::HousesPerTown
            << ",\"monsters\":" << enemies.size()
            << ",\"regularMonsters\":" << enemies.size() - bosses << ",\"bosses\":" << bosses
            << ",\"monsterLimit\":"
            << (level.Boss() ? LevelOne::BossEnemyLimit : LevelOne::EnemyLimit)
            << ",\"respawnIntervalSeconds\":" << LevelOne::EnemyRespawnInterval
            << ",\"loot\":" << level.Loot().size() << ",\"loadedChunks\":" << world.Chunks().size()
            << "},\"player\":{\"health\":" << Number(level.Health())
            << ",\"maxHealth\":" << Number(level.MaxHealth()) << ",\"dead\":" << level.Dead()
            << ",\"level\":" << level.Level() << ",\"credits\":" << society.credits
            << ",\"cargo\":" << society.cargo << ",\"insight\":" << society.insight
            << ",\"wanted\":" << Number(society.wanted) << ",\"position\":";
        PointJson(out, level.Player().WorldPosition());
        out << "},\"society\":{\"trades\":" << society.trades << ",\"raids\":" << society.raids
            << ",\"hunts\":" << society.hunts << ",\"lettersSent\":" << society.lettersSent
            << ",\"lettersDelivered\":" << society.lettersDelivered
            << ",\"lettersInTransit\":" << society.LettersInTransit()
            << ",\"rumorVisits\":" << society.rumorVisits
            << ",\"migrations\":" << society.migrations
            << ",\"simulationTime\":" << society.SimulationTime()
            << ",\"objective\":" << JsonString(society.Objective()) << "},\"towns\":[";
        for (int i = 0; i < Society::TownCount; ++i)
        {
            if (i)
            {
                out << ',';
            }
            const auto town = society.Town(level.Scene(), i);
            int population = 0, living = 0, present = 0;
            long long credits = 0, cargo = 0, insight = 0;
            double health = 0, maxHealth = 0;
            std::array<int, 6> jobs{};
            std::array<int, 8> actions{};
            for (const auto& npc : residents)
            {
                present +=
                    society.NearbyTown(npc.WorldPosition(), Society::TownSafeRadius) == i ? 1 : 0;
                if (npc.home != i)
                {
                    continue;
                }
                ++population;
                living += npc.health > 0 ? 1 : 0;
                health += Number(npc.health);
                maxHealth += Number(npc.MaxHealth());
                credits += npc.credits;
                cargo += npc.cargo;
                insight += npc.insight;
                ++jobs[size_t(npc.job)];
                ++actions[size_t(npc.action)];
            }
            out << "{\"id\":" << i << ",\"name\":" << JsonString(Society::TownName(i))
                << ",\"position\":";
            PointJson(out, Society::TownCenter(i));
            out << ",\"safeRadius\":" << Society::TownSafeRadius
                << ",\"housingCapacity\":" << Society::HousesPerTown
                << ",\"vacantHouses\":" << Society::HousesPerTown - population
                << ",\"prosperity\":" << (town ? town->reports[i].prosperity : 0)
                << ",\"safety\":" << (town ? town->reports[i].safety : 0)
                << ",\"learning\":" << (town ? town->reports[i].learning : 0)
                << ",\"treasury\":" << (town ? town->treasury : 0)
                << ",\"stock\":" << (town ? town->stock : 0) << ",\"residents\":" << population
                << ",\"aliveResidents\":" << living << ",\"presentNpcs\":" << present
                << ",\"residentHealth\":" << health << ",\"residentMaxHealth\":" << maxHealth
                << ",\"residentCredits\":" << credits << ",\"residentCargo\":" << cargo
                << ",\"residentInsight\":" << insight << ",\"jobs\":[";
            for (size_t j = 0; j < jobs.size(); ++j)
            {
                if (j)
                {
                    out << ',';
                }
                out << jobs[j];
            }
            out << "],\"actions\":[";
            for (size_t j = 0; j < actions.size(); ++j)
            {
                if (j)
                {
                    out << ',';
                }
                out << actions[j];
            }
            out << "]}";
        }
        out << "],\"npcs\":[";
        bool comma = false;
        for (const auto& npc : residents)
        {
            if (comma)
            {
                out << ',';
            }
            comma = true;
            const auto target = dynamic_cast<const NpcActor*>(level.Scene().Find(npc.target));
            out << "{\"id\":" << npc.identity << ",\"name\":" << JsonString(npc.Name())
                << ",\"home\":" << npc.home << ",\"houseSlot\":" << npc.houseSlot
                << ",\"housePosition\":";
            PointJson(out, npc.HomePosition());
            out << ",\"migrationCooldown\":" << Number(npc.migrationCooldown)
                << ",\"jobId\":" << int(npc.job)
                << ",\"job\":" << JsonString(Society::JobName(npc.job))
                << ",\"actionId\":" << int(npc.action)
                << ",\"action\":" << JsonString(Society::ActionName(npc.action))
                << ",\"alive\":" << (npc.health > 0) << ",\"health\":" << Number(npc.health)
                << ",\"maxHealth\":" << Number(npc.MaxHealth()) << ",\"credits\":" << npc.credits
                << ",\"cargo\":" << npc.cargo << ",\"insight\":" << npc.insight
                << ",\"notoriety\":" << Number(npc.notoriety)
                << ",\"recoverySeconds\":" << Number(npc.recovery)
                << ",\"attackCooldown\":" << Number(npc.attackCooldown)
                << ",\"workSeconds\":" << Number(npc.work)
                << ",\"decisions\":" << JsonString(std::to_string(npc.decisions))
                << ",\"targetNpc\":" << (target ? target->identity : 0)
                << ",\"againstPlayer\":" << npc.againstPlayer
                << ",\"destinationTown\":" << npc.destinationTown
                << ",\"inSafeZone\":" << Society::InTownSafeZone(npc.WorldPosition())
                << ",\"position\":";
            PointJson(out, npc.WorldPosition());
            out << ",\"destination\":";
            PointJson(out, npc.destination);
            out << ",\"weights\":{\"survival\":" << Number(npc.survivalWeight)
                << ",\"wealth\":" << Number(npc.wealthWeight)
                << ",\"insight\":" << Number(npc.insightWeight)
                << ",\"plunder\":" << Number(npc.plunderWeight) << "},\"reports\":[";
            bool reportComma = false;
            for (int i = 0; i < Society::TownCount; ++i)
            {
                const auto& report = npc.reports[i];
                if (!report.revision)
                {
                    continue;
                }
                if (reportComma)
                {
                    out << ',';
                }
                reportComma = true;
                out << "{\"town\":" << i << ",\"timestamp\":" << report.timestamp
                    << ",\"prosperity\":" << report.prosperity << ",\"safety\":" << report.safety
                    << ",\"learning\":" << report.learning
                    << ",\"population\":" << report.population << '}';
            }
            out << "]}";
        }
        out << "],\"houses\":[";
        comma = false;
        for (const auto& house : level.Scene().Actors<HouseActor>())
        {
            if (comma)
            {
                out << ',';
            }
            comma = true;
            out << "{\"town\":" << house.town << ",\"slot\":" << house.slot
                << ",\"owner\":" << house.owner << ",\"position\":";
            PointJson(out, Society::HouseCenter(house.town, house.slot));
            out << '}';
        }
        out << "],\"monsters\":[";
        comma = false;
        for (const auto& enemy : enemies)
        {
            if (comma)
            {
                out << ',';
            }
            comma = true;
            out << "{\"id\":" << JsonString(std::to_string(enemy.persistentId))
                << ",\"kind\":" << int(enemy.kind) << ",\"health\":" << Number(enemy.health)
                << ",\"maxHealth\":" << Number(enemy.maxHealth) << ",\"position\":";
            PointJson(out, enemy.WorldPosition());
            out << '}';
        }
        out << "],\"news\":[";
        comma = false;
        for (const auto& message : society.News())
        {
            if (comma)
            {
                out << ',';
            }
            comma = true;
            out << JsonString(message);
        }
        out << "]}";
        return out.str();
    }
} // namespace

struct WorldMonitor::Impl
{
    std::filesystem::path path;
    std::string session;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::string pending;
    bool stopping = false;
    Clock::time_point next{};
    uint64_t sequence = 0;

    ~Impl()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_one();
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void Run()
    {
        bool reported = false;
        for (;;)
        {
            std::string message;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock,
                          [&]
                          {
                              return stopping || !pending.empty();
                          });
                if (pending.empty() && stopping)
                {
                    break;
                }
                message.swap(pending);
            }
            bool ok = false;
            try
            {
                auto temporary = path;
                temporary += L".tmp";
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                file.write(message.data(), std::streamsize(message.size()));
                file.flush();
                const bool written = bool(file);
                file.close();
                ok = written && !file.fail()
                     && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
            catch (const std::exception&)
            {
                ok = false;
            }
            if (!ok && !reported)
            {
                std::cerr << "[Monitor] JSON 저장 실패 / " << path.u8string() << '\n';
            }
            reported = !ok;
        }
    }
};

WorldMonitor::WorldMonitor()
    : impl_(std::make_unique<Impl>())
{
}

WorldMonitor::~WorldMonitor() = default;

void WorldMonitor::Start(const std::filesystem::path& directory)
{
    if (impl_->worker.joinable())
    {
        return;
    }
    try
    {
        std::filesystem::create_directories(directory);
        impl_->path = directory / L"state.json";
        impl_->session = std::to_string(Timestamp()) + "-" + std::to_string(GetCurrentProcessId());
        impl_->worker = std::thread(
            [this]
            {
                impl_->Run();
            });
        std::cout << "[Monitor] JSON 상태: " << impl_->path.u8string() << " / 0.5초 갱신\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "[Monitor] 시작 실패 / " << error.what() << '\n';
    }
}

void WorldMonitor::Tick(const LevelOne& level, const World& world, bool paused)
{
    const auto now = Clock::now();
    if (!impl_->worker.joinable() || now < impl_->next)
    {
        return;
    }
    impl_->next = now + std::chrono::milliseconds(500);
    auto message = Snapshot(level, world, paused, impl_->session, ++impl_->sequence);
    {
        const std::lock_guard<std::mutex> lock(impl_->mutex);
        // At most one waiting snapshot; slow storage cannot accumulate a frame backlog.
        impl_->pending = std::move(message);
    }
    impl_->wake.notify_one();
}
