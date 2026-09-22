#pragma once
#include "Actor.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

// A snapshot view contains stable actor pointers; it must not outlive FlushDestroyed/Clear.
template <class T> class ActorView
{
  public:

    class Iterator
    {
      public:

        using iterator_category = std::forward_iterator_tag;
        using value_type = std::remove_const_t<T>;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;
        Iterator() = default;

        explicit Iterator(typename std::vector<T*>::const_iterator current)
            : current_(current)
        {
        }

        reference operator*() const
        {
            return **current_;
        }

        pointer operator->() const
        {
            return *current_;
        }

        Iterator& operator++()
        {
            ++current_;
            return *this;
        }

        Iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }

        bool operator==(const Iterator& other) const
        {
            return current_ == other.current_;
        }

        bool operator!=(const Iterator& other) const
        {
            return !(*this == other);
        }

      private:

        typename std::vector<T*>::const_iterator current_;
    };

    Iterator begin() const
    {
        return Iterator(actors_.begin());
    }

    Iterator end() const
    {
        return Iterator(actors_.end());
    }

    size_t size() const
    {
        return actors_.size();
    }

    bool empty() const
    {
        return actors_.empty();
    }

    T& operator[](size_t index) const
    {
        return *actors_[index];
    }

  private:

    friend class SceneGraph;
    std::vector<T*> actors_;
};

class SceneGraph
{
  public:

    SceneGraph() = default;
    SceneGraph(const SceneGraph&) = delete;
    SceneGraph& operator=(const SceneGraph&) = delete;
    SceneGraph(SceneGraph&& other) noexcept;
    SceneGraph& operator=(SceneGraph&& other) noexcept;

    template <class T, class... Args> T& Spawn(ActorId parent, Args&&... args)
    {
        static_assert(std::is_base_of_v<Actor, T>);
        if (parent && !Find(parent))
        {
            throw std::invalid_argument("Invalid actor parent");
        }
        auto actor = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *actor;
        actor->id_ = nextId_++;
        actor->parent_ = parent;
        actor->scene_ = this;
        const ActorId id = actor->id_;
        actors_.emplace(id, std::move(actor));
        for (auto& query : queries_)
        {
            if (query.second.matches(&result))
            {
                query.second.ids.push_back(id);
            }
        }
        return result;
    }

    template <class T, class... Args>
    T& SpawnAt(ActorId parent, WorldPoint position, Args&&... args)
    {
        auto& actor = Spawn<T>(parent, std::forward<Args>(args)...);
        actor.SetWorldPosition(position);
        return actor;
    }

    Actor* Find(ActorId id);
    const Actor* Find(ActorId id) const;

    template <class T> ActorView<T> Actors()
    {
        ActorView<T> result;
        const auto& ids = Query(typeid(T),
                                [](const Actor* actor)
                                {
                                    return dynamic_cast<const T*>(actor) != nullptr;
                                });
        for (auto id : ids)
        {
            if (auto actor = dynamic_cast<T*>(Find(id)))
            {
                result.actors_.push_back(actor);
            }
        }
        return result;
    }

    template <class T> ActorView<const T> Actors() const
    {
        ActorView<const T> result;
        const auto& ids = Query(typeid(T),
                                [](const Actor* actor)
                                {
                                    return dynamic_cast<const T*>(actor) != nullptr;
                                });
        for (auto id : ids)
        {
            if (auto actor = dynamic_cast<const T*>(Find(id)))
            {
                result.actors_.push_back(actor);
            }
        }
        return result;
    }

    bool Attach(ActorId child, ActorId parent, bool preserveWorldPosition = true);
    std::vector<ActorId> Children(ActorId parent) const;
    void Destroy(ActorId id);
    void FlushDestroyed();
    void Clear();
    void Update(UpdatePhase phase, float dt, ActorUpdateContext& context);
    void Render(RenderLayer layer, ActorRenderContext& context);
    bool IsActive(ActorId id) const;
    bool IsVisible(ActorId id) const;

  private:

    void Rebind();

    struct CachedQuery
    {
        std::function<bool(const Actor*)> matches;
        std::vector<ActorId> ids;
    };

    const std::vector<ActorId>& Query(std::type_index type,
                                      std::function<bool(const Actor*)> matches) const;
    mutable std::map<std::type_index, CachedQuery> queries_;
    std::map<ActorId, std::unique_ptr<Actor>> actors_;
    ActorId nextId_ = 1;
    int traversals_ = 0;
};
