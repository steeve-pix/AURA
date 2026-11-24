#include "SimulationStep.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointSolver.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Motion.hpp"

namespace aura::app {
    void stepSimulation(body::AuraBody3D &auraBody, const body::AuraSkeleton3D &skeleton,
                        std::span<BodyJoint> joints, std::span<body::BodyPart3D * const> parts,
                        const std::vector<body::BodyCollisionPair> &bodyPairs,
                        std::span<const physics::RoomBody> roomBodies, const physics::RoomBounds &room,
                        BodyDiagnosticHistory &diagnosticHistory,
                        physics::FloorFrictionModel floorFriction, const SimulationTrace &trace) {
        if (trace) trace("begin", "");
        const auto dt = static_cast<float>(FIXED_DT);
        const bool physicalGround = floorFriction == physics::FloorFrictionModel::Coulomb;
        const auto solveJoint = [&](BodyJoint &joint) {
            const auto com = physicalGround ? body::wholeBodyCenterOfMass(auraBody) : std::array<double, 3>{};
            body::solveBodyJoint(auraBody, skeleton, *joint.partA, *joint.partB, joint.constraint, physicalGround);
            // Internal pose corrections may redistribute mass, but cannot translate
            // the system horizontally. This preserves existing momentum/velocities
            // and all relative geometry; room contacts still take precedence.
            if (physicalGround) body::recenterHorizontallyWithinRoom(auraBody, com, room);
        };
        diagnosticHistory.leftFootNormalImpulse = 0.0f;
        diagnosticHistory.rightFootNormalImpulse = 0.0f;
        physics::FloorCollisionAudit leftFinalAudit, rightFinalAudit;
        std::vector<physics::RoomBody> finalRoomBodies(roomBodies.begin(), roomBodies.end());
        if (physicalGround)
            for (auto &entry: finalRoomBodies) {
                if (entry.body == &auraBody.leftFoot.body) entry.floorAudit = &leftFinalAudit;
                if (entry.body == &auraBody.rightFoot.body) entry.floorAudit = &rightFinalAudit;
            }
        const double energyBeforeIntegration = bodyEnergy(parts);
        diagnosticHistory.sampleMotorWork(joints, dt);
        for (auto &joint: joints) {
            if (joint.motor) body::applyJointMotor(*joint.partA, *joint.partB, joint.constraint);
        }
        for (auto *part: parts) {
            physics::applyForce(part->body, math::Vec3{0.0f, -9.81f, 0.0f} * part->body.mass);
            physics::updateLinearAcceleration(part->body);
            physics::updateAngularAcceleration(part->body);
            const float previousY = part->body.position.y;
            physics::integrateLinearMotion(part->body, dt);
            diagnosticHistory.gravityWork -= static_cast<double>(part->body.mass) * 9.81 *
                    (part->body.position.y - previousY);
            physics::integrateAngularMotion(part->body, dt);
        }
        if (trace) trace("integration", "");
        double stageEnergy = bodyEnergy(parts);
        diagnosticHistory.integrationEnergy += stageEnergy - energyBeforeIntegration;
        for (int iteration = 0; iteration < jointIterations; ++iteration) {
            for (auto *part: parts) {
                const bool isLeftFoot = part == &auraBody.leftFoot;
                const bool isRightFoot = part == &auraBody.rightFoot;
                physics::FloorCollisionAudit audit;
                if (part == &auraBody.rightShin)
                    body::resolveRightShinFloorWithFootTranslation(auraBody, dt / jointIterations, floorFriction);
                else {
                    physics::resolveFloorCollision(part->body, part->size, part->floorShape,
                                                   0.0f, dt / jointIterations,
                                                   (isLeftFoot || isRightFoot) ? &audit : nullptr, floorFriction);
                    if (isLeftFoot) {
                        for (const auto &impulse: audit.normalImpulses)
                            diagnosticHistory.leftFootNormalImpulse += impulse.y;
                    }
                    if (isRightFoot) {
                        for (const auto &impulse: audit.normalImpulses)
                            diagnosticHistory.rightFootNormalImpulse += impulse.y;
                    }
                }
            }
            if (trace) trace("floor", "");
            const double afterFloor = bodyEnergy(parts);
            diagnosticHistory.floorEnergy += afterFloor - stageEnergy;
            for (auto &joint: joints) {
                solveJoint(joint);
                if (trace) trace("joint", joint.name);
            }
            // The forward sweep already visited the last connection.
            for (int i = static_cast<int>(joints.size()) - 2; i >= 0; --i) {
                solveJoint(joints[i]);
                if (trace) trace("joint", joints[i].name);
            }
            const auto beforeSelf = physicalGround ? body::wholeBodyCenterOfMass(auraBody) : std::array<double, 3>{};
            body::solveBodySelfCollisions(auraBody, skeleton, bodyPairs, room, 4, .01f, .001f, physicalGround);
            if (physicalGround) body::recenterHorizontallyWithinRoom(auraBody, beforeSelf, room);
            if (trace) trace("self_collision", "");
            stageEnergy = bodyEnergy(parts);
            diagnosticHistory.jointEnergy += stageEnergy - afterFloor;
            // Room contacts have the last word on containment; later joint
            // geometry must not leave a body outside the rendered enclosure.
            physics::resolveRoomCollisions(finalRoomBodies, room, floorFriction, dt / jointIterations);
            if (physicalGround) {
                for (const auto &impulse: leftFinalAudit.normalImpulses)
                    diagnosticHistory.leftFootNormalImpulse += impulse.y;
                for (const auto &impulse: rightFinalAudit.normalImpulses)
                    diagnosticHistory.rightFootNormalImpulse += impulse.y;
            }
            if (trace) trace("room", "");
            const double afterRoom = bodyEnergy(parts);
            diagnosticHistory.roomEnergy += afterRoom - stageEnergy;
            stageEnergy = afterRoom;
        }
        for (auto *part: parts) {
            physics::clearForce(part->body);
            physics::clearTorque(part->body);
        }
    }
}
