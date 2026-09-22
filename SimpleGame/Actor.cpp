#include "stdafx.h"
#include "Actor.h"
#include "SceneGraph.h"

ActorId Actor::Id() const
{
    return id_;
}

ActorId Actor::Parent() const
{
    return parent_;
}

WorldPoint Actor::LocalPosition() const
{
    return position_;
}

WorldPoint Actor::WorldPosition() const
{
    WorldPoint result = position_;
    if (scene_)
    {
        for (auto parent = scene_->Find(parent_); parent; parent = scene_->Find(parent->Parent()))
        {
            const auto point = parent->LocalPosition();
            result.x += point.x;
            result.y += point.y;
        }
    }
    return result;
}

void Actor::SetLocalPosition(WorldPoint point)
{
    position_ = point;
}

void Actor::SetWorldPosition(WorldPoint point)
{
    if (scene_)
    {
        if (const auto parent = scene_->Find(parent_))
        {
            const auto origin = parent->WorldPosition();
            point.x -= origin.x;
            point.y -= origin.y;
        }
    }
    position_ = point;
}

void Actor::Destroy()
{
    if (scene_)
    {
        scene_->Destroy(id_);
    }
}

bool Actor::PendingDestroy() const
{
    return pendingDestroy_;
}

void Actor::Update(float, ActorUpdateContext&)
{
}

void Actor::Render(ActorRenderContext& context) const
{
    context.Draw(*this);
}

double Actor::Depth() const
{
    const auto p = WorldPosition();
    return p.x + p.y;
}

void ActorRenderContext::Draw(const Actor& actor) const
{
    const auto found = draws_.find(std::type_index(typeid(actor)));
    if (found != draws_.end())
    {
        found->second(actor);
    }
}

GroupActor::GroupActor() = default;

PresentationActor::PresentationActor(RenderLayer layer, std::function<void()> draw)
    : draw_(std::move(draw))
{
    renderLayer = layer;
}

void PresentationActor::Render(ActorRenderContext&) const
{
    if (draw_)
    {
        draw_();
    }
}
