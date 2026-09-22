#include "stdafx.h"
#include "SceneGraph.h"
#include <cassert>

namespace
{
    class TraversalGuard
    {
      public:

        explicit TraversalGuard(int& count)
            : count_(count)
        {
            ++count_;
        }

        ~TraversalGuard()
        {
            --count_;
        }

        TraversalGuard(const TraversalGuard&) = delete;
        TraversalGuard& operator=(const TraversalGuard&) = delete;

      private:

        int& count_;
    };
} // namespace

SceneGraph::SceneGraph(SceneGraph&& other) noexcept
{
    *this = std::move(other);
}

SceneGraph& SceneGraph::operator=(SceneGraph&& other) noexcept
{
    assert(!traversals_ && !other.traversals_);
    if (this != &other)
    {
        actors_ = std::move(other.actors_);
        queries_ = std::move(other.queries_);
        nextId_ = other.nextId_;
        Rebind();
    }
    return *this;
}

void SceneGraph::Rebind()
{
    for (auto& entry : actors_)
    {
        entry.second->scene_ = this;
    }
}

Actor* SceneGraph::Find(ActorId id)
{
    const auto found = actors_.find(id);
    return found != actors_.end() && !found->second->pendingDestroy_ ? found->second.get()
                                                                     : nullptr;
}

const Actor* SceneGraph::Find(ActorId id) const
{
    const auto found = actors_.find(id);
    return found != actors_.end() && !found->second->pendingDestroy_ ? found->second.get()
                                                                     : nullptr;
}

std::vector<ActorId> SceneGraph::Children(ActorId parent) const
{
    std::vector<ActorId> result;
    for (const auto& entry : actors_)
    {
        if (entry.second->parent_ == parent && !entry.second->pendingDestroy_)
        {
            result.push_back(entry.first);
        }
    }
    return result;
}

bool SceneGraph::Attach(ActorId child, ActorId parent, bool preserveWorldPosition)
{
    auto actor = Find(child);
    if (!actor || (parent && !Find(parent)))
    {
        return false;
    }
    for (auto ancestor = Find(parent); ancestor; ancestor = Find(ancestor->Parent()))
    {
        if (ancestor->Id() == child)
        {
            return false;
        }
    }
    const auto position = actor->WorldPosition();
    actor->parent_ = parent;
    if (preserveWorldPosition)
    {
        actor->SetWorldPosition(position);
    }
    return true;
}

void SceneGraph::Destroy(ActorId id)
{
    auto actor = Find(id);
    if (!actor)
    {
        return;
    }
    const auto children = Children(id);
    for (auto child : children)
    {
        Destroy(child);
    }
    actor->pendingDestroy_ = true;
}

void SceneGraph::FlushDestroyed()
{
    if (traversals_)
    {
        return;
    }
    bool removed = false;
    for (auto it = actors_.begin(); it != actors_.end();)
    {
        if (it->second->pendingDestroy_)
        {
            it = actors_.erase(it);
            removed = true;
        }
        else
        {
            ++it;
        }
    }
    if (removed)
    {
        for (auto& entry : queries_)
        {
            auto& ids = entry.second.ids;
            ids.erase(std::remove_if(ids.begin(),
                                     ids.end(),
                                     [this](ActorId id)
                                     {
                                         return Find(id) == nullptr;
                                     }),
                      ids.end());
        }
    }
}

const std::vector<ActorId>& SceneGraph::Query(std::type_index type,
                                              std::function<bool(const Actor*)> matches) const
{
    const auto existing = queries_.find(type);
    if (existing != queries_.end())
    {
        return existing->second.ids;
    }
    CachedQuery query{std::move(matches), {}};
    for (const auto& entry : actors_)
    {
        if (!entry.second->pendingDestroy_ && query.matches(entry.second.get()))
        {
            query.ids.push_back(entry.first);
        }
    }
    return queries_.emplace(type, std::move(query)).first->second.ids;
}

void SceneGraph::Clear()
{
    for (auto& entry : actors_)
    {
        entry.second->pendingDestroy_ = true;
    }
    FlushDestroyed();
}

bool SceneGraph::IsActive(ActorId id) const
{
    for (auto actor = Find(id); actor; actor = Find(actor->Parent()))
    {
        if (!actor->active)
        {
            return false;
        }
    }
    return Find(id) != nullptr;
}

bool SceneGraph::IsVisible(ActorId id) const
{
    for (auto actor = Find(id); actor; actor = Find(actor->Parent()))
    {
        if (!actor->visible)
        {
            return false;
        }
    }
    return Find(id) != nullptr;
}

void SceneGraph::Update(UpdatePhase phase, float dt, ActorUpdateContext& context)
{
    std::vector<ActorId> order;
    for (const auto& entry : actors_)
    {
        if (entry.second->updatePhase == phase)
        {
            order.push_back(entry.first);
        }
    }
    const TraversalGuard guard(traversals_);
    for (auto id : order)
    {
        if (IsActive(id))
        {
            Find(id)->Update(dt, context);
        }
    }
}

void SceneGraph::Render(RenderLayer layer, ActorRenderContext& context)
{
    struct Entry
    {
        ActorId id;
        double depth;
    };

    std::vector<Entry> order;
    for (const auto& entry : actors_)
    {
        if (entry.second->renderLayer == layer && IsActive(entry.first) && IsVisible(entry.first))
        {
            order.push_back({entry.first,
                             context.depth ? context.depth(*entry.second) : entry.second->Depth()});
        }
    }
    std::stable_sort(order.begin(),
                     order.end(),
                     [](const Entry& a, const Entry& b)
                     {
                         return a.depth < b.depth;
                     });
    const TraversalGuard guard(traversals_);
    for (const auto& entry : order)
    {
        if (IsActive(entry.id) && IsVisible(entry.id))
        {
            Find(entry.id)->Render(context);
        }
    }
}
