#pragma once
#include "Body2D.hpp"
#include "BodyGeometry.hpp"
#include "Impulses.hpp"
#include "World2D.hpp"
#include "aura/math/Math.hpp"

namespace aura::physics {
    inline bool intersectsFloor(const Body2D &body, const World2D &world) noexcept {
        return bottom(body) < world.floorHeight;
    }

    inline void correctFloorPenetration(Body2D &body, const World2D &world) noexcept {
        const float penetration =
                world.floorHeight - bottom(body);

        if (penetration <= 0.0f) {
            return;
        }

        body.position.y +=
                penetration;
    }

    inline void stopDownwardVelocity(Body2D &body) noexcept {
        if (body.velocity.y < 0.0f)
            body.velocity.y = 0.0f;
    }

    inline void stopAngularVelocity(Body2D &body) noexcept {
        body.angularVelocity = 0.0f;
    }

    inline void resolveFloorCollision(Body2D &body, const World2D &world) noexcept {
        if (!intersectsFloor(body, world)) {
            return;
        }

        correctFloorPenetration(body, world);
        stopDownwardVelocity(body);
        stopAngularVelocity(body);
    }

    inline void applyFloorFriction(Body2D &body, const World2D &world) noexcept {
        if (!isGrounded(body, world)) {
            return;
        }

        const math::Vec2 contactPoint = lowestPoint(body);
        const math::Vec2 contactVelocity = velocityAtWorldPoint(body, contactPoint);
        const math::Vec2 tangent{1.0f, 0.0f};
        const float tangentVelocity = contactVelocity.dot(tangent);

        const math::Vec2 offset = contactPoint - body.position;
        const float inverseMass = 1.0f / body.mass;
        const float rotationalTerm = math::cross(offset, tangent);
        const float effectiveInverseMass =
                inverseMass + (rotationalTerm * rotationalTerm) / body.momentOfInertia;

        if (effectiveInverseMass <= 0.0f) {
            return;
        }

        const float impulseMagnitude = -tangentVelocity / effectiveInverseMass;
        const math::Vec2 impulse = tangent * impulseMagnitude;
        applyImpulseAtPoint(body, impulse, contactPoint);
    }
}
