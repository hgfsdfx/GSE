#pragma once
#include "Actor.h"
#include "ModelMath.h"
#include <string>

// Reusable catalog-backed decoration. Gameplay/collision remains the owning level's policy.
class ModelActor : public Actor
{
  public:

    enum class Kind
    {
        Environment,
        Npc,
        Robot
    };
    ModelActor(Kind kind, std::string asset, int index = 1);
    void Update(float dt, ActorUpdateContext& context) override;
    void Render(ActorRenderContext& context) const override;
    Kind kind;
    std::string asset;
    int index;
    ModelMath::Vec3 scale{24, 24, 24};
    float yaw = 0, animationTime = 0, emissionScale = 1;
    std::string clip = "Idle";
    bool loop = true;
};
