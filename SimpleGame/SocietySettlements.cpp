#include "stdafx.h"
#include "Society.h"
#include "SceneGraph.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
using GameMath::Distance;

namespace
{
    template <size_t N>
    bool MergeReports(std::array<TownReport, N>& destination,
                      const std::array<TownReport, N>& source)
    {
        bool changed = false;
        for (size_t i = 0; i < N; ++i)
        {
            if (source[i].revision > destination[i].revision)
            {
                destination[i] = source[i];
                changed = true;
            }
        }
        return changed;
    }

    float Preference(const NpcActor& npc, const TownReport& report)
    {
        return npc.wealthWeight * report.prosperity + npc.survivalWeight * report.safety
               + npc.insightWeight * report.learning + npc.plunderWeight * (1 - report.safety) * .5f
               - std::max(0, report.population - Society::ResidentsPerTown) * .08f;
    }
} // namespace

void Society::InitializeHouses(SceneGraph& scene, ActorId root)
{
    for (int town = 0; town < TownCount; ++town)
    {
        for (int slot = 0; slot < HousesPerTown; ++slot)
        {
            houses_[town][slot] = scene.Spawn<HouseActor>(root, town, slot).Id();
        }
    }
}

HouseActor* Society::House(SceneGraph& scene, int town, int slot) const
{
    if (town < 0 || town >= TownCount || slot < 0 || slot >= HousesPerTown)
    {
        return nullptr;
    }
    return dynamic_cast<HouseActor*>(scene.Find(houses_[town][slot]));
}

const HouseActor* Society::House(const SceneGraph& scene, int town, int slot) const
{
    if (town < 0 || town >= TownCount || slot < 0 || slot >= HousesPerTown)
    {
        return nullptr;
    }
    return dynamic_cast<const HouseActor*>(scene.Find(houses_[town][slot]));
}

size_t Society::LettersInTransit() const
{
    return letters_.size();
}

double Society::SimulationTime() const
{
    return societyTime_;
}

void Society::RefreshTownReports(SceneGraph& scene)
{
    const auto residents = scene.Actors<NpcActor>();
    for (int i = 0; i < TownCount; ++i)
    {
        auto town = Town(scene, i);
        if (!town)
        {
            continue;
        }
        int population = 0, researchers = 0, traders = 0;
        float danger = 0;
        for (const auto& npc : residents)
        {
            if (npc.home != i)
            {
                continue;
            }
            ++population;
            researchers += npc.job == NpcJob::Hacker || npc.job == NpcJob::Seeker ? 1 : 0;
            traders += npc.job == NpcJob::Merchant ? 1 : 0;
            danger += npc.notoriety * .005f + npc.plunderWeight * .08f;
        }
        auto& report = town->reports[i];
        report.revision = nextReport_++;
        report.timestamp = societyTime_;
        report.population = population;
        report.prosperity =
            std::clamp(town->treasury / 6000.f + town->stock / 400.f + traders * .04f, 0.f, 2.f);
        report.safety = std::clamp(1.f - danger, .05f, 1.f);
        report.learning = std::clamp(.3f + researchers * .10f + float(i % 3) * .12f, 0.f, 2.f);
    }
}

void Society::Communicate(NpcActor& npc, ActorUpdateContext& context)
{
    const int index = NearbyTown(npc.WorldPosition(), TownSafeRadius);
    if (index < 0)
    {
        npc.lastVisitedTown = -1;
        return;
    }
    auto town = Town(context.scene, index);
    if (!town)
    {
        return;
    }
    // Reports are carried by actual visitors, never read from a remote town here.
    const bool delivered = MergeReports(town->reports, npc.reports);
    MergeReports(npc.reports, town->reports);
    if (npc.lastVisitedTown != index && index != npc.home && delivered)
    {
        ++rumorVisits;
        Log(npc.Name() + " / " + TownName(index) + "에 다른 마을의 소문 전달");
    }
    npc.lastVisitedTown = index;
}

void Society::UpdateCommunication(float dt, ActorUpdateContext& context)
{
    societyTime_ += dt;
    for (auto it = letters_.begin(); it != letters_.end();)
    {
        it->remaining -= dt;
        if (it->remaining > 0)
        {
            ++it;
            continue;
        }
        if (auto town = Town(context.scene, it->to))
        {
            MergeReports(town->reports, it->reports);
            ++lettersDelivered;
            Log(std::string(TownName(it->from)) + " → " + TownName(it->to) + " / 편지 도착");
        }
        it = letters_.erase(it);
    }
    letterTimer_ -= dt;
    if (letterTimer_ <= 0)
    {
        letterTimer_ = 12;
        RefreshTownReports(context.scene);
        for (int i = 0; i < TownCount && letters_.size() < 128; ++i)
        {
            const auto town = Town(context.scene, i);
            if (!town)
            {
                continue;
            }
            const int destination = NeighborTown(i, lettersSent / TownCount + i);
            Letter letter;
            letter.from = i;
            letter.to = destination;
            letter.remaining = float(Distance(TownCenter(i), TownCenter(destination)) / 110.0);
            letter.reports = town->reports;
            letters_.push_back(letter);
            ++lettersSent;
        }
    }
}

bool Society::TryRelocate(NpcActor& npc, ActorUpdateContext& context)
{
    if (npc.identity == 4 || npc.againstPlayer || npc.health < npc.MaxHealth() * .5f)
    {
        return false;
    }
    const auto& homeReport = npc.reports[npc.home];
    if (!homeReport.revision)
    {
        return false;
    }
    int choice = npc.home;
    float best = Preference(npc, homeReport) + .16f;
    for (int i = 0; i < TownCount; ++i)
    {
        const auto& report = npc.reports[i];
        if (i == npc.home || !report.revision || report.population >= HousesPerTown
            || societyTime_ - report.timestamp > 180)
        {
            continue;
        }
        const float score = Preference(npc, report);
        if (score > best)
        {
            choice = i;
            best = score;
        }
    }
    if (choice == npc.home)
    {
        return false;
    }
    npc.action = NpcAction::Relocate;
    npc.target = 0;
    npc.destinationTown = choice;
    SetDestination(npc, TownCenter(choice));
    Log(npc.Name() + " / 안전·재산·탐구 소문 비교 → " + TownName(choice) + " 이사 출발");
    return true;
}

void Society::CompleteRelocation(NpcActor& npc, ActorUpdateContext& context)
{
    int vacant = -1;
    for (int slot = 0; slot < HousesPerTown; ++slot)
    {
        const auto house = House(context.scene, npc.destinationTown, slot);
        if (house && !house->owner)
        {
            vacant = slot;
            break;
        }
    }
    if (vacant >= 0)
    {
        auto previous = House(context.scene, npc.home, npc.houseSlot);
        auto next = House(context.scene, npc.destinationTown, vacant);
        if (previous)
        {
            previous->owner = 0;
        }
        next->owner = npc.identity;
        npc.home = npc.destinationTown;
        npc.houseSlot = vacant;
        ++migrations;
        Log(npc.Name() + " / " + TownName(npc.home) + " " + std::to_string(vacant + 1)
            + "번 집 입주");
        RefreshTownReports(context.scene);
    }
    else
    {
        Log(npc.Name() + " / 이사 목적지에 빈집이 없어 귀환");
    }
    npc.migrationCooldown = 120;
    npc.action = NpcAction::Rest;
    npc.destinationTown = npc.home;
    SetDestination(npc, npc.HomePosition());
}
