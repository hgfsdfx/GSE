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
    r_.Text(label.x - 56, label.y - 5, "CENTRAL PLAZA", Muted, 1.5f);
    if (scan_ && !level_.Dead())
    {
        WorldRing(player_, level_.Range(), Cyan.Alpha(.23f));
        WorldRing(player_, level_.MagnetRadius(), Green.Alpha(.27f));
    }
    for (const auto& pulse : level_.Pulses())
    {
        const float progress = 1 - std::clamp(pulse.remaining / 1.3f, 0.f, 1.f);
        const Point center = Project(pulse.position.x, pulse.position.y);
        for (int i = 0; i < 48; ++i)
        {
            const double a = i * 6.2831853 / 48, b = (i + 1) * 6.2831853 / 48;
            r_.Triangle(center,
                        Project(pulse.position.x + std::cos(a) * pulse.radius,
                                pulse.position.y + std::sin(a) * pulse.radius),
                        Project(pulse.position.x + std::cos(b) * pulse.radius,
                                pulse.position.y + std::sin(b) * pulse.radius),
                        Pink.Alpha(.08f + progress * .15f));
        }
        WorldRing(pulse.position, pulse.radius, Pink.Emissive(1), 2);
        WorldRing(pulse.position, pulse.radius * progress, Amber.Alpha(.8f), 2);
        r_.Text(center.x - 24, center.y - 8, "EVADE", Pink, 1.4f);
    }
}

void Prototype::DrawLevelEnemy(const LevelEnemy& enemy)
{
    const Point ground = Project(enemy.position.x, enemy.position.y);
    if (ground.x < -100 || ground.x > r_.Width() + 100 || ground.y < -100
        || ground.y > r_.Height() + 100)
    {
        return;
    }
    const bool boss = enemy.kind == EnemyKind::Boss;
    const float size = boss ? 27.f : enemy.kind == EnemyKind::Armored ? 13.f : 10.f;
    const Point p =
        ground + Point(0, -(boss ? 32.f : 18.f) + std::sin(time_ * 3 + float(enemy.id % 50)) * 2);
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
        r_.Text(p.x - 48, p.y - size - 30, "WARDEN-01", Pink, 1.4f);
    }
    if (enemy.spawnGrace > 0)
    {
        r_.Ring(ground, size + 10 + std::sin(time_ * 5) * 3, 1, color.Alpha(.6f));
    }
}

