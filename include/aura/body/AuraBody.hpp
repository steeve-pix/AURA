#pragma once
#include <vector>
#include <algorithm>

#include "BodyPart.hpp"
#include "aura/physics/Joint2D.hpp"
#include "aura/physics/JointConstraint.hpp"
#include "aura/physics/Physics.hpp"
#include "aura/physics/World2D.hpp"

namespace aura::body {
    struct AuraBody {
        std::vector<BodyPart> parts;
        std::vector<physics::Joint2D> joints;
    };

    struct SupportInterval {
        float minX = 0.0f;
        float maxX = 0.0f;
        bool valid = false;
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

    inline void applyAllJointMotors(AuraBody &aura) noexcept {
        for (physics::Joint2D &joint: aura.joints) {
            BodyPart *partA =
                    findPart(aura, joint.partA);

            BodyPart *partB =
                    findPart(aura, joint.partB);

            if (partA == nullptr || partB == nullptr) {
                continue;
            }

            physics::applyJointMotor(partA->body, partB->body, joint);
        }
    }

    inline void stepAllBodyParts(AuraBody &aura, const physics::World2D &world, float dt) noexcept {
        for (BodyPart &part: aura.parts) {
            physics::stepBody(part.body, world, dt);
        }
    }

    inline bool isLeftFootGrounded(AuraBody &aura, const physics::World2D &world) noexcept {
        BodyPart *foot =
                findPart(aura, BodyPartType::LeftFoot);

        if (foot == nullptr) {
            return false;
        }

        return physics::isGrounded(foot->body, world);
    }

    inline bool isRightFootGrounded(AuraBody &aura, const physics::World2D &world) noexcept {
        BodyPart *foot =
                findPart(aura, BodyPartType::RightFoot);

        if (foot == nullptr) {
            return false;
        }

        return physics::isGrounded(foot->body, world);
    }

    inline math::Vec2 centerOfMass(const AuraBody &aura) noexcept {
        math::Vec2 weightedPosition{};
        float totalMass = 0.0f;

        for (const BodyPart &part: aura.parts) {
            weightedPosition += part.body.position * part.body.mass;

            totalMass += part.body.mass;
        }

        if (totalMass == 0.0f) {
            return {};
        }

        return weightedPosition * (1.0f / totalMass);
    }

    inline SupportInterval supportInterval(AuraBody &aura, const physics::World2D &world) noexcept {
        BodyPart *leftFoot =
                findPart(aura, BodyPartType::LeftFoot);

        BodyPart *rightFoot =
                findPart(aura, BodyPartType::RightFoot);

        SupportInterval support{};

        if (leftFoot != nullptr && physics::isGrounded(leftFoot->body, world)) {
            support.minX =
                    physics::left(leftFoot->body);

            support.maxX =
                    physics::right(leftFoot->body);

            support.valid = true;
        }

        if (rightFoot != nullptr && physics::isGrounded(rightFoot->body, world)) {
            const float rightFootLeft =
                    physics::left(rightFoot->body);

            const float rightFootRight =
                    physics::right(rightFoot->body);

            if (!support.valid) {
                support.minX = rightFootLeft;
                support.maxX = rightFootRight;
                support.valid = true;
            } else {
                support.minX = std::min(support.minX, rightFootLeft);
                support.maxX = std::max(support.maxX, rightFootRight);
            }
        }

        return support;
    }

    inline bool isBalanced(AuraBody &aura, const physics::World2D &world) noexcept {
        const SupportInterval support =
                supportInterval(aura, world);

        if (!support.valid) {
            return false;
        }

        const math::Vec2 com =
                centerOfMass(aura);

        return com.x >= support.minX &&
               com.x <= support.maxX;
    }
}
