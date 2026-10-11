#pragma once

#include <string>
#include "aura/physics/RigidBody3D.hpp"
#include "aura/physics/CollisionShape.hpp"

namespace aura::body {
    struct BodyPart3D {
        std::string name;
        physics::RigidBody3D body;
        math::Vec3 size{1.0f, 1.0f, 1.0f};
        physics::CollisionShape collisionShape{};
        // Floor support has a separate shape; Box preserves the prior default.
        physics::CollisionShape floorShape{};
    };
}
