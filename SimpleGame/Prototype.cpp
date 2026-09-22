#include "stdafx.h"
#include "Prototype.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace
{
    const Color Cyan(.22f, .94f, .91f), Pink(.97f, .26f, .59f), Amber(1, .7f, .3f);
    const Color White(.82f, .9f, .94f), Muted(.37f, .5f, .59f), Panel(.025f, .055f, .09f, .96f);

    Color Accent(unsigned style)
    {
        const Color palette[] = {Cyan, Pink, Color(.53f, .45f, 1), Amber, Color(.32f, .68f, 1)};
        return palette[style % 5];
    }

    bool SameDevice(const Device& a, const Device& b)
    {
        return a.valid && b.valid && a.key == b.key && a.type == b.type;
    }

    std::string Decimal(float n)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << n;
        return out.str();
    }

    // Canonical mesh coordinates have no camera offset or zoom baked in.
    Point ProjectMesh(double x, double y, float height = 0)
    {
        return {float((x - y) * .82), float((x + y) * .43 - height)};
    }

    std::string BuildingMeshKey(const Building& b, bool illuminated)
    {
        // Identical buildings can share a mesh even in different world chunks.
        std::ostringstream out;
        out << "building:" << std::hexfloat << b.width << ':' << b.depth << ':' << b.height << ':'
            << b.style << ':' << illuminated;
        return out.str();
    }
} // namespace

Prototype::Prototype(Renderer& renderer, const std::filesystem::path& savePath)
    : r_(renderer),
      savePath_(savePath.parent_path() / L"level_one.save")
{
    BindActorRenderers();
    level_.Start(world_);
    level_.Load(savePath_, world_);
    saveAllowed_ = !level_.SaveBlocked();
    if (!saveAllowed_)
    {
        Notify("저장 파일 읽기 실패 / 원본 보존됨");
    }
    world_.Stream(level_.Player().WorldPosition(), StreamRadius());
    if (!world_.CanWalk(level_.Player().WorldPosition()))
    {
        const auto key = world_.KeyAt(level_.Player().WorldPosition());
        level_.Player().SetWorldPosition(
            {key.x * World::ChunkSize + 50, key.y * World::ChunkSize + 50});
    }
    camera_ = level_.Player().WorldPosition();
    target_ = world_.NearestDevice(level_.Player().WorldPosition());
    const std::string message = level_.TakeMessage();
    if (!message.empty())
    {
        Notify(message);
    }
}

int Prototype::StreamRadius() const
{
    const double reach =
        (r_.Width() / (4 * .82 * zoom_) + r_.Height() / (4 * .43 * zoom_) + 280 / (.86 * zoom_));
    return std::clamp(static_cast<int>(std::ceil(reach / World::ChunkSize)) + 1, 2, 6);
}

void Prototype::Notify(const std::string& message)
{
    toast_ = message;
    toastTime_ = 6;
}

bool Prototype::Save()
{
    if (!saveAllowed_)
    {
        Notify("저장 불가 / 손상된 저장 파일을 먼저 옮겨 주세요");
        return false;
    }
    if (!level_.Save(savePath_, world_))
    {
        Notify("저장 실패 / 폴더 쓰기 권한을 확인해 주세요");
        std::cerr << "진행 상황 저장 실패: " << savePath_.u8string() << '\n';
        return false;
    }
    return true;
}

void Prototype::ReleaseKeys()
{
    keys_.fill(false);
    hackProgress_ = 0;
}

