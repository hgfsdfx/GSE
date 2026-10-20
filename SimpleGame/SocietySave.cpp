#include "stdafx.h"
#include "Society.h"
#include "SceneGraph.h"
#include "World.h"
#include <cmath>
#include <istream>
#include <ostream>
#include <map>

namespace
{
    bool InRange(double value, double low, double high)
    {
        return std::isfinite(value) && value >= low && value <= high;
    }

    void WriteReports(std::ostream& stream,
                      const std::array<TownReport, Society::TownCount>& reports)
    {
        for (const auto& report : reports)
        {
            stream << ' ' << report.revision << ' ' << report.timestamp << ' ' << report.prosperity
                   << ' ' << report.safety << ' ' << report.learning << ' ' << report.population;
        }
        stream << '\n';
    }

    bool ReadReports(std::istream& stream,
                     std::array<TownReport, Society::TownCount>& reports,
                     double time,
                     uint64_t nextRevision)
    {
        for (auto& report : reports)
        {
            if (!(stream >> report.revision >> report.timestamp >> report.prosperity
                  >> report.safety >> report.learning >> report.population)
                || report.revision >= nextRevision || !InRange(report.timestamp, 0, time)
                || !InRange(report.prosperity, 0, 2) || !InRange(report.safety, 0, 1)
                || !InRange(report.learning, 0, 2) || report.population < 0
                || report.population > Society::HousesPerTown)
            {
                return false;
            }
        }
        return true;
    }
} // namespace

bool Society::Write(std::ostream& stream, const SceneGraph& scene) const
{
    stream << "SOCIETY 3\n"
           << credits << ' ' << cargo << ' ' << insight << ' ' << wanted << ' ' << pkEnabled << ' '
           << story << ' ' << escapeTime << ' ' << trades << ' ' << raids << ' ' << hunts << '\n'
           << societyTime_ << ' ' << nextReport_ << ' ' << letterTimer_ << ' ' << lettersSent << ' '
           << lettersDelivered << ' ' << rumorVisits << ' ' << migrations << '\n';
    for (int i = 0; i < TownCount; ++i)
    {
        const auto town = Town(scene, i);
        if (!town)
        {
            return false;
        }
        stream << town->treasury << ' ' << town->stock;
        WriteReports(stream, town->reports);
    }
    const auto residents = scene.Actors<NpcActor>();
    if (residents.size() != Population)
    {
        return false;
    }
    stream << residents.size() << '\n';
    for (const auto& npc : residents)
    {
        const auto house = House(scene, npc.home, npc.houseSlot);
        if (!house || house->owner != npc.identity)
        {
            return false;
        }
        const auto target = dynamic_cast<const NpcActor*>(scene.Find(npc.target));
        const auto p = npc.WorldPosition();
        stream << npc.identity << ' ' << npc.home << ' ' << int(npc.job) << ' ' << int(npc.action)
               << ' ' << p.x << ' ' << p.y << ' ' << npc.health << ' ' << npc.credits << ' '
               << npc.cargo << ' ' << npc.insight << ' ' << npc.survivalWeight << ' '
               << npc.wealthWeight << ' ' << npc.insightWeight << ' ' << npc.plunderWeight << ' '
               << npc.notoriety << ' ' << npc.recovery << ' ' << npc.work << ' '
               << npc.attackCooldown << ' ' << (target ? target->identity : 0) << ' '
               << npc.againstPlayer << ' ' << npc.destinationTown << ' ' << npc.destination.x << ' '
               << npc.destination.y << ' ' << npc.decisions << ' ' << npc.houseSlot << ' '
               << npc.migrationCooldown;
        WriteReports(stream, npc.reports);
    }
    stream << letters_.size() << '\n';
    for (const auto& letter : letters_)
    {
        stream << letter.from << ' ' << letter.to << ' ' << letter.remaining;
        WriteReports(stream, letter.reports);
    }
    return bool(stream);
}

