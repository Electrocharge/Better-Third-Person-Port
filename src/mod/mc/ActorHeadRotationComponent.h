#pragma once

#include "_HeaderOutputPredefine.h"
#include "IEntityComponent.h"

struct ActorHeadRotationComponent : IEntityComponent {
    static constexpr uint32_t type_hash = 0xBABE7211;

    float mYHeadRot;
    float mYHeadRotOld;
};