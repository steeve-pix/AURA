#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include "aura/sim/AuraSimulation.hpp"

#include "../app/SimulationStep.hpp"
#include "aura/body/BodySelfCollision.hpp"
#include "aura/body/ComponentMass.hpp"
#include "aura/physics/SupportPolygon.hpp"

namespace aura::sim {
    namespace {
        BalanceMeasurements balanceMeasurements(const AuraObservation &obs, float pelvisReference,
                                                float torsoReference, float comReference) {
            const auto &torso = *std::find_if(obs.bodyPartStates.begin(), obs.bodyPartStates.end(),
                                              [](const auto &part) { return part.name == "torso"; });
            const auto &head = *std::find_if(obs.bodyPartStates.begin(), obs.bodyPartStates.end(),
                                             [](const auto &part) { return part.name == "head"; });
            return {
                .pelvisUp = obs.pelvisUp.y, .torsoUp = torso.up.y,
                .pelvisHeightRatio = (obs.pelvisPosition.y - obs.floorY) / pelvisReference,
                .torsoHeightRatio = (torso.position.y - obs.floorY) / torsoReference,
                .comHeightRatio = (obs.centerOfMassPosition.y - obs.floorY) / comReference,
                .comVelocity = obs.centerOfMassVelocity,
                .pelvisAngularSpeed = obs.pelvisAngularVelocity.length(),
                .torsoAngularSpeed = torso.angularVelocity.length(),
                .leftFootContact = obs.leftFootGrounded, .rightFootContact = obs.rightFootGrounded,
                .supportPolygonValid = obs.supportPolygon.vertices.size() >= 3, .supportMargin = obs.supportMargin,
                .torsoContact = !torso.geometricFloorContactPoints.empty(),
                .headContact = !head.geometricFloorContactPoints.empty()
            };
        }
    }

    AuraSimulation::AuraSimulation(BalanceConfig balanceConfig, TerminationMode terminationMode,
                                   physics::FloorFrictionModel floorFriction)
        : auraBody(body::createAuraBody3D()), skeleton(body::createAuraSkeleton3D(auraBody)),
          joints(app::makeBodyJoints(auraBody, skeleton)), parts(body::collisionParts(auraBody)),
          bodyPairs(body::bodyCollisionPairs(auraBody, skeleton, 3)), balanceClassifier(balanceConfig),
          selectedTerminationMode(terminationMode), selectedFloorFriction(floorFriction) {
        (void) terminationModeName(selectedTerminationMode);
        standingPelvisHeight = auraBody.pelvis.body.position.y - room.minimum.y;
        standingTorsoHeight = auraBody.torso.body.position.y - room.minimum.y;
        standingComHeight = body::componentCenterOfMass(parts).y - room.minimum.y;
        for (std::size_t actionIndex = 0; actionIndex < actionSpecs.size(); ++actionIndex) {
            const auto &spec = actionSpecs[actionIndex];
            const auto joint = std::find_if(joints.begin(), joints.end(), [&](const auto &entry) {
                return std::string_view(entry.name) == spec.jointName;
            });
            if (joint == joints.end()) throw std::logic_error("Action references an unknown joint");
            if (spec.jointMinimum(joint->constraint) != static_cast<float>(spec.minimum) ||
                spec.jointMaximum(joint->constraint) != static_cast<float>(spec.maximum))
                throw std::logic_error("Action limits disagree with the physical joint");
            actionJointIndices[actionIndex] = static_cast<std::size_t>(joint - joints.begin());
        }
        body::configureBodyCollisionJoints(skeleton);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            roomBodies[i] = {&parts[i]->body, parts[i]->size, parts[i]->floorShape};
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
        for (std::size_t i = 0; i < actionSpecs.size(); ++i)
            actionSpecs[i].jointTarget(joints[actionJointIndices[i]].constraint) =
                    currentAction.*(actionSpecs[i].target);
        app::stepSimulation(auraBody, skeleton, joints, parts, bodyPairs, roomBodies, room, diagnosticHistory,
                            selectedFloorFriction);
        simulatedTime += app::FIXED_DT;
        ++stepCount;
        balanceClassifier.update(balanceMeasurements(observation(), standingPelvisHeight,
                                                     standingTorsoHeight, standingComHeight), app::FIXED_DT);
    }

    void AuraSimulation::reset() {
        auraBody = body::createAuraBody3D();
        parts = body::collisionParts(auraBody);
        skeleton = body::createAuraSkeleton3D(auraBody);
        body::configureBodyCollisionJoints(skeleton);
        bodyPairs = body::bodyCollisionPairs(auraBody, skeleton, 3);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            roomBodies[i] = {&parts[i]->body, parts[i]->size, parts[i]->floorShape};
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
        balanceClassifier.reset();
    }

    void AuraSimulation::stepArenaPhysics() {
        for (std::size_t i = 0; i < actionSpecs.size(); ++i)
            actionSpecs[i].jointTarget(joints[actionJointIndices[i]].constraint) = currentAction.*(actionSpecs[i].target);
        app::stepSimulation(auraBody, skeleton, joints, parts, bodyPairs, roomBodies, room, diagnosticHistory,
                            selectedFloorFriction);
        simulatedTime += app::FIXED_DT;
        ++stepCount;
    }

