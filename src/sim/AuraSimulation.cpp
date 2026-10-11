#include "aura/sim/AuraSimulation.hpp"

#include "../app/SimulationStep.hpp"
#include "aura/body/BodySelfCollision.hpp"

namespace aura::sim {
    AuraSimulation::AuraSimulation()
        : auraBody(body::createAuraBody3D()), skeleton(body::createAuraSkeleton3D(auraBody)),
          joints(app::makeBodyJoints(auraBody, skeleton)), parts(body::collisionParts(auraBody)),
          bodyPairs(body::bodyCollisionPairs(auraBody, skeleton, 3)) {
        for (std::size_t i = 0; i < parts.size(); ++i) {
            roomBodies[i] = {&parts[i]->body, parts[i]->size};
        }
        // Start above the floor so gravity is visible immediately.
        for (auto *part: parts) part->body.position.y += 0.6f;

        diagnosticHistory.initialEnergy = app::bodyEnergy(parts);
    }

    body::AuraBody3D &AuraSimulation::body() {
        return auraBody;
    }

    body::AuraSkeleton3D &AuraSimulation::getSkeleton() {
        return skeleton;
    }

    std::array<app::BodyJoint, 15> &AuraSimulation::getJoints() {
        return joints;
    }

    std::array<body::BodyPart3D *, 16> &AuraSimulation::getParts() {
        return parts;
    }

    const std::vector<body::BodyCollisionPair> &AuraSimulation::getBodyPairs() const {
        return bodyPairs;
    }

    const physics::RoomBounds &AuraSimulation::getRoom() const {
        return room;
    }

    std::array<physics::RoomBody, 16> &AuraSimulation::getRoomBodies() {
        return roomBodies;
    }

    app::BodyDiagnosticHistory &AuraSimulation::getDiagnosticHistory() {
        return diagnosticHistory;
    }

    double AuraSimulation::time() const {
        return simulatedTime;
    }

    void AuraSimulation::step() {
        app::stepSimulation(auraBody, skeleton, joints, parts, bodyPairs, roomBodies, room, diagnosticHistory);
        simulatedTime += app::FIXED_DT;
    }

    void AuraSimulation::reset() {
        auraBody = body::createAuraBody3D();
        parts = body::collisionParts(auraBody);
        skeleton = body::createAuraSkeleton3D(auraBody);
        body::configureBodyCollisionJoints(skeleton);
        bodyPairs = body::bodyCollisionPairs(auraBody, skeleton, 3);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            roomBodies[i] = {&parts[i]->body, parts[i]->size};
        }
        for (auto *part: parts) {
            part->body.position.y += 0.6f;
        }
        diagnosticHistory = app::BodyDiagnosticHistory{};
        diagnosticHistory.initialEnergy = app::bodyEnergy(parts);
        simulatedTime = 0.0;
    }
}