void Prototype::Key(unsigned char key, bool down)
{
    if (key >= 'A' && key <= 'Z')
    {
        key = static_cast<unsigned char>(key + 32);
    }
    const bool pressed = down && !keys_[key];
    keys_[key] = down;
    if (!pressed)
    {
        return;
    }
    if (key == 'r' && (level_.Dead() || level_.Cleared()))
    {
        // Retrying is explicit. An unreadable existing save remains protected.
        const bool canSave = saveAllowed_;
        BindActorRenderers();
        level_.Start(world_);
        saveAllowed_ = canSave;
        camera_ = level_.Player().WorldPosition();
        paused_ = false;
        trace_ = 0;
        lockdown_ = 0;
        saveTime_ = 0;
        ReleaseKeys();
        Notify(level_.TakeMessage());
        if (saveAllowed_)
        {
            Save();
        }
        return;
    }
    if (key == '\t')
    {
        scan_ = !scan_;
    }
    if (key == 'p')
    {
        paused_ = !paused_;
        hackProgress_ = 0;
    }
    if (key == '+' || key == '=')
    {
        zoom_ = std::min(1.4f, zoom_ + .1f);
    }
    if (key == '-')
    {
        zoom_ = std::max(.7f, zoom_ - .1f);
    }
    if (key == 'k' && Save())
    {
        Notify("진행 상황을 저장했습니다");
    }
    auto& effects = r_.PostEffects();
    if (key == 'o')
    {
        effects.enabled = !effects.enabled;
        Notify(effects.enabled ? "후처리 켜짐" : "후처리 꺼짐");
    }
    if (key == 'b')
    {
        effects.bloom = !effects.bloom;
        Notify(effects.bloom ? "블룸 켜짐" : "블룸 꺼짐");
    }
    if (key == 'v')
    {
        effects.vignette = !effects.vignette;
        Notify(effects.vignette ? "비네트 켜짐" : "비네트 꺼짐");
    }
    if (key == 'f')
    {
        effects.edgeBlur = !effects.edgeBlur;
        Notify(effects.edgeBlur ? "가장자리 흐림 켜짐" : "가장자리 흐림 꺼짐");
    }
    if (key == '[' || key == ']')
    {
        effects.exposure = std::clamp(effects.exposure + (key == ']' ? .1f : -.1f), .25f, 4.f);
        Notify("노출 / " + Decimal(effects.exposure));
    }
    if (key == ',' || key == '.')
    {
        effects.bloomStrength =
            std::clamp(effects.bloomStrength + (key == '.' ? .05f : -.05f), 0.f, 2.f);
        Notify("블룸 강도 / " + Decimal(effects.bloomStrength));
    }
    if (key == '1' || key == '2')
    {
        effects.vignetteStrength =
            std::clamp(effects.vignetteStrength + (key == '2' ? .05f : -.05f), 0.f, .95f);
        Notify("비네트 강도 / " + Decimal(effects.vignetteStrength));
    }
    if (key == '3' || key == '4')
    {
        effects.edgeBlurStrength =
            std::clamp(effects.edgeBlurStrength + (key == '4' ? .05f : -.05f), 0.f, 1.f);
        Notify("가장자리 흐림 강도 / " + Decimal(effects.edgeBlurStrength));
    }
}

void Prototype::Update(float dt)
{
    if (paused_)
    {
        return;
    }
    dt = std::clamp(dt, 0.f, .05f);
    time_ += dt;
    toastTime_ = std::max(0.f, toastTime_ - dt);
    if (level_.Dead())
    {
        moving_ = false;
        return;
    }
    saveTime_ += dt;
    lockdown_ = std::max(0.f, lockdown_ - dt);
    const float sx = float(keys_['d']) - float(keys_['a']);
    const float sy = float(keys_['s']) - float(keys_['w']);
    moving_ = sx != 0 || sy != 0;
    level_.Player().input = {sx, sy};
    level_.Player().running = keys_[' '];
    const bool wasCleared = level_.Cleared();
    level_.Update(dt, world_, StreamRadius());
    const double follow = 1 - std::exp(-dt * 8);
    camera_.x += (level_.Player().WorldPosition().x - camera_.x) * follow;
    camera_.y += (level_.Player().WorldPosition().y - camera_.y) * follow;
    target_ = world_.NearestDevice(level_.Player().WorldPosition());
    const bool wantsHack =
        !level_.Dead() && keys_['e'] && target_.valid && !moving_ && lockdown_ <= 0;
    if (wantsHack)
    {
        if (!SameDevice(target_, hacking_))
        {
            hacking_ = target_;
            hackProgress_ = 0;
        }
        hackProgress_ += dt / (world_.Credits() >= 150 ? 1.35f : 1.8f);
        if (hackProgress_ >= 1)
        {
            Hack(target_);
            hackProgress_ = 0;
            keys_['e'] = false;
        }
    }
    else
    {
        hackProgress_ = 0;
        hacking_.valid = false;
    }
    bool watched = false;
    const auto key = world_.KeyAt(level_.Player().WorldPosition());
    for (const auto& device : world_.Devices(key))
    {
        if (device.type == DeviceType::Camera && world_.Powered(key)
            && !world_.Changes(key).cameraOff
            && std::hypot(level_.Player().WorldPosition().x - device.position.x,
                          level_.Player().WorldPosition().y - device.position.y)
                   < 155)
        {
            watched = true;
        }
    }
    if (watched && wantsHack)
    {
        trace_ = std::min(100.f, trace_ + dt * 9);
    }
    else if (!wantsHack)
    {
        trace_ = std::max(0.f, trace_ - dt * 3);
    }
    if (trace_ >= 100)
    {
        lockdown_ = 8;
        trace_ = 65;
        hackProgress_ = 0;
        Notify("접속 차단 / 8초 후 다시 시도");
    }

    const std::string message = level_.TakeMessage();
    if (!message.empty())
    {
        Notify(message);
    }
    if (saveTime_ > 20 || level_.Dead() || (!wasCleared && level_.Cleared()))
    {
        Save();
        saveTime_ = 0;
    }
}

