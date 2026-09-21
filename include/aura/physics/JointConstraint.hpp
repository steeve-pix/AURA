#pragma once
#include "Body2D.hpp"
#include "Joint2D.hpp"
#include "JointGeometry.hpp"

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

    inline void correctJointVelocity(Body2D &bodyA, Body2D &bodyB) noexcept {
        const math::Vec2 relativeVelocity =
                jointRelativeVelocity(bodyA, bodyB);

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

        bodyA.velocity +=
                relativeVelocity * weightA;

        bodyB.velocity -=
                relativeVelocity * weightB;
    }

    inline void solveJoint(Body2D &bodyA, Body2D &bodyB, const Joint2D &joint) noexcept {
        correctJointPosition(bodyA, bodyB, joint);
        correctJointVelocity(bodyA, bodyB);
    }
}
