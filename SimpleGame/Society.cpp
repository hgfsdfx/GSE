#include "stdafx.h"
#include "Society.h"
#include "SceneGraph.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
#include <iostream>
using GameMath::Distance;

TownActor::TownActor(int value)
    : index(value)
{
    renderLayer = RenderLayer::GroundEffect;
    SetWorldPosition(Society::TownCenter(index));
}

NpcActor::NpcActor(int id, int town, NpcJob profession)
    : identity(id),
      home(town),
      job(profession)
{
    updatePhase = UpdatePhase::Npc;
    destinationTown = home;
    houseSlot = (identity - 1) % Society::ResidentsPerTown;
    destination = HomePosition();
    work = .15f * ((identity - 1) % 20);
    think = .1f * (identity % 20);
    playerScan = .03f * (identity % 20);
    migrationCooldown = 30.f + identity % 30;
    communicationTimer = .1f * (identity % 20);
    survivalWeight = .8f + float(identity % 4) * .15f;
    wealthWeight = job == NpcJob::Merchant || job == NpcJob::Raider ? 1.5f : .7f;
    insightWeight = job == NpcJob::Hacker || job == NpcJob::Seeker ? 1.6f : .4f;
    plunderWeight = job == NpcJob::Raider ? .95f : job == NpcJob::Hunter ? .08f : .01f;
    SetWorldPosition(HomePosition());
    if (identity == 4)
    {
        const auto center = Society::TownCenter(0);
        SetWorldPosition({center.x + 24, center.y + 24});
    }
}

float NpcActor::MaxHealth() const
{
    return 90.f + std::min(90.f, float(insight / 10) * 5);
}

WorldPoint NpcActor::HomePosition() const
{
    return Society::HouseDoor(home, houseSlot);
}

std::string NpcActor::Name() const
{
    if (identity == 4)
    {
        return "패치";
    }
    return std::string(Society::JobName(job)) + " " + std::to_string(identity);
}

void NpcActor::Update(float dt, ActorUpdateContext& context)
{
    context.society.UpdateNpc(*this, dt, context);
}

WorldPoint Society::TownCenter(int index)
{
    const WorldPoint centers[] = {{90, 90},
                                  {90 + TownSpacing, 90},
                                  {90 + 2 * TownSpacing, 90},
                                  {90, 90 + TownSpacing},
                                  {90 + TownSpacing, 90 + TownSpacing},
                                  {90 + 2 * TownSpacing, 90 + TownSpacing},
                                  {90 + TownSpacing, 90 + 2 * TownSpacing}};
    return centers[std::clamp(index, 0, TownCount - 1)];
}

int Society::HomeTown(int identity)
{
    return std::clamp((identity - 1) / ResidentsPerTown, 0, TownCount - 1);
}

WorldPoint Society::HouseCenter(int town, int slot)
{
    const auto center = TownCenter(town);
    const double columns[] = {-360, -180, 180, 360};
    const double rows[] = {-400, -240, -80, 80, 240, 400};
    slot = std::clamp(slot, 0, HousesPerTown - 1);
    return {center.x + columns[slot % 4], center.y + rows[slot / 4]};
}

WorldPoint Society::HouseDoor(int town, int slot)
{
    const auto center = HouseCenter(town, slot);
    return {center.x, center.y + 42};
}

Building Society::HouseGeometry(int town, int slot)
{
    const auto center = HouseCenter(town, slot);
    return {center.x - 32, center.y - 28, 64, 56, 42.f, unsigned(town % 5), false};
}

HouseActor::HouseActor(int townIndex, int houseIndex)
    : BuildingActor(Society::HouseGeometry(townIndex, houseIndex), {}),
      town(townIndex),
      slot(houseIndex)
{
}

int Society::NeighborTown(int town, uint64_t choice)
{
    std::array<int, TownCount> neighbors{};
    int count = 0;
    for (int i = 0; i < TownCount; ++i)
    {
        if (i != town && Distance(TownCenter(i), TownCenter(town)) <= TownSpacing + 1)
        {
            neighbors[count++] = i;
        }
    }
    return count ? neighbors[choice % count] : town;
}

