#include "stdafx.h"
#include "GameplayActors.h"
#include "SceneGraph.h"
#include "NavigationGrid.h"
#include "GameMath.h"
#include <algorithm>
#include <cmath>
using GameMath::Direction;
using GameMath::Distance;

PulseActor::PulseActor(float lifetime, float size)
    : remaining(lifetime),
      radius(size)
{

    updatePhase = UpdatePhase::Pulse;
    renderLayer = RenderLayer::GroundEffect;
}

void PulseActor::Update(float dt, ActorUpdateContext& context)
{
    remaining -= dt;
    if (remaining <= 0)
    {
        if (Distance(context.player.WorldPosition(), WorldPosition()) < radius)
        {
            context.events.Hurt(24, context.player.WorldPosition());
        }
        Destroy();
    }
}

CombatNumberActor::CombatNumberActor(int number, bool heal)
    : value(number),
      healing(heal)
{

    updatePhase = UpdatePhase::Effect;
    renderLayer = RenderLayer::Overlay;
}

void CombatNumberActor::Update(float dt, ActorUpdateContext&)
{
    remaining -= dt;
    if (remaining <= 0)
    {
        Destroy();
    }
}
