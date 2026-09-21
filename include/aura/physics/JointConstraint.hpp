#pragma once
#include "Body2D.hpp"
#include "Impulses.hpp"
#include "Joint2D.hpp"
#include "JointGeometry.hpp"
#include "aura/math/Math.hpp"

namespace aura::physics {
    inline void correctJointPosition(Body2D &bodyA, Body2D &bodyB, const Joint2D &joint) noexcept {
        const math::Vec2 error =
                jointError(bodyA, bodyB, joint);

        const float inverseMassA =
                1.0f / bodyA.mass;

        const float inverseMassB =
                1.0f / bodyB.mass;

        const float totalInverseMass =
                inverseMassA + inverseMassB;

        const float weightA =
                inverseMassA / totalInverseMass;

        const float weightB =
                inverseMassB / totalInverseMass;

        bodyA.position +=
                error * weightA;

        bodyB.position -=
                error * weightB;
    }

    inline void correctJointVelocity(Body2D &bodyA, Body2D &bodyB, const Joint2D &joint) noexcept {
        const math::Vec2 anchorA =
                worldAnchorA(bodyA, joint);

        const math::Vec2 anchorB =
                worldAnchorB(bodyB, joint);

        const math::Vec2 relativeVelocity =
                jointRelativeVelocity(bodyA, bodyB, joint);

        if (relativeVelocity.lengthSquared() == 0.0f) {
            return;
        }

        const math::Vec2 direction =
                relativeVelocity.normalized();

        const math::Vec2 offsetA =
                anchorA - bodyA.position;

        const math::Vec2 offsetB =
                anchorB - bodyB.position;

        const float inverseMassA =
                1.0f / bodyA.mass;

        const float inverseMassB =
                1.0f / bodyB.mass;

        const float rotationalA =
                math::cross(offsetA, direction);

        const float rotationalB =
                math::cross(offsetB, direction);

        // This simplified solver does not model the full rotational effective
        // mass. Relax the impulse when anchor rotation contributes to avoid
        // overshooting the constraint.
        const float effectiveInverseMass =
                inverseMassA + inverseMassB +
                (rotationalA * rotationalA) / bodyA.momentOfInertia
                + (rotationalB * rotationalB) / bodyB.momentOfInertia;

        const float relativeSpeed =
                relativeVelocity.dot(direction);

        const float impulseMagnitude =
                -relativeSpeed / effectiveInverseMass;

        const math::Vec2 impulse =
                direction * impulseMagnitude;

        applyImpulseAtPoint(bodyA, impulse * -1.0f, anchorA);
        applyImpulseAtPoint(bodyB, impulse, anchorB);
    }

    inline void solveJoint(Body2D &bodyA, Body2D &bodyB, const Joint2D &joint) noexcept {
        correctJointPosition(bodyA, bodyB, joint);
        correctJointVelocity(bodyA, bodyB, joint);
    }
}
