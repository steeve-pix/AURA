#include "../src/app/SimulationStep.hpp"
#include "aura/sim/AuraSimulation.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include "aura/body/JointConstraint.hpp"
#include "aura/physics/Motion.hpp"

namespace {
bool motorContract() {
    using namespace aura;
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) std::cerr << "Motor contract failure: " << message << '\n';
        passed &= condition;
    };
    sim::AuraSimulation simulation;
    check(sim::actionSpecs.size() == 15, "fifteen actions are registered");
    for (const auto &spec : sim::actionSpecs) {
        const auto &joints = simulation.getJoints();
        const auto joint = std::find_if(joints.begin(), joints.end(), [&](const auto &entry) {
            return std::string_view(entry.name) == spec.jointName;
        });
        check(joint != joints.end(), "action joint exists");
        if (joint == joints.end()) continue;
        check(joint->motor == (std::string_view(joint->name) != "head"),
              "supported motors are enabled; coupled head motor is opt-in");
        check(joint->constraint.minAngle == static_cast<float>(spec.minimum) &&
              joint->constraint.maxAngle == static_cast<float>(spec.maximum), "limits match skeleton");
        check(joint->constraint.motorStiffness > 0 && joint->constraint.motorDamping > 0,
              "existing motor gains are positive");
        const bool elbow = std::string_view(spec.jointName)=="leftElbow" || std::string_view(spec.jointName)=="rightElbow";
        check((joint->constraint.hingeAxis - math::Vec3{elbow?-1.0f:1.0f,0,0}).length() < 1e-6f,
              "motor uses its anatomical parent-local hinge axis");
    }

    for (float requested : {-100.0f, 100.0f}) {
        sim::AuraAction action;
        for (const auto &spec : sim::actionSpecs) action.*(spec.target) = requested;
        simulation.setAction(action);
        simulation.step();
        for (const auto &spec : sim::actionSpecs) {
            const float expected = static_cast<float>(requested < 0 ? spec.minimum : spec.maximum);
            check(simulation.action().*(spec.target) == expected, "targets clamp to physical limits");
            const auto &joints = simulation.getJoints();
            const auto joint = std::find_if(joints.begin(), joints.end(), [&](const auto &entry) {
                return std::string_view(entry.name) == spec.jointName;
            });
            check(joint->constraint.targetAngle == expected, "action maps to its named joint");
        }
        simulation.reset();
    }
    simulation.setMotorEnabled("waist", false);
    simulation.reset();
    check(!simulation.getJoints()[0].motor, "motor-enable selection survives reset");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto action = simulation.action();
        action.waistTarget = invalid;
        bool rejected = false;
        try { simulation.setAction(action); } catch (const std::invalid_argument &) { rejected = true; }
        check(rejected && simulation.action().waistTarget == 0, "invalid action is rejected transactionally");
    }

    // Compare equivalent full-body simulations before landing, with existing
    // gravity, gains, collisions and solver order. Verify response, not tracking.
    for (std::size_t index = 0; index < sim::actionSpecs.size(); ++index) {
        const auto &spec = sim::actionSpecs[index];
        for (int sign : {-1, 1}) {
            if (sign < 0 && spec.minimum == 0) continue;
            sim::AuraSimulation neutral, commanded;
            if (std::string_view(spec.jointName) == "head") {
                // Independently test head control with its coupled neck motor
                // disabled. Simultaneous activation is a documented blocker.
                for (auto *instance : {&neutral, &commanded}) {
                    instance->setMotorEnabled("neck", false);
                    instance->setMotorEnabled("head", true);
                }
            }
            sim::AuraAction action;
            const float target = static_cast<float>((sign < 0 ? spec.minimum : spec.maximum) * 0.2);
            action.*(spec.target) = target;
            commanded.setAction(action);
            const auto findJoint = [&](sim::AuraSimulation &instance) -> app::BodyJoint & {
                auto &joints = instance.getJoints();
                return *std::find_if(joints.begin(), joints.end(), [&](const auto &entry) {
                    return std::string_view(entry.name) == spec.jointName;
                });
            };
            auto &joint = findJoint(commanded);
            joint.constraint.targetAngle = target;
            body::applyJointMotor(*joint.partA, *joint.partB, joint.constraint);
            const auto axis = joint.partA->body.orientation.rotate(joint.constraint.hingeAxis).normalized();
            check((joint.partA->body.torque + joint.partB->body.torque).length() < 1e-6f,
                  "motor applies equal and opposite torques");
            check(sign * joint.partB->body.torque.dot(axis) > 0, "torque points toward target");
            physics::clearTorque(joint.partA->body);
            physics::clearTorque(joint.partB->body);
            for (int step = 0; step < 24; ++step) {
                neutral.step();
                commanded.step();
                for (const auto &entry : commanded.getJoints()) {
                    const auto angle = body::relativeJointAngle(*entry.partA, *entry.partB, entry.constraint);
                    check(std::isfinite(angle) && angle >= entry.constraint.minAngle - 1e-5f &&
                          angle <= entry.constraint.maxAngle + 1e-5f, "commanded joints respect twist limits");
                }
            }
            const auto &neutralJoint = findJoint(neutral);
            const auto neutralAngle = body::relativeJointAngle(*neutralJoint.partA, *neutralJoint.partB, neutralJoint.constraint);
            const auto commandedAngle = body::relativeJointAngle(*joint.partA, *joint.partB, joint.constraint);
            const auto response = commandedAngle - neutralAngle;
            std::cout << "motorResponse " << spec.jointName << " target=" << target << " delta=" << response << '\n';
            check(sign * response > 1e-5f, "each motor moves its joint in the commanded direction");
            if (std::string_view(spec.jointName)=="leftElbow" || std::string_view(spec.jointName)=="rightElbow") {
                const auto relative = joint.partA->body.orientation.conjugate()*joint.partB->body.orientation;
                const auto neutralRelative = neutralJoint.partA->body.orientation.conjugate()*neutralJoint.partB->body.orientation;
                check(relative.rotate({0,-1,0}).z > neutralRelative.rotate({0,-1,0}).z + 1e-5f,
                      "full-body elbow command moves the forearm forward relative to its upper arm");
            }
        }
    }
    return passed;
}
}