bool Society::Read(std::istream& stream, SceneGraph& scene, ActorId root)
{
    std::string header;
    int version = 0, pk = 0;
    if (!(stream >> header >> version >> credits >> cargo >> insight >> wanted >> pk >> story
          >> escapeTime >> trades >> raids >> hunts)
        || header != "SOCIETY" || version < 1 || version > 3 || credits < 0 || credits > 1000000
        || cargo < 0 || cargo > 10000 || insight < 0 || insight > 1000000
        || !InRange(wanted, 0, 100) || pk < 0 || pk > 1 || story < 0 || story > 4
        || !InRange(escapeTime, 0, 75))
    {
        return false;
    }
    pkEnabled = pk != 0;
    if (version == 3)
    {
        if (!(stream >> societyTime_ >> nextReport_ >> letterTimer_ >> lettersSent
              >> lettersDelivered >> rumorVisits >> migrations)
            || !InRange(societyTime_, 0, 1.0e12) || !InRange(letterTimer_, 0, 12)
            || nextReport_ == 0 || nextReport_ > 1000000000000000ULL
            || lettersDelivered > lettersSent)
        {
            return false;
        }
    }
    const int savedTowns = version == 3 ? TownCount : 3;
    const int savedPopulation = version == 1 ? 18 : version == 2 ? 36 : Population;
    for (int i = 0; i < TownCount; ++i)
    {
        auto& town = scene.Spawn<TownActor>(root, i);
        towns_[i] = town.Id();
        if (i < savedTowns)
        {
            if (!(stream >> town.treasury >> town.stock) || town.treasury < 0
                || town.treasury > 1000000 || town.stock < 0 || town.stock > 10000)
            {
                return false;
            }
            if (version == 3 && !ReadReports(stream, town.reports, societyTime_, nextReport_))
            {
                return false;
            }
        }
    }
    InitializeHouses(scene, root);
    size_t count = 0;
    if (!(stream >> count) || count != size_t(savedPopulation))
    {
        return false;
    }
    std::map<int, ActorId> identities;
    std::map<ActorId, int> targets;
    for (size_t i = 0; i < count; ++i)
    {
        int identity, home, job, action, target, against;
        WorldPoint position;
        if (!(stream >> identity >> home >> job >> action) || identity < 1
            || identity > savedPopulation || identities.count(identity) || home < 0
            || home >= savedTowns || (version < 3 && home != ((identity - 1) % 18) / 6)
            || job != (identity - 1) % 6 || action < 0
            || action > int(version == 3 ? NpcAction::Relocate : NpcAction::Recover))
        {
            return false;
        }
        auto& npc = scene.Spawn<NpcActor>(root,
                                          identity,
                                          version == 3 ? home : HomeTown(identity),
                                          static_cast<NpcJob>(job));
        if (!(stream >> position.x >> position.y >> npc.health >> npc.credits >> npc.cargo
              >> npc.insight >> npc.survivalWeight >> npc.wealthWeight >> npc.insightWeight
              >> npc.plunderWeight >> npc.notoriety >> npc.recovery >> npc.work
              >> npc.attackCooldown >> target >> against >> npc.destinationTown >> npc.destination.x
              >> npc.destination.y >> npc.decisions)
            || !InRange(position.x, -World::CoordinateLimit, World::CoordinateLimit)
            || !InRange(position.y, -World::CoordinateLimit, World::CoordinateLimit)
            || npc.credits < 0 || npc.credits > 1000000 || npc.cargo < 0 || npc.cargo > 10000
            || npc.insight < 0 || npc.insight > 1000000 || !InRange(npc.health, 0, npc.MaxHealth())
            || !InRange(npc.survivalWeight, 0, 3) || !InRange(npc.wealthWeight, 0, 3)
            || !InRange(npc.insightWeight, 0, 3) || !InRange(npc.plunderWeight, 0, 1)
            || !InRange(npc.notoriety, 0, 100) || !InRange(npc.recovery, 0, 35)
            || !InRange(npc.work, 0, 6.1) || !InRange(npc.attackCooldown, 0, 2) || target < 0
            || target > savedPopulation || against < 0 || against > 1 || npc.destinationTown < 0
            || npc.destinationTown >= savedTowns
            || !InRange(npc.destination.x, -World::CoordinateLimit, World::CoordinateLimit)
            || !InRange(npc.destination.y, -World::CoordinateLimit, World::CoordinateLimit))
        {
            return false;
        }
        npc.SetWorldPosition(position);
        npc.action = static_cast<NpcAction>(action);
        npc.againstPlayer = against != 0;
        if (version == 3)
        {
            if (!(stream >> npc.houseSlot >> npc.migrationCooldown) || npc.houseSlot < 0
                || npc.houseSlot >= HousesPerTown || !InRange(npc.migrationCooldown, 0, 300)
                || !ReadReports(stream, npc.reports, societyTime_, nextReport_))
            {
                return false;
            }
        }
        else
        {
            // Preserve identities, jobs, wealth and growth; relocate legacy residents to new homes.
            npc.SetWorldPosition(npc.HomePosition());
            npc.destinationTown = npc.home;
            npc.destination = npc.HomePosition();
            npc.action = NpcAction::Rest;
            npc.againstPlayer = false;
            npc.work = 0;
            target = 0;
        }
        auto house = House(scene, npc.home, npc.houseSlot);
        if (!house || house->owner)
        {
            return false;
        }
        house->owner = npc.identity;
        const float work = npc.work;
        SetDestination(npc, npc.destination);
        npc.work = work;
        npc.goalAssigned = version == 3;
        identities[identity] = npc.Id();
        targets[npc.Id()] = target;
    }
    for (int identity = savedPopulation + 1; identity <= Population; ++identity)
    {
        auto& npc = scene.Spawn<NpcActor>(root,
                                          identity,
                                          HomeTown(identity),
                                          static_cast<NpcJob>((identity - 1) % 6));
        auto house = House(scene, npc.home, npc.houseSlot);
        if (!house || house->owner)
        {
            return false;
        }
        house->owner = identity;
    }
    for (const auto& entry : targets)
    {
        auto npc = static_cast<NpcActor*>(scene.Find(entry.first));
        npc->target = entry.second ? identities.at(entry.second) : 0;
    }
    if (version == 3)
    {
        size_t letters = 0;
        if (!(stream >> letters) || letters > 128)
        {
            return false;
        }
        for (size_t i = 0; i < letters; ++i)
        {
            Letter letter;
            if (!(stream >> letter.from >> letter.to >> letter.remaining) || letter.from < 0
                || letter.from >= TownCount || letter.to < 0 || letter.to >= TownCount
                || letter.from == letter.to || !InRange(letter.remaining, 0, 120)
                || !ReadReports(stream, letter.reports, societyTime_, nextReport_))
            {
                return false;
            }
            letters_.push_back(letter);
        }
        if (letters_.size() > lettersSent - lettersDelivered)
        {
            return false;
        }
    }
    else
    {
        RefreshTownReports(scene);
    }
    for (auto& npc : scene.Actors<NpcActor>())
    {
        if (npc.health <= 0 || npc.recovery > 0 || npc.action == NpcAction::Recover)
        {
            Respawn(npc, scene);
        }
    }
    Log(version == 3 ? "7개 마을·주택·편지·소문·이사 진행 복원"
                     : "기존 주민 자산 유지 / 7개 마을·140명·개별 주택으로 확장");
    return bool(stream);
}