bool Society::InTownSafeZone(WorldPoint point, double padding)
{
    return BlocksMonsterTravel(point, point, padding);
}

bool Society::BlocksMonsterTravel(WorldPoint from, WorldPoint to, double padding)
{
    const double dx = to.x - from.x, dy = to.y - from.y;
    const double lengthSquared = dx * dx + dy * dy;
    const double radius = TownSafeRadius + padding;
    for (int town = 0; town < TownCount; ++town)
    {
        const auto center = TownCenter(town);
        const double t =
            lengthSquared > 0
                ? std::clamp(((center.x - from.x) * dx + (center.y - from.y) * dy) / lengthSquared,
                             0.0,
                             1.0)
                : 0;
        const double x = from.x + t * dx - center.x;
        const double y = from.y + t * dy - center.y;
        if (x * x + y * y <= radius * radius)
        {
            return true;
        }
    }
    return false;
}

const char* Society::TownName(int index)
{
    const char* names[] = {"패치의 은신처",
                           "네온 시장",
                           "루멘 쉼터",
                           "크롬 항구",
                           "오로라 정원",
                           "회로 광장",
                           "새벽 관측소"};
    return names[std::clamp(index, 0, TownCount - 1)];
}

const char* Society::JobName(NpcJob job)
{
    const char* names[] = {"사냥꾼", "약탈자", "해커", "상인", "탐구자", "경비"};
    return names[std::clamp(int(job), 0, 5)];
}

const char* Society::ActionName(NpcAction action)
{
    const char* names[] =
        {"휴식", "사냥", "추격", "거래", "해킹", "깨달음 탐구", "회복 대기", "이사 중"};
    return names[std::clamp(int(action), 0, 7)];
}

int Society::BuyPrice(int town)
{
    return 8 + town * 4;
}

int Society::SellPrice(int town)
{
    return 5 + town * 4;
}

void Society::Initialize(SceneGraph& scene, ActorId root)
{
    for (int town = 0; town < TownCount; ++town)
    {
        towns_[town] = scene.Spawn<TownActor>(root, town).Id();
    }
    InitializeHouses(scene, root);
    for (int identity = 1; identity <= Population; ++identity)
    {
        scene.Spawn<NpcActor>(root,
                              identity,
                              HomeTown(identity),
                              static_cast<NpcJob>((identity - 1) % 6));
        House(scene, HomeTown(identity), (identity - 1) % ResidentsPerTown)->owner = identity;
    }
    RefreshTownReports(scene);
    Log("녹스 / 패치의 은신처에서 G로 첫 의뢰를 받으세요");
}

TownActor* Society::Town(SceneGraph& scene, int index) const
{
    return dynamic_cast<TownActor*>(scene.Find(towns_[index]));
}

const TownActor* Society::Town(const SceneGraph& scene, int index) const
{
    return dynamic_cast<const TownActor*>(scene.Find(towns_[index]));
}

int Society::NearbyTown(WorldPoint point, double range) const
{
    for (int i = 0; i < TownCount; ++i)
    {
        if (Distance(point, TownCenter(i)) <= range)
        {
            return i;
        }
    }
    return -1;
}

const NpcActor* Society::NearestNpc(const SceneGraph& scene, WorldPoint point, double range) const
{
    const NpcActor* nearest = nullptr;
    for (const auto& npc : scene.Actors<NpcActor>())
    {
        const double distance = Distance(point, npc.WorldPosition());
        if (npc.health > 0 && scene.IsActive(npc.Id()) && scene.IsVisible(npc.Id())
            && distance < range)
        {
            range = distance;
            nearest = &npc;
        }
    }
    return nearest;
}

void Society::Log(const std::string& message)
{
    news_.push_front(message);
    if (news_.size() > 4)
    {
        news_.pop_back();
    }
    std::cout << "[Society] " << message << '\n';
}

const std::deque<std::string>& Society::News() const
{
    return news_;
}

