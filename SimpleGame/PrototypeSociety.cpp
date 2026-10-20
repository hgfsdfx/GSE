#include "stdafx.h"
#include "Prototype.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>

namespace
{
    const Color Cyan(.22f, .94f, .91f), Amber(1, .7f, .3f), Pink(.97f, .26f, .59f);
    const Color White(.82f, .9f, .94f), Muted(.42f, .57f, .64f), Panel(.025f, .055f, .09f, .94f);

    Color JobColor(NpcJob job)
    {
        const Color colors[] =
            {Color(.35f, .9f, .48f), Pink, Cyan, Amber, Color(.67f, .5f, 1), Color(.4f, .65f, 1)};
        return colors[std::clamp(int(job), 0, 5)];
    }
} // namespace

void Prototype::DrawNpc(const NpcActor& npc)
{
    const auto position = npc.WorldPosition();
    const auto p = Project(position.x, position.y);
    if (p.x < -70 || p.x > r_.Width() + 70 || p.y < -70 || p.y > r_.Height() + 100)
    {
        return;
    }
    const auto color = npc.flash > 0 ? White : JobColor(npc.job);
    if (npc.health <= 0)
    {
        r_.Line(p + Point(-8, 0), p + Point(8, 0), 4, Muted);
        if (scan_)
        {
            r_.Text(p.x - 28, p.y - 20, "회복 " + std::to_string(int(npc.recovery)), Muted, .9f);
        }
        return;
    }
    r_.Circle(p, 10 * zoom_, Color(0, 0, 0, .45f), 12);
    const char* presets[] =
        {"hunter_01", "raider_01", "hacker_01", "merchant_01", "seeker_01", "guard_01"};
    const auto* model =
        assets_.Npc(npc.identity == 4 ? "patch" : presets[std::clamp(int(npc.job), 0, 5)]);
    const bool drawn = DrawActorModel(npc, model, 24, npc.flash > 0);
    const auto torso = Project(position.x, position.y, drawn ? 34.f : 22.f);
    if (!drawn)
    {
        r_.Rect(torso.x - 6 * zoom_, torso.y, 12 * zoom_, 18 * zoom_, color);
        r_.Circle(torso + Point(0, -5 * zoom_), 6 * zoom_, Color(.75f, .8f, .84f), 12);
        r_.Line(torso + Point(7 * zoom_, 4 * zoom_),
                torso + Point(13 * zoom_, 0),
                3,
                color.Emissive(2));
    }
    r_.Rect(torso.x - 16, torso.y - 19, 32, 3, Muted);
    r_.Rect(torso.x - 16, torso.y - 19, 32 * npc.health / npc.MaxHealth(), 3, color);
    if (scan_ || npc.identity == 4)
    {
        const auto label = npc.Name() + " / " + Society::ActionName(npc.action);
        r_.Text(torso.x - r_.TextWidth(label, .9f) / 2, torso.y - 36, label, color, .9f);
    }
}

