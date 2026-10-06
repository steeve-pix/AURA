// Headless measurement tool, not a passing stability regression.
// Keep setup and step ordering aligned with src/main.cpp; only upper-body motor flags differ.
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>

#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Motion.hpp"

namespace {
    struct BodyJoint {
        const char *name{};
        aura::body::BodyPart3D *partA{};
        aura::body::BodyPart3D *partB{};
        aura::body::Joint3D &constraint;
        bool motor = false;
        bool floorAware = false;
        float markerRadius = 0.0f;
    };


    struct JointMeasurements {
        float maxGap = 0.0f;
        float maxOvershoot = 0.0f;
        float peakRelativeOmega = 0.0f;
        float peakHingeOmega = 0.0f;
        double firstGapFailure = std::numeric_limits<double>::infinity();
        double firstLimitFailure = std::numeric_limits<double>::infinity();
        double firstInvalidState = std::numeric_limits<double>::infinity();

        double firstFailure() const {
            return std::min({firstGapFailure, firstLimitFailure, firstInvalidState});
        }
    };

    std::string timeText(double time) {
        if (!std::isfinite(time)) return "never";
        std::ostringstream text;
        text << std::fixed << std::setprecision(6) << time;
        return text.str();
    }
}

int main(int argc, char **argv) {
    bool contactWindow = false;
    bool headWindow = false;
    bool ankleWindow = false;
    bool wristWindow = false;
    bool handFloorReplay = false;
    bool handContactCycles = false;
    bool handContactCyclesDone = false;
    bool handPositionStages = false;
    bool handPositionStagesDone = false;
    bool pinnedWristReplay = false;
    bool pinnedWristReplayDone = false;
    bool pinnedElbowReplay = false;
    bool pinnedShoulderReplay = false;
    bool shoulderComponentReplay = false;
    bool shoulderComponentRotationReplay = false;
    bool shoulderRotationFeasibility = false;
    bool shoulderCapacityComparison = false;
    bool shoulderLimitReplay = false;
    bool rightElbowSubtree = false;
    bool rightKneeSubtree = false;
    bool rightShinFloorSubtree = false;
    bool rightShinFloorMotion = false;
    bool shinContactAudit = false;
    int auditedShinContacts = 0;
    bool shinFloorReplay = false;
    bool headPivot = false;
    bool headLeafPosition = false;
    bool neckSubtree = false;
    bool leftShoulderSubtree = false;
    bool majorBranches = false;
    bool waistGroups = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--contact-window") contactWindow = true;
        else if (argument == "--waist-groups") waistGroups = true;
        else if (argument == "--head-window") headWindow = true;
        else if (argument == "--ankle-window") ankleWindow = true;
        else if (argument == "--wrist-window") wristWindow = true;
        else if (argument == "--hand-floor-replay") handFloorReplay = true;
        else if (argument == "--hand-contact-cycles") handContactCycles = true;
        else if (argument == "--hand-position-stages") handPositionStages = true;
        else if (argument == "--pinned-wrist-replay") pinnedWristReplay = true;
        else if (argument == "--pinned-elbow-replay") pinnedElbowReplay = true;
        else if (argument == "--pinned-shoulder-replay") pinnedShoulderReplay = true;
        else if (argument == "--shoulder-component-replay") shoulderComponentReplay = true;
        else if (argument == "--shoulder-component-rotation-replay") shoulderComponentRotationReplay = true;
        else if (argument == "--shoulder-rotation-feasibility") shoulderRotationFeasibility = true;
        else if (argument == "--shoulder-capacity-comparison") shoulderCapacityComparison = true;
        else if (argument == "--shoulder-limit-replay") shoulderLimitReplay = true;
        else if (argument == "--right-elbow-subtree") rightElbowSubtree = true;
        else if (argument == "--right-knee-subtree") rightKneeSubtree = true;
        else if (argument == "--right-shin-floor-subtree") rightShinFloorSubtree = true;
        else if (argument == "--right-shin-floor-motion") rightShinFloorMotion = true;
        else if (argument == "--shin-contact-audit") shinContactAudit = true;
        else if (argument == "--shin-floor-replay") shinFloorReplay = true;
        else if (argument == "--head-pivot") headPivot = true;
        else if (argument == "--head-leaf-position") headLeafPosition = true;
        else if (argument == "--neck-subtree") neckSubtree = true;
        else if (argument == "--left-shoulder-subtree") leftShoulderSubtree = true;
        else if (argument == "--major-branches") majorBranches = true;
        else {
            std::cerr << "Usage: aura_full_body_diagnostics [--contact-window | --head-window | --ankle-window | --wrist-window] [--waist-groups] [--head-pivot] [--head-leaf-position] [--neck-subtree] [--left-shoulder-subtree] [--major-branches] [--right-knee-subtree] [--right-elbow-subtree] [--right-shin-floor-subtree] [--right-shin-floor-motion] [--shin-floor-replay] [--shin-contact-audit] [--hand-floor-replay] [--hand-contact-cycles] [--hand-position-stages] [--pinned-wrist-replay] [--pinned-elbow-replay] [--pinned-shoulder-replay] [--shoulder-component-replay] [--shoulder-component-rotation-replay] [--shoulder-rotation-feasibility] [--shoulder-capacity-comparison] [--shoulder-limit-replay]\n";
            return 2;
        }
    }
    if (shinFloorReplay) ankleWindow = true;
    if (shoulderCapacityComparison) shoulderRotationFeasibility = true;
    if (shoulderComponentRotationReplay || shoulderRotationFeasibility || shoulderLimitReplay) shoulderComponentReplay = true;
    if (pinnedShoulderReplay || shoulderComponentReplay) pinnedElbowReplay = true;
    if (pinnedElbowReplay) pinnedWristReplay = true;
    if (handFloorReplay || handContactCycles || handPositionStages || pinnedWristReplay) wristWindow = true;
    if (static_cast<int>(contactWindow) + static_cast<int>(headWindow) + static_cast<int>(ankleWindow) + static_cast<int>(wristWindow) > 1) {
        std::cerr << "Choose one diagnostic window.\n";
        return 2;
    }
    // This experiment measures the waist-group baseline, never the original pairwise waist.
    if (headWindow) waistGroups = true;
    if (shinContactAudit || wristWindow || rightElbowSubtree) rightShinFloorMotion = true;
    if (rightShinFloorMotion) rightShinFloorSubtree = true;
    if (rightShinFloorSubtree) rightKneeSubtree = true;
    if (ankleWindow || rightKneeSubtree) majorBranches = true;
    if (majorBranches) leftShoulderSubtree = true;
    if (leftShoulderSubtree) neckSubtree = true;
    if (neckSubtree) { waistGroups = true; headLeafPosition = true; }
    if (headLeafPosition) headPivot = true;
    auto auraBody = aura::body::createAuraBody3D();
    const std::array<aura::body::BodyPart3D *, 16> parts{
        &auraBody.head, &auraBody.neck, &auraBody.torso, &auraBody.pelvis,
        &auraBody.leftUpperArm, &auraBody.leftForearm, &auraBody.leftHand,
        &auraBody.rightUpperArm, &auraBody.rightForearm, &auraBody.rightHand,
        &auraBody.leftThigh, &auraBody.leftShin, &auraBody.leftFoot,
        &auraBody.rightThigh, &auraBody.rightShin, &auraBody.rightFoot
    };
    // Start above the floor so gravity is visible immediately.
    for (auto *part: parts) part->body.position.y += 0.6f;

    auto skeleton = aura::body::createAuraSkeleton3D(auraBody);

    // Ordered connections refer to the skeleton's joints; flags and radii belong to this demo.
    // Root outward: spine/head, left arm, right arm, left leg, right leg.
    std::array<BodyJoint, 15> joints{
        {
            {"waist", &auraBody.torso, &auraBody.pelvis, skeleton.waist, false, false, 0.0f},
            {"neck", &auraBody.torso, &auraBody.neck, skeleton.neck, false, false, 0.0f},
            {"head", &auraBody.neck, &auraBody.head, skeleton.head, false, false, 0.0f},
            {"leftShoulder", &auraBody.torso, &auraBody.leftUpperArm, skeleton.leftShoulder, false, false, 0.145f},
            {"leftElbow", &auraBody.leftUpperArm, &auraBody.leftForearm, skeleton.leftElbow, false, false, 0.14f},
            {"leftWrist", &auraBody.leftForearm, &auraBody.leftHand, skeleton.leftWrist, false, false, 0.12f},
            {"rightShoulder", &auraBody.torso, &auraBody.rightUpperArm, skeleton.rightShoulder, false, false, 0.145f},
            {"rightElbow", &auraBody.rightUpperArm, &auraBody.rightForearm, skeleton.rightElbow, false, false, 0.14f},
            {"rightWrist", &auraBody.rightForearm, &auraBody.rightHand, skeleton.rightWrist, false, false, 0.12f},
            {"leftHip", &auraBody.pelvis, &auraBody.leftThigh, skeleton.leftHip, true, false, 0.16f},
            {"leftKnee", &auraBody.leftThigh, &auraBody.leftShin, skeleton.leftKnee, true, false, 0.14f},
            {"leftAnkle", &auraBody.leftShin, &auraBody.leftFoot, skeleton.leftAnkle, false, true, 0.12f},
            {"rightHip", &auraBody.pelvis, &auraBody.rightThigh, skeleton.rightHip, true, false, 0.16f},
            {"rightKnee", &auraBody.rightThigh, &auraBody.rightShin, skeleton.rightKnee, true, false, 0.14f},
            {"rightAnkle", &auraBody.rightShin, &auraBody.rightFoot, skeleton.rightAnkle, false, true, 0.12f},
        }
    };

    constexpr int jointIterations = 16;
    constexpr double FIXED_DT = 1.0 / 120.0;
    double diagnosticTime = 0.0;
    int diagnosticIteration = 0;
    const char *diagnosticSweep = "forward";
    const auto headSample = [&](const char *stage) {
        const auto &head = auraBody.head;
        const auto &neck = auraBody.neck;
        const auto &joint = skeleton.head;
        const auto axis = neck.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();
        const float omega = (head.body.angularVelocity - neck.body.angularVelocity).dot(axis);
        const float headGap = (aura::body::localToWorldPoint(head, joint.localAnchorB) -
                               aura::body::localToWorldPoint(neck, joint.localAnchorA)).length();
        const float neckGap = (aura::body::localToWorldPoint(neck, skeleton.neck.localAnchorB) -
                               aura::body::localToWorldPoint(auraBody.torso, skeleton.neck.localAnchorA)).length();
        const auto worldHead = aura::body::localToWorldPoint(head, joint.localAnchorB);
        const auto worldNeck = aura::body::localToWorldPoint(neck, joint.localAnchorA);
        const auto anchorRelativeVelocity = aura::physics::velocityAtWorldPoint(head.body, worldHead) -
                                            aura::physics::velocityAtWorldPoint(neck.body, worldNeck);
        std::cout << "head," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep
                  << ',' << stage << ',' << aura::body::relativeJointAngle(neck, head, joint)
                  << ',' << omega << ',' << headGap << ',' << neckGap
                  << ',' << head.body.angularVelocity.length() << ',' << neck.body.angularVelocity.length()
                  << ',' << anchorRelativeVelocity.x << ',' << anchorRelativeVelocity.y << ',' << anchorRelativeVelocity.z
                  << ',' << anchorRelativeVelocity.length() << '\n';
    };
    const auto neckSample = [&](const char *stage) {
        const auto neckAnchor = aura::body::localToWorldPoint(auraBody.neck, skeleton.neck.localAnchorB);
        const auto torsoAnchor = aura::body::localToWorldPoint(auraBody.torso, skeleton.neck.localAnchorA);
        const auto headAnchor = aura::body::localToWorldPoint(auraBody.head, skeleton.head.localAnchorB);
        const auto neckHeadAnchor = aura::body::localToWorldPoint(auraBody.neck, skeleton.head.localAnchorA);
        const auto neckRelativeVelocity = aura::physics::velocityAtWorldPoint(auraBody.neck.body, neckAnchor) -
                                          aura::physics::velocityAtWorldPoint(auraBody.torso.body, torsoAnchor);
        const auto headRelativeVelocity = aura::physics::velocityAtWorldPoint(auraBody.head.body, headAnchor) -
                                          aura::physics::velocityAtWorldPoint(auraBody.neck.body, neckHeadAnchor);
        const auto headRelativeOmega = auraBody.head.body.angularVelocity - auraBody.neck.body.angularVelocity;
        std::cout << "neck," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep << ',' << stage
                  << ',' << aura::body::relativeJointAngle(auraBody.torso, auraBody.neck, skeleton.neck)
                  << ',' << (neckAnchor - torsoAnchor).length() << ',' << (headAnchor - neckHeadAnchor).length()
                  << ',' << aura::body::relativeJointAngle(auraBody.neck, auraBody.head, skeleton.head)
                  << ',' << neckRelativeVelocity.x << ',' << neckRelativeVelocity.y << ',' << neckRelativeVelocity.z
                  << ',' << neckRelativeVelocity.length()
                  << ',' << headRelativeVelocity.x << ',' << headRelativeVelocity.y << ',' << headRelativeVelocity.z
                  << ',' << headRelativeVelocity.length()
                  << ',' << headRelativeOmega.x << ',' << headRelativeOmega.y << ',' << headRelativeOmega.z
                  << ',' << headRelativeOmega.length() << '\n';
    };
    const auto shoulderSample = [&](const char *stage) {
        const auto snapshot = [&](const auto &a, const auto &b, const auto &joint) {
            const auto worldA = aura::body::localToWorldPoint(a, joint.localAnchorA);
            const auto worldB = aura::body::localToWorldPoint(b, joint.localAnchorB);
            const auto relativeVelocity = aura::physics::velocityAtWorldPoint(b.body, worldB) -
                                          aura::physics::velocityAtWorldPoint(a.body, worldA);
            const auto relativeOmega = b.body.angularVelocity - a.body.angularVelocity;
            return std::array<float, 10>{(worldB - worldA).length(), aura::body::relativeJointAngle(a, b, joint),
                relativeVelocity.length(), relativeOmega.length(), relativeVelocity.x, relativeVelocity.y,
                relativeVelocity.z, relativeOmega.x, relativeOmega.y, relativeOmega.z};
        };
        const auto shoulder = snapshot(auraBody.torso, auraBody.leftUpperArm, skeleton.leftShoulder);
        const auto elbow = snapshot(auraBody.leftUpperArm, auraBody.leftForearm, skeleton.leftElbow);
        const auto wrist = snapshot(auraBody.leftForearm, auraBody.leftHand, skeleton.leftWrist);
        std::cout << "leftShoulder," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep << ',' << stage;
        for (const auto &values: {shoulder, elbow, wrist}) {
            for (float value: values) std::cout << ',' << value;
        }
        std::cout << '\n';
    };
    const auto tracingAnkle = [&] {
        return ankleWindow && diagnosticTime >= 1.25 - 1e-9 && diagnosticTime <= 1.30 + 1e-9;
    };
    const auto ankleSample = [&](const char *stage, const aura::body::AuraBody3D *state = nullptr,
                                 const char *label = "rightAnkle") {
        const auto &snapshot = state == nullptr ? auraBody : *state;
        const auto &shin = snapshot.rightShin;
        const auto &foot = snapshot.rightFoot;
        const auto &joint = skeleton.rightAnkle;
        const auto worldA = aura::body::localToWorldPoint(shin, joint.localAnchorA);
        const auto worldB = aura::body::localToWorldPoint(foot, joint.localAnchorB);
        const auto axis = shin.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();
        const auto relativeVelocity = aura::physics::velocityAtWorldPoint(foot.body, worldB) -
                                      aura::physics::velocityAtWorldPoint(shin.body, worldA);
        std::cout << label << ',' << diagnosticTime << ',' << diagnosticIteration << ',' << stage
                  << ',' << (worldB - worldA).length()
                  << ',' << aura::body::relativeJointAngle(shin, foot, joint)
                  << ',' << (foot.body.angularVelocity - shin.body.angularVelocity).dot(axis)
                  << ',' << relativeVelocity.length()
                  << ',' << aura::physics::lowestPoint(foot.body, foot.size).y
                  << ',' << aura::physics::floorContactPoints(foot.body, foot.size, 0.0f, 0.01f).size()
                  << ',' << shin.body.velocity.length() << ',' << shin.body.angularVelocity.length()
                  << ',' << foot.body.velocity.length() << ',' << foot.body.angularVelocity.length()
                  << ',' << (foot.body.angularVelocity - shin.body.angularVelocity).x
                  << ',' << (foot.body.angularVelocity - shin.body.angularVelocity).y
                  << ',' << (foot.body.angularVelocity - shin.body.angularVelocity).z << '\n';
    };
    const auto kneeSample = [&](const char *stage) {
        const auto &shin = auraBody.rightShin;
        const auto &foot = auraBody.rightFoot;
        const auto &joint = skeleton.rightAnkle;
        const auto worldA = aura::body::localToWorldPoint(shin, joint.localAnchorA);
        const auto worldB = aura::body::localToWorldPoint(foot, joint.localAnchorB);
        const auto relativeVelocity = aura::physics::velocityAtWorldPoint(foot.body, worldB) -
                                      aura::physics::velocityAtWorldPoint(shin.body, worldA);
        const auto relativeOmega = foot.body.angularVelocity - shin.body.angularVelocity;
        std::cout << "rightKnee," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep << ',' << stage
                  << ',' << (worldB - worldA).length() << ',' << aura::body::relativeJointAngle(shin, foot, joint)
                  << ',' << relativeVelocity.length()
                  << ',' << relativeVelocity.x << ',' << relativeVelocity.y << ',' << relativeVelocity.z
                  << ',' << relativeOmega.x << ',' << relativeOmega.y << ',' << relativeOmega.z
                  << ',' << aura::physics::lowestPoint(foot.body, foot.size).y
                  << ',' << aura::body::relativeJointAngle(auraBody.rightThigh, shin, skeleton.rightKnee) << '\n';
    };
    const auto tracingWrist = [&] {
        return wristWindow && diagnosticTime >= 1.40 - 1e-9 && diagnosticTime <= 1.44 + 1e-9;
    };
    const auto wristSample = [&](const char *stage) {
        const auto &forearm = auraBody.rightForearm;
        const auto &hand = auraBody.rightHand;
        const auto &joint = skeleton.rightWrist;
        const auto worldA = aura::body::localToWorldPoint(forearm, joint.localAnchorA);
        const auto worldB = aura::body::localToWorldPoint(hand, joint.localAnchorB);
        const auto relativeVelocity = aura::physics::velocityAtWorldPoint(hand.body, worldB) -
                                      aura::physics::velocityAtWorldPoint(forearm.body, worldA);
        const auto relativeOmega = hand.body.angularVelocity - forearm.body.angularVelocity;
        const auto elbowGap = aura::body::localToWorldPoint(forearm, skeleton.rightElbow.localAnchorB) -
                              aura::body::localToWorldPoint(auraBody.rightUpperArm, skeleton.rightElbow.localAnchorA);
        std::cout << "rightWrist," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep << ',' << stage
                  << ',' << (worldB - worldA).length() << ',' << aura::body::relativeJointAngle(forearm, hand, joint)
                  << ',' << relativeVelocity.length()
                  << ',' << relativeOmega.x << ',' << relativeOmega.y << ',' << relativeOmega.z
                  << ',' << elbowGap.length()
                  << ',' << forearm.body.velocity.length() << ',' << forearm.body.angularVelocity.length()
                  << ',' << hand.body.velocity.length() << ',' << hand.body.angularVelocity.length() << '\n';
    };
    const auto handReplaySample = [&](const aura::body::AuraBody3D &state, const char *variant,
                                      const aura::math::Vec3 &delta) {
        const auto jointGap = [&](const auto &a, const auto &b, const auto &joint) {
            return aura::body::localToWorldPoint(b, joint.localAnchorB) -
                   aura::body::localToWorldPoint(a, joint.localAnchorA);
        };
        const auto &forearm = state.rightForearm;
        const auto &hand = state.rightHand;
        const auto &wrist = skeleton.rightWrist;
        const auto velocity = aura::physics::velocityAtWorldPoint(hand.body,
                                  aura::body::localToWorldPoint(hand, wrist.localAnchorB)) -
                              aura::physics::velocityAtWorldPoint(forearm.body,
                                  aura::body::localToWorldPoint(forearm, wrist.localAnchorA));
        std::cout << "handFloorReplay," << diagnosticTime << ',' << diagnosticIteration << ',' << variant
                  << ',' << delta.y << ',' << jointGap(forearm, hand, wrist).length()
                  << ',' << aura::body::relativeJointAngle(forearm, hand, wrist)
                  << ',' << velocity.length()
                  << ',' << jointGap(state.rightUpperArm, forearm, skeleton.rightElbow).length()
                  << ',' << jointGap(state.torso, state.rightUpperArm, skeleton.rightShoulder).length()
                  << ',' << aura::physics::lowestPoint(hand.body, hand.size).y << '\n';
    };
    const auto handCycleSample = [&](const aura::body::AuraBody3D &state, int cycle) {
        const auto gap = [](const auto &a, const auto &b, const auto &joint) {
            return (aura::body::localToWorldPoint(b, joint.localAnchorB) -
                    aura::body::localToWorldPoint(a, joint.localAnchorA)).length();
        };
        const auto relativeVelocity = aura::physics::velocityAtWorldPoint(state.rightHand.body,
            aura::body::localToWorldPoint(state.rightHand, skeleton.rightWrist.localAnchorB)) -
            aura::physics::velocityAtWorldPoint(state.rightForearm.body,
            aura::body::localToWorldPoint(state.rightForearm, skeleton.rightWrist.localAnchorA));
        std::cout << "handContactCycle," << diagnosticTime << ',' << diagnosticIteration << ',' << cycle
                  << ',' << aura::physics::lowestPoint(state.rightHand.body, state.rightHand.size).y
                  << ',' << gap(state.rightForearm, state.rightHand, skeleton.rightWrist)
                  << ',' << gap(state.rightUpperArm, state.rightForearm, skeleton.rightElbow)
                  << ',' << gap(state.torso, state.rightUpperArm, skeleton.rightShoulder)
                  << ',' << relativeVelocity.length() << '\n';
    };
    const auto handPositionSample = [&](const aura::body::AuraBody3D &state, const char *stage) {
        const auto gap = [](const auto &a, const auto &b, const auto &joint) {
            return (aura::body::localToWorldPoint(b, joint.localAnchorB) -
                    aura::body::localToWorldPoint(a, joint.localAnchorA)).length();
        };
        std::cout << "handPositionStage," << diagnosticTime << ',' << diagnosticIteration << ',' << stage
                  << ',' << aura::physics::lowestPoint(state.rightHand.body, state.rightHand.size).y
                  << ',' << gap(state.rightForearm, state.rightHand, skeleton.rightWrist)
                  << ',' << gap(state.rightUpperArm, state.rightForearm, skeleton.rightElbow)
                  << ',' << gap(state.torso, state.rightUpperArm, skeleton.rightShoulder) << '\n';
    };
    const auto pinnedWristSample = [&](const aura::body::AuraBody3D &state, const char *stage) {
        const auto gap = [](const auto &a, const auto &b, const auto &joint) {
            return (aura::body::localToWorldPoint(b, joint.localAnchorB) -
                    aura::body::localToWorldPoint(a, joint.localAnchorA)).length();
        };
        std::cout << "pinnedWristReplay," << diagnosticTime << ',' << diagnosticIteration << ',' << stage
                  << ',' << aura::physics::lowestPoint(state.rightHand.body, state.rightHand.size).y
                  << ',' << gap(state.rightForearm, state.rightHand, skeleton.rightWrist)
                  << ',' << gap(state.rightUpperArm, state.rightForearm, skeleton.rightElbow)
                  << ',' << gap(state.torso, state.rightUpperArm, skeleton.rightShoulder)
                  << ',' << gap(state.torso, state.pelvis, skeleton.waist)
                  << ',' << gap(state.torso, state.neck, skeleton.neck)
                  << ',' << gap(state.torso, state.leftUpperArm, skeleton.leftShoulder)
                  << ',' << gap(state.pelvis, state.leftThigh, skeleton.leftHip)
                  << ',' << gap(state.pelvis, state.rightThigh, skeleton.rightHip)
                  << ',' << aura::physics::lowestPoint(state.leftFoot.body, state.leftFoot.size).y
                  << ',' << aura::physics::lowestPoint(state.rightFoot.body, state.rightFoot.size).y << '\n';
    };
    const auto componentRotationSample = [&](const aura::body::AuraBody3D &state, const char *stage) {
        std::cout << "shoulderComponentRotation," << diagnosticTime << ',' << diagnosticIteration << ',' << stage
                  << ',' << aura::body::relativeJointAngle(state.torso, state.rightUpperArm, skeleton.rightShoulder)
                  << ',' << skeleton.rightShoulder.minAngle << ',' << skeleton.rightShoulder.maxAngle << '\n';
        pinnedWristSample(state, stage);
    };
    const auto testRotationFeasibility = [&](const aura::body::AuraBody3D &initial, bool armSide) {
        const auto pivot = aura::body::localToWorldPoint(initial.rightUpperArm, skeleton.rightShoulder.localAnchorB);
        const auto axis = initial.torso.body.orientation.rotate(skeleton.rightShoulder.hingeAxis.normalized()).normalized();
        const float relativeRequest = skeleton.rightShoulder.maxAngle -
            aura::body::relativeJointAngle(initial.torso, initial.rightUpperArm, skeleton.rightShoulder);
        // A rotation increases B-relative-to-A when B (arm) moves, but decreases
        // it when A (torso side) moves. Both trials pursue the same relative request.
        const float requestedAngle = armSide ? relativeRequest : -relativeRequest;
        const auto rotatedCopy = [&](float alpha) {
            auto candidate = initial;
            const std::array torsoSide{
                &candidate.torso, &candidate.neck, &candidate.head,
                &candidate.leftUpperArm, &candidate.leftForearm, &candidate.leftHand,
                &candidate.pelvis, &candidate.leftThigh, &candidate.leftShin, &candidate.leftFoot,
                &candidate.rightThigh, &candidate.rightShin, &candidate.rightFoot
            };
            const auto rotation = aura::math::Quaternion::fromAxisAngle(axis, alpha * requestedAngle);
            if (armSide) {
                const std::array arm{&candidate.rightUpperArm, &candidate.rightForearm, &candidate.rightHand};
                aura::body::rotateSubtreeAroundWorldPoint(arm, pivot, rotation);
            } else {
                aura::body::rotateSubtreeAroundWorldPoint(torsoSide, pivot, rotation);
            }
            return candidate;
        };
        const auto safe = [](const aura::body::AuraBody3D &state) {
            return aura::physics::lowestPoint(state.rightHand.body, state.rightHand.size).y >= 0.0f &&
                   aura::physics::lowestPoint(state.leftFoot.body, state.leftFoot.size).y >= 0.0f &&
                   aura::physics::lowestPoint(state.rightFoot.body, state.rightFoot.size).y >= 0.0f;
        };
        if (!safe(initial)) {
            std::cerr << "Rotation feasibility requires a floor-valid hand and both feet.\n";
            return 0.0f;
        }
        const auto sample = [&](const aura::body::AuraBody3D &state, const char *stage, float alpha) {
            std::cout << "rotationFeasibility," << (armSide ? "arm" : "torso") << ',' << stage << ',' << requestedAngle << ',' << alpha
                      << ',' << aura::body::relativeJointAngle(state.torso, state.rightUpperArm, skeleton.rightShoulder)
                      << ',' << aura::physics::lowestPoint(state.rightHand.body, state.rightHand.size).y
                      << ',' << aura::physics::lowestPoint(state.leftFoot.body, state.leftFoot.size).y
                      << ',' << aura::physics::lowestPoint(state.rightFoot.body, state.rightFoot.size).y;
            const std::array<const aura::body::BodyPart3D *, 16> copiedParts{
                &state.head, &state.neck, &state.torso, &state.pelvis,
                &state.leftUpperArm, &state.leftForearm, &state.leftHand,
                &state.rightUpperArm, &state.rightForearm, &state.rightHand,
                &state.leftThigh, &state.leftShin, &state.leftFoot,
                &state.rightThigh, &state.rightShin, &state.rightFoot
            };
            for (const auto &joint : joints) {
                const auto indexA = std::find(parts.begin(), parts.end(), joint.partA) - parts.begin();
                const auto indexB = std::find(parts.begin(), parts.end(), joint.partB) - parts.begin();
                std::cout << ',' << (aura::body::localToWorldPoint(*copiedParts[indexB], joint.constraint.localAnchorB) -
                                    aura::body::localToWorldPoint(*copiedParts[indexA], joint.constraint.localAnchorA)).length();
            }
            std::cout << '\n';
        };
        std::cout << "Requested geometric rotation toward maxAngle from the original valid pose; not an existing limit violation. Checks right hand and both feet with strict Y >= 0.\n"
                  << "record,side,stage,requestedRotation,alpha,shoulderAngle,handY,leftFootY,rightFootY";
        for (const auto &joint : joints) std::cout << ',' << joint.name << "Gap";
        std::cout << '\n';
        sample(initial, "initial", 0.0f);
        sample(rotatedCopy(1.0f), "full_request", 1.0f);
        // Establish the first unsafe bracket along the path before bisection:
        // floor clearance need not be monotonic over a large rotation.
        float lower = 0.0f;
        float upper = 1.0f;
        bool foundUnsafe = false;
        for (int i = 1; i <= 128; ++i) {
            const float alpha = static_cast<float>(i) / 128.0f;
            if (!safe(rotatedCopy(alpha))) { upper = alpha; foundUnsafe = true; break; }
            lower = alpha;
        }
        if (foundUnsafe) {
            for (int i = 0; i < 24; ++i) {
                const float middle = (lower + upper) * 0.5f;
                if (middle == lower || middle == upper) break;
                if (safe(rotatedCopy(middle))) lower = middle;
                else upper = middle;
            }
        }
        sample(rotatedCopy(lower), "safe_fraction", lower);
        if (foundUnsafe) sample(rotatedCopy(upper), "unsafe_bracket", upper);
        return std::abs(requestedAngle) * lower;
    };
    const auto testShoulderLimitRepair = [&](const aura::body::AuraBody3D &initial) {
        auto candidate = initial;
        const auto sample = [&](const char *stage) {
            const auto gap = [](const auto &a, const auto &b, const auto &joint) {
                return (aura::body::localToWorldPoint(b, joint.localAnchorB) -
                        aura::body::localToWorldPoint(a, joint.localAnchorA)).length();
            };
            const std::array<const aura::body::BodyPart3D *, 16> allParts{
                &candidate.head, &candidate.neck, &candidate.torso, &candidate.pelvis,
                &candidate.leftUpperArm, &candidate.leftForearm, &candidate.leftHand,
                &candidate.rightUpperArm, &candidate.rightForearm, &candidate.rightHand,
                &candidate.leftThigh, &candidate.leftShin, &candidate.leftFoot,
                &candidate.rightThigh, &candidate.rightShin, &candidate.rightFoot
            };
            float minimumY = std::numeric_limits<float>::infinity();
            int penetratingParts = 0;
            for (const auto *part : allParts) {
                const float y = aura::physics::lowestPoint(part->body, part->size).y;
                minimumY = std::min(minimumY, y);
                if (y < 0.0f) ++penetratingParts;
            }
            std::cout << "shoulderLimitReplay," << stage
                      << ',' << aura::body::relativeJointAngle(candidate.torso, candidate.rightUpperArm, skeleton.rightShoulder)
                      << ',' << gap(candidate.torso, candidate.rightUpperArm, skeleton.rightShoulder)
                      << ',' << gap(candidate.rightUpperArm, candidate.rightForearm, skeleton.rightElbow)
                      << ',' << gap(candidate.rightForearm, candidate.rightHand, skeleton.rightWrist)
                      << ',' << aura::physics::lowestPoint(candidate.rightHand.body, candidate.rightHand.size).y
                      << ',' << aura::physics::lowestPoint(candidate.leftFoot.body, candidate.leftFoot.size).y
                      << ',' << aura::physics::lowestPoint(candidate.rightFoot.body, candidate.rightFoot.size).y
                      << ',' << aura::body::relativeJointAngle(candidate.rightUpperArm, candidate.rightForearm, skeleton.rightElbow)
                      << ',' << aura::body::relativeJointAngle(candidate.rightForearm, candidate.rightHand, skeleton.rightWrist)
                      << ',' << minimumY << ',' << penetratingParts << '\n';
        };
        std::cout << "Copied actual shoulder-limit violation: rotate full right arm to 1.7 rad, then use existing child-component angular correction with maxAngle=1.5. No integration, floor clamps, or velocity updates.\n"
                  << "record,stage,shoulderAngle,shoulderGap,elbowGap,wristGap,handY,leftFootY,rightFootY,elbowAngle,wristAngle,minimumBodyY,penetratingParts\n";
        sample("initial");
        const auto pivot = aura::body::localToWorldPoint(candidate.rightUpperArm, skeleton.rightShoulder.localAnchorB);
        const auto axis = candidate.torso.body.orientation.rotate(skeleton.rightShoulder.hingeAxis.normalized()).normalized();
        const float initialAngle = aura::body::relativeJointAngle(candidate.torso, candidate.rightUpperArm, skeleton.rightShoulder);
        const std::array arm{&candidate.rightUpperArm, &candidate.rightForearm, &candidate.rightHand};
        aura::body::rotateSubtreeAroundWorldPoint(arm, pivot,
            aura::math::Quaternion::fromAxisAngle(axis, 1.7f - initialAngle));
        sample("forced_1.7");
        std::cout << "shoulderLimitRequestedRotation,"
                  << -aura::body::jointAngleError(candidate.torso, candidate.rightUpperArm, skeleton.rightShoulder) << '\n';
        aura::body::correctBranchAngleAroundPivot(candidate, skeleton, skeleton.rightShoulder,
            aura::body::MajorBodyBranch3D::RightShoulder);
        sample("corrected_1.5");
    };
    const auto solveConnection = [&](BodyJoint &connection) {
        auto &a = *connection.partA;
        auto &b = *connection.partB;
        if (&connection.constraint == &skeleton.waist && waistGroups) {
            aura::body::correctJointAngle(a, b, connection.constraint);
            const bool eitherFootTouchesFloor =
                !aura::physics::floorContactPoints(auraBody.leftFoot.body, auraBody.leftFoot.size, 0.0f, 0.01f).empty() ||
                !aura::physics::floorContactPoints(auraBody.rightFoot.body, auraBody.rightFoot.size, 0.0f, 0.01f).empty();
            aura::body::correctWaistPositionWithFloorContact(auraBody, connection.constraint, eitherFootTouchesFloor);
            aura::body::solveJointVelocityConstraints(a, b, connection.constraint);
            return;
        }
        if (neckSubtree && &connection.constraint == &skeleton.neck) {
            const bool log = headWindow && diagnosticTime >= 0.70 - 1e-9 && diagnosticTime <= 0.82 + 1e-9;
            if (log) neckSample("A");
            aura::body::correctNeckAngleAroundPivot(auraBody, skeleton, connection.constraint);
            if (log) neckSample("B");
            aura::body::correctNeckPositionAsSubtree(auraBody, connection.constraint);
            if (log) neckSample("C");
            for (int i = 1; i <= aura::body::jointVelocityIterations; ++i) {
                aura::body::correctNeckAnchorVelocityAsSubtree(auraBody, connection.constraint);
                if (log) neckSample(("D" + std::to_string(i)).c_str());
                aura::body::correctNeckAngularVelocityAsSubtree(auraBody, connection.constraint);
                if (log) neckSample(("E" + std::to_string(i)).c_str());
            }
            return;
        }
        if (headWindow && &connection.constraint == &skeleton.head &&
            diagnosticTime >= 0.70 - 1e-9 && diagnosticTime <= 0.82 + 1e-9) {
            const float angle = aura::body::relativeJointAngle(a, b, connection.constraint);
            constexpr float nearLimit = 0.05f;
            const bool log = angle <= connection.constraint.minAngle + nearLimit ||
                             angle >= connection.constraint.maxAngle - nearLimit ||
                             std::abs(diagnosticTime - 0.741666667) < 1e-8;
            if (log) headSample("A");
            if (headPivot) aura::body::correctHeadAngleAroundPivot(auraBody, skeleton, connection.constraint);
            else aura::body::correctJointAngle(a, b, connection.constraint);
            if (log) headSample("B");
            if (headLeafPosition) aura::body::correctHeadPositionAsLeaf(auraBody, connection.constraint);
            else aura::body::correctJointPosition(a, b, connection.constraint);
            if (log) headSample("C");
            for (int velocityIteration = 1; velocityIteration <= aura::body::jointVelocityIterations; ++velocityIteration) {
                aura::body::correctJointVelocity(a, b, connection.constraint);
                if (log) headSample(("D" + std::to_string(velocityIteration)).c_str());
                aura::body::correctJointAngularVelocity(a, b, connection.constraint);
                if (log) headSample(("E" + std::to_string(velocityIteration)).c_str());
            }
            return;
        }
        if (headPivot && &connection.constraint == &skeleton.head) {
            aura::body::correctHeadAngleAroundPivot(auraBody, skeleton, connection.constraint);
            if (headLeafPosition) aura::body::correctHeadPositionAsLeaf(auraBody, connection.constraint);
            else aura::body::correctJointPosition(a, b, connection.constraint);
            aura::body::solveJointVelocityConstraints(a, b, connection.constraint);
            return;
        }
        if (leftShoulderSubtree && &connection.constraint == &skeleton.leftShoulder) {
            const bool log = headWindow && diagnosticTime >= 0.70 - 1e-9 && diagnosticTime <= 0.82 + 1e-9;
            if (log) shoulderSample("A");
            aura::body::correctLeftShoulderAngleAroundPivot(auraBody, skeleton, connection.constraint);
            if (log) shoulderSample("B");
            aura::body::correctLeftShoulderPositionAsSubtree(auraBody, connection.constraint);
            if (log) shoulderSample("C");
            for (int i = 1; i <= aura::body::jointVelocityIterations; ++i) {
                aura::body::correctLeftShoulderAnchorVelocityAsSubtree(auraBody, connection.constraint);
                if (log) shoulderSample(("D" + std::to_string(i)).c_str());
                aura::body::correctLeftShoulderAngularVelocityAsSubtree(auraBody, connection.constraint);
                if (log) shoulderSample(("E" + std::to_string(i)).c_str());
            }
            return;
        }
        if (majorBranches && (&connection.constraint == &skeleton.rightShoulder || &connection.constraint == &skeleton.leftHip || &connection.constraint == &skeleton.rightHip)) {
            const auto branch = &connection.constraint == &skeleton.rightShoulder
                ? aura::body::MajorBodyBranch3D::RightShoulder
                : &connection.constraint == &skeleton.leftHip
                    ? aura::body::MajorBodyBranch3D::LeftHip : aura::body::MajorBodyBranch3D::RightHip;
            aura::body::correctBranchAngleAroundPivot(auraBody, skeleton, connection.constraint, branch);
            aura::body::correctBranchPositionAsSubtree(auraBody, connection.constraint, branch);
            aura::body::solveBranchSubtreeVelocityConstraints(auraBody, connection.constraint, branch);
            return;
        }
        if (rightKneeSubtree && &connection.constraint == &skeleton.rightKnee) {
            constexpr auto branch = aura::body::MajorBodyBranch3D::RightKnee;
            const bool log = tracingAnkle();
            if (log) kneeSample("before");
            aura::body::correctRightKneeAngleAroundPivot(auraBody, skeleton, connection.constraint, skeleton.rightAnkle);
            if (log) kneeSample("afterAngle");
            aura::body::correctBranchPositionAsSubtree(auraBody, connection.constraint, branch);
            if (log) kneeSample("afterPosition");
            aura::body::solveBranchSubtreeVelocityConstraints(auraBody, connection.constraint, branch);
            if (log) kneeSample("afterVelocity");
            return;
        }
        if (tracingWrist() && (&connection.constraint == &skeleton.rightElbow ||
                               &connection.constraint == &skeleton.rightWrist)) {
            const bool elbow = &connection.constraint == &skeleton.rightElbow;
            wristSample(elbow ? "A" : "E");
            constexpr auto branch = aura::body::MajorBodyBranch3D::RightElbow;
            if (elbow && rightElbowSubtree)
                aura::body::correctRightElbowAngleAroundPivot(auraBody, skeleton, connection.constraint, skeleton.rightWrist);
            else aura::body::correctJointAngle(a, b, connection.constraint);
            wristSample(elbow ? "B" : "F");
            if (elbow && rightElbowSubtree)
                aura::body::correctBranchPositionAsSubtree(auraBody, connection.constraint, branch);
            else aura::body::correctJointPosition(a, b, connection.constraint);
            wristSample(elbow ? "C" : "G");
            // Expand the same velocity loop to measure each pair without changing it.
            for (int i = 1; i <= aura::body::jointVelocityIterations; ++i) {
                if (elbow && rightElbowSubtree) {
                    aura::body::correctBranchAnchorVelocityAsSubtree(auraBody, connection.constraint, branch);
                    aura::body::correctBranchAngularVelocityAsSubtree(auraBody, connection.constraint, branch);
                } else {
                    aura::body::correctJointVelocity(a, b, connection.constraint);
                    aura::body::correctJointAngularVelocity(a, b, connection.constraint);
                }
                wristSample(((elbow ? "D" : "H") + std::to_string(i)).c_str());
            }
            if (elbow && std::string(diagnosticSweep) == "backward") wristSample("I");
            return;
        }
        if (rightElbowSubtree && &connection.constraint == &skeleton.rightElbow) {
            constexpr auto branch = aura::body::MajorBodyBranch3D::RightElbow;
            aura::body::correctRightElbowAngleAroundPivot(auraBody, skeleton, connection.constraint, skeleton.rightWrist);
            aura::body::correctBranchPositionAsSubtree(auraBody, connection.constraint, branch);
            aura::body::solveBranchSubtreeVelocityConstraints(auraBody, connection.constraint, branch);
            return;
        }
        if (!connection.floorAware) {
            aura::body::solveJoint(a, b, connection.constraint);
            return;
        }
        const bool logAnkle = tracingAnkle() && &connection.constraint == &skeleton.rightAnkle;
        aura::body::correctJointAngle(a, b, connection.constraint);
        if (logAnkle) ankleSample("E");
        const bool touchingFloor = !aura::physics::floorContactPoints(b.body, b.size, 0.0f, 0.01f).empty();
        aura::body::correctJointPositionWithFloorContact(a, b, connection.constraint, touchingFloor);
        if (logAnkle) ankleSample("F");
        // Same four corrections as solveJointVelocityConstraints; sample each pair.
        for (int i = 1; i <= aura::body::jointVelocityIterations; ++i) {
            aura::body::correctJointVelocity(a, b, connection.constraint);
            if (logAnkle) ankleSample(("G" + std::to_string(i) + "anchor").c_str());
            aura::body::correctJointAngularVelocity(a, b, connection.constraint);
            if (logAnkle) ankleSample(("G" + std::to_string(i)).c_str());
        }
    };

    constexpr float gapThreshold = 0.001f;
    // Numerical tolerance, not an allowed anatomical range extension.
    constexpr float limitTolerance = 0.00001f;
    const int steps = contactWindow ? 48 : headWindow ? 98 : ankleWindow ? 156 : wristWindow ? 172 : 1200;
    std::array<JointMeasurements, 15> measurements{};
    bool finite = true;
    float maxDistance = 0.0f;
    float minimumLowestY = 0.0f;
    double firstFloorContact = std::numeric_limits<double>::infinity();
    double firstNonfinite = std::numeric_limits<double>::infinity();
    const auto finiteVector = [](const aura::math::Vec3 &v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };

    const auto anchorGaps = [&] {
        std::array<float, 15> gaps{};
        for (std::size_t i = 0; i < joints.size(); ++i) {
            const auto &j = joints[i];
            gaps[i] = (aura::body::localToWorldPoint(*j.partB, j.constraint.localAnchorB) -
                       aura::body::localToWorldPoint(*j.partA, j.constraint.localAnchorA)).length();
        }
        return gaps;
    };
    const auto lowestPoints = [&] {
        std::array<float, 16> ys{};
        for (std::size_t i = 0; i < parts.size(); ++i)
            ys[i] = aura::physics::lowestPoint(parts[i]->body, parts[i]->size).y;
        return ys;
    };
    const auto contactCounts = [&] {
        std::array<std::size_t, 16> counts{};
        for (std::size_t i = 0; i < parts.size(); ++i)
            counts[i] = aura::physics::floorContactPoints(parts[i]->body, parts[i]->size, 0.0f, 0.01f).size();
        return counts;
    };
    if (contactWindow) {
        std::cout << std::fixed << std::setprecision(9)
                  << "Waist correction=" << (waistGroups ? "groups" : "pairwise") << "\n"
                  << "Baseline contact window: 0.35-0.40s; hip/knee motors only; 120Hz; 16 iterations.\n"
                  << "Time labels are step END times. Iterations are 1-based.\n"
                  << "A=before floor, B=after floor, C=after forward, D=after backward, within EACH iteration.\n"
                  << "contact,time,iteration,part,countA,countB,countC,countD,lowestA,lowestB,deltaX,deltaY,deltaZ,lowestC,lowestD\n"
                  << "gap,time,iteration,joint,A,B,C,D,beforeLastWaist\n";
    }

    if (headWindow) {
        std::cout << std::fixed << std::setprecision(9)
                  << "Head angle correction=" << (headPivot ? "pivot leaf" : "pairwise COM") << "\n"
                  << "Head position correction=" << (headLeafPosition ? "leaf only" : "pairwise sharing") << "\n"
                  << "Left shoulder correction=" << (leftShoulderSubtree ? "child subtree" : "pairwise") << "\n"
                  << "Neck correction=" << (neckSubtree ? "child subtree" : "pairwise") << "\n"
                  << "Head correction window: 0.70-0.82s; waist groups ON; hip/knee motors only; 120Hz; 16 iterations.\n"
                  << "Near-limit band=0.05 rad plus reference step 0.741667s; head limits=[-0.5,+0.5]; step END times; iterations 1-based.\n"
                  << "A=before angle, B=after angle, C=after position, D1..D4=after anchor velocity, E1..E4=after angular velocity; four inner iterations.\n"
                  << "head,time,iteration,sweep,stage,angle,hingeOmega,headGap,neckGap,headSpeed,neckSpeed,anchorRelVx,anchorRelVy,anchorRelVz,anchorRelSpeed\n"
                  << "neck,time,iteration,sweep,stage,neckAngle,neckGap,headGap,headRelativeAngle,neckRelVx,neckRelVy,neckRelVz,neckRelSpeed,headRelVx,headRelVy,headRelVz,headRelSpeed,headRelOmegaX,headRelOmegaY,headRelOmegaZ,headRelOmegaMagnitude\n";
    }

    if (headWindow && leftShoulderSubtree) {
        std::cout << "leftShoulder,time,iteration,sweep,stage,[shoulder/elbow/wrist: gap,angle,anchorSpeed,relativeOmegaMagnitude,relVx,relVy,relVz,relOmegaX,relOmegaY,relOmegaZ]\n";
    }

    if (ankleWindow) {
        std::cout << std::fixed << std::setprecision(9)
                  << "Right ankle window: 1.25-1.30s; major branches ON; upper-body motors OFF; 120Hz; 16 outer / 4 inner iterations.\n"
                  << "Time labels are step END times; iterations 1-based; floor Y=0, contact tolerance=0.01.\n"
                  << "A=before all floor solves, AshinBefore/AshinAfter=right-shin floor solve, Afoot=before right-foot floor solve, B=after right-foot floor solve, Cbefore/C=before/after forward right hip, D=forward right knee, E=ankle angle, F=ankle position, G1anchor..G4anchor=after anchor impulses, G1..G4=after angular velocity correction, H=backward right knee, I=backward right hip, J=end backward sweep.\n"
                  << "Shin floor replay=" << (shinFloorReplay ? "copied poses only" : "OFF") << "\n"
                  << "Right shin floor position=" << (rightShinFloorMotion ? "shin-foot position+velocity" : rightShinFloorSubtree ? "shin-foot translation" : "shin only") << "\n"
                  << "Right elbow correction=" << (rightElbowSubtree ? "forearm-hand subtree" : "pairwise") << "\n"
              << "Right knee correction=" << (rightKneeSubtree ? "shin-foot subtree" : "pairwise") << "\n"
                  << "joint,time,iteration,stage,gap,angle,hingeOmega,anchorRelSpeed,footLowestY,footContacts,shinLinearSpeed,shinAngularSpeed,footLinearSpeed,footAngularSpeed,relativeOmegaX,relativeOmegaY,relativeOmegaZ\n";
        if (rightKneeSubtree)
            std::cout << "rightKnee,time,iteration,sweep,stage,ankleGap,ankleAngle,ankleRelSpeed,ankleRelVx,ankleRelVy,ankleRelVz,ankleRelOmegaX,ankleRelOmegaY,ankleRelOmegaZ,footLowestY,kneeAngle\n";
    }

    if (shinContactAudit) {
        std::cout << std::fixed << std::setprecision(9)
                  << "First eight active right-shin live floor responses; same loaded baseline settings.\n"
                  << "joint,time,iteration,stage,gap,angle,hingeOmega,anchorRelSpeed,footLowestY,footContacts,shinLinearSpeed,shinAngularSpeed,footLinearSpeed,footAngularSpeed,relativeOmegaX,relativeOmegaY,relativeOmegaZ\n";
    }
    if (wristWindow) {
        std::cout << std::fixed << std::setprecision(9)
                  << "Right wrist window: 1.40-1.44s; current hierarchy and right-shin motion ON; upper-body motors OFF; 120Hz; 16 outer / 4 inner iterations.\n"
                  << "Right elbow correction=" << (rightElbowSubtree ? "forearm-hand subtree" : "pairwise") << "\n"
                  << "Step END times; iterations 1-based; both sweeps logged; gap threshold=0.001.\n"
                  << "A=before elbow, B=elbow angle, C=elbow position, D1..D4=elbow velocity pairs; E=before wrist, F=wrist angle, G=wrist position, H1..H4=wrist velocity pairs; I=completed backward elbow; start/end=entire outer iteration; floorBefore/AfterForearm and floorBefore/AfterHand isolate contact triggers.\n"
                  << "joint,time,iteration,sweep,stage,wristGap,wristAngle,wristAnchorRelSpeed,wristRelOmegaX,wristRelOmegaY,wristRelOmegaZ,elbowGap,forearmLinearSpeed,forearmAngularSpeed,handLinearSpeed,handAngularSpeed\n";
    }
    if (handFloorReplay) {
        std::cout << "Right-hand copied-state replay: A=hand only; B=forearm+hand; C=upperArm+forearm+hand when B disturbs elbow.\n"
                  << "Original hand floor velocity response retained identically in A/B/C; other bodies' velocities unchanged. No live corrections added.\n"
                  << "record,time,iteration,variant,handDeltaY,wristGap,wristAngle,wristAnchorRelSpeed,elbowGap,shoulderGap,handLowestY\n";
    }
    if (handContactCycles) {
        std::cout << "Copied first penetrating right-hand pose in wrist window; four floor/wrist/elbow-subtree/shoulder-subtree cycles, no integration.\n"
                  << "Floor dt remains physics dt / 16; original hand-only impulses retained; live ordering unchanged. Cycle 0 is before contact.\n"
                  << "record,time,iteration,cycle,handLowestY,wristGap,elbowGap,shoulderGap,wristAnchorRelSpeed\n";
    }
    if (handPositionStages) {
        std::cout << "Copied first penetrating right-hand pose: unchanged floor solve, then wrist/elbow-subtree/shoulder-subtree POSITION corrections only. No angle or joint velocity corrections, no integration, live state untouched.\n"
                  << "record,time,iteration,stage,handLowestY,wristGap,elbowGap,shoulderGap\n";
    }
    if (pinnedWristReplay) {
        std::cout << "Copied penetrating right-hand pose: original floor solve, then forearm-only wrist position repair. Hand fixed after floor; no angle or joint velocity repairs. Live state untouched.\n"
                  << "Elbow replay=" << (pinnedElbowReplay ? "upper-arm-only position repair; forearm and hand fixed" : "OFF") << '\n'
                  << "Shoulder replay=" << (pinnedShoulderReplay ? "torso-only position repair; entire right arm fixed" : "OFF") << '\n'
                  << "Shoulder component replay=" << (shoulderComponentReplay ? "all 13 torso-side parts; right arm fixed; no floor clamp" : "OFF") << '\n'
                  << "record,time,iteration,stage,handLowestY,wristGap,elbowGap,shoulderGap,waistGap,neckGap,leftShoulderGap,leftHipGap,rightHipGap,leftFootLowestY,rightFootLowestY\n";
    }
    if (shoulderComponentRotationReplay) {
        std::cout << "Component rotation replay: after contact-rooted position repairs, force shoulder to maxAngle + 0.2 rad by rotating the torso-side component about the shared shoulder pivot, then rotate that same component back to the limit. Right arm fixed; no floor clamp, integration, or velocity updates.\n"
                  << "record,time,iteration,stage,shoulderAngle,minAngle,maxAngle\n";
    }
    // Sample after every complete step, not only at the once-per-second log interval.
    for (int frame = 0; frame < steps; ++frame) {
        diagnosticTime = (frame + 1) * FIXED_DT;
        const auto dt = static_cast<float>(FIXED_DT);
        for (auto &joint: joints) {
            if (joint.motor) aura::body::applyJointMotor(*joint.partA, *joint.partB, joint.constraint);
        }
        for (auto *part: parts) {
            aura::physics::applyForce(part->body, aura::math::Vec3{0.0f, -9.81f, 0.0f} * part->body.mass);
            aura::physics::updateLinearAcceleration(part->body);
            aura::physics::updateAngularAcceleration(part->body);
            aura::physics::integrateLinearMotion(part->body, dt);
            aura::physics::integrateAngularMotion(part->body, dt);
        }
        const double stepTime = (frame + 1) * FIXED_DT;
        const bool trace = contactWindow && stepTime >= 0.35 - 1e-9 && stepTime <= 0.40 + 1e-9;
        for (int iteration = 0; iteration < jointIterations; ++iteration) {
            diagnosticIteration = iteration + 1;
            diagnosticSweep = "floor";
            if (tracingWrist()) wristSample("start");
            if (tracingAnkle()) ankleSample("A");
            const auto gapsA = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowA = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countA = trace ? contactCounts() : std::array<std::size_t, 16>{};
            std::array<aura::math::Vec3, 16> floorDelta{};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                auto *part = parts[i];
                const auto before = part->body.position;
                if (tracingWrist() && part == &auraBody.rightForearm) wristSample("floorBeforeForearm");
                if (tracingWrist() && part == &auraBody.rightHand) wristSample("floorBeforeHand");
                if (pinnedWristReplay && !pinnedWristReplayDone && tracingWrist() &&
                    part == &auraBody.rightHand && aura::physics::lowestPoint(part->body, part->size).y < 0.0f) {
                    auto replay = auraBody;
                    aura::physics::resolveFloorCollision(replay.rightHand.body, replay.rightHand.size,
                                                        0.0f, dt / jointIterations);
                    pinnedWristSample(replay, "after_floor");
                    const auto error = aura::body::localToWorldPoint(replay.rightHand, skeleton.rightWrist.localAnchorB) -
                                       aura::body::localToWorldPoint(replay.rightForearm, skeleton.rightWrist.localAnchorA);
                    replay.rightForearm.body.position += error;
                    pinnedWristSample(replay, "after_pinned_wrist_position");
                    if (pinnedElbowReplay) {
                        const auto elbowError = aura::body::localToWorldPoint(replay.rightForearm, skeleton.rightElbow.localAnchorB) -
                                                aura::body::localToWorldPoint(replay.rightUpperArm, skeleton.rightElbow.localAnchorA);
                        replay.rightUpperArm.body.position += elbowError;
                        pinnedWristSample(replay, "after_contact_rooted_elbow_position");
                        if (pinnedShoulderReplay || shoulderComponentReplay) {
                            const auto shoulderError = aura::body::localToWorldPoint(replay.rightUpperArm, skeleton.rightShoulder.localAnchorB) -
                                                       aura::body::localToWorldPoint(replay.torso, skeleton.rightShoulder.localAnchorA);
                            if (shoulderComponentReplay) {
                                // Cut at the right shoulder. Translate the complete
                                // opposite component, without moving any right-arm part.
                                const std::array torsoSide{
                                    &replay.torso, &replay.neck, &replay.head,
                                    &replay.leftUpperArm, &replay.leftForearm, &replay.leftHand,
                                    &replay.pelvis,
                                    &replay.leftThigh, &replay.leftShin, &replay.leftFoot,
                                    &replay.rightThigh, &replay.rightShin, &replay.rightFoot
                                };
                                aura::body::translateSubtree(torsoSide, shoulderError);
                                pinnedWristSample(replay, "after_shoulder_component_position");
                                if (shoulderLimitReplay) testShoulderLimitRepair(replay);
                                if (shoulderRotationFeasibility) {
                                    const float torsoCapacity = testRotationFeasibility(replay, false);
                                    if (shoulderCapacityComparison) {
                                        // Both capacity searches start from this identical, untouched pose.
                                        const float armCapacity = testRotationFeasibility(replay, true);
                                        const float required = std::abs(skeleton.rightShoulder.maxAngle -
                                            aura::body::relativeJointAngle(replay.torso, replay.rightUpperArm, skeleton.rightShoulder));
                                        std::cout << "record,torsoCapacity,armCapacity,totalCapacity,requiredRelativeRotation\n"
                                                  << "shoulderCapacity," << torsoCapacity << ',' << armCapacity << ','
                                                  << torsoCapacity + armCapacity << ',' << required << '\n';
                                    }
                                }
                                if (shoulderComponentRotationReplay) {
                                    componentRotationSample(replay, "rotation_initial");
                                    const auto pivot = aura::body::localToWorldPoint(replay.rightUpperArm,
                                        skeleton.rightShoulder.localAnchorB);
                                    const auto axis = replay.torso.body.orientation.rotate(
                                        skeleton.rightShoulder.hingeAxis.normalized()).normalized();
                                    const float initialAngle = aura::body::relativeJointAngle(replay.torso,
                                        replay.rightUpperArm, skeleton.rightShoulder);
                                    const float forcedAngle = skeleton.rightShoulder.maxAngle + 0.2f;
                                    // Rotating A in positive hinge direction decreases B relative to A.
                                    aura::body::rotateSubtreeAroundWorldPoint(torsoSide, pivot,
                                        aura::math::Quaternion::fromAxisAngle(axis, initialAngle - forcedAngle));
                                    componentRotationSample(replay, "rotation_forced");
                                    const float error = aura::body::jointAngleError(replay.torso,
                                        replay.rightUpperArm, skeleton.rightShoulder);
                                    const auto correctionAxis = replay.torso.body.orientation.rotate(
                                        skeleton.rightShoulder.hingeAxis.normalized()).normalized();
                                    aura::body::rotateSubtreeAroundWorldPoint(torsoSide, pivot,
                                        aura::math::Quaternion::fromAxisAngle(correctionAxis, error));
                                    componentRotationSample(replay, "rotation_corrected");
                                }
                            } else {
                                replay.torso.body.position += shoulderError;
                                pinnedWristSample(replay, "after_contact_rooted_shoulder_position");
                            }
                        }
                    }
                    pinnedWristReplayDone = true;
                }
                if (handPositionStages && !handPositionStagesDone && tracingWrist() &&
                    part == &auraBody.rightHand && aura::physics::lowestPoint(part->body, part->size).y < 0.0f) {
                    auto replay = auraBody;
                    handPositionSample(replay, "before_floor");
                    aura::physics::resolveFloorCollision(replay.rightHand.body, replay.rightHand.size,
                                                        0.0f, dt / jointIterations);
                    handPositionSample(replay, "after_floor");
                    aura::body::correctJointPosition(replay.rightForearm, replay.rightHand, skeleton.rightWrist);
                    handPositionSample(replay, "after_wrist_position");
                    aura::body::correctBranchPositionAsSubtree(replay, skeleton.rightElbow,
                        aura::body::MajorBodyBranch3D::RightElbow);
                    handPositionSample(replay, "after_elbow_position");
                    aura::body::correctBranchPositionAsSubtree(replay, skeleton.rightShoulder,
                        aura::body::MajorBodyBranch3D::RightShoulder);
                    handPositionSample(replay, "after_shoulder_position");
                    handPositionStagesDone = true;
                }
                if (handContactCycles && !handContactCyclesDone && tracingWrist() &&
                    part == &auraBody.rightHand && aura::physics::lowestPoint(part->body, part->size).y < 0.0f) {
                    // Capture the historical contact trajectory, then evaluate the
                    // current hierarchical elbow/shoulder corrections on a copy.
                    auto replay = auraBody;
                    handCycleSample(replay, 0);
                    for (int cycle = 1; cycle <= 4; ++cycle) {
                        aura::physics::resolveFloorCollision(replay.rightHand.body, replay.rightHand.size,
                                                            0.0f, dt / jointIterations);
                        aura::body::solveJoint(replay.rightForearm, replay.rightHand, skeleton.rightWrist);
                        aura::body::correctRightElbowAngleAroundPivot(replay, skeleton, skeleton.rightElbow, skeleton.rightWrist);
                        aura::body::correctBranchPositionAsSubtree(replay, skeleton.rightElbow,
                            aura::body::MajorBodyBranch3D::RightElbow);
                        aura::body::solveBranchSubtreeVelocityConstraints(replay, skeleton.rightElbow,
                            aura::body::MajorBodyBranch3D::RightElbow);
                        aura::body::correctBranchAngleAroundPivot(replay, skeleton, skeleton.rightShoulder,
                            aura::body::MajorBodyBranch3D::RightShoulder);
                        aura::body::correctBranchPositionAsSubtree(replay, skeleton.rightShoulder,
                            aura::body::MajorBodyBranch3D::RightShoulder);
                        aura::body::solveBranchSubtreeVelocityConstraints(replay, skeleton.rightShoulder,
                            aura::body::MajorBodyBranch3D::RightShoulder);
                        handCycleSample(replay, cycle);
                    }
                    handContactCyclesDone = true;
                }
                if (handFloorReplay && tracingWrist() && part == &auraBody.rightHand) {
                    // Identical hand floor response in every copy. Only the scope
                    // of the positional translation differs; the live state is untouched.
                    auto handOnly = auraBody;
                    aura::physics::resolveFloorCollision(handOnly.rightHand.body, handOnly.rightHand.size,
                                                        0.0f, dt / jointIterations);
                    const auto delta = handOnly.rightHand.body.position - part->body.position;
                    if (delta.lengthSquared() > 0.0f) {
                        handReplaySample(auraBody, "before", delta);
                        handReplaySample(handOnly, "A_hand", delta);
                        auto pair = handOnly;
                        const std::array forearmOnly{&pair.rightForearm};
                        aura::body::translateSubtree(forearmOnly, delta);
                        handReplaySample(pair, "B_forearm_hand", delta);
                        const auto beforeElbow = aura::body::localToWorldPoint(auraBody.rightForearm, skeleton.rightElbow.localAnchorB) -
                                                 aura::body::localToWorldPoint(auraBody.rightUpperArm, skeleton.rightElbow.localAnchorA);
                        const auto pairElbow = aura::body::localToWorldPoint(pair.rightForearm, skeleton.rightElbow.localAnchorB) -
                                               aura::body::localToWorldPoint(pair.rightUpperArm, skeleton.rightElbow.localAnchorA);
                        if ((pairElbow - beforeElbow).length() > 1e-6f) {
                            auto arm = pair;
                            const std::array upperArmOnly{&arm.rightUpperArm};
                            aura::body::translateSubtree(upperArmOnly, delta);
                            handReplaySample(arm, "C_whole_arm", delta);
                        }
                    }
                }
                if (tracingAnkle() && part == &auraBody.rightShin) {
                    ankleSample("AshinBefore");
                    if (shinFloorReplay) {
                        // Evaluate the new response on a copy of this exact contacting pose.
                        // The live run keeps its selected knee/floor settings unchanged.
                        auto replay = auraBody;
                        ankleSample("before", &replay, "shinFloorReplay");
                        aura::body::resolveRightShinFloorWithFootTranslation(replay, dt / jointIterations);
                        ankleSample("after", &replay, "shinFloorReplay");
                        auto motionReplay = auraBody;
                        aura::body::resolveRightShinFloorWithFootMotion(motionReplay, dt / jointIterations);
                        ankleSample("afterVelocityPropagation", &motionReplay, "shinFloorReplay");
                    }
                }
                if (tracingAnkle() && part == &auraBody.rightFoot) ankleSample("Afoot");
                if (rightShinFloorMotion && part == &auraBody.rightShin) {
                    const bool audit = shinContactAudit && auditedShinContacts < 8;
                    const auto beforeMotion = audit ? auraBody : aura::body::AuraBody3D{};
                    aura::body::resolveRightShinFloorWithFootMotion(auraBody, dt / jointIterations);
                    if (audit && ((part->body.position - beforeMotion.rightShin.body.position).lengthSquared() > 0.0f ||
                                  (part->body.velocity - beforeMotion.rightShin.body.velocity).lengthSquared() > 0.0f ||
                                  (part->body.angularVelocity - beforeMotion.rightShin.body.angularVelocity).lengthSquared() > 0.0f)) {
                        ankleSample("before", &beforeMotion, "shinLiveContact");
                        ankleSample("after", nullptr, "shinLiveContact");
                        ++auditedShinContacts;
                    }
                }
                else if (rightShinFloorSubtree && part == &auraBody.rightShin)
                    aura::body::resolveRightShinFloorWithFootTranslation(auraBody, dt / jointIterations);
                else
                    aura::physics::resolveFloorCollision(part->body, part->size, 0.0f, dt / jointIterations);
                if (tracingWrist() && part == &auraBody.rightForearm) wristSample("floorAfterForearm");
                if (tracingWrist() && part == &auraBody.rightHand) wristSample("floorAfterHand");
                if (tracingAnkle() && part == &auraBody.rightShin) ankleSample("AshinAfter");
                if (tracingAnkle() && part == &auraBody.rightFoot) ankleSample("B");
                if (trace) floorDelta[i] = part->body.position - before;
            }
            const auto gapsB = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowB = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countB = trace ? contactCounts() : std::array<std::size_t, 16>{};
            diagnosticSweep = "forward";
            for (auto &joint: joints) {
                if (tracingAnkle() && &joint.constraint == &skeleton.rightHip) ankleSample("Cbefore");
                solveConnection(joint);
                if (tracingAnkle() && &joint.constraint == &skeleton.rightHip) ankleSample("C");
                if (tracingAnkle() && &joint.constraint == &skeleton.rightKnee) ankleSample("D");
            }
            const auto gapsC = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowC = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countC = trace ? contactCounts() : std::array<std::size_t, 16>{};
            diagnosticSweep = "backward";
            std::array<float, 15> beforeLastWaist{};
            for (int i = static_cast<int>(joints.size()) - 2; i >= 0; --i) {
                if (trace && i == 0) beforeLastWaist = anchorGaps();
                solveConnection(joints[i]);
                if (tracingAnkle() && &joints[i].constraint == &skeleton.rightKnee) ankleSample("H");
                if (tracingAnkle() && &joints[i].constraint == &skeleton.rightHip) ankleSample("I");
            }
            if (tracingAnkle()) ankleSample("J");
            if (tracingWrist()) wristSample("end");
            if (trace) {
                const auto gapsD = anchorGaps();
                const auto lowD = lowestPoints();
                const auto countD = contactCounts();
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    if (countA[i] + countB[i] + countC[i] + countD[i] == 0) continue;
                    const auto &delta = floorDelta[i];
                    std::cout << "contact," << stepTime << ',' << iteration + 1 << ',' << parts[i]->name
                              << ',' << countA[i] << ',' << countB[i] << ',' << countC[i] << ',' << countD[i]
                              << ',' << lowA[i] << ',' << lowB[i]
                              << ',' << delta.x << ',' << delta.y << ',' << delta.z
                              << ',' << lowC[i] << ',' << lowD[i] << '\n';
                }
                for (std::size_t i = 0; i < joints.size(); ++i) {
                    std::cout << "gap," << stepTime << ',' << iteration + 1 << ',' << joints[i].name
                              << ',' << gapsA[i] << ',' << gapsB[i] << ',' << gapsC[i] << ',' << gapsD[i]
                              << ',' << beforeLastWaist[i] << '\n';
                }
            }
        }
        for (auto *part: parts) {
            aura::physics::clearForce(part->body);
            aura::physics::clearTorque(part->body);
        }

        const double time = (frame + 1) * FIXED_DT;
        for (const auto *part: parts) {
            const auto &b = part->body;
            const auto &q = b.orientation;
            finite = finite && finiteVector(b.position) && finiteVector(b.velocity) &&
                     finiteVector(b.angularVelocity) && finiteVector(b.acceleration) &&
                     finiteVector(b.angularAcceleration) && std::isfinite(q.w) &&
                     std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
            maxDistance = std::max(maxDistance, b.position.length());
            minimumLowestY = std::min(minimumLowestY, aura::physics::lowestPoint(b, part->size).y);
            if (!aura::physics::floorContactPoints(b, part->size, 0.0f, 0.01f).empty()) {
                firstFloorContact = std::min(firstFloorContact, time);
            }
        }
        for (std::size_t i = 0; i < joints.size(); ++i) {
            const auto &connection = joints[i];
            const auto &a = *connection.partA;
            const auto &b = *connection.partB;
            const auto &joint = connection.constraint;
            auto &m = measurements[i];
            const float gap = (aura::body::localToWorldPoint(b, joint.localAnchorB) -
                               aura::body::localToWorldPoint(a, joint.localAnchorA)).length();
            const float angle = aura::body::relativeJointAngle(a, b, joint);
            const auto relativeOmega = b.body.angularVelocity - a.body.angularVelocity;
            const auto worldAxis = a.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();
            const float omega = relativeOmega.length();
            const float hingeOmega = std::abs(relativeOmega.dot(worldAxis));
            if (!std::isfinite(gap) || !std::isfinite(angle) || !std::isfinite(omega)) {
                m.firstInvalidState = std::min(m.firstInvalidState, time);
                finite = false;
                continue;
            }
            const float overshoot = std::max({0.0f, joint.minAngle - angle, angle - joint.maxAngle});
            m.maxGap = std::max(m.maxGap, gap);
            m.maxOvershoot = std::max(m.maxOvershoot, overshoot);
            m.peakRelativeOmega = std::max(m.peakRelativeOmega, omega);
            m.peakHingeOmega = std::max(m.peakHingeOmega, hingeOmega);
            if (gap > gapThreshold) m.firstGapFailure = std::min(m.firstGapFailure, time);
            if (overshoot > limitTolerance) m.firstLimitFailure = std::min(m.firstLimitFailure, time);
        }
        if (!finite) {
            firstNonfinite = time;
            std::cerr << "Nonfinite state at t=" << time << "s; stopping run.\n";
            break;
        }
    }

    if (contactWindow || headWindow || ankleWindow || wristWindow) {
        std::cout << "Window complete: finite=" << (finite ? "yes" : "NO") << '\n';
        return finite ? 0 : 1;
    }

    std::array<std::size_t, 15> order{};
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return measurements[a].firstFailure() < measurements[b].firstFailure();
    });
    std::cout << std::fixed << std::setprecision(6)
              << "Waist correction=" << (waistGroups ? "groups" : "pairwise") << "\n"
              << "Right shin floor position=" << (rightShinFloorMotion ? "shin-foot position+velocity" : rightShinFloorSubtree ? "shin-foot translation" : "shin only") << "\n"
              << "Right elbow correction=" << (rightElbowSubtree ? "forearm-hand subtree" : "pairwise") << "\n"
              << "Right knee correction=" << (rightKneeSubtree ? "shin-foot subtree" : "pairwise") << "\n"
              << "Major branch corrections=" << (majorBranches ? "ON" : "OFF") << "\n"
              << "Baseline: 10s, 120Hz, 16 iterations; gravity/floor ON; hip/knee motors ON; upper-body motors OFF.\n"
              << "Gap threshold=" << gapThreshold << "; limit overshoot tolerance=" << limitTolerance << " rad.\n"
              << "Sampled after complete steps. Times in seconds; omega in rad/s. Relative omega is full vector magnitude.\n"
              << "finite=" << (finite ? "yes" : "NO") << " maxPositionDistance=" << maxDistance
              << " minimumLowestY=" << minimumLowestY << " firstFloorContactBand=" << timeText(firstFloorContact) << "s\n\n"
              << std::left << std::setw(18) << "Joint" << std::right
              << std::setw(12) << "First fail" << std::setw(12) << "First gap" << std::setw(12) << "First limit"
              << std::setw(12) << "Max gap" << std::setw(16) << "Max overshoot"
              << std::setw(16) << "Peak rel omega" << std::setw(16) << "Peak hinge" << '\n'
              << std::string(114, '-') << '\n';
    for (auto i: order) {
        const auto &m = measurements[i];
        std::cout << std::left << std::setw(18) << joints[i].name << std::right
                  << std::setw(12) << timeText(m.firstFailure()) << std::setw(12) << timeText(m.firstGapFailure)
                  << std::setw(12) << timeText(m.firstLimitFailure) << std::scientific << ' ' << std::setw(12) << m.maxGap
                  << std::setw(16) << m.maxOvershoot << std::setw(16) << m.peakRelativeOmega
                  << std::setw(16) << m.peakHingeOmega << '\n';
    }
    float maxAnchorGap = 0.0f;
    float peakRelativeOmega = 0.0f;
    for (const auto &m: measurements) {
        maxAnchorGap = std::max(maxAnchorGap, m.maxGap);
        peakRelativeOmega = std::max(peakRelativeOmega, m.peakRelativeOmega);
    }
    const auto firstJoint = order.front();
    std::cout << "\nRun summary: firstFailingJoint="
              << (std::isfinite(measurements[firstJoint].firstFailure()) ? joints[firstJoint].name : "none")
              << " firstFailureTime=" << timeText(measurements[firstJoint].firstFailure())
              << std::scientific << " maxAnchorGap=" << maxAnchorGap
              << " peakRelativeAngularSpeed=" << peakRelativeOmega
              << " minimumBodyY=" << minimumLowestY
              << " firstNonfiniteTime=" << timeText(firstNonfinite) << '\n';
    // This diagnostic reports threshold violations without declaring the baseline stable.
    return finite ? 0 : 1;
}