void Prototype::Hack(const Device& target)
{
    if (target.type == DeviceType::Power)
    {
        if (world_.Powered(target.key))
        {
            Notify("중계기 정상 / 복구할 필요가 없습니다");
            return;
        }
        world_.Change(target.key).eventSolved = true;
        Notify("전력 복구 / 150 크레딧 획득 / 해킹 속도 증가");
    }
    else if (target.type == DeviceType::Camera)
    {
        if (!world_.Powered(target.key))
        {
            Notify("카메라 전력 없음 / 중계기를 먼저 복구하세요");
            return;
        }
        auto& change = world_.Change(target.key);
        change.cameraOff = !change.cameraOff;
        Notify(change.cameraOff ? "카메라 영상 반복 / 지역 추적 억제" : "카메라 감시 복원");
        trace_ = std::min(100.f, trace_ + 14);
    }
    else
    {
        if (!world_.Powered(target.key))
        {
            Notify("건물 전력 없음 / 중계기를 먼저 복구하세요");
            return;
        }
        auto& change = world_.Change(target.key);
        change.lightsOff = !change.lightsOff;
        if (change.lightsOff && !change.dataTaken)
        {
            // Recover the archive through the exterior terminal, without entering geometry.
            change.dataTaken = true;
            Notify("건물 소등 / 자료 다운로드 / 75 크레딧 획득");
        }
        else
        {
            Notify(change.lightsOff ? "건물 조명 꺼짐" : "건물 조명 켜짐");
        }
        trace_ = std::min(100.f, trace_ + 22);
    }
    Save();
}

Point Prototype::Project(double x, double y, float height) const
{
    // Subtract camera in double precision before sending small floats to OpenGL.
    const double dx = x - camera_.x, dy = y - camera_.y;
    return {r_.Width() * .5f + float((dx - dy) * .82) * zoom_,
            r_.Height() * .56f + float((dx + dy) * .43 - height) * zoom_};
}