void Prototype::DrawTown(const TownActor& town)
{
    const auto center = town.WorldPosition();
    const auto p = Project(center.x, center.y);
    const float margin = 1100 * zoom_;
    if (p.x < -margin || p.x > r_.Width() + margin || p.y < -margin || p.y > r_.Height() + margin)
    {
        return;
    }
    auto plaza = [&](double x, double y, double width, double depth)
    {
        r_.Quad(Project(center.x + x, center.y + y),
                Project(center.x + x + width, center.y + y),
                Project(center.x + x + width, center.y + y + depth),
                Project(center.x + x, center.y + y + depth),
                Color(.06f, .16f, .19f));
    };
    // Cross-shaped market square follows the reserved roads and leaves all exits open.
    plaza(-46, -560, 92, 1120);
    plaza(-560, -46, 1120, 92);
    WorldRing(center, float(Society::TownSafeRadius), Cyan.Emissive(.8f).Alpha(.7f), 2);
    WorldRing(center, 72, Amber.Alpha(.6f), 2);
    auto stall = [&](WorldPoint offset, const char* label, Color color)
    {
        const auto point = Project(center.x + offset.x, center.y + offset.y);
        r_.Rect(point.x - 28 * zoom_, point.y - 42 * zoom_, 56 * zoom_, 7 * zoom_, color);
        r_.Line(point + Point(-24, -35) * zoom_, point + Point(-24, -8) * zoom_, 3, Muted);
        r_.Line(point + Point(24, -35) * zoom_, point + Point(24, -8) * zoom_, 3, Muted);
        r_.Rect(point.x - 26 * zoom_,
                point.y - 12 * zoom_,
                52 * zoom_,
                10 * zoom_,
                Color(.12f, .23f, .28f));
        r_.Text(point.x - r_.TextWidth(label, .9f) / 2, point.y - 60 * zoom_, label, color, .9f);
    };
    stall({-140, 0}, "의료소 / T", Cyan);
    stall({140, 0}, "부품 상점 / I Y", Amber);
    stall({0, -160}, "강화소 / U", Pink);
    stall({0, 160}, "우편·소문 게시판", Cyan);
    for (const auto offset : {WorldPoint{-600, 0}, {600, 0}, {0, -600}, {0, 600}})
    {
        const auto base = Project(center.x + offset.x, center.y + offset.y);
        const auto top = Project(center.x + offset.x, center.y + offset.y, 28);
        r_.Line(base, top, 4 * zoom_, Muted);
        r_.Circle(top, 4 * zoom_, Cyan.Emissive(2), 12);
    }
    const std::string title = Society::TownName(town.index);
    r_.Text(p.x - r_.TextWidth(title, 1.25f) / 2, p.y - 140 * zoom_, title, Amber, 1.25f);
    const std::string protection = town.index == 0 ? "시작 마을 / 몬스터 차단" : "마을 방어 구역";
    r_.Text(p.x - r_.TextWidth(protection, .9f) / 2, p.y + 60 * zoom_, protection, Cyan, .9f);
}

void Prototype::DrawHouse(const HouseActor& house)
{
    const auto geometry = house.Geometry();
    const auto p = Project(geometry.x, geometry.y);
    if (p.x < -160 || p.x > r_.Width() + 160 || p.y < -160 || p.y > r_.Height() + 160)
    {
        return;
    }
    const std::string key = "village-house:v1:" + std::to_string(house.town % 5);
    if (!DrawBuildingModel(geometry, true) && r_.BeginCachedMesh(key, p, zoom_))
    {
        const Color accents[] = {Cyan, Amber, Pink, Color(.5f, .7f, 1), Color(.6f, 1, .5f)};
        const auto accent = accents[house.town % 5];
        auto point = [](double x, double y, float height = 0)
        {
            return Point(float((x - y) * .82), float((x + y) * .43 - height));
        };
        const auto a = point(0, 0, 42), b = point(64, 0, 42), c = point(64, 56, 42),
                   d = point(0, 56, 42);
        r_.Quad(point(0, 56), point(64, 56), c, d, Color(.11f, .19f, .25f));
        r_.Quad(point(64, 0), point(64, 56), c, b, Color(.06f, .12f, .19f));
        r_.Quad(a, b, c, d, Color(.19f, .26f, .33f));
        r_.Line(d, c, 2, accent.Emissive(1.5f));
        r_.Quad(point(26, 56, 0),
                point(40, 56, 0),
                point(40, 56, 25),
                point(26, 56, 25),
                Color(.025f, .06f, .10f));
        r_.Quad(point(7, 56, 17),
                point(20, 56, 17),
                point(20, 56, 30),
                point(7, 56, 30),
                Amber.Emissive(1));
        r_.Quad(point(46, 56, 17),
                point(58, 56, 17),
                point(58, 56, 30),
                point(46, 56, 30),
                Cyan.Emissive(1));
        r_.EndCachedMesh();
    }
    if (scan_)
    {
        const auto label = house.owner ? "NPC " + std::to_string(house.owner) + "의 집"
                                       : std::string("빈집 / 이사 가능");
        r_.Text(p.x - r_.TextWidth(label, .85f) / 2,
                p.y - 48 * zoom_,
                label,
                house.owner ? White : Muted,
                .85f);
    }
}