    void AuraSimulation::classifyArenaTick() {
        balanceClassifier.update(balanceMeasurements(observation(), standingPelvisHeight,
                                                     standingTorsoHeight, standingComHeight), app::FIXED_DT);
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
        AuraObservation obs{
            pelvisPosition(),
            pelvisVelocity(),
            pelvisOrientation(),
            pelvisAngularVelocity(),
            auraBody.leftFoot.body.position,
            auraBody.rightFoot.body.position,
            auraBody.pelvis.body.orientation.rotate(math::Vec3{0.0f, 1.0f, 0.0f})
        };
        obs.balance = balanceClassifier.assessment();
        obs.neutralHeightReferences = {standingPelvisHeight, standingTorsoHeight, standingComHeight};

        obs.centerOfMassPosition = body::componentCenterOfMass(parts);
        obs.centerOfMassVelocity = body::componentCenterOfMassVelocity(parts);


        constexpr float contactTolerance = diagnosticFloorContactTolerance;
        obs.floorY = room.minimum.y;

        obs.leftFootContactPoints = physics::floorContactPoints(auraBody.leftFoot.body, auraBody.leftFoot.size,
                                                                auraBody.leftFoot.floorShape, room.minimum.y,
                                                                contactTolerance);

        obs.rightFootContactPoints = floorContactPoints(auraBody.rightFoot.body, auraBody.rightFoot.size,
                                                        auraBody.rightFoot.floorShape, room.minimum.y,
                                                        contactTolerance);

        std::vector<physics::GroundPoint> groundPoints;
        groundPoints.reserve(obs.leftFootContactPoints.size() + obs.rightFootContactPoints.size());

        for (const auto &point: obs.leftFootContactPoints) {
            groundPoints.push_back(physics::projectToGround(point));
        }

        for (const auto &point: obs.rightFootContactPoints) {
            groundPoints.push_back(physics::projectToGround(point));
        }

        obs.supportPolygon = physics::convexHull(groundPoints);
        const auto projectedCOM = physics::projectToGround(obs.centerOfMassPosition);
        obs.centerOfMassInsideSupportPolygon =
                physics::containsPoint(obs.supportPolygon, projectedCOM);
        obs.supportMargin = physics::supportMargin(obs.supportPolygon, projectedCOM);

        obs.leftFootGrounded = !obs.leftFootContactPoints.empty();

        obs.rightFootGrounded = !obs.rightFootContactPoints.empty();

        obs.leftFootVelocity = auraBody.leftFoot.body.velocity;
        obs.rightFootVelocity = auraBody.rightFoot.body.velocity;


        obs.bodyPartStates.reserve(parts.size());

        for (const auto *part: parts) {
            // Reuse the exact foot samples already used by support geometry.
            auto contacts = part == &auraBody.leftFoot
                                ? obs.leftFootContactPoints
                                : part == &auraBody.rightFoot
                                      ? obs.rightFootContactPoints
                                      : physics::floorContactPoints(part->body, part->size, part->floorShape,
                                                                    obs.floorY, contactTolerance);
            obs.bodyPartStates.push_back({
                .name = part->name,
                .velocity = part->body.velocity,
                .position = part->body.position,
                .orientation = part->body.orientation,
                .up = part->body.orientation.rotate({0.0f, 1.0f, 0.0f}),
                .angularVelocity = part->body.angularVelocity,
                .floorShape = part->floorShape,
                .floorClearance = physics::lowestPoint(part->body, part->size, part->floorShape).y - obs.floorY,
                .geometricFloorContactPoints = std::move(contacts)
            });
        }

        obs.jointStates.reserve(joints.size());

        for (const auto &joint: joints) {
            const auto &parent = joint.partA->body;
            const auto &child = joint.partB->body;

            const auto parentInverse = parent.orientation.conjugate();

            const auto relativeOrientation =
                    parentInverse * child.orientation;

            const auto relativeAngularVelocity =
                    parentInverse.rotate(child.angularVelocity - parent.angularVelocity);

            obs.jointStates.push_back({
                .name = joint.name,
                .relativeOrientation = relativeOrientation,
                .relativeAngularVelocity = relativeAngularVelocity,
                .worldAnchorA = body::localToWorldPoint(*joint.partA, joint.constraint.localAnchorA),
                .worldAnchorB = body::localToWorldPoint(*joint.partB, joint.constraint.localAnchorB)
            });
        }

        return obs;
    }

    void AuraSimulation::setAction(const AuraAction &action) {
        auto clamped = action;
        for (const auto &spec: actionSpecs) {
            auto &target = clamped.*(spec.target);
            if (!std::isfinite(target)) throw std::invalid_argument("Joint targets must be finite");
            target = std::clamp(target, static_cast<float>(spec.minimum), static_cast<float>(spec.maximum));
        }
        currentAction = clamped;
    }

    void AuraSimulation::setMotorEnabled(std::string_view jointName, bool enabled) {
        const auto joint = std::find_if(joints.begin(), joints.end(), [&](const auto &entry) {
            return std::string_view(entry.name) == jointName;
        });
        if (joint == joints.end()) throw std::invalid_argument("Unknown motor joint");
        joint->motor = enabled;
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
        return terminationStatus().done();
    }

    TerminationStatus AuraSimulation::terminationStatus() const {
        return evaluateTermination(selectedTerminationMode, isStanding(), balanceClassifier.assessment().state,
                                   stepCount, MAX_EPISODE_STEPS);
    }
}
