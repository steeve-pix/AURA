#include "../src/app/SimulationStep.hpp"
#include "aura/sim/AuraSimulation.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

// Executes the application's single configuration, including self-collision.
// Historical experiment flags/stepping do not participate in this gate.
int main() {
    // The visual app and Python both construct this wrapper. A fresh instance
    // must enable the same collision hinge policy as an explicitly reset one.
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
    bool passed = pairs.size() == 105;
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
