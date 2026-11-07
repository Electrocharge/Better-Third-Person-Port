#pragma once

#include "EntityContext.h"
#include "ActorHeadRotationComponent.h"
#include "ActorRotationComponent.h"

class Actor {
public:
    CLASS_FIELD(EntityContext, mEntityContext, 0x8);
    CLASS_FIELD(ActorRotationComponent, mActorRotationComponent, 0x218);
public:
    template<typename T>
    T* tryGetComponent() {
        return mEntityContext.enttRegistry.try_get<T>(mEntityContext.entity);
    }

    float& getHeadRotation() {
        return this->tryGetComponent<ActorHeadRotationComponent>()->mYHeadRot;
    }
};