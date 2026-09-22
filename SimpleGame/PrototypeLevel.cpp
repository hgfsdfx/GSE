#include "stdafx.h"
#include "Prototype.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
    const Color Cyan(.22f, .94f, .91f);
    const Color Pink(.97f, .26f, .59f);
    const Color Amber(1, .7f, .3f);
    const Color Green(.34f, 1, .55f);
    const Color White(.82f, .9f, .94f);
    const Color Muted(.37f, .5f, .59f);
    const Color Panel(.025f, .055f, .09f, .96f);

    std::string Decimal(float value)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << value;
        return stream.str();
    }
} // namespace

void Prototype::WorldRing(WorldPoint center, float radius, Color color, float width)
{
    for (int i = 0; i < 64; ++i)
    {
        const double a = i * 6.2831853 / 64, b = (i + 1) * 6.2831853 / 64;
        r_.Line(Project(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius),
                Project(center.x + std::cos(b) * radius, center.y + std::sin(b) * radius),
                width,
                color);
    }
}

void Prototype::DrawLevelGround()
{
    const auto arena = level_.BossArena();
    WorldRing(arena, 260, Color(.16f, .32f, .38f), 2);
    WorldRing(arena, 245, Cyan.Alpha(.18f));
    const Point label = Project(arena.x, arena.y);
    r_.Text(label.x - r_.TextWidth("중앙 광장", 1.5f) / 2, label.y - 5, "중앙 광장", Muted, 1.5f);
    if (scan_ && !level_.Dead())
    {
        WorldRing(level_.Player().WorldPosition(), level_.Range(), Cyan.Alpha(.23f));
        WorldRing(level_.Player().WorldPosition(), level_.MagnetRadius(), Green.Alpha(.27f));
    }
}

void Prototype::DrawPulse(const PulseActor& pulse)
{
    const float progress = 1 - std::clamp(pulse.remaining / 1.3f, 0.f, 1.f);
    const Point center = Project(pulse.WorldPosition().x, pulse.WorldPosition().y);
    for (int i = 0; i < 48; ++i)
    {
        const double a = i * 6.2831853 / 48, b = (i + 1) * 6.2831853 / 48;
        r_.Triangle(center,
                    Project(pulse.WorldPosition().x + std::cos(a) * pulse.radius,
                            pulse.WorldPosition().y + std::sin(a) * pulse.radius),
                    Project(pulse.WorldPosition().x + std::cos(b) * pulse.radius,
                            pulse.WorldPosition().y + std::sin(b) * pulse.radius),
                    Pink.Alpha(.08f + progress * .15f));
    }
    WorldRing(pulse.WorldPosition(), pulse.radius, Pink.Emissive(1), 2);
    WorldRing(pulse.WorldPosition(), pulse.radius * progress, Amber.Alpha(.8f), 2);
    r_.Text(center.x - r_.TextWidth("회피", 1.4f) / 2, center.y - 8, "회피", Pink, 1.4f);
}