void Prototype::DrawOpenWorldHud()
{
    const float ui = std::min(1.f, std::min(r_.Width() / 1120.f, r_.Height() / 680.f));
    const float w = r_.Width() / ui, h = r_.Height() / ui;
    const auto& citizens = level_.Citizens();
    const auto player = level_.Player().WorldPosition();
    auto rect = [&](float x, float y, float width, float height, Color color)
    {
        r_.Rect(x * ui, y * ui, width * ui, height * ui, color);
    };
    auto text = [&](float x,
                    float y,
                    const std::string& value,
                    Color color = White,
                    float scale = 1.15f,
                    float maxWidth = 0)
    {
        if (maxWidth > 0)
        {
            const float measured = r_.TextWidth(value, scale);
            if (measured > maxWidth)
            {
                scale *= maxWidth / measured;
            }
        }
        r_.Text(x * ui, y * ui, value, color, scale * ui);
    };
    rect(0, 0, w, 92, Panel);
    text(25, 18, "나이트 프로토콜 / 녹스", Cyan, 2.1f);
    text(26,
         61,
         "고스트  LV " + std::to_string(level_.Level()) + " / 경험치 "
             + std::to_string(level_.Experience()) + "/" + std::to_string(level_.NextExperience()),
         White);
    text(425,
         21,
         "자금 " + std::to_string(citizens.credits) + "  부품 " + std::to_string(citizens.cargo)
             + "  깨달음 " + std::to_string(citizens.insight),
         Amber,
         1.25f);
    text(425,
         59,
         "체력 " + std::to_string(int(level_.Health())) + "/"
             + std::to_string(int(level_.MaxHealth())) + "  스마트폰 +"
             + std::to_string(level_.WeaponRank()),
         White);
    rect(690, 62, 155, 6, Muted);
    rect(690, 62, 155 * level_.Health() / level_.MaxHealth(), 6, Color(.3f, .9f, .5f));
    text(w - 240,
         22,
         "선제 PK " + std::string(citizens.pkEnabled ? "켜짐" : "꺼짐") + " / 수배 "
             + std::to_string(int(citizens.wanted)),
         Pink);
    text(w - 240, 58, "장기 수배: 테러 혐의 / 23:48", Muted, 1.05f);

    rect(22, 111, 302, 200, Panel);
    text(36, 126, "패치의 의뢰 / 정전된 골목", Amber, 1.3f);
    text(36, 159, citizens.Objective(), White, 1.1f, 274);
    text(36, 190, "중계기 (116,118) / 자료 (235,324)", Muted, 1.05f, 274);
    text(36,
         216,
         "7개 마을 / 편지 운송 " + std::to_string(citizens.LettersInTransit()) + "건",
         Muted,
         1.05f,
         274);
    text(36, 243, "드론 자동 사냥 / 처치 " + std::to_string(level_.Kills()) + "/20", White);
    text(36,
         270,
         level_.Cleared() ? "구역 보스 무력화 / 탐험 계속 가능" : "LV4·20기 처치 후 중앙 광장 보스",
         Muted,
         1.05f,
         274);

    const float mx = w - 246, my = 115;
    rect(mx, my, 224, 260, Panel);
    text(mx + 12, my + 12, "거점 지도 / 위쪽이 북쪽", Muted);
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            rect(mx + 22 + x * 60, my + 44 + y * 60, 56, 56, Color(.07f, .15f, .19f));
        }
    }
    auto dot = [&](WorldPoint p, float radius, Color color)
    {
        const double origin = 90 - Society::TownBuildingClearRadius;
        const double span = 2 * Society::TownSpacing + 2 * Society::TownBuildingClearRadius;
        const float x = float((p.x - origin) / span), y = float((p.y - origin) / span);
        if (x >= 0 && y >= 0 && x < 1 && y < 1)
        {
            r_.Circle({(mx + 22 + x * 180) * ui, (my + 44 + y * 180) * ui}, radius * ui, color, 12);
        }
    };
    for (int i = 0; i < Society::TownCount; ++i)
    {
        dot(Society::TownCenter(i), 5, Amber);
    }
    for (const auto& npc : level_.Scene().Actors<NpcActor>())
    {
        if (npc.health > 0)
        {
            dot(npc.WorldPosition(), 2, JobColor(npc.job));
        }
    }
    dot(player, 4, Cyan);
    text(mx + 12, my + 231, "금색 마을 / 청록 고스트", Muted, 1.05f);

    const int townIndex = citizens.NearbyTown(player);
    rect(22, 325, 302, 125, Panel);
    if (townIndex >= 0)
    {
        const auto town = citizens.Town(level_.Scene(), townIndex);
        text(36, 339, Society::TownName(townIndex), Amber, 1.3f);
        if (town)
        {
            text(36,
                 369,
                 "재고 " + std::to_string(town->stock) + " / 시장 자금 "
                     + std::to_string(town->treasury));
            text(36,
                 397,
                 "부품 매입 " + std::to_string(Society::SellPrice(townIndex)) + " / 판매 "
                     + std::to_string(Society::BuyPrice(townIndex)) + " 크레딧",
                 Muted);
        }
        text(36, 425, "T 회복 12 / I 구매 / Y 판매 / U 강화", Cyan, 1.05f, 274);
    }
    else
    {
        int nearest = 0;
        for (int i = 1; i < Society::TownCount; ++i)
        {
            if (GameMath::Distance(player, Society::TownCenter(i))
                < GameMath::Distance(player, Society::TownCenter(nearest)))
            {
                nearest = i;
            }
        }
        text(36, 339, "가까운 거점 / " + std::string(Society::TownName(nearest)), Amber, 1.1f, 274);
        const auto center = Society::TownCenter(nearest);
        text(36,
             373,
             "좌표 " + std::to_string(int(center.x)) + "," + std::to_string(int(center.y)));
        text(36,
             405,
             "거리 " + std::to_string(int(GameMath::Distance(player, center)))
                 + " / 마을에서 거래 가능",
             Muted);
    }
    if (const auto npc = citizens.NearestNpc(level_.Scene(), player, 220))
    {
        rect(344, 111, 365, 136, Panel);
        text(358,
             125,
             npc->Name() + " / " + Society::ActionName(npc->action),
             JobColor(npc->job),
             1.25f);
        text(358,
             155,
             "HP " + std::to_string(int(npc->health)) + "  자산 " + std::to_string(npc->credits)
                 + "  부품 " + std::to_string(npc->cargo));
        text(358,
             184,
             "깨달음 " + std::to_string(npc->insight) + "  약탈 weight "
                 + std::to_string(int(npc->plunderWeight * 100)) + "%",
             Muted);
        text(358,
             213,
             "성향 생존/재산/탐구 " + std::to_string(int(npc->survivalWeight * 100)) + "/"
                 + std::to_string(int(npc->wealthWeight * 100)) + "/"
                 + std::to_string(int(npc->insightWeight * 100)),
             Muted,
             1.0f);
    }
    if (const auto boss = level_.Boss())
    {
        text(344, 268, "감시자-01 / " + std::to_string(int(boss->health)) + " HP", Pink);
    }
    rect(22, h - 211, 515, 88, Panel);
    text(36,
         h - 201,
         "도시 소식 / 거래 " + std::to_string(citizens.trades) + "  약탈 "
             + std::to_string(citizens.raids),
         Amber,
         1.0f);
    int line = 0;
    for (const auto& news : citizens.News())
    {
        if (line >= 2)
        {
            break;
        }
        text(36, h - 177 + line * 23, news, Muted, 1.0f, 483);
        ++line;
    }
    rect(0, h - 102, w, 102, Panel);
    text(25, h - 89, "WASD 이동 / Space 달리기 / E 길게 해킹 / G 의뢰 / C 전력 우회", White, 1.15f);
    text(25, h - 60, "T 회복 / I 구매 / Y 판매 / U 강화 / J 선제 PK / H 공격·반격", Cyan, 1.15f);
    text(
        25,
        h - 29,
        "Tab 주민·사거리 표시 / P 정지 / K 저장 / O 후처리 / B 블룸 / V 비네트 / F 흐림 / Esc 종료",
        Muted,
        1.05f);
    text(w - 265,
         h - 88,
         "좌표 " + std::to_string(int(player.x)) + "," + std::to_string(int(player.y)),
         Muted,
         1.05f);
    text(w - 265,
         h - 60,
         "마을 7곳 / 주민 " + std::to_string(Society::Population) + "명 / 이사 "
             + std::to_string(citizens.migrations),
         Muted,
         1.05f);
    if (hackProgress_ > 0)
    {
        rect(w / 2 - 100, h - 115, 200 * hackProgress_, 5, Cyan);
    }
    if (toastTime_ > 0)
    {
        rect(25, 94, w - 50, 15, Panel);
        text(30, 95, toast_, White, 1.0f, w - 60);
    }
    if (level_.Dead() || paused_)
    {
        rect(0, 110, w, h - 212, Color(.01f, .025f, .05f, .8f));
        text(w / 2 - 160,
             h / 2 - 25,
             level_.Dead() ? "신호 끊김 / R 새로 시작" : "일시정지 / P 계속하기",
             Pink,
             1.8f);
    }
}
