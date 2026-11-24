#include "aura/sim/AuraSimulation.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/LocalJointVelocity.hpp"
#include "aura/physics/Inertia.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace aura;

namespace {
    bool passed = true;

    void check(bool condition, const char *message) {
        if (!condition) {
            passed = false;
            std::cerr << "FAIL " << message << '\n';
        }
    }

    math::Quaternion pose(float pitch, float y, float z) {
        const math::Vec3 swing{0, y, z};
        return math::Quaternion::fromAxisAngle(swing.normalized(), swing.length()) *
               math::Quaternion::fromAxisAngle({1, 0, 0}, pitch);
    }

    void attach(body::BodyPart3D &parent, body::BodyPart3D &child, const body::Joint3D &joint,
                const math::Quaternion &relative) {
        child.body.orientation = (parent.body.orientation * relative).normalized();
        child.body.position = body::localToWorldPoint(parent, joint.localAnchorA) -
                              child.body.orientation.rotate(joint.localAnchorB);
    }

    float gap(const body::BodyPart3D &a, const body::BodyPart3D &b, const body::Joint3D &j) {
        return (body::localToWorldPoint(a, j.localAnchorA) - body::localToWorldPoint(b, j.localAnchorB)).length();
    }

    float energy(const body::BodyPart3D &p) {
        const auto w = p.body.orientation.conjugate().rotate(p.body.angularVelocity);
        const auto I = p.body.momentOfInertia;
        return .5f * (I.x * w.x * w.x + I.y * w.y * w.y + I.z * w.z * w.z);
    }
}