void Prototype::Ground(const GroundActor& actor)
{
    const auto found = world_.Chunks().find(actor.key);
    if (found == world_.Chunks().end())
    {
        return;
    }
    Chunk chunk;
    chunk.key = actor.key;
    const auto origin = actor.WorldPosition();
    std::ostringstream signature;
    signature << "ground:" << level_.Seed() << ':' << chunk.key.x << ':' << chunk.key.y
              << std::hexfloat;
    for (auto id : level_.Scene().Children(actor.Parent()))
    {
        const auto building = dynamic_cast<const BuildingActor*>(level_.Scene().Find(id));
        if (!building || !level_.Scene().IsActive(id) || !level_.Scene().IsVisible(id))
        {
            continue;
        }
        auto geometry = building->Geometry();
        geometry.x += chunk.key.x * World::ChunkSize - origin.x;
        geometry.y += chunk.key.y * World::ChunkSize - origin.y;
        signature << ':' << geometry.x << ':' << geometry.y << ':' << geometry.width << ':'
                  << geometry.depth;
        chunk.buildings.push_back(geometry);
    }
    const std::string meshKey = signature.str();
    if (r_.BeginCachedMesh(meshKey, Project(origin.x, origin.y), zoom_))
    {
        GroundMesh(chunk);
        r_.EndCachedMesh();
    }
    if (scan_)
    {
        const auto devices = world_.Devices(chunk.key);
        for (size_t i = 1; i < devices.size(); ++i)
        {
            const Point a = Project(devices[0].position.x, devices[0].position.y),
                        b = Project(devices[i].position.x, devices[i].position.y);
            for (int j = 0; j < 12; j += 2)
            {
                r_.Line(a + (b - a) * (j / 12.f),
                        a + (b - a) * ((j + 1) / 12.f),
                        1,
                        Cyan.Alpha(.3f));
            }
        }
    }
}

void Prototype::GroundMesh(const Chunk& chunk)
{
    const double x = chunk.key.x * World::ChunkSize, y = chunk.key.y * World::ChunkSize;
    auto project = [&](double a, double b)
    {
        return ProjectMesh(a - x, b - y);
    };
    auto quad = [&](double a, double b, double w, double d, Color c)
    {
        r_.Quad(project(a, b), project(a + w, b), project(a + w, b + d), project(a, b + d), c);
    };
    quad(x, y, 640, 640, Color(.047f, .073f, .105f));
    quad(x + 102, y + 102, 535, 535, Color(.08f, .11f, .15f));
    // Two connected arterial roads along the north and west edges of every chunk.
    quad(x, y, 98, 640, Color(.025f, .047f, .073f));
    quad(x, y, 640, 98, Color(.025f, .047f, .073f));
    for (int i = 110; i < 640; i += 48)
    {
        r_.Line(project(x + 49, y + i),
                project(x + 49, y + i + 21),
                1,
                Color(.29f, .37f, .4f, .6f));
        r_.Line(project(x + i, y + 49),
                project(x + i + 21, y + 49),
                1,
                Color(.29f, .37f, .4f, .6f));
    }
    for (int i = 0; i < 6; ++i)
    {
        quad(x + 110 + i * 10, y + 8, 4, 80, Color(.25f, .33f, .39f, .65f));
        quad(x + 8, y + 110 + i * 10, 80, 4, Color(.25f, .33f, .39f, .65f));
    }
    r_.Line(project(x + 100, y + 100), project(x + 640, y + 100), 2, Color(.18f, .28f, .34f));
    r_.Line(project(x + 100, y + 100), project(x + 100, y + 640), 2, Color(.18f, .28f, .34f));
    for (int i = 150; i < 640; i += 90)
    {
        r_.Line(project(x + i, y + 105), project(x + i, y + 640), .7f, Color(.11f, .16f, .2f));
        r_.Line(project(x + 105, y + i), project(x + 640, y + i), .7f, Color(.11f, .16f, .2f));
    }
    for (const auto& b : chunk.buildings)
    {
        quad(b.x + 8, b.y + 25, b.width + 35, b.depth + 32, Color(0, .01f, .025f, .55f));
    }
}

void Prototype::DrawBuilding(const Building& b, const ChunkKey& key)
{
    const Point a = Project(b.x, b.y), c = Project(b.x + b.width, b.y + b.depth);
    if (c.y < -80 || a.y - b.height * zoom_ > r_.Height() + 100
        || std::max(a.x, c.x) + 200 * zoom_ < 0 || std::min(a.x, c.x) - 200 * zoom_ > r_.Width())
    {
        return;
    }
    const auto& changes = world_.Changes(key);
    const bool power = world_.Powered(key) && !(b.hackable && changes.lightsOff);
    const std::string meshKey = BuildingMeshKey(b, power);
    if (r_.BeginCachedMesh(meshKey, a, zoom_))
    {
        DrawBuildingMesh(b, power);
        r_.EndCachedMesh();
    }
    if (power)
    {
        // Time-dependent lights remain dynamic; the antenna itself is cached.
        const Point antenna = Project(b.x + std::min(130.f, b.width - 16), b.y + 45, b.height);
        r_.Circle(antenna + Point(0, -25 * zoom_),
                  2,
                  Pink.Alpha(.55f + .4f * std::sin(time_ * 2 + b.style)));
    }
}

