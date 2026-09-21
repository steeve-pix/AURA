#pragma once
#include "Body2D.hpp"
#include "Joint2D.hpp"
#include "JointGeometry.hpp"

namespace aura::physics {
    inline void correctJointPosition(Body2D &bodyA, Body2D &bodyB, const Joint2D &joint) noexcept {
        const math::Vec2 error =
                jointError(bodyA, bodyB, joint);

        const float inverseMassA =
                1 / bodyA.mass;

        const float inverseMassB =
                1 / bodyB.mass;

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
}