int main() {
    for (bool left: {false, true})
        for (bool tilted: {false, true}) {
            auto b = body::createAuraBody3D();
            auto skeleton = body::createAuraSkeleton3D(b);
            auto &j = left ? skeleton.leftShoulder : skeleton.rightShoulder;
            auto &upper = left ? b.leftUpperArm : b.rightUpperArm;
            auto &forearm = left ? b.leftForearm : b.rightForearm;
            auto &hand = left ? b.leftHand : b.rightHand;
            const auto &elbow = left ? skeleton.leftElbow : skeleton.rightElbow;
            const auto &wrist = left ? skeleton.leftWrist : skeleton.rightWrist;
            const float outward = left ? -1.f : 1.f, inward = -outward;
            if (tilted) b.torso.body.orientation = math::Quaternion::fromAxisAngle({1, 2, 3}, .7f);
            check(j.type == body::JointType::SwingTwist && j.motorControlsSwing,
                  "shoulders have three motor coordinates");
            // Independent anatomical evidence from a hanging upper-arm vector.
            check(pose(-1.5707963f, 0, 0).rotate({0, -1, 0}).z > .999f, "negative pitch raises arm forward");
            check(pose(-3.f, 0, 0).rotate({0, -1, 0}).y > .98f, "forward pitch can reach overhead");
            check(outward * pose(0, 0, outward * 1.5707963f).rotate({0, -1, 0}).x > .999f,
                  "mirrored sideways lift points away from torso");
            check(inward * pose(-1.5707963f, inward * .5f, 0).rotate({0, -1, 0}).x > .47f,
                  "across control moves a forward arm toward the opposite hand");

            // Differentiate actual coordinate measurements, not Euler-angle assumptions.
            for (const auto q: {
                     pose(0, 0, 0), pose(-1.2f, inward * .5f, outward * 1.1f), pose(-2.8f, .7f, outward * 1.7f)
                 }) {
                attach(b.torso, upper, j, q);
                const auto gradients = body::jointCoordinateGradients(b.torso, upper, j);
                const std::array<math::Vec3, 3> directions{math::Vec3{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                for (const auto direction: directions) {
                    const auto original = upper.body.orientation;
                    const auto world = b.torso.body.orientation.rotate(direction);
                    constexpr float epsilon = .0002f;
                    std::array < std::array < float, 3 >, 2 > values{};
                    for (int sign = 0; sign < 2; ++sign) {
                        upper.body.orientation =
                                math::Quaternion::fromAxisAngle(world, sign ? epsilon : -epsilon) * original;
                        const auto swing = body::relativeJointSwingAngles(b.torso, upper, j);
                        values[sign] = {body::relativeJointAngle(b.torso, upper, j), swing.y, swing.z};
                    }
                    upper.body.orientation = original;
                    for (int i = 0; i < 3; ++i)
                        check(std::abs((values[1][i] - values[0][i]) / (2 * epsilon) - gradients[i].dot(world)) < .003f,
                              "compound coordinate gradient agrees with finite differences in both frames");
                }
            }
            // All six bounds: preserve descendants and remove only outward velocity.
            for (int coordinate = 0; coordinate < 3; ++coordinate)
                for (bool maximum: {false, true}) {
                    const std::array<float, 3> lo{j.minAngle, j.minSwingY, j.minSwingZ};
                    const std::array<float, 3> hi{j.maxAngle, j.maxSwingY, j.maxSwingZ};
                    std::array < float, 3 > angles{-1.1f, inward * .4f, outward * .7f};
                    const float limit = maximum ? hi[coordinate] : lo[coordinate];
                    angles[coordinate] = limit + (maximum ? .07f : -.07f);
                    attach(b.torso, upper, j, pose(angles[0], angles[1], angles[2]));
                    attach(upper, forearm, elbow, math::Quaternion::fromAxisAngle(elbow.hingeAxis, .6f));
                    attach(forearm, hand, wrist, math::Quaternion::fromAxisAngle(wrist.hingeAxis, .2f));
                    upper.body.velocity = forearm.body.velocity = hand.body.velocity = {.1f, .2f, .3f};
                    const auto torsoBefore = b.torso.body;
                    const auto handBefore = hand.body.velocity;
                    if (left) body::correctLeftShoulderAngleAroundPivot(b, skeleton, j);
                    else body::correctBranchAngleAroundPivot(b, skeleton, j, body::MajorBodyBranch3D::RightShoulder,
                                                             true);
                    const auto swing = body::relativeJointSwingAngles(b.torso, upper, j);
                    const std::array<float, 3> measured{body::relativeJointAngle(b.torso, upper, j), swing.y, swing.z};
                    for (int i = 0; i < 3; ++i)
                        check(std::abs(measured[i] - (i == coordinate ? limit : angles[i])) < 2e-5f,
                              "bound projection clamps one coordinate and preserves the others");
                    check(gap(b.torso, upper, j) < 2e-6f && gap(upper, forearm, elbow) < 2e-6f && gap(
                              forearm, hand, wrist) < 2e-6f,
                          "compound projection keeps shoulder elbow wrist attached");
                    check(std::abs(body::relativeJointAngle(upper, forearm, elbow) - .6f) < 2e-5f &&
                          std::abs(body::relativeJointAngle(forearm, hand, wrist) - .2f) < 2e-5f,
                          "compound projection preserves descendant flexion");
                    check((b.torso.body.position - torsoBefore.position).length() == 0 &&
                          (hand.body.velocity - handBefore).length() == 0,
                          "projection does not move parent or copy descendant velocities");
                    const auto gradient = body::jointCoordinateGradients(b.torso, upper, j)[coordinate];
                    b.torso.body.angularVelocity = {};
                    upper.body.angularVelocity = gradient * (maximum ? 1.f : -1.f);
                    const float before = energy(b.torso) + energy(upper);
                    body::correctLocalJointAngularLimitVelocity(b.torso, upper, j);
                    check(std::abs((upper.body.angularVelocity - b.torso.body.angularVelocity).dot(gradient)) < 2e-5f,
                          "compound limit impulse removes outward coordinate speed");
                    check(energy(b.torso) + energy(upper) <= before + 1e-5f,
                          "limit impulse does not add angular energy");
                    b.torso.body.angularVelocity = {};
                    upper.body.angularVelocity = gradient * (maximum ? -1.f : 1.f);
                    const auto inwardVelocity = upper.body.angularVelocity;
                    body::correctLocalJointAngularLimitVelocity(b.torso, upper, j);
                    check((upper.body.angularVelocity - inwardVelocity).length() == 0,
                          "inward motion at a bound is allowed");
                }
            // An overhead request from backward extension must take the legal
            // forward route, even when a quaternion's shortest path crosses a stop.
            attach(b.torso, upper, j, pose(.8f, 0, 0));
            b.torso.body.angularVelocity = upper.body.angularVelocity = {};
            j.targetAngle = -3.f;
            const auto pitchGradient = body::jointCoordinateGradients(b.torso, upper, j)[0];
            check(body::jointMotorTorqueVector(b.torso, upper, j).dot(pitchGradient) < 0,
                  "overhead request from backward stop takes permitted forward path");

            // Full vector damping dissipates power, including swing about thin arm axis.
            attach(b.torso, upper, j, pose(-1.1f, .3f, outward * .8f));
            j.motorStiffness = 0;
            b.torso.body.angularVelocity = {.1f, .2f, .3f};
            upper.body.angularVelocity = {1.2f, -.4f, .7f};
            const auto omega = upper.body.angularVelocity - b.torso.body.angularVelocity;
            float dampingPower = 0;
            for (const auto gradient: body::jointCoordinateGradients(b.torso, upper, j)) {
                const float rate = omega.dot(gradient);
                dampingPower -= j.motorDamping * rate * rate;
            }
            check(std::abs(body::jointMotorTorqueVector(b.torso, upper, j).dot(omega) - dampingPower) < 1e-5f,
                  "3D coordinate damping dissipates the expected power");
        }
    // Exercise real motor trajectories with gravity, all 105 self-collision
    // pairs and the unchanged application solver. Falling is allowed here:
    // this verifies mechanics, not learned balance or exact pose tracking.
    for (int request = 0; request < 4; ++request) {
        sim::AuraSimulation simulation;
        float maxGap = 0, maxLimit = 0, maxFloor = 0, maxOverlap = 0;
        for (int step = 0; step < 1200; ++step) {
            sim::AuraAction action;
            const float ramp = step < 120 ? step / 120.f : step < 360 ? 1.f : step < 480 ? (480 - step) / 120.f : 0.f;
            if (request == 0 || request == 3) action.leftShoulderTarget =
                                              action.rightShoulderTarget = -1.5707963f * ramp;
            if (request == 1) action.leftShoulderTarget = action.rightShoulderTarget = -3.f * ramp;
            if (request == 2) {
                action.leftShoulderSwingZTarget = -1.5707963f * ramp;
                action.rightShoulderSwingZTarget = 1.5707963f * ramp;
            }
            if (request == 3) {
                action.leftShoulderSwingYTarget = .32f * ramp;
                action.rightShoulderSwingYTarget = -.32f * ramp;
            }
            simulation.step(action);
            check(app::bodyMechanics(simulation.getParts()).finite, "moving shoulder full body remains finite");
            for (const auto &entry: simulation.getJoints()) {
                const auto &j = entry.constraint;
                maxGap = std::max(maxGap, gap(*entry.partA, *entry.partB, j));
                maxLimit = std::max(maxLimit, std::abs(body::jointAngleError(*entry.partA, *entry.partB, j)));
                if (j.type == body::JointType::SwingTwist) {
                    const auto v = body::relativeJointSwingAngles(*entry.partA, *entry.partB, j);
                    maxLimit = std::max({
                        maxLimit, j.minSwingY - v.y, v.y - j.maxSwingY, j.minSwingZ - v.z, v.z - j.maxSwingZ
                    });
                }
            }
            for (const auto *part: simulation.getParts())
                maxFloor = std::max(maxFloor, -physics::lowestPoint(part->body, part->size, part->floorShape).y);
            maxOverlap = std::max(
                maxOverlap,
                body::bodyCollisionMetrics(simulation.body(), simulation.getBodyPairs()).maximumPenetration);
        }
        std::cout << "shoulderMotion request=" << request << " duration=10s maxGap=" << maxGap
                << " maxLimit=" << maxLimit << " maxFloor=" << maxFloor << " maxOverlap=" << maxOverlap << '\n';
        check(maxGap <= .001f && maxLimit <= 1e-5f && maxFloor <= .001f && maxOverlap <= .001f,
              "moving shoulder preserves attachment limits floor and collision tolerances");
    }
    std::cout <<
            "Mirrored shoulder poses, compound gradients, six limits, subtree integrity and energy checks complete.\n";
    return passed ? 0 : 1;
}
