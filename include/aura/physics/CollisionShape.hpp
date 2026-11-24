#pragma once
#include "RigidBody3D.hpp"
#include <algorithm>

namespace aura::physics {
    enum class CollisionShapeType { Sphere, Capsule, Box };

    // Dimensions come from the physical body part. Capsules follow local Y;
    // spheres use half the smallest dimension unless radius overrides it.
    struct CollisionShape {
        CollisionShapeType type = CollisionShapeType::Box;
        float radius = 0.0f;
    };

    inline float collisionShapeRadius(const math::Vec3 &size, CollisionShape shape) {
        return shape.radius > 0.0f ? shape.radius : 0.5f * std::min({size.x, size.y, size.z});
    }

    struct BodyContact {
        math::Vec3 pointA{}, pointB{}, normal{}; // normal points from A toward B
        float penetration = 0;
        bool overlapping = false;
    };

    struct ContactImpulse {
        float magnitude{}, normalSpeedBefore{}, normalSpeedAfter{};
        double energyChange{};
        math::Vec3 momentumChange{};
        bool applied{};
    };

    BodyContact detectBodyContact(const RigidBody3D &a, const math::Vec3 &sizeA, CollisionShape shapeA,
                                  const RigidBody3D &b, const math::Vec3 &sizeB, CollisionShape shapeB);

    // Zero restitution, no friction. One common world contact point, equal/opposite
    // impulses on endpoints only; no projection or descendant velocity copying.
    ContactImpulse resolveBodyContactVelocity(RigidBody3D &a, RigidBody3D &b,
                                              const BodyContact &contact);
}
