#include "stdafx.h"
#include "ModelActor.h"
#include <algorithm>
#include <utility>

ModelActor::ModelActor(Kind type, std::string name, int variant)
    : kind(type),
      asset(std::move(name)),
      index(variant)
{
    updatePhase = UpdatePhase::Decoration;
}

void ModelActor::Update(float dt, ActorUpdateContext&)
{
    animationTime += std::max(0.f, dt);
}

void ModelActor::Render(ActorRenderContext& context) const
{
    context.Draw(*this);
}
