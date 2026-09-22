#pragma once
#include <vector>

#include "BodyPart.hpp"
#include "aura/physics/Joint2D.hpp"
#include "aura/physics/JointConstraint.hpp"

namespace aura::body {
    struct AuraBody {
        std::vector<BodyPart> parts;
        std::vector<physics::Joint2D> joints;
    };

    inline BodyPart *findPart(AuraBody &aura, BodyPartType type) noexcept {
        for (BodyPart &part: aura.parts) {
            if (part.type == type) {
                return &part;
            }
        }

        return nullptr;
    }

    inline bool solveJoint(AuraBody &aura, physics::Joint2D &joint) noexcept {
        BodyPart *partA =
                findPart(aura, joint.partA);

        BodyPart *partB =
                findPart(aura, joint.partB);

        if (partA == nullptr || partB == nullptr) {
            return false;
        }

        physics::solveJoint(partA->body, partB->body, joint);

        return true;
    }

    inline void solveAllJoints(AuraBody &aura) noexcept {
        for (physics::Joint2D &joint: aura.joints) {
            solveJoint(aura, joint);
        }
    }
}
