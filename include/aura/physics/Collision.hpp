#pragma once
#include "RigidBody3D.hpp"

namespace aura::physics {
    bool intersectFloor(const RigidBody3D &body, float halfHeight, float floorY);

    void resolveFloorCollision(RigidBody3D &body, float halfHeight, float floorY, float dt);
}