void Prototype::DrawBuildingMesh(const Building& b, bool power)
{
    auto project = [&](double x, double y, float height = 0)
    {
        return ProjectMesh(x - b.x, y - b.y, height);
    };
    const Point a = project(b.x, b.y), c = project(b.x + b.width, b.y + b.depth);
    const Point d = project(b.x, b.y + b.depth), e = project(b.x + b.width, b.y);
    const Color accent = power ? Accent(b.style).Emissive(3) : Color(.15f, .22f, .26f);
    const float h = b.height;
    const Point up(0, -h), ar = a + up, cr = c + up, dr = d + up, er = e + up;
    r_.Quad(d, c, cr, dr, Color(.075f, .105f, .165f));
    r_.Quad(e, c, cr, er, Color(.045f, .065f, .12f));
    r_.Quad(ar, er, cr, dr, Color(.12f, .16f, .22f));
    r_.Line(ar, er, 1, Color(.3f, .36f, .43f));
    r_.Line(ar, dr, 1, Color(.26f, .32f, .4f));
    r_.Line(dr, cr, 2, accent.Alpha(.7f));
    r_.Line(er, cr, 2, accent.Alpha(.6f));
    // Individually seeded windows on both visible building faces.
    for (int floor = 18; floor < int(b.height) - 12; floor += 20)
    {
        for (int col = 14; col + 10 < std::min(b.width, b.depth) - 4; col += 23)
        {
            const bool lit = (World::Hash(uint64_t(floor * 181 + col * 19 + b.style)) % 5) != 0;
            const Color window = power && lit
                                     ? accent.Alpha(.4f + .4f * float((floor + col) % 3) / 2)
                                     : Color(.085f, .135f, .19f);
            r_.Quad(project(b.x + col, b.y + b.depth + .1, floor),
                    project(b.x + col + 10, b.y + b.depth + .1, floor),
                    project(b.x + col + 10, b.y + b.depth + .1, floor + 9),
                    project(b.x + col, b.y + b.depth + .1, floor + 9),
                    window);
            r_.Quad(project(b.x + b.width + .1, b.y + col, floor),
                    project(b.x + b.width + .1, b.y + col + 10, floor),
                    project(b.x + b.width + .1, b.y + col + 10, floor + 9),
                    project(b.x + b.width + .1, b.y + col, floor + 9),
                    window.Alpha(window.a * .7f));
        }
    }
    const Point ra = project(b.x + 30, b.y + 32, h + 1), rb = project(b.x + 84, b.y + 32, h + 1),
                rc = project(b.x + 84, b.y + 80, h + 1), rd = project(b.x + 30, b.y + 80, h + 1);
    r_.Quad(ra, rb, rc, rd, Color(.075f, .105f, .145f));
    r_.Line(ra, rb, 2, Muted.Alpha(.4f));
    for (int i = 0; i < 4; ++i)
    {
        r_.Line(ra + Point(-i * 5, 5 + i * 3), rb + Point(-i * 5, 5 + i * 3), 1, Muted.Alpha(.25f));
    }
    const Point antenna = project(b.x + std::min(130.f, b.width - 16), b.y + 45, h);
    r_.Line(antenna, antenna + Point(0, -25), 2, Muted);
    const Point sign = project(b.x + 40, b.y + b.depth + 1, h - 25);
    const char* names[] = {"노바", "사이버", "넥서스", "라멘", "호텔"};
    r_.Rect(sign.x - 6, sign.y - 5, 68, 19, Panel);
    r_.Text(sign.x, sign.y, names[b.style % 5], accent, 1.7f);
    r_.Line(project(b.x + b.width, b.y + b.depth, 9),
            project(b.x + b.width, b.y + b.depth, h - 6),
            2,
            accent.Alpha(.8f));
}

