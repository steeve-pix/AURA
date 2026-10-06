#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"

int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    constexpr float tolerance = 1e-5f;
    for (bool tiltedParent: {false, true}) {
        for (float angle: {-1.8f, -0.4f, 0.4f, 1.8f}) {
            auto body = aura::body::createAuraBody3D();
            const auto skeleton = aura::body::createAuraSkeleton3D(body);
            if (tiltedParent)
                body.torso.body.orientation = aura::math::Quaternion::fromAxisAngle({1, 2, 3}, 0.7f);
            auto &upper = body.leftUpperArm;
            auto &forearm = body.leftForearm;
            auto &hand = body.leftHand;
            const auto assemble = [](auto &parent, auto &child, const auto &joint, float childAngle) {
                child.body.orientation = parent.body.orientation *
                    aura::math::Quaternion::fromAxisAngle({1, 0, 0}, childAngle);
                child.body.position = aura::body::localToWorldPoint(parent, joint.localAnchorA) -
                                      child.body.orientation.rotate(joint.localAnchorB);
            };
            assemble(body.torso, upper, skeleton.leftShoulder, angle);
            upper.body.position += aura::math::Vec3{0.003f, -0.002f, 0.001f};
            assemble(upper, forearm, skeleton.leftElbow, 0.6f);
            assemble(forearm, hand, skeleton.leftWrist, 0.2f);
            for (auto *part: {&upper, &forearm, &hand}) part->body.velocity = {0.1f, 0.2f, 0.3f};
            const auto gap = [](const auto &a, const auto &b, const auto &joint) {
                return aura::body::localToWorldPoint(b, joint.localAnchorB) -
                       aura::body::localToWorldPoint(a, joint.localAnchorA);
            };
            const auto relativeV = [](const auto &a, const auto &b, const auto &joint) {
                return aura::physics::velocityAtWorldPoint(b.body, aura::body::localToWorldPoint(b, joint.localAnchorB)) -
                       aura::physics::velocityAtWorldPoint(a.body, aura::body::localToWorldPoint(a, joint.localAnchorA));
            };
            const auto beforeTorso = body.torso.body;
            const auto beforeRightArm = body.rightUpperArm.body;
            const auto pivot = aura::body::localToWorldPoint(upper, skeleton.leftShoulder.localAnchorB);
            const float elbowGap = gap(upper, forearm, skeleton.leftElbow).length();
            const float wristGap = gap(forearm, hand, skeleton.leftWrist).length();
            aura::body::correctLeftShoulderAngleAroundPivot(body, skeleton, skeleton.leftShoulder);
            check(std::abs(aura::body::relativeJointAngle(body.torso, upper, skeleton.leftShoulder) -
                           std::clamp(angle, -1.5f, 1.5f)) < tolerance,
                  "shoulder subtree rotation enforces both limits with a rotated torso");
            check((aura::body::localToWorldPoint(upper, skeleton.leftShoulder.localAnchorB) - pivot).length() < tolerance &&
                  std::abs(gap(upper, forearm, skeleton.leftElbow).length() - elbowGap) < tolerance &&
                  std::abs(gap(forearm, hand, skeleton.leftWrist).length() - wristGap) < tolerance,
                  "rotation preserves the shoulder pivot and descendant anchor relationships");
            const auto elbowVector = gap(upper, forearm, skeleton.leftElbow);
            const auto wristVector = gap(forearm, hand, skeleton.leftWrist);
            aura::body::correctLeftShoulderPositionAsSubtree(body, skeleton.leftShoulder);
            check(gap(body.torso, upper, skeleton.leftShoulder).length() < tolerance &&
                  (gap(upper, forearm, skeleton.leftElbow) - elbowVector).length() < tolerance &&
                  (gap(forearm, hand, skeleton.leftWrist) - wristVector).length() < tolerance,
                  "shoulder position repair closes its gap without changing elbow or wrist gaps");
            check(std::abs(aura::body::relativeJointAngle(upper, forearm, skeleton.leftElbow) - 0.6f) < tolerance &&
                  std::abs(aura::body::relativeJointAngle(forearm, hand, skeleton.leftWrist) - 0.2f) < tolerance,
                  "descendant angles are unchanged by shoulder geometry correction");
            check(relativeV(upper, forearm, skeleton.leftElbow).length() < tolerance &&
                  relativeV(forearm, hand, skeleton.leftWrist).length() < tolerance,
                  "coherent translating subtree stays velocity-continuous through geometry correction");
            check((body.torso.body.position - beforeTorso.position).length() == 0.0f &&
                  body.torso.body.orientation.w == beforeTorso.orientation.w &&
                  body.torso.body.orientation.x == beforeTorso.orientation.x &&
                  body.torso.body.orientation.y == beforeTorso.orientation.y &&
                  body.torso.body.orientation.z == beforeTorso.orientation.z,
                  "shoulder geometric correction leaves torso fixed");

            // Give descendants distinct motion; shoulder impulses must preserve it, not zero it.
            upper.body.angularVelocity = {0.2f, 0.3f, 0.4f};
            forearm.body.angularVelocity = {0.25f, 0.28f, 0.41f};
            hand.body.angularVelocity = {0.22f, 0.31f, 0.45f};
            const auto elbowV = relativeV(upper, forearm, skeleton.leftElbow);
            const auto wristV = relativeV(forearm, hand, skeleton.leftWrist);
            const auto elbowOmega = forearm.body.angularVelocity - upper.body.angularVelocity;
            const auto wristOmega = hand.body.angularVelocity - forearm.body.angularVelocity;
            const auto geometryUpper = upper.body.position;
            const auto geometryHand = hand.body.position;
            aura::body::solveLeftShoulderSubtreeVelocityConstraints(body, skeleton.leftShoulder);
            check((relativeV(upper, forearm, skeleton.leftElbow) - elbowV).length() < tolerance &&
                  (relativeV(forearm, hand, skeleton.leftWrist) - wristV).length() < tolerance,
                  "shoulder velocity corrections preserve both descendant anchor velocities");
            check((forearm.body.angularVelocity - upper.body.angularVelocity - elbowOmega).length() < tolerance &&
                  (hand.body.angularVelocity - forearm.body.angularVelocity - wristOmega).length() < tolerance,
                  "shoulder velocity corrections preserve both relative angular velocities");
            check((upper.body.position - geometryUpper).length() == 0.0f &&
                  (hand.body.position - geometryHand).length() == 0.0f &&
                  (body.rightUpperArm.body.position - beforeRightArm.position).length() == 0.0f &&
                  (body.rightUpperArm.body.velocity - beforeRightArm.velocity).length() == 0.0f,
                  "velocity correction leaves geometry and the right arm unchanged");
        }
    }
    return passed ? 0 : 1;
}
