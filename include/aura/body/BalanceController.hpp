#pragma once
#include "AuraBody.hpp"

namespace aura::body {
    inline void applyBalanceController(AuraBody &aura, const physics::World2D &world, float gain) {
        const SupportInterval support =
                supportInterval(aura, world);

        if (!support.valid) {
            return;
        }

        const float error =
                normalizedBalanceErrorX(aura, world);

        const float correction =
                error * gain;

        for (physics::Joint2D &joint: aura.joints) {
            const bool isLeftHip =
                    joint.partA == BodyPartType::Torso
                    && joint.partB == BodyPartType::LeftThigh;

            const bool isRightHip =
                    joint.partA == BodyPartType::Torso
                    && joint.partB == BodyPartType::RightThigh;

            if (isLeftHip || isRightHip) {
                joint.targetAngle = correction;
            }
        }
    }
}
