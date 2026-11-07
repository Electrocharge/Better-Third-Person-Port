#pragma once

#include "_HeaderOutputPredefine.h"
#include "IEntityComponent.h"
#include "StateVectorComponent.h"

// Layout is ASSUMED to mirror ActorHeadRotationComponent (two floats). The mod
// verifies this at runtime (see BetterThirdPerson.cpp) and stops touching the
// component if the values do not look like yaw angles.
struct MobBodyRotationComponent : IEntityComponent {
    static constexpr uint32_t type_hash = btpFnv1a32("MobBodyRotationComponent");

    float mYBodyRot;
    float mYBodyRotOld;
};
static_assert(sizeof(MobBodyRotationComponent) == 8);
