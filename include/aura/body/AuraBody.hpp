#pragma once
#include <iomanip>
#include <iostream>
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

    struct FootContactState {
        bool leftGrounded = false;
        bool rightGrounded = false;
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

    inline bool updateGrounded(bool wasGrounded, float bodyBottom, float floorHeight) noexcept {
        const float distance = bodyBottom - floorHeight;
        return wasGrounded ? distance <= 0.003f : distance <= 0.001f;
    }

    inline void updateFootContactState(AuraBody &aura, const physics::World2D &world,
                                       FootContactState &contactState) noexcept {
        BodyPart *leftFoot = findPart(aura, BodyPartType::LeftFoot);
        BodyPart *rightFoot = findPart(aura, BodyPartType::RightFoot);

        contactState.leftGrounded = leftFoot != nullptr && updateGrounded(
                contactState.leftGrounded, physics::bottom(leftFoot->body), world.floorHeight);
        contactState.rightGrounded = rightFoot != nullptr && updateGrounded(
                contactState.rightGrounded, physics::bottom(rightFoot->body), world.floorHeight);
    }

    inline bool solveJoint(AuraBody &aura, physics::Joint2D &joint) noexcept {
        BodyPart *partA =
                findPart(aura, joint.partA);

        BodyPart *partB =
                findPart(aura, joint.partB);

        if (partA == nullptr || partB == nullptr) {
            return false;
        }

        static int velocitySolveCalls = 0;
        static bool foundIncrease = false;
        static bool printedHeader = false;
        static bool printedNoIncrease = false;

        const int jointCount = std::max(1, static_cast<int>(aura.joints.size()));
        const int callsPerFrame = jointCount * 8;
        const int frame = velocitySolveCalls / callsPerFrame + 1;
        const int iteration = (velocitySolveCalls / jointCount) % 8 + 1;
        ++velocitySolveCalls;

        if (frame <= 5) {
            const auto kineticEnergy = [](const physics::Body2D &body) noexcept {
                const float linear =
                        0.5f * body.mass * body.velocity.lengthSquared();
                const float angular =
                        0.5f * body.momentOfInertia * body.angularVelocity * body.angularVelocity;
                return linear + angular;
            };

            physics::correctJointPosition(partA->body, partB->body, joint);

            const float beforeEnergy =
                    kineticEnergy(partA->body) + kineticEnergy(partB->body);
            physics::correctJointVelocity(partA->body, partB->body, joint);
            const float afterEnergy =
                    kineticEnergy(partA->body) + kineticEnergy(partB->body);
            const float delta = afterEnergy - beforeEnergy;

            if (delta > 0.000001f) {
                if (!printedHeader) {
                    std::cerr << "Joint velocity corrections that increase pair KE (frames 1-5)\n"
                              << std::left << std::setw(9) << "frame"
                              << std::setw(12) << "iteration"
                              << std::setw(34) << "joint"
                              << std::right << std::setw(16) << "delta KE" << '\n';
                    printedHeader = true;
                }

                std::cerr << std::setprecision(9)
                          << std::left << std::setw(9) << frame
                          << std::setw(12) << iteration
                          << std::setw(34) << (partA->name + " -> " + partB->name)
                          << std::right << std::setw(16) << delta << '\n';
                foundIncrease = true;
            }

            physics::correctJointAngle(partA->body, partB->body, joint);
            physics::correctJointAngularVelocity(partA->body, partB->body, joint);
        } else {
            if (!foundIncrease && !printedNoIncrease) {
                // std::cout << "No joint KE increase above 1e-6 in frames 1-5\n";
                printedNoIncrease = true;
            }

            physics::solveJoint(partA->body, partB->body, joint);
        }

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

    inline SupportInterval supportInterval(AuraBody &aura, const physics::World2D &world,
                                           bool leftGrounded, bool rightGrounded) noexcept {
        BodyPart *leftFoot =
                findPart(aura, BodyPartType::LeftFoot);

        BodyPart *rightFoot =
                findPart(aura, BodyPartType::RightFoot);

        SupportInterval support{};

        if (leftFoot != nullptr && leftGrounded) {
            support.minX =
                    physics::left(leftFoot->body);

            support.maxX =
                    physics::right(leftFoot->body);

            support.valid = true;
        }

        if (rightFoot != nullptr && rightGrounded) {
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

    inline SupportInterval supportInterval(AuraBody &aura, const physics::World2D &world) noexcept {
        BodyPart *leftFoot = findPart(aura, BodyPartType::LeftFoot);
        BodyPart *rightFoot = findPart(aura, BodyPartType::RightFoot);
        return supportInterval(aura, world,
                leftFoot != nullptr && physics::isGrounded(leftFoot->body, world),
                rightFoot != nullptr && physics::isGrounded(rightFoot->body, world));
    }

    inline SupportInterval supportInterval(AuraBody &aura, const physics::World2D &world,
                                           const FootContactState &contactState) noexcept {
        return supportInterval(aura, world, contactState.leftGrounded, contactState.rightGrounded);
    }

    inline float balanceErrorX(AuraBody &aura, const physics::World2D &world) noexcept {
        const SupportInterval support =
                supportInterval(aura, world);

        if (!support.valid) {
            return 0.0f;
        }
        const math::Vec2 com =
                centerOfMass(aura);

        const float supportCenterX =
                (support.minX + support.maxX) / 2.0f;


        return com.x - supportCenterX;
    }

    inline float normalizedBalanceErrorX(AuraBody &aura, const physics::World2D &world) noexcept {
        const SupportInterval support =
                supportInterval(aura, world);

        if (!support.valid) {
            return 0.0f;
        }

        const float halfWidth =
                (support.maxX - support.minX) * 0.5f;

        if (halfWidth <= 0.0f) {
            return 0.0f;
        }

        return balanceErrorX(aura, world) / halfWidth;
    }

    inline float normalizedBalanceErrorX(AuraBody &aura, const physics::World2D &world,
                                         const FootContactState &contactState) noexcept {
        const SupportInterval support = supportInterval(aura, world, contactState);
        if (!support.valid) {
            return 0.0f;
        }

        const float halfWidth = (support.maxX - support.minX) * 0.5f;
        if (halfWidth <= 0.0f) {
            return 0.0f;
        }

        const float supportCenterX = (support.minX + support.maxX) * 0.5f;
        return (centerOfMass(aura).x - supportCenterX) / halfWidth;
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

    inline void resolveAllFloorCollisions(AuraBody &aura, const physics::World2D &world) noexcept {
        for (BodyPart &part: aura.parts) {
            physics::resolveFloorCollision(part.body, world);
        }
    }


    inline void solveBodyConstraints(AuraBody &aura, const physics::World2D &world, int iterations = 8) noexcept {
        BodyPart *leftFoot = findPart(aura, BodyPartType::LeftFoot);
        BodyPart *rightFoot = findPart(aura, BodyPartType::RightFoot);

        for (int interation = 0; interation < iterations; ++interation) {
            solveAllJoints(aura);

            resolveAllFloorCollisions(aura, world);

            if (leftFoot != nullptr) {
                physics::applyFloorFriction(leftFoot->body, world);
            }

            if (rightFoot != nullptr) {
                physics::applyFloorFriction(rightFoot->body, world);
            }
        }
    }
}
