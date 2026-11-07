#pragma once

#include "_HeaderOutputPredefine.h"
#include "IEntityComponent.h"

// entt type id used by Bedrock = FNV-1a (32-bit) of the component name.
constexpr uint32_t btpFnv1a32(const char *s) {
    uint32_t h = 0x811c9dc5u;
    while (*s) { h = (h ^ static_cast<uint8_t>(*s++)) * 0x01000193u; }
    return h;
}

// Layout confirmed from game code (BoatMoveControlSystem): posDelta lives at +0x18.
struct StateVectorComponent : IEntityComponent {
    static constexpr uint32_t type_hash = btpFnv1a32("StateVectorComponent");

    float mPos[3];
    float mPosPrev[3];
    float mPosDelta[3];
};
static_assert(sizeof(StateVectorComponent) == 0x24);