void Prototype::DrawEnemyActor(const EnemyActor& enemy)
{
    const Point ground = Project(enemy.WorldPosition().x, enemy.WorldPosition().y);
    if (ground.x < -100 || ground.x > r_.Width() + 100 || ground.y < -100
        || ground.y > r_.Height() + 100)
    {
        return;
    }
    const bool boss = enemy.kind == EnemyKind::Boss;
    const float size = boss ? 27.f : enemy.kind == EnemyKind::Armored ? 17.f : 13.f;
    const Point p =
        ground
        + Point(0,
                -(boss ? 32.f : 18.f) + std::sin(time_ * 3 + float(enemy.persistentId % 50)) * 2);
    const Color color = enemy.hitFlash > 0                 ? White
                        : boss                             ? Pink
                        : enemy.kind == EnemyKind::Armored ? Amber
                                                           : Pink;
    r_.Circle(ground, size, Color(0, .01f, .025f, .55f));
    r_.Quad(p + Point(0, -size),
            p + Point(size, 0),
            p + Point(0, size),
            p + Point(-size, 0),
            Color(.14f, .18f, .25f));
    r_.Line(p + Point(-size, 0), p + Point(size, 0), 2, color.Emissive(1.8f));
    r_.Circle(p, boss ? 7.f : 4.f, color.Emissive(2.5f));
    for (int sign : {-1, 1})
    {
        r_.Line(p + Point(sign * size, 0), p + Point(sign * size * 1.5f, -5), 3, Muted);
        r_.Ring(p + Point(sign * size * 1.6f, -5), boss ? 10.f : 5.f, 1.5f, color);
    }
    const float ratio = std::clamp(enemy.health / enemy.maxHealth, 0.f, 1.f);
    r_.Rect(p.x - size, p.y - size - 12, size * 2, 3, Color(.12f, .13f, .18f));
    r_.Rect(p.x - size, p.y - size - 12, size * 2 * ratio, 3, color);
    if (boss)
    {
        r_.Text(p.x - r_.TextWidth("감시자-01", 1.4f) / 2,
                p.y - size - 30,
                "감시자-01",
                Pink,
                1.4f);
    }
    else if (scan_)
    {
        const bool armored = enemy.kind == EnemyKind::Armored;
        const std::string label = armored ? "장갑형" : "정찰형";
        r_.Text(p.x - r_.TextWidth(label, 1.1f) / 2, p.y - size - 25, label, color, 1.1f);
    }
    if (enemy.spawnGrace > 0)
    {
        r_.Ring(ground, size + 10 + std::sin(time_ * 5) * 3, 1, color.Alpha(.6f));
    }
}

void Prototype::DrawLootActor(const LootActor& item)
{
    const Point p =
        Project(item.WorldPosition().x, item.WorldPosition().y, 6 + std::sin(time_ * 4) * 2);
    const Color color = item.kind == LootKind::Semiconductor ? Cyan
                        : item.kind == LootKind::Upgrade     ? Amber
                                                             : Green;
    if (item.kind == LootKind::Semiconductor)
    {
        r_.Quad(p + Point(0, -6),
                p + Point(6, 0),
                p + Point(0, 6),
                p + Point(-6, 0),
                color.Emissive(2));
        r_.Rect(p.x - 2, p.y - 2, 4, 4, Panel);
    }
    else if (item.kind == LootKind::Upgrade)
    {
        r_.Rect(p.x - 6, p.y - 7, 12, 14, color.Emissive(1.5f));
        r_.Line(p + Point(-3, 2), p + Point(0, -2), 2, Panel);
        r_.Line(p + Point(0, -2), p + Point(3, 2), 2, Panel);
    }
    else
    {
        r_.Rect(p.x - 7, p.y - 6, 14, 12, Color(.06f, .18f, .13f));
        r_.Rect(p.x - 2, p.y - 5, 4, 10, color.Emissive(1.5f));
        r_.Rect(p.x - 5, p.y - 2, 10, 4, color.Emissive(1.5f));
    }
}

void Prototype::DrawProjectileActor(const ProjectileActor& shot)
{
    const Point p = Project(shot.WorldPosition().x, shot.WorldPosition().y, 20);
    const Point tail = Project(shot.WorldPosition().x - shot.direction.x * 14,
                               shot.WorldPosition().y - shot.direction.y * 14,
                               20);
    const Color color = shot.hostile ? Pink : Cyan;
    r_.Line(tail, p, shot.hostile ? 3.f : 2.f, color.Emissive(3));
    r_.Quad(p + Point(0, -4),
            p + Point(4, 0),
            p + Point(0, 4),
            p + Point(-4, 0),
            color.Emissive(3));
}

void Prototype::DrawCombatNumber(const CombatNumberActor& number)
{
    const Point p = Project(number.WorldPosition().x,
                            number.WorldPosition().y,
                            42 + (1 - number.remaining / .8f) * 22);
    r_.Text(p.x - 6,
            p.y,
            (number.healing ? "+" : "") + std::to_string(number.value),
            (number.healing ? Green : White).Alpha(std::min(1.f, number.remaining * 3)),
            1.5f);
}

