#pragma once

#include "Actor.h"

class ClientInstance {
public:
    Actor* getLocalPlayer() {
        return callVirtualFunc<32, Actor*>(this);
    }
};