// Executes the application's single configuration, including self-collision.
// Historical experiment flags/stepping do not participate in this gate.
int main() {
    // The visual app and Python both construct this wrapper. A fresh instance
    // must enable the same collision hinge policy as an explicitly reset one.
    const bool motorPassed = motorContract();
    aura::sim::AuraSimulation simulation;
    for (int initialization = 0; initialization < 2; ++initialization) {
        if (simulation.getBodyPairs().size() != 105) return 1;
        for (const auto& joint : simulation.getJoints()) {
            const bool expected = joint.constraint.type == aura::body::JointType::Hinge;
            if (joint.constraint.enforceHingeAxis != expected) {
                std::cerr << "Collision joint setup missing for " << joint.name
                          << (initialization == 0 ? " after construction" : " after reset") << '\n';
                return 1;
            }
        }
        simulation.reset();
    }

    auto body = aura::body::createAuraBody3D();
    const auto parts = aura::body::collisionParts(body);
    for (auto *part: parts) part->body.position.y += 0.6f;
    auto skeleton = aura::body::createAuraSkeleton3D(body);
    aura::body::configureBodyCollisionJoints(skeleton);
    auto joints = aura::app::makeBodyJoints(body, skeleton);
    const auto pairs = aura::body::bodyCollisionPairs(body, skeleton, 3);
    const aura::physics::RoomBounds room;
    std::array<aura::physics::RoomBody, 16> roomBodies;
    for (std::size_t i = 0; i < parts.size(); ++i)
        roomBodies[i] = {&parts[i]->body, parts[i]->size};
    aura::app::BodyDiagnosticHistory history;
    history.initialEnergy = aura::app::bodyEnergy(parts);
    double maxGap = 0, maxOverlap = 0, maxRoom = 0, maxFloor = 0, maxRadius = 0;
    bool passed = motorPassed && pairs.size() == 105;
    for (int step = 1; step <= 1200; ++step) {
        aura::app::stepSimulation(body, skeleton, joints, parts, pairs, roomBodies, room, history);
        const auto mechanics = aura::app::bodyMechanics(parts);
        passed &= mechanics.finite;
        const auto collisions = aura::body::bodyCollisionMetrics(body, pairs);
        maxOverlap = std::max(maxOverlap, double(collisions.maximumPenetration));
        for (const auto *part: parts) {
            maxRoom = std::max(maxRoom, double(aura::physics::roomPenetration(part->body, part->size, room)));
            maxFloor = std::max(maxFloor, double(-aura::physics::lowestPoint(
                                    part->body, part->size, part->floorShape).y));
            maxRadius = std::max(maxRadius, double(part->body.position.length()));
        }
        for (const auto &joint: joints) {
            const auto a = aura::body::localToWorldPoint(*joint.partA, joint.constraint.localAnchorA);
            const auto b = aura::body::localToWorldPoint(*joint.partB, joint.constraint.localAnchorB);
            maxGap = std::max(maxGap, double((b - a).length()));
            const auto angle = aura::body::relativeJointAngle(*joint.partA, *joint.partB, joint.constraint);
            passed &= std::isfinite(angle) && angle >= joint.constraint.minAngle - 1e-5f &&
                    angle <= joint.constraint.maxAngle + 1e-5f;
            if (joint.constraint.type == aura::body::JointType::SwingTwist) {
                const auto swing = aura::body::relativeJointSwingAngles(*joint.partA, *joint.partB, joint.constraint);
                passed &= swing.y >= joint.constraint.minSwingY - 1e-5f &&
                        swing.y <= joint.constraint.maxSwingY + 1e-5f &&
                        swing.z >= joint.constraint.minSwingZ - 1e-5f &&
                        swing.z <= joint.constraint.maxSwingZ + 1e-5f;
            }
        }
        if (step % 120 == 0) {
            history.observe(parts, joints, step * aura::app::FIXED_DT);
            history.printSummary(parts, joints, step * aura::app::FIXED_DT, false, false, false);
        } else history.observe(parts, joints, step * aura::app::FIXED_DT);
    }
    passed &= maxGap <= 0.001 && maxOverlap <= 0.001 && maxRoom <= 0.001 && maxFloor <= 0.001 && maxRadius < 15;
    std::cout << "runtimeBaseline passed=" << passed << " maxGap=" << maxGap
            << " maxPenetration=" << maxOverlap << " maxRoom=" << maxRoom << " maxFloor=" << maxFloor
            << " maxRadius=" << maxRadius << '\n';
    return passed ? 0 : 1;
}
