#pragma once

#include "_HeaderOutputPredefine.h"
#include "IEntityComponent.h"

// entt type id used by Bedrock = FNV-1a (32-bit) of the component name.
constexpr uint32_t btpFnv1a32(const char *s) {
    uint32_t h = 0x811c9dc5u;
    while (*s) { h = (h ^ static_cast<uint8_t>(*s++)) * 0x01000193u; }
    return h;
}

// Layout is ASSUMED to mirror ActorHeadRotationComponent (two floats). The mod verifies
// this at runtime and stops touching the component if the values do not look like yaw angles.
struct MobBodyRotationComponent : IEntityComponent {
    static constexpr uint32_t type_hash = btpFnv1a32("MobBodyRotationComponent");

    float mYBodyRot;
    float mYBodyRotOld;
};
static_assert(sizeof(MobBodyRotationComponent) == 8);
