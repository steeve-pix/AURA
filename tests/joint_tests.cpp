#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Inertia.hpp"
#include "aura/physics/Motion.hpp"

int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *name) {
        if (!condition) {
            std::cerr << "FAIL " << name << '\n';
            passed = false;
        }
    };
    constexpr float tolerance = 1e-5f;
    const aura::body::Joint3D hip{{0.0f, -1.0f, 0.0f}, {0.0f, 0.9f, 0.0f}};

    for (const float torsoMass : {1.0f, 2.0f}) {
        aura::body::BodyPart3D torso;
        aura::body::BodyPart3D thigh;
        torso.body.mass = torsoMass;
        torso.body.position = {0.0f, 1.0f, 0.0f};
        thigh.body.position = {0.0f, -0.9f, 0.0f};

        const auto error = [&] {
            return aura::body::localToWorldPoint(thigh, hip.localAnchorB) -
                   aura::body::localToWorldPoint(torso, hip.localAnchorA);
        };
        check(error().length() < tolerance, "initial hip anchors match");

        thigh.body.position.y += 0.5f;
        check((error() - aura::math::Vec3{0.0f, 0.5f, 0.0f}).length() < tolerance,
              "deliberate displacement produces a nonzero error");
        const auto torsoBefore = torso.body.position;
        const auto thighBefore = thigh.body.position;
        const auto weightedPositionBefore = torsoBefore * torsoMass + thighBefore;

        aura::body::correctJointPosition(torso, thigh, hip);
        check(error().length() < tolerance, "position correction aligns hip anchors");
        const float torsoMovement = torso.body.position.y - torsoBefore.y;
        const float thighMovement = thighBefore.y - thigh.body.position.y;
        check(torsoMovement > 0.0f && thighMovement > 0.0f,
              "body parts move toward each other");
        check(std::abs(torsoMovement * torsoMass - thighMovement) < tolerance,
              "heavier body moves less");
        check((torso.body.position * torsoMass + thigh.body.position -
               weightedPositionBefore).length() < tolerance,
              "correction preserves center of mass");

        const auto correctedTorso = torso.body.position;
        const auto correctedThigh = thigh.body.position;
        aura::body::correctJointPosition(torso, thigh, hip);
        check((torso.body.position - correctedTorso).length() < tolerance &&
              (thigh.body.position - correctedThigh).length() < tolerance,
              "aligned anchors need no further correction");

        torso.body.velocity = {0.0f, 0.0f, 0.0f};
        thigh.body.velocity = {0.0f, 2.0f, 0.0f};
        const auto torsoAnchor = aura::body::localToWorldPoint(torso, hip.localAnchorA);
        const auto thighAnchor = aura::body::localToWorldPoint(thigh, hip.localAnchorB);
        const auto velocityError = [&] {
            return aura::physics::velocityAtWorldPoint(thigh.body, thighAnchor) -
                   aura::physics::velocityAtWorldPoint(torso.body, torsoAnchor);
        };
        check((velocityError() - aura::math::Vec3{0.0f, 2.0f, 0.0f}).length() < tolerance,
              "deliberate velocity difference separates anchors");

        aura::body::correctJointVelocity(torso, thigh, hip);
        check(velocityError().length() < tolerance, "corrected anchor velocities match");
        const aura::math::Vec3 expectedVelocity{0.0f, 2.0f / (torsoMass + 1.0f), 0.0f};
        check((torso.body.velocity - expectedVelocity).length() < tolerance &&
              (thigh.body.velocity - expectedVelocity).length() < tolerance,
              "shared velocity accounts for both masses");
        check((torso.body.velocity * torsoMass + thigh.body.velocity -
               aura::math::Vec3{0.0f, 2.0f, 0.0f}).length() < tolerance,
              "velocity correction preserves linear momentum");
        check((torso.body.position - correctedTorso).length() < tolerance &&
              (thigh.body.position - correctedThigh).length() < tolerance,
              "velocity correction preserves matching anchor positions");
    }

    aura::body::BodyPart3D angleTorso;
    aura::body::BodyPart3D angleThigh;
    angleTorso.body.orientation = aura::math::Quaternion::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.7f);
    for (const float angle : {0.0f, -0.5f, 0.5f, -1.2f, 1.2f, -3.5f, 3.5f}) {
        angleThigh.body.orientation = angleTorso.body.orientation *
            aura::math::Quaternion::fromAxisAngle(hip.hingeAxis, angle);
        const float expected = std::remainder(angle, 2.0f * std::numbers::pi_v<float>);
        check(std::abs(aura::body::relativeJointAngle(angleTorso, angleThigh, hip) - expected) < tolerance,
              "hinge angle is signed and measured in torso's local frame");
        const auto q = angleThigh.body.orientation;
        angleThigh.body.orientation = {-q.w, -q.x, -q.y, -q.z};
        check(std::abs(aura::body::relativeJointAngle(angleTorso, angleThigh, hip) - expected) < tolerance,
              "equivalent quaternion signs give the same hinge angle");
    }

    for (const float torsoYaw : {0.0f, 0.7f}) {
        for (const float torsoInertia : {1.0f, 2.0f}) {
            for (const float startAngle : {-1.0f, -0.8f, -0.5f, 0.0f, 0.5f, 0.8f, 1.0f}) {
                aura::body::BodyPart3D limitedTorso;
                aura::body::BodyPart3D limitedThigh;
                auto limitedHip = hip;
                limitedHip.minAngle = -0.8f;
                limitedHip.maxAngle = 0.8f;
                limitedTorso.body.momentOfInertia.x = torsoInertia;
                limitedTorso.body.orientation = aura::math::Quaternion::fromAxisAngle(
                    {0.0f, 1.0f, 0.0f}, torsoYaw
                );
                limitedThigh.body.orientation = limitedTorso.body.orientation *
                    aura::math::Quaternion::fromAxisAngle({1.0f, 0.0f, 0.0f}, startAngle);
                const auto torsoBefore = limitedTorso;
                const auto thighBefore = limitedThigh;
                const float expectedAngle = std::clamp(startAngle, -0.8f, 0.8f);
                const float expectedError = startAngle - expectedAngle;
                const float beforeAngle = aura::body::relativeJointAngle(limitedTorso, limitedThigh, limitedHip);
                const float beforeError = aura::body::jointAngleError(limitedTorso, limitedThigh, limitedHip);
                check(std::abs(beforeAngle - startAngle) < tolerance,
                      "deliberate starting angle is measured correctly");
                check(std::abs(beforeError - expectedError) < tolerance,
                      "angle error has correct magnitude and sign");

                aura::body::correctJointAngle(limitedTorso, limitedThigh, limitedHip);
                const float afterAngle = aura::body::relativeJointAngle(limitedTorso, limitedThigh, limitedHip);
                check(std::abs(afterAngle - expectedAngle) < tolerance,
                      "angle correction reaches limit or preserves valid angle");
                check(std::abs(aura::body::jointAngleError(limitedTorso, limitedThigh, limitedHip)) < tolerance,
                      "corrected angle has approximately zero limit error");
                const float rotationA = aura::body::relativeJointAngle(torsoBefore, limitedTorso, limitedHip);
                const float rotationB = aura::body::relativeJointAngle(thighBefore, limitedThigh, limitedHip);
                check(std::abs(rotationA * torsoInertia + rotationB) < tolerance,
                      "opposing angle corrections are weighted by X inertia");
                check(std::abs(limitedTorso.body.orientation.length() - 1.0f) < tolerance &&
                      std::abs(limitedThigh.body.orientation.length() - 1.0f) < tolerance,
                      "corrected orientations remain normalized");
                if (torsoYaw == 0.0f && torsoInertia == 1.0f && std::abs(startAngle) == 1.0f) {
                    std::cout << "Angle test: before = " << beforeAngle << ", error = " << beforeError
                              << ", after = " << afterAngle << '\n';
                }
            }
        }
    }

    for (const float torsoYaw : {0.0f, 0.7f}) {
        for (const float torsoInertia : {1.0f, 2.0f}) {
            for (const float sign : {-1.0f, 1.0f}) {
                aura::body::BodyPart3D limitedTorso;
                aura::body::BodyPart3D limitedThigh;
                limitedTorso.body.momentOfInertia.x = torsoInertia;
                limitedTorso.body.orientation = aura::math::Quaternion::fromAxisAngle(
                    {0.0f, 1.0f, 0.0f}, torsoYaw
                );
                limitedThigh.body.orientation = limitedTorso.body.orientation *
                    aura::math::Quaternion::fromAxisAngle(hip.hingeAxis, sign * 0.8f);
                const auto worldAxis = limitedTorso.body.orientation.rotate(hip.hingeAxis);
                limitedThigh.body.angularVelocity = worldAxis * (sign * 2.0f);
                const float energyBefore = 0.5f * limitedThigh.body.angularVelocity.lengthSquared();

                aura::body::correctJointAngularVelocity(limitedTorso, limitedThigh, hip);
                const float relativeSpeed =
                    (limitedThigh.body.angularVelocity - limitedTorso.body.angularVelocity).dot(worldAxis);
                check(std::abs(relativeSpeed) < tolerance,
                      "angle limit stops outward relative angular velocity");
                const auto momentum = limitedTorso.body.angularVelocity * torsoInertia +
                                      limitedThigh.body.angularVelocity;
                check((momentum - worldAxis * (sign * 2.0f)).length() < tolerance,
                      "angle-limit correction conserves hinge angular momentum");
                const float energyAfter =
                    0.5f * torsoInertia * limitedTorso.body.angularVelocity.lengthSquared() +
                    0.5f * limitedThigh.body.angularVelocity.lengthSquared();
                check(energyAfter <= energyBefore + tolerance,
                      "angle-limit correction does not add kinetic energy");
                if (torsoYaw == 0.0f && torsoInertia == 1.0f && sign == 1.0f) {
                    std::cout << "Angular limit: relative speed before = 2, after = " << relativeSpeed
                              << ", energy before = " << energyBefore << ", after = " << energyAfter << '\n';
                }

                limitedTorso.body.angularVelocity = {};
                limitedThigh.body.angularVelocity = worldAxis * (-sign * 2.0f);
                aura::body::correctJointAngularVelocity(limitedTorso, limitedThigh, hip);
                check(limitedTorso.body.angularVelocity.length() < tolerance &&
                      (limitedThigh.body.angularVelocity - worldAxis * (-sign * 2.0f)).length() < tolerance,
                      "angle limit permits rotation back into allowed range");
            }
        }
    }

    auto hinge = hip;
    hinge.hingeAxis = {2.0f, 0.0f, 0.0f};
    angleThigh.body.orientation = angleTorso.body.orientation *
        aura::math::Quaternion::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.6f) *
        aura::math::Quaternion::fromAxisAngle({1.0f, 0.0f, 0.0f}, 0.5f);
    check(std::abs(aura::body::relativeJointAngle(angleTorso, angleThigh, hinge) - 0.5f) < tolerance,
          "twist extraction excludes swing and normalizes hinge axis");
    angleThigh.body.orientation = angleTorso.body.orientation * aura::math::Quaternion{0.0f, 0.0f, 1.0f, 0.0f};
    check(aura::body::relativeJointAngle(angleTorso, angleThigh, hinge) == 0.0f,
          "undefined twist at perpendicular half-turn has a finite fallback");
    hinge.hingeAxis = {};
    bool rejectedZeroAxis = false;
    try {
        aura::body::relativeJointAngle(angleTorso, angleThigh, hinge);
    } catch (const std::invalid_argument &) {
        rejectedZeroAxis = true;
    }
    check(rejectedZeroAxis, "zero hinge axis is rejected");

    for (const float initialThighAngle : {0.0f, 0.5f}) {
        aura::body::BodyPart3D fallingTorso;
        aura::body::BodyPart3D fallingThigh;
        fallingTorso.size = {1.2f, 2.0f, 0.6f};
        fallingThigh.size = {0.5f, 1.8f, 0.5f};
        fallingTorso.body.position = {0.0f, 4.0f, 0.0f};
        fallingThigh.body.position = {0.0f, 2.1f, 0.0f};
        fallingThigh.body.orientation = aura::math::Quaternion::fromAxisAngle(
            {1.0f, 0.0f, 0.0f}, initialThighAngle
        );
        for (auto *part : {&fallingTorso, &fallingThigh}) {
            part->body.momentOfInertia = aura::physics::boxMomentOfInertia(part->body.mass, part->size);
        }

        constexpr float dt = 1.0f / 120.0f;
        constexpr int iterations = 8;
        float maxAnchorError = 0.0f;
        for (int step = 0; step < 600; ++step) {
            for (auto *part : {&fallingTorso, &fallingThigh}) {
                aura::physics::applyForce(part->body, aura::math::Vec3{0.0f, -9.81f, 0.0f} * part->body.mass);
                aura::physics::updateLinearAcceleration(part->body);
                aura::physics::updateAngularAcceleration(part->body);
                aura::physics::integrateLinearMotion(part->body, dt);
                aura::physics::integrateAngularMotion(part->body, dt);
            }
            for (int iteration = 0; iteration < iterations; ++iteration) {
                aura::physics::resolveFloorCollision(fallingTorso.body, fallingTorso.size, 0.0f, dt / iterations);
                aura::physics::resolveFloorCollision(fallingThigh.body, fallingThigh.size, 0.0f, dt / iterations);
                aura::body::solveJoint(fallingTorso, fallingThigh, hip);
            }
            const float anchorError =
                (aura::body::localToWorldPoint(fallingThigh, hip.localAnchorB) -
                 aura::body::localToWorldPoint(fallingTorso, hip.localAnchorA)).length();
            check(std::isfinite(anchorError) && anchorError < 1e-4f, "hip stays connected under gravity");
            if (anchorError > maxAnchorError) maxAnchorError = anchorError;
            for (auto *part : {&fallingTorso, &fallingThigh}) {
                check(std::isfinite(part->body.velocity.lengthSquared()), "body velocity remains finite");
                check(part->body.position.length() < 20.0f, "gravity scene stays within viewable bounds");
                aura::physics::clearForce(part->body);
                aura::physics::clearTorque(part->body);
            }
        }
        check(fallingTorso.body.position.y < 3.5f, "both parts fall under gravity");
        const float thighBottom = aura::physics::lowestPoint(fallingThigh.body, fallingThigh.size).y;
        check(std::abs(thighBottom) < 0.02f, "connected body reaches the floor");
        std::cout << "Five-second gravity test (initial angle " << initialThighAngle
                  << "): max anchor error = " << maxAnchorError
                  << ", thigh bottom = " << thighBottom << '\n';

    }

    // Compare the passive knee, isolated two-motor chain, and loaded two-motor chain.
    for (int scenario = 0; scenario < 3; ++scenario) {
        aura::body::BodyPart3D torso, thigh, shin;
        torso.size = {1.2f, 2.0f, 0.6f};
        thigh.size = {0.5f, 1.8f, 0.5f};
        shin.size = {0.45f, 1.6f, 0.45f};
        torso.body.position = {0.0f, 5.0f, 0.0f};
        thigh.body.position = {0.0f, 3.1f, 0.0f};
        shin.body.position = {0.0f, 1.4f, 0.0f};
        auto motorHip = hip;
        motorHip.targetAngle = 0.5f;
        motorHip.motorStiffness = 15.0f;
        motorHip.motorDamping = 4.0f;
        aura::body::Joint3D knee;
        knee.localAnchorA = {0.0f, -0.9f, 0.0f};
        knee.localAnchorB = {0.0f, 0.8f, 0.0f};
        knee.minAngle = 0.0f;
        knee.maxAngle = 2.2f;
        knee.targetAngle = 0.6f;
        knee.motorStiffness = 10.0f;
        knee.motorDamping = 4.0f;
        for (auto *part : {&torso, &thigh, &shin}) {
            part->body.momentOfInertia = aura::physics::boxMomentOfInertia(part->body.mass, part->size);
        }
        constexpr float dt = 1.0f / 120.0f;
        float minAngle = 2.2f, maxAngle = 0.0f, maxGap = 0.0f;
        for (int step = 0; step < 1200; ++step) {
            aura::body::applyJointMotor(torso, thigh, motorHip);
            if (scenario != 0) aura::body::applyJointMotor(thigh, shin, knee);
            for (auto *part : {&torso, &thigh, &shin}) {
                if (scenario != 1) {
                    aura::physics::applyForce(part->body, aura::math::Vec3{0.0f, -9.81f, 0.0f} * part->body.mass);
                }
                aura::physics::updateLinearAcceleration(part->body);
                aura::physics::updateAngularAcceleration(part->body);
                aura::physics::integrateLinearMotion(part->body, dt);
                aura::physics::integrateAngularMotion(part->body, dt);
            }
            for (int iteration = 0; iteration < 8; ++iteration) {
                if (scenario != 1) {
                    for (auto *part : {&torso, &thigh, &shin}) {
                        aura::physics::resolveFloorCollision(part->body, part->size, 0.0f, dt / 8);
                    }
                }
                aura::body::solveJoint(torso, thigh, motorHip);
                aura::body::solveJoint(thigh, shin, knee);
            }
            const float angle = aura::body::relativeJointAngle(thigh, shin, knee);
            const float gap = (aura::body::localToWorldPoint(thigh, knee.localAnchorA) -
                               aura::body::localToWorldPoint(shin, knee.localAnchorB)).length();
            minAngle = std::min(minAngle, angle);
            maxAngle = std::max(maxAngle, angle);
            maxGap = std::max(maxGap, gap);
            check(std::isfinite(angle) && angle >= -1e-5f && angle <= 2.2f + 1e-5f,
                  "knee stays within one-way limits");
            check(std::isfinite(gap) && gap < 0.02f, "shin remains attached to thigh");
            for (auto *part : {&torso, &thigh, &shin}) {
                check(std::isfinite(part->body.angularVelocity.lengthSquared()) &&
                      std::isfinite(part->body.velocity.lengthSquared()) && part->body.position.length() < 20.0f,
                      "three-part chain remains finite and bounded");
                aura::physics::clearForce(part->body);
                aura::physics::clearTorque(part->body);
            }
        }
        check(maxAngle > 0.1f, "knee folds in the positive direction");
        const float hipAngle = aura::body::relativeJointAngle(torso, thigh, motorHip);
        const float kneeAngle = aura::body::relativeJointAngle(thigh, shin, knee);
        const float hipOmega = (thigh.body.angularVelocity - torso.body.angularVelocity).dot(
            torso.body.orientation.rotate(motorHip.hingeAxis).normalized());
        const float kneeOmega = (shin.body.angularVelocity - thigh.body.angularVelocity).dot(
            thigh.body.orientation.rotate(knee.hingeAxis).normalized());
        if (scenario == 1) {
            check(std::abs(hipAngle - motorHip.targetAngle) < 1e-4f &&
                  std::abs(kneeAngle - knee.targetAngle) < 1e-4f,
                  "both isolated motors converge to independent targets");
            check(std::abs(hipOmega) < 1e-4f && std::abs(kneeOmega) < 1e-4f,
                  "both isolated hinges settle to zero relative speed");
            check(std::abs(aura::body::jointMotorTorque(torso, thigh, motorHip)) < 1e-4f &&
                  std::abs(aura::body::jointMotorTorque(thigh, shin, knee)) < 1e-4f,
                  "both isolated motor torques settle to zero");
        }
        std::cout << "Ten-second chain test (scenario " << scenario << "): hip=" << hipAngle
                  << " knee=" << kneeAngle << " hipOmega=" << hipOmega << " kneeOmega=" << kneeOmega
                  << " hipTorque=" << aura::body::jointMotorTorque(torso, thigh, motorHip)
                  << " kneeTorque=" << aura::body::jointMotorTorque(thigh, shin, knee)
                  << ", knee angle range = " << minAngle << " to " << maxAngle
                  << ", max knee anchor gap = " << maxGap << '\n';
    }

    return passed ? 0 : 1;
}