std::string Society::Objective() const
{
    if (story == 0)
    {
        return "G / 은신처에서 패치의 의뢰 받기";
    }
    if (story == 1)
    {
        return "E / (0,0) 구역 중계기 복구 → 건물 자료 확보";
    }
    if (story == 2)
    {
        return "정전! E 중계기 복구 또는 C 전력 우회 → 은신처 G / "
               + std::to_string(int(escapeTime)) + "초";
    }
    if (story == 3)
    {
        return "접속 봉쇄 / C 전력 우회 후 은신처 G";
    }
    return "조작 기록 확보 / 거래·성장·중앙 광장 보스에 도전";
}

void Society::Tick(float dt, ActorUpdateContext& context)
{
    UpdateCommunication(dt, context);
    wanted = std::max(0.f, wanted - dt);
    if (story == 2)
    {
        escapeTime = std::max(0.f, escapeTime - dt);
        if (escapeTime <= 0)
        {
            story = 3;
            context.world.Change({0, 0}).eventSolved = false;
            Log("경찰 접속 봉쇄 / C 전력 우회로 탈출 경로 확보");
        }
    }
}

bool Society::AttackNpc(ActorUpdateContext& context, bool manual)
{
    auto& player = context.player;
    if (player.stats.Dead() || player.stats.fireTimer_ > 0)
    {
        return false;
    }
    const auto point = player.WorldPosition();
    NpcActor* victim = nullptr;
    double distance = player.stats.Range();
    bool defending = false;
    for (auto& npc : context.scene.Actors<NpcActor>())
    {
        const bool hostile = npc.againstPlayer && npc.action == NpcAction::Raid;
        if (npc.health <= 0 || !context.scene.IsActive(npc.Id())
            || (!hostile && !(manual && pkEnabled)))
        {
            continue;
        }
        const double candidate = Distance(point, npc.WorldPosition());
        if (candidate > player.stats.Range() || (victim && defending && !hostile)
            || (victim && hostile == defending && candidate >= distance)
            || !context.navigation.ClearPath(context.world, point, npc.WorldPosition()))
        {
            continue;
        }
        victim = &npc;
        distance = candidate;
        defending = hostile;
    }
    if (!victim)
    {
        return false;
    }
    victim->health = std::max(0.f, victim->health - player.stats.Damage());
    victim->flash = .2f;
    victim->target = 0;
    victim->againstPlayer = true;
    victim->action = NpcAction::Raid;
    SetDestination(*victim, point);
    victim->think = .35f;
    if (!defending)
    {
        wanted = std::min(100.f, wanted + 15);
    }
    player.stats.fireTimer_ = player.stats.FireInterval();
    context.scene.SpawnAt<CombatNumberActor>(context.events.GameplayRoot(),
                                             victim->WorldPosition(),
                                             int(player.stats.Damage()));
    if (victim->health <= 0)
    {
        Defeat(*victim, nullptr, context, true, defending);
    }
    return true;
}