void Prototype::DrawDevice(const Device& device)
{
    Point p = Project(device.position.x, device.position.y);
    if (p.x < -80 || p.x > r_.Width() + 80 || p.y < -80 || p.y > r_.Height() + 80)
    {
        return;
    }
    const bool powered = world_.Powered(device.key), selected = SameDevice(device, target_);
    Color color = powered ? Cyan.Emissive(2.5f) : Amber.Emissive(.8f);
    const auto& change = world_.Changes(device.key);
    if (device.type == DeviceType::Camera)
    {
        color = change.cameraOff ? Muted : powered ? Pink.Emissive(3) : Muted;
        if (scan_ && powered && !change.cameraOff)
        {
            r_.Triangle(p,
                        Project(device.position.x + 125, device.position.y + 95),
                        Project(device.position.x - 25, device.position.y + 140),
                        Pink.Alpha(.055f));
        }
        r_.Line(p, p + Point(0, -37 * zoom_), 3, Muted);
        r_.Rect(p.x - 8 * zoom_, p.y - 44 * zoom_, 17 * zoom_, 9 * zoom_, Color(.2f, .3f, .37f));
        r_.Circle(p + Point(8 * zoom_, -39 * zoom_), 2.5f, color);
    }
    else if (device.type == DeviceType::Power)
    {
        r_.Rect(p.x - 9 * zoom_, p.y - 22 * zoom_, 18 * zoom_, 25 * zoom_, Color(.15f, .23f, .28f));
        r_.Rect(p.x - 6 * zoom_, p.y - 19 * zoom_, 12 * zoom_, 9 * zoom_, color);
        r_.Line(p + Point(-4, -5), p + Point(4, -5), 2, color);
    }
    else
    {
        // Exterior lighting terminal; its indicator is separate from building illumination.
        color = !powered ? Muted : change.lightsOff ? Amber : Cyan;
        r_.Rect(p.x - 8 * zoom_, p.y - 25 * zoom_, 16 * zoom_, 25 * zoom_, Color(.12f, .18f, .23f));
        r_.Rect(p.x - 5 * zoom_, p.y - 21 * zoom_, 10 * zoom_, 11 * zoom_, color);
        r_.Line(p + Point(-3, -5) * zoom_, p + Point(3, -5) * zoom_, 2 * zoom_, color);
    }
    if (scan_ || selected)
    {
        // Selection graphics are markers, not additional luminous surfaces.
        const Color marker = color.Emissive(0);
        r_.Ring(p, selected ? 18 : 11, 1, marker.Alpha(selected ? .95f : .45f));
        if (selected)
        {
            r_.Line(p + Point(0, -50), p + Point(0, -68), 1, marker);
            const char* label = device.type == DeviceType::Power    ? "중계기"
                                : device.type == DeviceType::Camera ? "카메라"
                                                                    : "조명";
            r_.Text(p.x - r_.TextWidth(label, 1.5f) / 2, p.y - 83, label, marker, 1.5f);
        }
    }
}

void Prototype::DrawPlayer(const PlayerActor& actor)
{
    const Point p = Project(actor.WorldPosition().x, actor.WorldPosition().y);
    if (actor.stats.Invulnerability() > 0 && std::fmod(time_, .16f) < .05f)
    {
        return;
    }
    const float step = actor.input.x != 0 || actor.input.y != 0 ? std::sin(time_ * 14) * 3 : 0;
    r_.Circle(p + Point(0, 2), 10, Color(0, 0, 0, .5f));
    r_.Line(p + Point(-4, -2), p + Point(-4 + step, -11), 4, Color(.13f, .2f, .26f));
    r_.Line(p + Point(4, -2), p + Point(4 - step, -11), 4, Color(.13f, .2f, .26f));
    r_.Quad(p + Point(-8, -25),
            p + Point(6, -25),
            p + Point(9, -8),
            p + Point(-8, -8),
            Color(.11f, .17f, .23f));
    r_.Line(p + Point(-7, -23), p + Point(-10, -10 + step), 3, Cyan);
    r_.Line(p + Point(7, -22), p + Point(11, -13 - step), 3, Color(.25f, .37f, .44f));
    r_.Circle(p + Point(0, -30), 6, Color(.16f, .23f, .3f));
    r_.Line(p + Point(-4, -31), p + Point(4, -31), 2, Cyan.Emissive(3));
    r_.Line(p + Point(-3, -20), p + Point(3, -20), 2, Cyan.Emissive(2));
}