void Prototype::DrawLevelHud()
{
    const float w = float(r_.Width()), h = float(r_.Height());
    const float ui = std::min(1.f, std::min(w / 1120.f, h / 680.f));
    const float uw = w / ui, uh = h / ui, bottom = uh - 109;
    auto rect = [&](float x, float y, float width, float height, Color color)
    {
        r_.Rect(x * ui, y * ui, width * ui, height * ui, color);
    };
    auto text = [&](float x,
                    float y,
                    const std::string& value,
                    Color color,
                    float scale = 1.4f,
                    float maxWidth = 0)
    {
        const float width = r_.TextWidth(value, scale);
        if (maxWidth > 0 && width > maxWidth)
        {
            scale *= maxWidth / width;
        }
        r_.Text(x * ui, y * ui, value, color, scale * ui);
    };
    auto centeredText = [&](float y, const std::string& value, Color color, float scale)
    {
        text((uw - r_.TextWidth(value, scale)) / 2, y, value, color, scale);
    };
    auto bar = [&](float x, float y, float width, float value, Color color)
    {
        rect(x, y, width, 6, Color(.10f, .16f, .22f));
        rect(x, y, width * std::clamp(value, 0.f, 1.f), 6, color);
    };
    rect(0, 0, uw, 94, Panel);
    rect(28, 26, 4, 39, Cyan);
    text(47, 25, "나이트 / 링크", White, 3.4f);
    text(49, 63, "레벨 01 / 수집가 구역", Muted, 1.3f);
    text(uw - 285, 25, "23:48 / 영원한 밤", Cyan, 1.5f);
    text(uw - 285, 54, "맵 시드 " + std::to_string(level_.Seed() % 100000000), Muted, 1.25f);
    rect(28, 92, uw - 56, 1, Color(.16f, .29f, .34f));

    rect(28, 120, 282, 155, Panel);
    rect(28, 120, 3, 155, level_.Cleared() ? Green : Amber);
    const auto enemies = level_.Enemies();
    const auto hostiles = std::count_if(
        enemies.begin(),
        enemies.end(),
        [&](const EnemyActor& enemy)
        {
            return enemy.health > 0
                   && std::hypot(enemy.WorldPosition().x - level_.Player().WorldPosition().x,
                                 enemy.WorldPosition().y - level_.Player().WorldPosition().y)
                          <= 650;
        });
    text(45, 137, "파밍 / 주변 적 " + std::to_string(hostiles), Muted, 1.3f);
    std::string title = "반도체 수집하기";
    std::string first = "적에게 접근하면 자동 발사";
    std::string second = "아이템에 다가가면 자동 습득";
    if (!level_.InDistrict(level_.Player().WorldPosition()))
    {
        title = "도시 순찰";
        first = "드론을 사냥하고 아이템 수집";
        second = "레벨 1 보스 / 중앙 광장";
    }
    else if (level_.Cleared())
    {
        title = "구역 확보 완료";
        first = "도시에서 파밍을 계속하세요";
        second = "R / 새 랜덤 맵 시작";
    }
    else if (level_.BossSpawned())
    {
        title = "감시자-01 처치";
        first = "중앙 광장으로 이동하세요";
        second = "붉은 충격파 구역을 피하세요";
    }
    else if (level_.WeaponRank() == 0 && level_.ChipsCollected() > 0)
    {
        title = "스마트폰 강화하기";
        first = "금색 강화 모듈을 수집하세요";
        second = "세 번째 처치 시 모듈 드랍";
    }
    else if (level_.WeaponRank() > 0)
    {
        title = "보스 전투 준비";
        first = "레벨 4 달성 및 적 20기 처치";
        second = "레벨이 오르면 능력치 증가";
    }
    text(45, 165, title, level_.Cleared() ? Green : Amber, 1.65f, 246);
    text(45, 198, first, White, 1.25f, 246);
    text(45, 221, second, White, 1.25f, 246);
    text(45,
         250,
         "처치 " + std::to_string(level_.Kills()) + "/20  레벨 " + std::to_string(level_.Level())
             + "/4",
         Muted,
         1.3f);

    rect(28, 288, 282, 191, Panel);
    text(45, 303, "체력", Green);
    text(169,
         303,
         std::to_string(int(level_.Health())) + "/" + std::to_string(int(level_.MaxHealth())),
         White);
    bar(45, 327, 246, level_.Health() / level_.MaxHealth(), Green);
    text(45, 351, "레벨 " + std::to_string(level_.Level()), Cyan);
    text(161,
         351,
         level_.Level() == 30 ? "최고 레벨"
                              : std::to_string(level_.Experience()) + "/"
                                    + std::to_string(level_.NextExperience()) + " 경험치",
         White,
         1.3f);
    bar(45,
        375,
        246,
        level_.Level() == 30 ? 1.f : float(level_.Experience()) / level_.NextExperience(),
        Cyan);
    text(45, 399, "스마트폰 강화 +" + std::to_string(level_.WeaponRank()), Amber);
    text(45, 425, "수집한 반도체 " + std::to_string(level_.ChipsCollected()), Muted, 1.25f);
    text(45, 450, "자석 범위 " + std::to_string(int(level_.MagnetRadius())), Muted, 1.25f);

    const float mapX = uw - 218, mapY = 120, ox = mapX + 27, oy = mapY + 47;
    const bool districtMap = level_.InDistrict(level_.Player().WorldPosition());
    const WorldPoint mapOrigin =
        districtMap
            ? WorldPoint{}
            : WorldPoint{(std::floor(level_.Player().WorldPosition().x / World::ChunkSize) - 1)
                             * World::ChunkSize,
                         (std::floor(level_.Player().WorldPosition().y / World::ChunkSize) - 1)
                             * World::ChunkSize};
    rect(mapX, mapY, 190, 210, Panel);
    text(mapX + 15,
         mapY + 15,
         districtMap ? "레벨 1 / 위쪽이 북쪽" : "주변 도시 / 위쪽이 북쪽",
         Muted,
         1.2f,
         160);
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            rect(ox + x * 45,
                 oy + y * 45,
                 42,
                 42,
                 districtMap && x == 1 && y == 1 ? Color(.22f, .12f, .23f)
                                                 : Color(.08f, .18f, .22f));
            rect(ox + x * 45, oy + y * 45, 3, 42, Muted.Alpha(.5f));
            rect(ox + x * 45, oy + y * 45, 42, 3, Muted.Alpha(.5f));
        }
    }
    for (const auto& enemy : level_.Enemies())
    {
        const double x = (enemy.WorldPosition().x - mapOrigin.x) / LevelOne::DistrictSize;
        const double y = (enemy.WorldPosition().y - mapOrigin.y) / LevelOne::DistrictSize;
        if (x < 0 || y < 0 || x >= 1 || y >= 1)
        {
            continue;
        }
        const Point p{float(ox + x * 135) * ui, float(oy + y * 135) * ui};
        r_.Circle(p, (enemy.kind == EnemyKind::Boss ? 4.f : 1.7f) * ui, Pink);
    }
    r_.Circle(
        {float(ox
               + (level_.Player().WorldPosition().x - mapOrigin.x) / LevelOne::DistrictSize * 135)
             * ui,
         float(oy
               + (level_.Player().WorldPosition().y - mapOrigin.y) / LevelOne::DistrictSize * 135)
             * ui},
        3 * ui,
        Cyan);
    text(mapX + 15, mapY + 191, "분홍: 적  청록: 플레이어", Muted, 1.0f);
    text(uw - 213, 348, "스마트폰 / 자동 조준", Cyan, 1.25f);
    text(uw - 213,
         375,
         "공격력 " + std::to_string(int(level_.Damage())) + "  간격 "
             + Decimal(level_.FireInterval()) + "초",
         White,
         1.25f,
         185);
    text(uw - 213,
         400,
         "사거리 " + std::to_string(int(level_.Range())) + " / 시야 내",
         Muted,
         1.2f);

    const auto& effects = r_.PostEffects();
    rect(uw - 218, 429, 190, 106, Panel);
    text(uw - 203,
         441,
         r_.PostEffectsAvailable() ? std::string("O 후처리 ") + (effects.enabled ? "켜짐" : "꺼짐")
                                   : "후처리 사용 불가",
         Cyan,
         1.15f);
    text(uw - 203,
         463,
         std::string("B 블룸 ") + (effects.bloom ? Decimal(effects.bloomStrength) : "꺼짐"),
         Muted,
         1.15f);
    text(uw - 203, 485, "V 비네트 / F 흐림", Muted, 1.15f);
    text(uw - 203, 509, "[ ] 노출 " + Decimal(effects.exposure), White, 1.1f);

    if (const auto boss = level_.Boss())
    {
        const float bx = uw / 2 - 185;
        rect(bx, 110, 370, 54, Panel);
        text(bx + 14,
             122,
             boss->health < boss->maxHealth * .5f ? "감시자-01 / 과부하" : "감시자-01 / 보안 코어",
             Pink,
             1.4f);
        bar(bx + 14, 148, 342, boss->health / boss->maxHealth, Pink);
    }
    rect(0, bottom, uw, 109, Panel);
    rect(28, bottom, uw - 56, 1, Color(.16f, .29f, .34f));
    text(29, bottom + 18, "WASD 이동 / Space 달리기", White, 1.3f);
    text(333, bottom + 18, "스마트폰 자동 발사", Cyan, 1.3f);
    text(567, bottom + 18, "Tab 사거리 / E 해킹", White, 1.3f);
    text(uw - 225, bottom + 18, "P 일시정지 / Esc 종료", Muted, 1.2f);
    text(29, bottom + 49, "청록 반도체 / 경험치", Cyan, 1.35f);
    text(245, bottom + 49, "금색 / 무기 강화", Amber, 1.35f);
    text(465, bottom + 49, "초록 / 체력 회복", Green, 1.35f);
    text(29, bottom + 81, "+/- 확대·축소  K 저장 / 20초마다 자동 저장", Muted, 1.1f);
    const std::string device =
        target_.type == DeviceType::Camera ? "카메라"
        : target_.type == DeviceType::Building
            ? (world_.Changes(target_.key).lightsOff ? "조명 켜기" : "조명 끄기")
            : "중계기";
    text(uw - 365,
         bottom + 48,
         target_.valid ? "E / " + device : "가까운 아이템을 자동으로 끌어당깁니다",
         Muted,
         1.1f);
    bar(uw - 365,
        bottom + 69,
        334,
        hackProgress_ > 0 ? hackProgress_ : 1 - level_.ShotCooldown() / level_.FireInterval(),
        Cyan);
    text(uw - 365,
         bottom + 87,
         hackProgress_ > 0 ? "해킹 중..." : "스마트폰 발사 대기",
         Muted,
         1.0f);
    if (toastTime_ > 0)
    {
        const float width = std::min(uw - 60, r_.TextWidth(toast_, 1.2f) + 32);
        rect((uw - width) / 2, bottom - 44, width, 32, Panel);
        text((uw - width) / 2 + 16, bottom - 34, toast_, White, 1.2f, width - 32);
    }
    if (level_.Dead())
    {
        rect(0, 94, uw, bottom - 94, Color(.01f, .02f, .04f, .75f));
        centeredText(uh / 2 - 30, "신호 끊김", Pink, 3);
        centeredText(uh / 2 + 15, "R / 새 맵에서 다시 시작", White, 1.5f);
    }
    else if (paused_)
    {
        rect(0, 94, uw, bottom - 94, Color(.01f, .025f, .05f, .65f));
        centeredText(uh / 2 - 15, "일시정지", White, 3);
        centeredText(uh / 2 + 24, "P 키로 계속하기", Cyan, 1.4f);
    }
    else if (level_.Cleared())
    {
        rect(uw / 2 - 190, 110, 380, 51, Panel);
        centeredText(125, "레벨 1 완료 / R 새로 시작", Green, 1.65f);
    }
}