void Society::Command(unsigned char key, ActorUpdateContext& context)
{
    auto& player = context.player;
    const auto point = player.WorldPosition();
    if (player.stats.Dead())
    {
        return;
    }
    if (key == 'j')
    {
        pkEnabled = !pkEnabled;
        player.npcAttackRequested = false;
        context.events.Say(pkEnabled ? "PK 켜짐 / 사냥꾼·약탈자와 교전 · H 주민 공격"
                                     : "선제 PK 꺼짐 / 적대 주민에게 자동 반격");
        return;
    }
    if (key == 'h')
    {
        player.npcAttackRequested = true;
        if (player.stats.fireTimer_ > 0)
        {
            return;
        }
        player.npcAttackRequested = false;
        if (!AttackNpc(context, true))
        {
            context.events.Say(pkEnabled
                                   ? "사거리·시야 내에 공격할 주민이 없습니다"
                                   : "사거리·시야 내 적대 주민 없음 / 선제 공격은 J로 PK 활성화");
        }
        return;
    }
    if (key == 'c')
    {
        if ((story != 2 && story != 3) || Distance(point, {116, 118}) > 125)
        {
            context.events.Say("전력 우회 / 정전 의뢰 중 (0,0) 중계기 근처에서 사용");
            return;
        }
        context.world.Change({0, 0}).eventSolved = true;
        context.world.Change({0, 0}).cameraOff = true;
        context.events.Say("전력 우회 / 감시 연결 차단, 은신처에서 G로 보고");
        return;
    }
    const int index = NearbyTown(point);
    if (index < 0)
    {
        context.events.Say("마을 광장에 가까이 가세요 / 미니맵의 금색 표시");
        return;
    }
    auto town = Town(context.scene, index);
    if (!town)
    {
        return;
    }
    if (key == 'g')
    {
        if (index != 0)
        {
            context.events.Say("패치는 첫 은신처 (90,90)에 있습니다");
            return;
        }
        if (story == 0)
        {
            story = 1;
            context.events.Say("패치: 중계기를 복구하고 같은 구역 건물의 자료를 가져와");
        }
        else if ((story == 2 || story == 3) && context.world.Powered({0, 0}))
        {
            story = 4;
            credits = std::min(1000000, credits + 150);
            insight = std::min(1000000, insight + 10);
            context.events.GainExperience(35);
            context.events.Say("조작 기록 발견 / 테러 누명은 베일의 함정! 보수 150, 깨달음 10");
            Log("패치의 정전 의뢰 완료 / 녹스의 거래망이 열렸습니다");
        }
        else
        {
            context.events.Say(Objective());
        }
        return;
    }
    if (wanted >= 25)
    {
        context.events.Say("수배 중 거래 거절 / 마을을 벗어나 추적이 진정될 때까지 기다리세요");
        return;
    }
    if (key != 'y' && town->treasury > 999500)
    {
        context.events.Say("시장 자금 보관 한도 / 지금은 판매만 가능합니다");
        return;
    }
    int cost = 0;
    if (key == 't')
    {
        cost = 12;
        if (player.stats.Health() >= player.stats.MaxHealth())
        {
            context.events.Say("이미 최대 체력입니다");
            return;
        }
        if (credits < cost || town->stock < 1)
        {
            context.events.Say("회복: 12 크레딧과 시장 재고 1개 필요");
            return;
        }
        --town->stock;
        player.stats.health_ = std::min(player.stats.MaxHealth(), player.stats.health_ + 40);
    }
    else if (key == 'y')
    {
        const int quantity = std::min({cargo,
                                       5,
                                       town->treasury / SellPrice(index),
                                       10000 - town->stock,
                                       (1000000 - credits) / SellPrice(index)});
        if (quantity <= 0)
        {
            context.events.Say("판매할 부품 또는 시장 자금·보관 공간 부족");
            return;
        }
        cost = -quantity * SellPrice(index);
        cargo -= quantity;
        town->stock += quantity;
    }
    else if (key == 'u')
    {
        cost = 80 + player.stats.WeaponRank() * 20;
        if (credits < cost || town->stock < 3 || player.stats.WeaponRank() >= 10)
        {
            context.events.Say("강화 비용 " + std::to_string(cost) + " / 재고 3 필요 / 최대 +10");
            return;
        }
        town->stock -= 3;
        ++player.stats.weaponRank_;
    }
    else if (key == 'i')
    {
        cost = BuyPrice(index);
        if (credits < cost || town->stock < 1 || cargo >= 10000)
        {
            context.events.Say("부품 구매 자금·재고 또는 가방 공간 부족");
            return;
        }
        --town->stock;
        ++cargo;
    }
    else
    {
        return;
    }
    credits = std::clamp(credits - cost, 0, 1000000);
    town->treasury = std::clamp(town->treasury + cost, 0, 1000000);
    ++trades;
    context.events.Say(std::string(TownName(index)) + " 거래 완료 / 잔액 "
                       + std::to_string(credits));
}

void Society::OnHack(const Device& device, ActorUpdateContext& context)
{
    if (story == 1 && device.type == DeviceType::Building && device.key == ChunkKey{0, 0}
        && context.world.Changes(device.key).dataTaken)
    {
        story = 2;
        escapeTime = 75;
        context.world.Change({0, 0}).eventSolved = false;
        context.world.Change({0, 0}).cameraOff = false;
        wanted = std::min(100.f, wanted + 20);
        context.events.Say(
            "관리자: 이제 네가 시간을 벌어 줘 / 정전! 중계기 복구 또는 C 우회 후 은신처로");
        Log("첫 의뢰의 함정 / 정전과 경찰 봉쇄 예고");
    }
}
