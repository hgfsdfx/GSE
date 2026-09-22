#pragma once
#include "WorldTypes.h"
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>

class SceneGraph;
class ActorRenderContext;
struct ActorUpdateContext;
using ActorId = uint64_t;

enum class UpdatePhase
{
    Player,
    Weapon,
    Enemy,
    Pulse,
    Projectile,
    Loot,
    Effect,
    Decoration
};

enum class RenderLayer
{
    Ground,
    GroundEffect,
    World,
    Marker,
    Overlay,
    Hud
};

// Position is local to the parent. Destruction and hierarchy are owned by SceneGraph.
class Actor
{
  public:

    Actor() = default;
    virtual ~Actor() = default;
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;
    ActorId Id() const;
    ActorId Parent() const;
    WorldPoint LocalPosition() const;
    WorldPoint WorldPosition() const;
    void SetLocalPosition(WorldPoint point);
    void SetWorldPosition(WorldPoint point);
    void Destroy();
    bool PendingDestroy() const;
    virtual void Update(float dt, ActorUpdateContext& context);
    virtual void Render(ActorRenderContext& context) const;
    virtual double Depth() const;
    bool active = true;
    bool visible = true;
    UpdatePhase updatePhase = UpdatePhase::Decoration;
    RenderLayer renderLayer = RenderLayer::World;

  private:

    friend class SceneGraph;
    ActorId id_ = 0;
    ActorId parent_ = 0;
    WorldPoint position_;
    SceneGraph* scene_ = nullptr;
    bool pendingDestroy_ = false;
};

// Type-specific drawing is registered by the presentation layer, not by gameplay.
class ActorRenderContext
{
  public:

    template <class T, class F> void Register(F&& draw)
    {
        draws_[std::type_index(typeid(T))] = [callback = std::forward<F>(draw)](const Actor& actor)
        {
            callback(static_cast<const T&>(actor));
        };
    }

    void Draw(const Actor& actor) const;
    std::function<double(const Actor&)> depth;

  private:

    std::unordered_map<std::type_index, std::function<void(const Actor&)>> draws_;
};

class GroupActor : public Actor
{
  public:

    GroupActor();
};

class PresentationRootActor : public GroupActor
{
};

// One visual actor may compose multiple primitives (HUD, scanner, city ground decoration).
class PresentationActor : public Actor
{
  public:

    PresentationActor(RenderLayer layer, std::function<void()> draw);
    void Render(ActorRenderContext& context) const override;

  private:

    std::function<void()> draw_;
};
