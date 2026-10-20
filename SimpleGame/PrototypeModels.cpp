#include "stdafx.h"
#include "Prototype.h"
#include <algorithm>
#include <cmath>

void Prototype::UpdateModelVisuals(float dt)
{
    ++modelVisualFrame_;
    const auto update = [&](const Actor& actor)
    {
        const auto position = actor.WorldPosition();
        auto inserted = modelVisuals_.try_emplace(actor.Id());
        auto& visual = inserted.first->second;
        visual.lastSeen = modelVisualFrame_;
        if (inserted.second)
        {
            visual.position = position;
            visual.clock = float(actor.Id() % 30) / 30.f;
        }
        const double dx = position.x - visual.position.x, dy = position.y - visual.position.y;
        const float distance = float(std::hypot(dx, dy));
        visual.speed = dt > 0 && distance < 50 ? distance / dt : 0;
        if (visual.speed > 2)
        {
            visual.yaw = float(std::atan2(dx, -dy));
        }
        const std::string clip = visual.speed > 180 ? "Run" : visual.speed > 2 ? "Walk" : "Idle";
        if (clip != visual.clip)
        {
            visual.clip = clip;
            visual.clock = 0;
        }
        const float rate =
            clip == "Idle" ? 1.f
                           : std::clamp(visual.speed / (clip == "Run" ? 180.f : 80.f), .4f, 2.f);
        visual.clock += dt * rate;
        visual.position = position;
    };
    update(level_.Player());
    for (const auto& npc : level_.Scene().Actors<NpcActor>())
    {
        update(npc);
    }
    for (const auto& enemy : level_.Scene().Actors<EnemyActor>())
    {
        update(enemy);
    }
    for (auto it = modelVisuals_.begin(); it != modelVisuals_.end();)
    {
        if (it->second.lastSeen != modelVisualFrame_)
        {
            it = modelVisuals_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool Prototype::DrawActorModel(const Actor& actor,
                               const ModelAppearance* appearance,
                               float scale,
                               bool flash)
{
    if (!appearance)
    {
        return false;
    }
    const auto position = actor.WorldPosition();
    const auto found = modelVisuals_.find(actor.Id());
    const ModelVisual fallback{position};
    const auto& visual = found == modelVisuals_.end() ? fallback : found->second;
    const float size = scale * zoom_;
    return r_.DrawModel(*appearance,
                        Project(position.x, position.y),
                        {size, size, size},
                        visual.yaw,
                        visual.clip,
                        visual.clock,
                        true,
                        1,
                        flash ? Color(1.8f, 1.8f, 1.8f) : Color{});
}

bool Prototype::DrawBuildingModel(const Building& building, bool illuminated)
{
    const auto* model = assets_.Environment("Building", int(building.style % 4) + 1);
    if (!model)
    {
        return false;
    }
    const auto& low = model->minimum;
    const auto& high = model->maximum;
    if (high[0] - low[0] < .001f || high[1] - low[1] < .001f || high[2] - low[2] < .001f)
    {
        return false;
    }
    const float sx = building.width / (high[0] - low[0]);
    const float sy = building.height / (high[1] - low[1]);
    const float sz = building.depth / (high[2] - low[2]);
    // Positive game Y is negative GLB Z. Fit the full exported bounds to the
    // existing gameplay footprint rather than changing collisions/navigation.
    const double x = building.x - low[0] * sx;
    const double y = building.y + high[2] * sz;
    return r_.DrawModel(*model,
                        Project(x, y, -low[1] * sy),
                        {sx * zoom_, sy * zoom_, sz * zoom_},
                        0,
                        "",
                        0,
                        true,
                        illuminated ? 1.f : 0.f);
}

void Prototype::DrawModelActor(const ModelActor& actor)
{
    const ModelAppearance* model = nullptr;
    switch (actor.kind)
    {
        case ModelActor::Kind::Environment:
            model = assets_.Environment(actor.asset, actor.index);
            break;
        case ModelActor::Kind::Npc:
            model = assets_.Npc(actor.asset);
            break;
        case ModelActor::Kind::Robot:
            model = assets_.Robot(actor.asset);
            break;
    }
    if (!model)
    {
        return;
    }
    const auto position = actor.WorldPosition();
    const auto origin = Project(position.x, position.y);
    float radius = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        radius += std::max(std::abs(model->minimum[axis]), std::abs(model->maximum[axis]))
                  * actor.scale[axis] * zoom_ * 2;
    }
    if (origin.x < -radius || origin.x > r_.Width() + radius || origin.y < -radius
        || origin.y > r_.Height() + radius)
    {
        return;
    }
    auto scale = actor.scale;
    for (float& component : scale)
    {
        component *= zoom_;
    }
    r_.DrawModel(*model,
                 origin,
                 scale,
                 actor.yaw,
                 actor.clip,
                 actor.animationTime,
                 actor.loop,
                 actor.emissionScale);
}
