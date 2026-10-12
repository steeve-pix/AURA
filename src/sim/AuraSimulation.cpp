#include <algorithm>
#include "aura/sim/AuraSimulation.hpp"

#include "../app/SimulationStep.hpp"
#include "aura/body/BodySelfCollision.hpp"

namespace aura::sim {
    AuraSimulation::AuraSimulation()
        : auraBody(body::createAuraBody3D()), skeleton(body::createAuraSkeleton3D(auraBody)),
          joints(app::makeBodyJoints(auraBody, skeleton)), parts(body::collisionParts(auraBody)),
          bodyPairs(body::bodyCollisionPairs(auraBody, skeleton, 3)) {
        body::configureBodyCollisionJoints(skeleton);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            roomBodies[i] = {&parts[i]->body, parts[i]->size};
        }
        // Start above the floor so gravity is visible immediately.
        for (auto *part: parts) part->body.position.y += 0.6f;
        initialPelvisHeight = auraBody.pelvis.body.position.y;

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
        joints[9].constraint.targetAngle = currentAction.leftHipTarget;
        joints[12].constraint.targetAngle = currentAction.rightHipTarget;
        joints[10].constraint.targetAngle = currentAction.leftKneeTarget;
        joints[13].constraint.targetAngle = currentAction.rightKneeTarget;
        app::stepSimulation(auraBody, skeleton, joints, parts, bodyPairs, roomBodies, room, diagnosticHistory);
        simulatedTime += app::FIXED_DT;
        ++stepCount;
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
        initialPelvisHeight = auraBody.pelvis.body.position.y;
        diagnosticHistory = app::BodyDiagnosticHistory{};
        diagnosticHistory.initialEnergy = app::bodyEnergy(parts);
        simulatedTime = 0.0;
        stepCount = 0.0;
        currentAction = AuraAction{};
    }

    math::Vec3 AuraSimulation::pelvisPosition() const {
        return auraBody.pelvis.body.position;
    }

    math::Vec3 AuraSimulation::pelvisVelocity() const {
        return auraBody.pelvis.body.velocity;
    }

    math::Quaternion AuraSimulation::pelvisOrientation() const {
        return auraBody.pelvis.body.orientation;
    }

    math::Vec3 AuraSimulation::pelvisAngularVelocity() const {
        return auraBody.pelvis.body.angularVelocity;
    }

    AuraObservation AuraSimulation::observation() const {
        return {
            pelvisPosition(),
            pelvisVelocity(),
            pelvisOrientation(),
            pelvisAngularVelocity(),
            auraBody.leftFoot.body.position,
            auraBody.rightFoot.body.position,
            auraBody.pelvis.body.orientation.rotate(math::Vec3{0.0f, 1.0f, 0.0f})
        };
    }

    void AuraSimulation::setAction(const AuraAction &action) {
        currentAction = action;

        currentAction.leftHipTarget =
                std::clamp(currentAction.leftHipTarget, -0.8f, 0.8f);

        currentAction.rightHipTarget =
                std::clamp(currentAction.rightHipTarget, -0.8f, 0.8f);

        currentAction.leftKneeTarget =
                std::clamp(currentAction.leftKneeTarget, 0.0f, 2.2f);

        currentAction.rightKneeTarget =
                std::clamp(currentAction.rightKneeTarget, 0.0f, 2.2f);
    }

    void AuraSimulation::step(const AuraAction &action) {
        setAction(action);
        step();
    }

    const AuraAction &AuraSimulation::action() const {
        return currentAction;
    }

    float AuraSimulation::pelvisHeight() const {
        return auraBody.pelvis.body.position.y;
    }

    bool AuraSimulation::isStanding() const {
        return pelvisHeight() > 0.5f * initialPelvisHeight
               && pelvisUprightness() > 0.5f;
    }

    float AuraSimulation::pelvisUprightness() const {
        const auto up =
                auraBody.pelvis.body.orientation.rotate(math::Vec3{0.0f, 1.0f, 0.0});

        return up.y;
    }

    std::size_t AuraSimulation::steps() const {
        return stepCount;
    }

    bool AuraSimulation::isTruncated() const {
        return stepCount >= MAX_EPISODE_STEPS;
    }

    bool AuraSimulation::isDone() const {
        return !isStanding() || isTruncated();
    }
}