void Prototype::DrawLevelLoot(const LevelLoot& item)
{
    const Point p = Project(item.position.x, item.position.y, 6 + std::sin(time_ * 4) * 2);
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

void Prototype::DrawLevelProjectile(const LevelProjectile& shot)
{
    const Point p = Project(shot.position.x, shot.position.y, 20);
    const Point tail = Project(shot.position.x - shot.direction.x * 14,
                               shot.position.y - shot.direction.y * 14,
                               20);
    const Color color = shot.hostile ? Pink : Cyan;
    r_.Line(tail, p, shot.hostile ? 3.f : 2.f, color.Emissive(3));
    r_.Quad(p + Point(0, -4),
            p + Point(4, 0),
            p + Point(0, 4),
            p + Point(-4, 0),
            color.Emissive(3));
}

void Prototype::DrawCombatNumbers()
{
    for (const auto& number : level_.Numbers())
    {
        const Point p =
            Project(number.position.x, number.position.y, 42 + (1 - number.remaining / .8f) * 22);
        r_.Text(p.x - 6,
                p.y,
                (number.healing ? "+" : "") + std::to_string(number.value),
                (number.healing ? Green : White).Alpha(std::min(1.f, number.remaining * 3)),
                1.5f);
    }
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
    auto text = [&](float x, float y, const std::string& value, Color color, float scale = 1.4f)
    {
        r_.Text(x * ui, y * ui, value, color, scale * ui);
    };
    auto bar = [&](float x, float y, float width, float value, Color color)
    {
        rect(x, y, width, 6, Color(.10f, .16f, .22f));
        rect(x, y, width * std::clamp(value, 0.f, 1.f), 6, color);
    };
    rect(0, 0, uw, 94, Panel);
    rect(28, 26, 4, 39, Cyan);
    text(47, 25, "NIGHT / LINK", White, 3.4f);
    text(49, 63, "LEVEL 01 / SCAVENGER DISTRICT", Muted, 1.3f);
    text(uw - 285, 25, "23:48 / PERMANENT NIGHT", Cyan, 1.5f);
    text(uw - 285, 54, "RUN SEED " + std::to_string(level_.Seed() % 100000000), Muted, 1.25f);
    rect(28, 92, uw - 56, 1, Color(.16f, .29f, .34f));

    rect(28, 120, 282, 155, Panel);
    rect(28, 120, 3, 155, level_.Cleared() ? Green : Amber);
    text(45, 137, "FARMING BASICS", Muted, 1.3f);
    std::string title = "GATHER SEMICONDUCTORS";
    std::string first = "APPROACH ENEMIES TO FIRE";
    std::string second = "WALK NEAR LOOT TO COLLECT";
    if (!level_.InDistrict(player_))
    {
        title = "RETURN TO THE DISTRICT";
        first = "FOLLOW THE MAP TO LEVEL 1";
        second = "ENCOUNTER PAUSED OUTSIDE";
    }
    else if (level_.Cleared())
    {
        title = "DISTRICT SECURED";
        first = "COLLECT THE BOSS REWARDS";
        second = "R / NEW RANDOM RUN";
    }
    else if (level_.BossSpawned())
    {
        title = "DEFEAT WARDEN-01";
        first = "HEAD TO THE CENTRAL PLAZA";
        second = "EVADE THE RED PULSE ZONES";
    }
    else if (level_.WeaponRank() == 0 && level_.ChipsCollected() > 0)
    {
        title = "UPGRADE YOUR PHONE";
        first = "COLLECT THE GOLD MODULE";
        second = "THIRD KILL DROPS A MODULE";
    }
    else if (level_.WeaponRank() > 0)
    {
        title = "PREPARE FOR THE BOSS";
        first = "REACH LEVEL 4 AND 20 KILLS";
        second = "LEVEL UPS BOOST YOUR STATS";
    }
    text(45, 165, title, level_.Cleared() ? Green : Amber, 1.65f);
    text(45, 198, first, White, 1.25f);
    text(45, 221, second, White, 1.25f);
    text(45,
         250,
         "KILLS " + std::to_string(level_.Kills()) + "/20  LV " + std::to_string(level_.Level())
             + "/4",
         Muted,
         1.3f);

    rect(28, 288, 282, 191, Panel);
    text(45, 303, "HEALTH", Green);
    text(169,
         303,
         std::to_string(int(level_.Health())) + "/" + std::to_string(int(level_.MaxHealth())),
         White);
    bar(45, 327, 246, level_.Health() / level_.MaxHealth(), Green);
    text(45, 351, "LEVEL " + std::to_string(level_.Level()), Cyan);
    text(161,
         351,
         level_.Level() == 30 ? "MAX LEVEL"
                              : std::to_string(level_.Experience()) + "/"
                                    + std::to_string(level_.NextExperience()) + " XP",
         White,
         1.3f);
    bar(45,
        375,
        246,
        level_.Level() == 30 ? 1.f : float(level_.Experience()) / level_.NextExperience(),
        Cyan);
    text(45, 399, "PHONE UPGRADE +" + std::to_string(level_.WeaponRank()), Amber);
    text(45, 425, "CHIPS COLLECTED " + std::to_string(level_.ChipsCollected()), Muted, 1.25f);
    text(45, 450, "MAGNET RADIUS " + std::to_string(int(level_.MagnetRadius())), Muted, 1.25f);

    const float mapX = uw - 218, mapY = 120, ox = mapX + 27, oy = mapY + 47;
    rect(mapX, mapY, 190, 210, Panel);
    text(mapX + 15, mapY + 15, "LEVEL 1 / NORTH UP", Muted, 1.2f);
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            rect(ox + x * 45,
                 oy + y * 45,
                 42,
                 42,
                 x == 1 && y == 1 ? Color(.22f, .12f, .23f) : Color(.08f, .18f, .22f));
            rect(ox + x * 45, oy + y * 45, 3, 42, Muted.Alpha(.5f));
            rect(ox + x * 45, oy + y * 45, 42, 3, Muted.Alpha(.5f));
        }
    }
    for (const auto& enemy : level_.Enemies())
    {
        const Point p{float(ox + enemy.position.x / LevelOne::DistrictSize * 135) * ui,
                      float(oy + enemy.position.y / LevelOne::DistrictSize * 135) * ui};
        r_.Circle(p, (enemy.kind == EnemyKind::Boss ? 4.f : 1.7f) * ui, Pink);
    }
    r_.Circle({float(ox + std::clamp(player_.x / LevelOne::DistrictSize, 0.0, 1.0) * 135) * ui,
               float(oy + std::clamp(player_.y / LevelOne::DistrictSize, 0.0, 1.0) * 135) * ui},
              3 * ui,
              Cyan);
    text(mapX + 15, mapY + 191, "PINK / HOSTILE  CYAN / YOU", Muted, 1.0f);
    text(uw - 213, 348, "SMARTPHONE / AUTO AIM", Cyan, 1.25f);
    text(uw - 213,
         375,
         "DMG " + std::to_string(int(level_.Damage())) + "  CD " + Decimal(level_.FireInterval()),
         White,
         1.25f);
    text(uw - 213, 400, "RANGE " + std::to_string(int(level_.Range())) + " / LOS", Muted, 1.2f);

    const auto& effects = r_.PostEffects();
    rect(uw - 218, 429, 190, 106, Panel);
    text(uw - 203,
         441,
         r_.PostEffectsAvailable() ? std::string("O POST FX ") + (effects.enabled ? "ON" : "OFF")
                                   : "POST FX UNAVAILABLE",
         Cyan,
         1.15f);
    text(uw - 203,
         463,
         std::string("B BLOOM ") + (effects.bloom ? Decimal(effects.bloomStrength) : "OFF"),
         Muted,
         1.15f);
    text(uw - 203, 485, "V SHADE / F EDGE", Muted, 1.15f);
    text(uw - 203, 509, "[ ] EXPOSURE " + Decimal(effects.exposure), White, 1.1f);

    if (const auto boss = level_.Boss())
    {
        const float bx = uw / 2 - 185;
        rect(bx, 110, 370, 54, Panel);
        text(bx + 14,
             122,
             boss->health < boss->maxHealth * .5f ? "WARDEN-01 / OVERDRIVE"
                                                  : "WARDEN-01 / SECURITY CORE",
             Pink,
             1.4f);
        bar(bx + 14, 148, 342, boss->health / boss->maxHealth, Pink);
    }
    rect(0, bottom, uw, 109, Panel);
    rect(28, bottom, uw - 56, 1, Color(.16f, .29f, .34f));
    text(29, bottom + 18, "WASD MOVE / SPACE RUN", White, 1.3f);
    text(333, bottom + 18, "PHONE AUTO FIRE", Cyan, 1.3f);
    text(567, bottom + 18, "TAB RANGE / E HACK", White, 1.3f);
    text(uw - 225, bottom + 18, "P PAUSE / ESC EXIT", Muted, 1.2f);
    text(29, bottom + 49, "CYAN CHIP / XP", Cyan, 1.35f);
    text(245, bottom + 49, "GOLD / UPGRADE", Amber, 1.35f);
    text(465, bottom + 49, "GREEN / HEAL", Green, 1.35f);
    text(29, bottom + 81, "+/- ZOOM  K SAVE  /  AUTO SAVE 20S", Muted, 1.1f);
    const std::string device = target_.type == DeviceType::Camera ? "CAMERA"
                               : target_.type == DeviceType::Door ? "DOOR"
                                                                  : "RELAY";
    text(uw - 365,
         bottom + 48,
         target_.valid ? "E / " + device : "MOVE NEAR LOOT TO ATTRACT",
         Muted,
         1.1f);
    bar(uw - 365,
        bottom + 69,
        334,
        hackProgress_ > 0 ? hackProgress_ : 1 - level_.ShotCooldown() / level_.FireInterval(),
        Cyan);
    text(uw - 365,
         bottom + 87,
         hackProgress_ > 0 ? "HACKING..." : "SMARTPHONE SHOT COOLDOWN",
         Muted,
         1.0f);
    if (toastTime_ > 0)
    {
        const float width = std::min(uw - 60, float(toast_.size()) * 7.2f + 32);
        rect((uw - width) / 2, bottom - 44, width, 32, Panel);
        text((uw - width) / 2 + 16, bottom - 34, toast_, White, 1.2f);
    }
    if (level_.Dead())
    {
        rect(0, 94, uw, bottom - 94, Color(.01f, .02f, .04f, .75f));
        text(uw / 2 - 126, uh / 2 - 30, "SIGNAL LOST", Pink, 3);
        text(uw / 2 - 150, uh / 2 + 15, "R / RETRY WITH A NEW MAP", White, 1.5f);
    }
    else if (paused_)
    {
        rect(0, 94, uw, bottom - 94, Color(.01f, .025f, .05f, .65f));
        text(uw / 2 - 72, uh / 2 - 15, "PAUSED", White, 3);
        text(uw / 2 - 104, uh / 2 + 24, "PRESS P TO RESUME", Cyan, 1.4f);
    }
    else if (level_.Cleared())
    {
        rect(uw / 2 - 190, 110, 380, 51, Panel);
        text(uw / 2 - 151, 125, "LEVEL 1 CLEAR / R NEW RUN", Green, 1.65f);
    }
}
