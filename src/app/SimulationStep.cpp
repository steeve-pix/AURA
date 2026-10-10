#include "SimulationStep.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointSolver.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Motion.hpp"

namespace aura::app {
    void stepSimulation(body::AuraBody3D& auraBody, const body::AuraSkeleton3D& skeleton,
        std::span<BodyJoint> joints, std::span<body::BodyPart3D* const> parts,
        const std::vector<body::BodyCollisionPair>& bodyPairs,
        std::span<const physics::RoomBody> roomBodies, const physics::RoomBounds& room,
        BodyDiagnosticHistory& diagnosticHistory) {
        const auto dt = static_cast<float>(FIXED_DT);
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
        double stageEnergy = bodyEnergy(parts);
        diagnosticHistory.integrationEnergy += stageEnergy - energyBeforeIntegration;
        for (int iteration = 0; iteration < jointIterations; ++iteration) {
            for (auto *part: parts) {
                if (part == &auraBody.rightShin)
                    body::resolveRightShinFloorWithFootTranslation(auraBody, dt / jointIterations);
                else
                    physics::resolveFloorCollision(part->body, part->size, 0.0f, dt / jointIterations);
            }
            const double afterFloor = bodyEnergy(parts);
            diagnosticHistory.floorEnergy += afterFloor - stageEnergy;
            for (auto& joint : joints)
                body::solveBodyJoint(auraBody, skeleton, *joint.partA, *joint.partB, joint.constraint);
            // The forward sweep already visited the last connection.
            for (int i = static_cast<int>(joints.size()) - 2; i >= 0; --i)
                body::solveBodyJoint(auraBody, skeleton, *joints[i].partA, *joints[i].partB, joints[i].constraint);
            body::solveBodySelfCollisions(auraBody, skeleton, bodyPairs, room);
            stageEnergy = bodyEnergy(parts);
            diagnosticHistory.jointEnergy += stageEnergy - afterFloor;
            // Room contacts have the last word on containment; later joint
            // geometry must not leave a body outside the rendered enclosure.
            physics::resolveRoomCollisions(roomBodies, room);
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