void Prototype::DrawSmartphone(const SmartphoneActor& actor)
{
    const auto owner = dynamic_cast<const PlayerActor*>(level_.Scene().Find(actor.Parent()));
    if (owner && owner->stats.Invulnerability() > 0 && std::fmod(time_, .16f) < .05f)
    {
        return;
    }
    const auto position = actor.WorldPosition();
    const Point p = Project(position.x, position.y);
    // The equipped ranged weapon is a smartphone, held beside the character.
    r_.Rect(p.x + 9, p.y - 24, 8, 14, Color(.12f, .16f, .22f));
    r_.Rect(p.x + 10, p.y - 22, 6, 9, Cyan.Emissive(2));
    r_.Circle(p + Point(13, -11), 1, White);
}

void Prototype::Draw()
{
    EnsurePresentationActors();
    r_.Begin(Color(.025f, .042f, .075f));
    auto& scene = level_.Scene();
    depthBuildings_ = std::as_const(scene).Actors<BuildingActor>();
    scene.Render(RenderLayer::Ground, actorRenderer_);
    scene.Render(RenderLayer::GroundEffect, actorRenderer_);
    scene.Render(RenderLayer::World, actorRenderer_);
    scene.Render(RenderLayer::Marker, actorRenderer_);
    r_.BeginOverlay();
    scene.Render(RenderLayer::Overlay, actorRenderer_);
    scene.Render(RenderLayer::Hud, actorRenderer_);
    r_.End();
}

void Prototype::DrawMarkers()
{
    if (scan_)
    {
        // Keep nearby threats discoverable when tall buildings cover their bodies.
        for (const auto& enemy : level_.Enemies())
        {
            if (!level_.Scene().IsActive(enemy.Id()) || !level_.Scene().IsVisible(enemy.Id())
                || enemy.kind == EnemyKind::Boss || enemy.health <= 0
                || std::hypot(enemy.WorldPosition().x - level_.Player().WorldPosition().x,
                              enemy.WorldPosition().y - level_.Player().WorldPosition().y)
                       > 650)
            {
                continue;
            }
            const Point marker = Project(enemy.WorldPosition().x, enemy.WorldPosition().y, 54);
            if (marker.x < 0 || marker.x > r_.Width() || marker.y < 0 || marker.y > r_.Height())
            {
                continue;
            }
            r_.Triangle(marker + Point(-4, -4), marker + Point(4, -4), marker + Point(0, 2), Pink);
        }
    }
    const Point p = Project(level_.Player().WorldPosition().x, level_.Player().WorldPosition().y);
    // An always-visible locator preserves orientation behind tall buildings.
    r_.Ring(p, 13, 1, Cyan.Alpha(.65f));
    r_.Circle(p + Point(0, -48), 2, Cyan);
    if (scan_)
    {
        const float radius = 45 + std::fmod(time_ * 28, 95.f);
        for (int i = 0; i < 48; ++i)
        {
            const double a = i * 6.2831853 / 48, b = (i + 1) * 6.2831853 / 48;
            r_.Line(Project(level_.Player().WorldPosition().x + std::cos(a) * radius,
                            level_.Player().WorldPosition().y + std::sin(a) * radius),
                    Project(level_.Player().WorldPosition().x + std::cos(b) * radius,
                            level_.Player().WorldPosition().y + std::sin(b) * radius),
                    1,
                    Cyan.Alpha((1 - (radius - 45) / 95) * .18f));
        }
    }
    if (target_.valid && (scan_ || hackProgress_ > 0))
    {
        const Point endpoint = Project(target_.position.x, target_.position.y);
        r_.Line(p + Point(0, -18), endpoint, 1.5f, Cyan.Alpha(.45f));
        r_.Ring(endpoint, 20 + std::sin(time_ * 3) * 2, 1, Cyan);
    }
}

void Prototype::Hud()
{
    DrawLevelHud();
}
