#pragma once
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include <array>

namespace aura::test {
    // Historical comparison only. Runtime contacts must not copy velocity changes.
    inline void resolveRightShinFloorWithLegacyFootMotion(body::AuraBody3D &body, float dt) {
        const auto oldVelocity = body.rightShin.body.velocity;
        const auto oldOmega = body.rightShin.body.angularVelocity;
        body::resolveRightShinFloorWithFootTranslation(body, dt);
        const auto deltaV = body.rightShin.body.velocity - oldVelocity;
        const auto deltaOmega = body.rightShin.body.angularVelocity - oldOmega;
        const std::array descendants{&body.rightFoot};
        body::translateSubtreeVelocity(descendants, deltaV);
        body::rotateSubtreeVelocityAroundWorldPoint(descendants, body.rightShin.body.position, deltaOmega);
    }
}
