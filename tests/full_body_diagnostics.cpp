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
#include "aura/body/ComponentMass.hpp"
#include "aura/body/ComponentVelocity.hpp"
#include "aura/body/LocalJointVelocity.hpp"
#include "aura/body/ComponentPosition.hpp"
#include "aura/physics/Impulse.hpp"
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
    bool leftLimbComponents = false;
    bool energyWindow = false;
    bool linearMassReplay = false;
    bool legVelocityReplay = false;
    bool localLegVelocityReplay = false;
    bool skeletonVelocityReplay = false;
    bool componentPositionReplay = false;
    bool componentPositions = false;
    bool localAngularLimits = false;
    bool angularMomentumDiagnostic = false;
    bool handGeometryMomentumDiagnostic = false;
    bool floorFailureDiagnostic = false;
    bool allLocalAnchors = false;
    bool rightLegLocalAnchors = false;
    bool liveVelocityMetrics = false;
    double energyStart = 4.38;
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
        else if (argument == "--left-limb-components") leftLimbComponents = true;
        else if (argument == "--energy-window") energyWindow = true;
        else if (argument == "--component-position-replay") { componentPositionReplay = true; allLocalAnchors = true; }
        else if (argument == "--component-positions") { componentPositions = true; allLocalAnchors = true; }
        else if (argument == "--local-angular-limits") { localAngularLimits = true; angularMomentumDiagnostic = true; componentPositions = true; allLocalAnchors = true; }
        else if (argument == "--angular-momentum-diagnostic") { angularMomentumDiagnostic = true; componentPositions = true; allLocalAnchors = true; }
        else if (argument == "--hand-geometry-momentum") { handGeometryMomentumDiagnostic = true; allLocalAnchors = true; }
        else if (argument == "--floor-failure-diagnostic") { floorFailureDiagnostic = true; allLocalAnchors = true; }
        else if (argument == "--all-local-anchors") { allLocalAnchors = true; liveVelocityMetrics = true; }
        else if (argument == "--skeleton-velocity-replay") skeletonVelocityReplay = true;
        else if (argument == "--right-leg-local-anchors") { rightLegLocalAnchors = true; liveVelocityMetrics = true; }
        else if (argument == "--live-velocity-metrics") liveVelocityMetrics = true;
        else if (argument == "--local-leg-velocity-replay") localLegVelocityReplay = true;
        else if (argument == "--leg-velocity-replay") legVelocityReplay = true;
        else if (argument == "--linear-mass-replay") linearMassReplay = true;
        else if (argument.starts_with("--energy-start=")) {
            try { energyStart = std::stod(argument.substr(15)); }
            catch (const std::exception &) { std::cerr << "Invalid energy start time.\n"; return 2; }
            if (!std::isfinite(energyStart) || energyStart < 0.0 || energyStart > 4.43) {
                std::cerr << "Energy start must be between 0 and 4.43 seconds.\n"; return 2;
            }
            energyWindow = true;
        }
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
            std::cerr << "Usage: aura_full_body_diagnostics [--contact-window | --head-window | --ankle-window | --wrist-window] [--waist-groups] [--head-pivot] [--head-leaf-position] [--neck-subtree] [--left-shoulder-subtree] [--major-branches] [--right-knee-subtree] [--right-elbow-subtree] [--left-limb-components] [--energy-window] [--linear-mass-replay] [--leg-velocity-replay] [--local-leg-velocity-replay] [--component-position-replay] [--component-positions] [--local-angular-limits] [--angular-momentum-diagnostic] [--hand-geometry-momentum] [--floor-failure-diagnostic] [--all-local-anchors] [--skeleton-velocity-replay] [--right-leg-local-anchors] [--live-velocity-metrics] [--energy-start=SECONDS] [--right-shin-floor-subtree] [--right-shin-floor-motion] [--shin-floor-replay] [--shin-contact-audit] [--hand-floor-replay] [--hand-contact-cycles] [--hand-position-stages] [--pinned-wrist-replay] [--pinned-elbow-replay] [--pinned-shoulder-replay] [--shoulder-component-replay] [--shoulder-component-rotation-replay] [--shoulder-rotation-feasibility] [--shoulder-capacity-comparison] [--shoulder-limit-replay]\n";
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
    if (energyWindow || linearMassReplay || legVelocityReplay || localLegVelocityReplay || rightLegLocalAnchors || liveVelocityMetrics || skeletonVelocityReplay || allLocalAnchors) leftLimbComponents = true;
    if (leftLimbComponents) rightElbowSubtree = true;
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
    const char *energyObject = "step";
    const auto tracingEnergy = [&] {
        return energyWindow && diagnosticTime >= energyStart - 1e-9 && diagnosticTime <= 4.43 + 1e-9;
    };
    const auto speed = [](const aura::math::Vec3 &v) {
        return std::hypot(static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z));
    };
    const auto energyStage = [&](const char *stage) {
        if (!tracingEnergy()) return;
        double maxV = -1.0, maxW = -1.0;
        const char *ownerV = "none", *ownerW = "none";
        for (auto *part : parts) {
            const double v = speed(part->body.velocity), w = speed(part->body.angularVelocity);
            if (v > maxV || !std::isfinite(v)) { maxV = v; ownerV = part->name.c_str(); }
            if (w > maxW || !std::isfinite(w)) { maxW = w; ownerW = part->name.c_str(); }
        }
        std::cout << "energyGlobal," << diagnosticTime << ',' << diagnosticIteration << ',' << stage
                  << ',' << ownerV << ',' << maxV << ',' << ownerW << ',' << maxW << '\n';
        for (std::size_t index : {5u, 8u, 11u, 14u}) {
            const auto &c = joints[index];
            const auto anchorA = aura::body::localToWorldPoint(*c.partA, c.constraint.localAnchorA);
            const auto anchorB = aura::body::localToWorldPoint(*c.partB, c.constraint.localAnchorB);
            const auto relativeV = aura::physics::velocityAtWorldPoint(c.partB->body, anchorB) -
                                   aura::physics::velocityAtWorldPoint(c.partA->body, anchorA);
            const auto relativeW = c.partB->body.angularVelocity - c.partA->body.angularVelocity;
            const auto axis = c.partA->body.orientation.rotate(c.constraint.hingeAxis.normalized()).normalized();
            std::cout << "energyJoint," << diagnosticTime << ',' << diagnosticIteration << ',' << stage << ',' << c.name
                      << ',' << speed(anchorB - anchorA) << ',' << aura::body::relativeJointAngle(*c.partA, *c.partB, c.constraint)
                      << ',' << relativeW.dot(axis) << ',' << speed(relativeV) << ',' << speed(relativeW) << '\n';
        }
    };
    std::array<bool, 5> crossedV{}, crossedW{};
    bool firstAnchorEnergyAmplification = false;
    const auto kineticEnergy = [](const aura::physics::RigidBody3D &b) {
        const auto localOmega = b.orientation.conjugate().rotate(b.angularVelocity);
        const auto square = [](float x) { return static_cast<double>(x) * x; };
        return 0.5 * b.mass * (square(b.velocity.x) + square(b.velocity.y) + square(b.velocity.z)) +
               0.5 * (b.momentOfInertia.x * square(localOmega.x) +
                      b.momentOfInertia.y * square(localOmega.y) +
                      b.momentOfInertia.z * square(localOmega.z));
    };
    std::array<bool, 2> linearReplayDone{};
    const auto replayLinearMass = [&] {
        const bool hip = std::string(energyObject) == "rightHip";
        const auto &joint = hip ? skeleton.rightHip : skeleton.rightKnee;
        std::cout << "Copied linear-mass replay: time=" << diagnosticTime << ", backward iteration 1, joint=" << energyObject
                  << "; linear variants retain parent/root angular impulses and disable descendant angular propagation; fourth variant uses component axis inertia and world-transformed parent inertia. No integration.\n"
                  << "record,joint,variant,stage,componentMass,comX,comY,comZ,totalKinetic,shinSpeed,footSpeed,anchorRelativeSpeed,momentumX,momentumY,momentumZ,thighSpeed,thighAngularSpeed,shinAngularSpeed,footAngularSpeed\n";
        for (int variant = 0; variant < 5; ++variant) {
            auto copy = auraBody;
            auto &parent = hip ? copy.pelvis : copy.rightThigh;
            auto &root = hip ? copy.rightThigh : copy.rightShin;
            const auto component = skeleton.collectComponent(copy, root, joint);
            const auto descendants = std::span<aura::body::BodyPart3D *const>(component).subspan(1);
            const float mass = aura::body::componentMass(component);
            const auto center = aura::body::componentCenterOfMass(component);
            const char *variantName = variant == 0 ? "original_rigid_propagation" :
                                      variant == 1 ? "root_mass_linear_only" :
                                      variant == 2 ? "component_mass_linear_only" :
                                      variant == 3 ? "component_mass_and_axis_inertia" : "component_anchor_velocity_and_axis_inertia";
            const auto sample = [&](const char *stage) {
                const std::array all{
                    &copy.head, &copy.neck, &copy.torso, &copy.pelvis,
                    &copy.leftUpperArm, &copy.leftForearm, &copy.leftHand,
                    &copy.rightUpperArm, &copy.rightForearm, &copy.rightHand,
                    &copy.leftThigh, &copy.leftShin, &copy.leftFoot,
                    &copy.rightThigh, &copy.rightShin, &copy.rightFoot
                };
                double total = 0.0;
                double px = 0.0, py = 0.0, pz = 0.0;
                for (const auto *part : all) {
                    total += kineticEnergy(part->body);
                    px += static_cast<double>(part->body.mass) * part->body.velocity.x;
                    py += static_cast<double>(part->body.mass) * part->body.velocity.y;
                    pz += static_cast<double>(part->body.mass) * part->body.velocity.z;
                }
                const auto va = aura::physics::velocityAtWorldPoint(parent.body, aura::body::localToWorldPoint(parent, joint.localAnchorA));
                const auto vb = aura::physics::velocityAtWorldPoint(root.body, aura::body::localToWorldPoint(root, joint.localAnchorB));
                std::cout << "linearMassReplay," << energyObject << ',' << variantName << ',' << stage << ',' << mass
                          << ',' << center.x << ',' << center.y << ',' << center.z << ',' << total
                          << ',' << speed(copy.rightShin.body.velocity) << ',' << speed(copy.rightFoot.body.velocity)
                          << ',' << speed(vb - va) << ',' << px << ',' << py << ',' << pz
                          << ',' << speed(copy.rightThigh.body.velocity) << ',' << speed(copy.rightThigh.body.angularVelocity)
                          << ',' << speed(copy.rightShin.body.angularVelocity) << ',' << speed(copy.rightFoot.body.angularVelocity) << '\n';
            };
            sample("before");
            const auto oldV = root.body.velocity;
            const auto oldW = root.body.angularVelocity;
            const auto anchorA = aura::body::localToWorldPoint(parent, joint.localAnchorA);
            const auto anchorB = aura::body::localToWorldPoint(root, joint.localAnchorB);
            if (variant == 4) {
                const auto audit = aura::body::correctJointComponentModeVelocity(parent, root, joint, component);
                std::cout << "componentRotationResponse," << energyObject << ",variant=" << variantName
                          << ",actualLinearWork=" << audit.linearWork << ",actualAngularWork=" << audit.angularWork
                          << ",actualWork=" << audit.linearWork + audit.angularWork << ",quadraticEnergy=" << audit.quadraticEnergy
                          << ",anchorModelWork=" << audit.predictedWork << '\n';
                sample("after_component_response");
                continue;
            }
            if (variant == 3) {
                // Copied-state approximation: a rigid component's scalar inertia
                // about the angular impulse axis. This is not a full inverse tensor.
                auto relativeV = aura::physics::velocityAtWorldPoint(root.body, anchorB) -
                                 aura::physics::velocityAtWorldPoint(parent.body, anchorA);
                if (relativeV.lengthSquared() >= 0.000001f) {
                    const auto direction = relativeV.normalized();
                    const auto rA = anchorA - parent.body.position;
                    const auto rB = anchorB - center;
                    const auto crossB = rB.cross(direction);
                    float rotationalB = 0.0f;
                    float inertiaB = 0.0f;
                    if (crossB.lengthSquared() > 1e-12f) {
                        inertiaB = aura::body::componentMomentOfInertiaAboutAxis(component, center, crossB);
                        rotationalB = crossB.lengthSquared() / inertiaB;
                    }
                    // Parent inverse inertia must also use its local principal frame.
                    const auto inverseParentInertia = [&](const aura::math::Vec3 &worldVector) {
                        const auto local = parent.body.orientation.conjugate().rotate(worldVector);
                        const auto &I = parent.body.momentOfInertia;
                        return parent.body.orientation.rotate({local.x / I.x, local.y / I.y, local.z / I.z});
                    };
                    const float rotationalA = rA.cross(direction).dot(inverseParentInertia(rA.cross(direction)));
                    const float denominator = 1.0f / parent.body.mass + 1.0f / mass + rotationalA + rotationalB;
                    const float predictedRelativeSpeed = relativeV.dot(direction);
                    const auto impulse = direction * (-predictedRelativeSpeed / denominator);
                    // Audit actual work against the anchor-based impulse model.
                    // Existing articulated velocities need not describe one rigid component.
                    const auto energyChange = [&](const auto &b, const auto &dv, const auto &dw) {
                        const auto localOld = b.orientation.conjugate().rotate(b.angularVelocity);
                        const auto localDelta = b.orientation.conjugate().rotate(dw);
                        const auto &I = b.momentOfInertia;
                        const double linearWork = b.mass * b.velocity.dot(dv);
                        const double angularWork = I.x * localOld.x * localDelta.x +
                            I.y * localOld.y * localDelta.y + I.z * localOld.z * localDelta.z;
                        const double quadratic = 0.5 * b.mass * dv.lengthSquared() +
                            0.5 * (I.x * localDelta.x * localDelta.x + I.y * localDelta.y * localDelta.y + I.z * localDelta.z * localDelta.z);
                        return std::array<double, 3>{linearWork, angularWork, quadratic};
                    };
                    const auto parentDv = -impulse * (1.0f / parent.body.mass);
                    const auto parentDw = inverseParentInertia(rA.cross(-impulse));
                    auto change = energyChange(parent.body, parentDv, parentDw);
                    parent.body.velocity -= impulse * (1.0f / parent.body.mass);
                    parent.body.angularVelocity += inverseParentInertia(rA.cross(-impulse));
                    const auto deltaLinear = impulse * (1.0f / mass);
                    const auto angularImpulse = rB.cross(impulse);
                    aura::math::Vec3 deltaAngular{};
                    if (inertiaB > 0.0f) deltaAngular = angularImpulse * (1.0f / inertiaB);
                    for (auto *part : component) {
                        const auto dv = deltaLinear + deltaAngular.cross(part->body.position - center);
                        const auto memberChange = energyChange(part->body, dv, deltaAngular);
                        for (int k = 0; k < 3; ++k) change[k] += memberChange[k];
                        part->body.velocity += deltaLinear + deltaAngular.cross(part->body.position - center);
                        part->body.angularVelocity += deltaAngular;
                    }
                    std::cout << "componentRotationResponse," << energyObject << ",variant=" << variantName << ",inertia=" << inertiaB
                              << ",deltaOmega=" << speed(deltaAngular) << ",impulse=" << speed(impulse)
                              << ",actualLinearWork=" << change[0] << ",actualAngularWork=" << change[1]
                              << ",actualWork=" << change[0] + change[1] << ",quadraticEnergy=" << change[2]
                              << ",anchorModelWork=" << relativeV.dot(impulse) << '\n';
                }
                sample("after_component_response");
                continue; // All members were updated together; no artificial copying stage.
            } else if (variant < 2) {
                aura::body::correctJointVelocity(parent, root, joint);
            } else {
                const auto relativeV = aura::physics::velocityAtWorldPoint(root.body, anchorB) -
                                       aura::physics::velocityAtWorldPoint(parent.body, anchorA);
                if (relativeV.lengthSquared() >= 0.000001f) {
                    const auto direction = relativeV.normalized();
                    const auto rA = anchorA - parent.body.position, rB = anchorB - root.body.position;
                    const auto inertiaResponse = [](const auto &body, const auto &v) {
                        return aura::math::Vec3{v.x / body.momentOfInertia.x,
                                               v.y / body.momentOfInertia.y, v.z / body.momentOfInertia.z};
                    };
                    // Deliberately retain the existing root rotational terms. Only
                    // the child translation inverse mass changes in this experiment.
                    const float inverseMassB = 1.0f / mass;
                    const float denominator = 1.0f / parent.body.mass + inverseMassB +
                        inertiaResponse(parent.body, rA.cross(direction)).cross(rA).dot(direction) +
                        inertiaResponse(root.body, rB.cross(direction)).cross(rB).dot(direction);
                    const auto impulse = direction * (-relativeV.dot(direction) / denominator);
                    aura::physics::applyImpulseAtPoint(parent.body, -impulse, anchorA);
                    aura::physics::applyImpulseAtPoint(root.body, impulse, anchorB);
                    root.body.velocity = oldV + impulse * inverseMassB;
                }
            }
            sample("after_parent_root_impulse");
            const auto deltaV = root.body.velocity - oldV;
            if (variant == 0) {
                const auto deltaW = root.body.angularVelocity - oldW;
                const auto deltaPivotV = deltaV - deltaW.cross(root.body.position - anchorB);
                aura::body::translateSubtreeVelocity(descendants, deltaPivotV);
                aura::body::rotateSubtreeVelocityAroundWorldPoint(descendants, anchorB, deltaW);
            } else {
                aura::body::translateSubtreeVelocity(descendants, deltaV);
            }
            sample("after_descendant_propagation");
        }
    };
    bool legReplayDone = false;
    const auto replayLegVelocity = [&] {
        auto copy = auraBody;
        const std::array parents{&copy.pelvis, &copy.rightThigh, &copy.rightShin};
        const std::array children{&copy.rightThigh, &copy.rightShin, &copy.rightFoot};
        const std::array constraints{&skeleton.rightHip, &skeleton.rightKnee, &skeleton.rightAnkle};
        const std::array names{"hip", "knee", "ankle"};
        std::array<std::vector<aura::body::BodyPart3D *>, 3> components;
        std::array<aura::math::Vec3, 3> measurementDirections;
        for (int j = 0; j < 3; ++j) {
            components[j] = skeleton.collectComponent(copy, *children[j], *constraints[j]);
            const auto a = aura::body::localToWorldPoint(*parents[j], constraints[j]->localAnchorA);
            const auto b = aura::body::localToWorldPoint(*children[j], constraints[j]->localAnchorB);
            measurementDirections[j] = (aura::physics::velocityAtWorldPoint(children[j]->body, b) -
                aura::physics::velocityAtWorldPoint(parents[j]->body, a)).normalized();
        }
        const std::array all{&copy.head, &copy.neck, &copy.torso, &copy.pelvis,
            &copy.leftUpperArm, &copy.leftForearm, &copy.leftHand,
            &copy.rightUpperArm, &copy.rightForearm, &copy.rightHand,
            &copy.leftThigh, &copy.leftShin, &copy.leftFoot,
            &copy.rightThigh, &copy.rightShin, &copy.rightFoot};
        const auto totalEnergy = [&] {
            double total = 0;
            for (const auto *part : all) total += kineticEnergy(part->body);
            return total;
        };
        std::cout << "Copied right-leg replay: t=3.725; four hip-knee-ankle / ankle-knee-hip sweeps; anchor velocity only. No angular-limit velocity, geometry, floor, or integration.\n"
                  << "record,sweep,totalKinetic,hipActual,kneeActual,ankleActual,hipMode,kneeMode,hipModeProjected,kneeModeProjected\n";
        const auto sample = [&](int sweep) {
            std::array<double, 3> actual{}, mode{}, projected{};
            for (int j = 0; j < 3; ++j) {
                const auto a = aura::body::localToWorldPoint(*parents[j], constraints[j]->localAnchorA);
                const auto b = aura::body::localToWorldPoint(*children[j], constraints[j]->localAnchorB);
                const auto parentV = aura::physics::velocityAtWorldPoint(parents[j]->body, a);
                actual[j] = speed(aura::physics::velocityAtWorldPoint(children[j]->body, b) - parentV);
                const auto relative = aura::body::componentAnchorVelocity(components[j], b, measurementDirections[j]) - parentV;
                mode[j] = speed(relative);
                projected[j] = relative.dot(measurementDirections[j]);
            }
            std::cout << "legVelocitySweep," << sweep << ',' << totalEnergy()
                      << ',' << actual[0] << ',' << actual[1] << ',' << actual[2]
                      << ',' << mode[0] << ',' << mode[1] << ',' << projected[0] << ',' << projected[1] << '\n';
        };
        sample(0);
        for (int sweep = 1; sweep <= 4; ++sweep) {
            int operation = 0;
            for (int j : {0, 1, 2, 2, 1, 0}) {
                const double before = totalEnergy();
                const auto audit = aura::body::correctJointComponentModeVelocity(*parents[j], *children[j], *constraints[j], components[j]);
                std::cout << "legVelocityImpulse," << sweep << ',' << ++operation << ',' << names[j]
                          << ',' << before << ',' << totalEnergy() << ',' << audit.predictedWork
                          << ',' << audit.linearWork << ',' << audit.angularWork << ',' << audit.quadraticEnergy
                          << ',' << audit.modeSpeedBefore << ',' << audit.modeSpeedAfter << '\n';
            }
            sample(sweep);
        }
    };
    bool localLegReplayDone = false;
    const auto replayLocalLegVelocity = [&] {
        auto copy = auraBody;
        const std::array parents{&copy.pelvis, &copy.rightThigh, &copy.rightShin};
        const std::array children{&copy.rightThigh, &copy.rightShin, &copy.rightFoot};
        const std::array constraints{&skeleton.rightHip, &skeleton.rightKnee, &skeleton.rightAnkle};
        const std::array names{"hip", "knee", "ankle"};
        const std::array all{&copy.head, &copy.neck, &copy.torso, &copy.pelvis,
            &copy.leftUpperArm, &copy.leftForearm, &copy.leftHand,
            &copy.rightUpperArm, &copy.rightForearm, &copy.rightHand,
            &copy.leftThigh, &copy.leftShin, &copy.leftFoot,
            &copy.rightThigh, &copy.rightShin, &copy.rightFoot};
        const auto totalEnergy = [&] {
            double total = 0;
            for (const auto *part : all) total += kineticEnergy(part->body);
            return total;
        };
        std::cout << "Copied local right-leg replay: t=3.725; eight hip-knee-ankle / ankle-knee-hip sweeps. Two bodies per impulse, no descendant propagation. No geometry, motors, contacts, angular limits or integration.\n"
                  << "record,sweep,totalKinetic,hipAnchorSpeed,kneeAnchorSpeed,ankleAnchorSpeed\n";
        const auto sample = [&](int sweep) {
            std::array<double, 3> actual{};
            for (int j = 0; j < 3; ++j) {
                const auto a = aura::body::localToWorldPoint(*parents[j], constraints[j]->localAnchorA);
                const auto b = aura::body::localToWorldPoint(*children[j], constraints[j]->localAnchorB);
                actual[j] = speed(aura::physics::velocityAtWorldPoint(children[j]->body, b) -
                                  aura::physics::velocityAtWorldPoint(parents[j]->body, a));
            }
            std::cout << "localLegVelocitySweep," << sweep << ',' << totalEnergy()
                      << ',' << actual[0] << ',' << actual[1] << ',' << actual[2] << '\n';
        };
        sample(0);
        for (int sweep = 1; sweep <= 8; ++sweep) {
            int operation = 0;
            for (int j : {0, 1, 2, 2, 1, 0}) {
                const double before = totalEnergy();
                const auto audit = aura::body::correctLocalJointVelocity(*parents[j], *children[j], *constraints[j]);
                const double after = totalEnergy();
                std::cout << "localLegVelocityImpulse," << sweep << ',' << ++operation << ',' << names[j]
                          << ',' << before << ',' << after << ',' << audit.predictedWork
                          << ',' << audit.measuredLinearWork << ',' << audit.measuredAngularWork << ',' << audit.quadraticEnergy
                          << ',' << after - before << ',' << audit.projectedSpeedBefore << ',' << audit.projectedSpeedAfter << '\n';
            }
            sample(sweep);
        }
    };
    bool skeletonReplayDone = false;
    const auto replaySkeletonVelocity = [&] {
        auto copy = auraBody;
        const std::array copyParts{&copy.head, &copy.neck, &copy.torso, &copy.pelvis,
            &copy.leftUpperArm, &copy.leftForearm, &copy.leftHand,
            &copy.rightUpperArm, &copy.rightForearm, &copy.rightHand,
            &copy.leftThigh, &copy.leftShin, &copy.leftFoot,
            &copy.rightThigh, &copy.rightShin, &copy.rightFoot};
        const auto remap = [&](const aura::body::BodyPart3D *part) {
            return copyParts.at(std::find(parts.begin(), parts.end(), part) - parts.begin());
        };
        auto copiedJoints = joints;
        for (auto &c : copiedJoints) { c.partA = remap(c.partA); c.partB = remap(c.partB); }
        const auto totalEnergy = [&] {
            double total = 0;
            for (const auto *part : copyParts) total += kineticEnergy(part->body);
            return total;
        };
        const auto anchorSpeed = [&](const BodyJoint &c) {
            const auto a = aura::body::localToWorldPoint(*c.partA, c.constraint.localAnchorA);
            const auto b = aura::body::localToWorldPoint(*c.partB, c.constraint.localAnchorB);
            return speed(aura::physics::velocityAtWorldPoint(c.partB->body, b) -
                         aura::physics::velocityAtWorldPoint(c.partA->body, a));
        };
        std::cout << "Copied full-skeleton velocity replay: t=3.725 backward outer iteration 1 before right-hip anchor impulse. Eight complete forward/reverse sweeps, local two-body impulses only. No integration, gravity, motors, contacts, geometry or angular limits.\n";
        for (const auto *part : copyParts) {
            const auto &b = part->body;
            std::cout << "skeletonVelocitySnapshot," << part->name << ',' << b.mass
                      << ',' << b.position.x << ',' << b.position.y << ',' << b.position.z
                      << ',' << b.orientation.w << ',' << b.orientation.x << ',' << b.orientation.y << ',' << b.orientation.z
                      << ',' << b.velocity.x << ',' << b.velocity.y << ',' << b.velocity.z
                      << ',' << b.angularVelocity.x << ',' << b.angularVelocity.y << ',' << b.angularVelocity.z
                      << ',' << b.momentOfInertia.x << ',' << b.momentOfInertia.y << ',' << b.momentOfInertia.z << '\n';
        }
        std::cout << "record,sweep,totalKinetic,maxAnchorSpeed,maxJoint,waist,neck,head,leftWrist,rightWrist,leftAnkle,rightAnkle\n";
        const auto sample = [&](int sweep) {
            double maximum = -1;
            const char *owner = "none";
            for (const auto &c : copiedJoints) {
                const double v = anchorSpeed(c);
                if (v > maximum) { maximum = v; owner = c.name; }
                std::cout << "skeletonVelocityJoint," << sweep << ',' << c.name << ',' << v << '\n';
            }
            std::cout << "skeletonVelocitySweep," << sweep << ',' << totalEnergy() << ',' << maximum << ',' << owner;
            for (int index : {0, 1, 2, 5, 8, 11, 14}) std::cout << ',' << anchorSpeed(copiedJoints[index]);
            std::cout << '\n';
        };
        sample(0);
        for (int sweep = 1; sweep <= 8; ++sweep) {
            for (int pass = 0; pass < 2; ++pass) {
                for (int n = 0; n < 15; ++n) {
                    auto &c = copiedJoints[pass == 0 ? n : 14 - n];
                    const double before = totalEnergy();
                    const auto audit = aura::body::correctLocalJointVelocity(*c.partA, *c.partB, c.constraint);
                    const double after = totalEnergy();
                    std::cout << "skeletonVelocityImpulse," << sweep << ',' << (pass == 0 ? "forward" : "backward")
                              << ',' << c.name << ',' << before << ',' << after << ',' << audit.predictedWork
                              << ',' << audit.measuredLinearWork << ',' << audit.measuredAngularWork
                              << ',' << audit.quadraticEnergy << ',' << after - before
                              << ',' << audit.projectedSpeedBefore << ',' << audit.projectedSpeedAfter << '\n';
                }
            }
            sample(sweep);
        }
        bool geometryPreserved = true;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto &old = parts[i]->body;
            const auto &now = copyParts[i]->body;
            geometryPreserved = geometryPreserved && (old.position - now.position).lengthSquared() == 0 &&
                old.orientation.w == now.orientation.w && old.orientation.x == now.orientation.x &&
                old.orientation.y == now.orientation.y && old.orientation.z == now.orientation.z;
        }
        std::cout << "skeletonVelocityGeometryPreserved," << geometryPreserved << '\n';
    };
    double peakKinetic = 0, peakLinear = 0, peakAngular = 0, peakAnchor = 0;
    const char *peakAnchorJoint = "none";
    std::size_t auditedLocalAnchors = 0;
    double maxLocalWorkDisagreement = 0, maxLocalEnergyDisagreement = 0;
    std::array<double, 3> rightLegPeakAnchor{};
    int suspiciousAnchorImpulses = 0, suspiciousLocalImpulses = 0;
    double worstAnchorEnergyRise = 0;
    const auto currentKinetic = [&] {
        double sum = 0;
        for (const auto *part : parts) sum += kineticEnergy(part->body);
        return sum;
    };
    const auto observeVelocityMetrics = [&] {
        if (!liveVelocityMetrics) return;
        const double energy = currentKinetic();
        if (std::isfinite(energy)) peakKinetic = std::max(peakKinetic, energy);
        for (const auto *part : parts) {
            const auto v = speed(part->body.velocity), w = speed(part->body.angularVelocity);
            if (std::isfinite(v)) peakLinear = std::max(peakLinear, v);
            if (std::isfinite(w)) peakAngular = std::max(peakAngular, w);
        }
        for (const auto &c : joints) {
            const auto a = aura::body::localToWorldPoint(*c.partA, c.constraint.localAnchorA);
            const auto b = aura::body::localToWorldPoint(*c.partB, c.constraint.localAnchorB);
            const auto v = speed(aura::physics::velocityAtWorldPoint(c.partB->body, b) - aura::physics::velocityAtWorldPoint(c.partA->body, a));
            if (std::isfinite(v) && v > peakAnchor) { peakAnchor = v; peakAnchorJoint = c.name; }
        }
        for (int j = 0; j < 3; ++j) {
            const auto &c = joints[12 + j];
            const auto a = aura::body::localToWorldPoint(*c.partA, c.constraint.localAnchorA);
            const auto b = aura::body::localToWorldPoint(*c.partB, c.constraint.localAnchorB);
            const auto v = speed(aura::physics::velocityAtWorldPoint(c.partB->body, b) - aura::physics::velocityAtWorldPoint(c.partA->body, a));
            if (std::isfinite(v)) rightLegPeakAnchor[j] = std::max(rightLegPeakAnchor[j], v);
        }
    };
    const auto velocitySweepSample = [&](const char *stage) {
        if (!liveVelocityMetrics) return;
        double maximum = -1;
        const char *owner = "none";
        for (const auto &c : joints) {
            const auto a = aura::body::localToWorldPoint(*c.partA, c.constraint.localAnchorA);
            const auto b = aura::body::localToWorldPoint(*c.partB, c.constraint.localAnchorB);
            const double v = speed(aura::physics::velocityAtWorldPoint(c.partB->body, b) - aura::physics::velocityAtWorldPoint(c.partA->body, a));
            if (v > maximum) { maximum = v; owner = c.name; }
        }
        std::cout << "liveVelocitySweep," << diagnosticTime << ',' << diagnosticIteration << ',' << stage
                  << ',' << currentKinetic() << ',' << maximum << ',' << owner << '\n';
    };
    using WholeMomentum = std::array<double, 3>;
    const auto wholeMomentum = [&] {
        WholeMomentum sum{};
        for (const auto *part : parts) {
            sum[0] += static_cast<double>(part->body.mass) * part->body.velocity.x;
            sum[1] += static_cast<double>(part->body.mass) * part->body.velocity.y;
            sum[2] += static_cast<double>(part->body.mass) * part->body.velocity.z;
        }
        return sum;
    };
    struct MomentumStage { int iteration; std::string stage; WholeMomentum before, after; };
    std::vector<MomentumStage> momentumStages;
    struct MomentumOperation { int iteration; std::string sweep, object, operation; WholeMomentum before, after; };
    std::vector<MomentumOperation> momentumOperations;
    bool firstAngularMomentumReported = false;
    bool firstMomentumReported = false;
    WholeMomentum stepMomentumBefore{};
    // Significant completed-step horizontal change: 0.1 kg m/s. Individual
    // operations >=0.01 kg m/s are retained in the first step's small trace.
    constexpr double horizontalMomentumThreshold = 0.1;
    std::array<WholeMomentum, 5> cumulativeMomentum{}; // integration, floor, anchor, angular-limit, geometry
    std::array<double, 5> cumulativeAbsoluteHorizontal{};
    const auto momentumStage = [&](int iteration, const char *stage, const WholeMomentum &before) {
        if (handGeometryMomentumDiagnostic && !firstMomentumReported)
            momentumStages.push_back({iteration, stage, before, wholeMomentum()});
    };
    const auto auditOperation = [&](const char *operationName, const auto &operation) {
        const auto measuredOperation = [&] {
            const std::string op = operationName;
            const auto momentumBefore = (handGeometryMomentumDiagnostic || angularMomentumDiagnostic) ? wholeMomentum() : WholeMomentum{};
            const bool angularTrace = angularMomentumDiagnostic && !firstAngularMomentumReported &&
                (op.find("Angular") != std::string::npos && op.find("Velocity") != std::string::npos);
            auto angularJoint = joints.end();
            aura::physics::RigidBody3D angularBeforeA, angularBeforeB;
            if (angularTrace) {
                angularJoint = std::find_if(joints.begin(), joints.end(), [&](const auto &c){return std::string(c.name)==energyObject;});
                if (angularJoint != joints.end()) { angularBeforeA=angularJoint->partA->body; angularBeforeB=angularJoint->partB->body; }
            }
            const bool geometryTrace = handGeometryMomentumDiagnostic && diagnosticIteration == 16 &&
                std::abs(diagnosticTime - 1.5083333333333333) < 1e-9 &&
                (op.find("Position") != std::string::npos || op.find("Angle") != std::string::npos);
            std::array<aura::physics::RigidBody3D, 16> geometryBefore{};
            double handBefore = 0, gapBefore = 0;
            auto tracedJoint = joints.end();
            if (geometryTrace) {
                for (std::size_t i = 0; i < parts.size(); ++i) geometryBefore[i] = parts[i]->body;
                handBefore = aura::physics::lowestPoint(auraBody.rightHand.body, auraBody.rightHand.size).y;
                tracedJoint = std::find_if(joints.begin(), joints.end(), [&](const auto &c) { return std::string(c.name) == energyObject; });
                if (tracedJoint != joints.end())
                    gapBefore = speed(aura::body::localToWorldPoint(*tracedJoint->partB, tracedJoint->constraint.localAnchorB) -
                        aura::body::localToWorldPoint(*tracedJoint->partA, tracedJoint->constraint.localAnchorA));
            }
            const bool anchor = std::string(operationName).find("Angular") == std::string::npos &&
                                std::string(operationName).find("Velocity") != std::string::npos;
            const double before = liveVelocityMetrics && anchor ? currentKinetic() : 0;
            operation();
            if (angularTrace && angularJoint != joints.end()) {
                const auto after = wholeMomentum();
                if (std::hypot(after[0]-momentumBefore[0],after[2]-momentumBefore[2]) > 0.01) {
                    firstAngularMomentumReported=true;
                    const auto &a=*angularJoint->partA; const auto &b=*angularJoint->partB; const auto &j=angularJoint->constraint;
                    const auto axis=a.body.orientation.rotate(j.hingeAxis.normalized()).normalized();
                    std::cout << std::scientific << std::setprecision(9)
                              << "firstAngularMomentumEvent," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep
                              << ',' << energyObject << ',' << operationName << ',' << aura::body::relativeJointAngle(a,b,j)
                              << ',' << j.minAngle << ',' << j.maxAngle << ',' << (angularBeforeB.angularVelocity-angularBeforeA.angularVelocity).dot(axis)
                              << ',' << (b.body.angularVelocity-a.body.angularVelocity).dot(axis)
                              << ',' << momentumBefore[0] << ',' << momentumBefore[2] << ',' << after[0] << ',' << after[2] << '\n';
                    const auto emit=[&](const char *side,const auto &old,const auto &now){
                        std::cout << "angularMomentumBody," << side << ',' << old.velocity.x << ',' << old.velocity.y << ',' << old.velocity.z
                                  << ',' << now.velocity.x << ',' << now.velocity.y << ',' << now.velocity.z
                                  << ',' << old.angularVelocity.x << ',' << old.angularVelocity.y << ',' << old.angularVelocity.z
                                  << ',' << now.angularVelocity.x << ',' << now.angularVelocity.y << ',' << now.angularVelocity.z << '\n';
                    };
                    emit("A",angularBeforeA,a.body); emit("B",angularBeforeB,b.body);
                }
            }
            if (geometryTrace && tracedJoint != joints.end()) {
                const double handAfter = aura::physics::lowestPoint(auraBody.rightHand.body, auraBody.rightHand.size).y;
                const double gapAfter = speed(aura::body::localToWorldPoint(*tracedJoint->partB, tracedJoint->constraint.localAnchorB) -
                    aura::body::localToWorldPoint(*tracedJoint->partA, tracedJoint->constraint.localAnchorA));
                std::string moved, translations;
                std::array<bool, 16> movedMask{};
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    const auto &old = geometryBefore[i]; const auto &now = parts[i]->body;
                    if ((old.position - now.position).lengthSquared() > 0 || old.orientation.w != now.orientation.w ||
                        old.orientation.x != now.orientation.x || old.orientation.y != now.orientation.y || old.orientation.z != now.orientation.z) {
                        movedMask[i] = true;
                        if (!moved.empty()) { moved += ';'; translations += ';'; }
                        moved += parts[i]->name;
                        const auto delta = now.position - old.position;
                        std::ostringstream detail;
                        detail << std::scientific << std::setprecision(9) << parts[i]->name << ':' << delta.x << '/' << delta.y << '/' << delta.z;
                        translations += detail.str();
                    }
                }
                const auto scope = [&](aura::body::BodyPart3D &start) {
                    const auto component = skeleton.collectComponent(auraBody, start, tracedJoint->constraint);
                    std::size_t count = 0;
                    for (auto *part : component) count += movedMask[std::find(parts.begin(), parts.end(), part) - parts.begin()];
                    return count == 0 ? "none" : count == component.size() ? "full" : "partial";
                };
                const std::string componentScope = std::string("A=") + scope(*tracedJoint->partA) + ";B=" + scope(*tracedJoint->partB);
                std::cout << "handGeometryOperation," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep
                          << ',' << energyObject << ',' << operationName << ',' << handBefore << ',' << handAfter << ',' << handAfter - handBefore
                          << ',' << gapBefore << ',' << gapAfter << ',' << (moved.empty() ? "none" : moved) << ',' << componentScope << ',' << (translations.empty() ? "none" : translations) << '\n';
            }
            if (handGeometryMomentumDiagnostic) {
                const auto after = wholeMomentum();
                const int category = op.starts_with("integrate") ? 0 : (op.starts_with("resolve") || op == "floorResponse") ? 1 :
                    op == "correctLocalJointVelocity" ? 2 : (op.find("Angular") != std::string::npos && op.find("Velocity") != std::string::npos) ? 3 : 4;
                WholeMomentum delta{};
                for (int i = 0; i < 3; ++i) { delta[i] = after[i] - momentumBefore[i]; cumulativeMomentum[category][i] += delta[i]; }
                const double horizontal = std::hypot(delta[0], delta[2]);
                cumulativeAbsoluteHorizontal[category] += horizontal;
                if (!firstMomentumReported && horizontal >= 0.01)
                    momentumOperations.push_back({diagnosticIteration, diagnosticSweep, energyObject, operationName, momentumBefore, after});
            }
            observeVelocityMetrics();
            if (liveVelocityMetrics && anchor) {
                const double after = currentKinetic();
                if (std::isfinite(before) && std::isfinite(after) && after - before > std::max(0.001, std::abs(before) * 1e-5)) {
                    ++suspiciousAnchorImpulses;
                    if (std::string(operationName) == "correctLocalJointVelocity") ++suspiciousLocalImpulses;
                    worstAnchorEnergyRise = std::max(worstAnchorEnergyRise, after - before);
                    if (suspiciousAnchorImpulses <= 20)
                        std::cout << "anchorEnergyInjection," << diagnosticTime << ',' << diagnosticIteration << ','
                                  << energyObject << ',' << operationName << ',' << before << ',' << after << '\n';
                }
            }
        };
        const bool hipReplay = std::string(energyObject) == "rightHip";
        const bool kneeReplay = std::string(energyObject) == "rightKnee";
        const int replayIndex = hipReplay ? 1 : 0;
        if (skeletonVelocityReplay && hipReplay && !skeletonReplayDone &&
            std::abs(diagnosticTime - 3.725) < 1e-9 && diagnosticIteration == 1 &&
            std::string(diagnosticSweep) == "backward" && std::string(operationName) == "correctBranchAnchorVelocityAsSubtree") {
            replaySkeletonVelocity();
            skeletonReplayDone = true;
        }
        if (localLegVelocityReplay && hipReplay && !localLegReplayDone &&
            std::abs(diagnosticTime - 3.725) < 1e-9 && diagnosticIteration == 1 &&
            std::string(diagnosticSweep) == "backward" && std::string(operationName) == "correctBranchAnchorVelocityAsSubtree") {
            replayLocalLegVelocity();
            localLegReplayDone = true;
        }
        if (legVelocityReplay && hipReplay && !legReplayDone &&
            std::abs(diagnosticTime - 3.725) < 1e-9 && diagnosticIteration == 1 &&
            std::string(diagnosticSweep) == "backward" && std::string(operationName) == "correctBranchAnchorVelocityAsSubtree") {
            replayLegVelocity();
            legReplayDone = true;
        }
        if (linearMassReplay && (hipReplay || kneeReplay) && !linearReplayDone[replayIndex] &&
            std::abs(diagnosticTime - 3.725) < 1e-9 && diagnosticIteration == 1 &&
            std::string(diagnosticSweep) == "backward" && std::string(operationName) == "correctBranchAnchorVelocityAsSubtree") {
            replayLinearMass();
            linearReplayDone[replayIndex] = true;
        }
        if (!energyWindow) { measuredOperation(); return; }
        std::array<aura::physics::RigidBody3D, 16> before{};
        for (std::size_t i = 0; i < parts.size(); ++i) before[i] = parts[i]->body;
        measuredOperation();
        double energyBefore = 0.0, energyAfter = 0.0;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            energyBefore += kineticEnergy(before[i]);
            energyAfter += kineticEnergy(parts[i]->body);
        }
        const std::string operationText = operationName;
        bool amplifiedThisOperation = false;
        if (!firstAnchorEnergyAmplification && energyBefore > 1.0 && energyAfter > 2.0 * energyBefore &&
            operationText.find("Velocity") != std::string::npos && operationText.find("Angular") == std::string::npos &&
            operationText.starts_with("correct")) {
            firstAnchorEnergyAmplification = true;
            amplifiedThisOperation = true;
            std::cout << "firstAnchorEnergyAmplification," << diagnosticTime << ',' << diagnosticIteration << ','
                      << diagnosticSweep << ',' << energyObject << ',' << operationName << ','
                      << energyBefore << ',' << energyAfter << '\n';
        }
        if ((tracingEnergy() || amplifiedThisOperation) && operationText == "correctBranchAnchorVelocityAsSubtree") {
            const auto joint = std::find_if(joints.begin(), joints.end(), [&](const auto &c) {
                return std::string(c.name) == energyObject;
            });
            if (joint != joints.end()) {
                // This corrector changes parent/root by the ordinary anchor impulse,
                // then descendants only by propagation. Reconstruct the intermediate
                // energy from those unchanged parent/root final states; no extra solve.
                double pairOnly = 0.0;
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    const bool endpoint = parts[i] == joint->partA || parts[i] == joint->partB;
                    pairOnly += kineticEnergy(endpoint ? parts[i]->body : before[i]);
                }
                std::cout << "energyDecomposition," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep
                          << ',' << energyObject << ',' << energyBefore << ',' << pairOnly << ',' << energyAfter << '\n';
            }
        }
        if (tracingEnergy()) {
            std::cout << "energyBalance," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep
                      << ',' << energyObject << ',' << operationName << ',' << energyBefore << ',' << energyAfter << '\n';
        }
        constexpr std::array thresholds{10.0, 50.0, 100.0, 1000.0, 1000000.0};
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto &after = parts[i]->body;
            const double v0 = speed(before[i].velocity), v1 = speed(after.velocity);
            const double w0 = speed(before[i].angularVelocity), w1 = speed(after.angularVelocity);
            for (std::size_t k = 0; k < thresholds.size(); ++k) {
                const auto crossing = [&](bool &seen, const char *kind, double a, double b) {
                    if (!seen && a < thresholds[k] && b >= thresholds[k]) {
                        seen = true;
                        std::cout << "energyCrossing," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep
                                  << ',' << energyObject << ',' << operationName << ',' << parts[i]->name
                                  << ',' << kind << ',' << thresholds[k] << ',' << a << ',' << b << '\n';
                    }
                };
                crossing(crossedV[k], "linear", v0, v1);
                crossing(crossedW[k], "angular", w0, w1);
            }
            if ((tracingEnergy() || amplifiedThisOperation) && (speed(after.velocity - before[i].velocity) > 0.0 ||
                                    speed(after.angularVelocity - before[i].angularVelocity) > 0.0)) {
                std::cout << "energyOperation," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep
                          << ',' << energyObject << ',' << operationName << ',' << parts[i]->name
                          << ',' << v0 << ',' << v1 << ',' << w0 << ',' << w1 << '\n';
            }
        }
    };
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
        energyObject = connection.name;
        auto &a = *connection.partA;
        auto &b = *connection.partB;
        const auto angularOperation = [&](const char *legacyName, const auto &legacy) {
            if (localAngularLimits)
                auditOperation("correctLocalJointAngularLimitVelocity", [&] {
                    aura::body::correctLocalJointAngularLimitVelocity(a, b, connection.constraint);
                });
            else auditOperation(legacyName, legacy);
        };
        const auto positionOperation = [&](const char *legacyName, const auto &legacy) {
            if (componentPositions)
                auditOperation("correctJointPositionWithComponents", [&] {
                    aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
                });
            else auditOperation(legacyName, legacy);
        };
        const auto anchorOperation = [&](const char *legacyName, const auto &legacy) {
            if (allLocalAnchors)
                auditOperation("correctLocalJointVelocity", [&] {
                    const double before = currentKinetic();
                    const auto audit = aura::body::correctLocalJointVelocity(a, b, connection.constraint);
                    const double after = currentKinetic();
                    ++auditedLocalAnchors;
                    const double work = audit.measuredLinearWork + audit.measuredAngularWork;
                    if (std::isfinite(work) && std::isfinite(audit.predictedWork))
                        maxLocalWorkDisagreement = std::max(maxLocalWorkDisagreement, std::abs(work - audit.predictedWork));
                    if (std::isfinite(before) && std::isfinite(after) && std::isfinite(work))
                        maxLocalEnergyDisagreement = std::max(maxLocalEnergyDisagreement, std::abs(after - before - work - audit.quadraticEnergy));
                });
            else auditOperation(legacyName, legacy);
        };
        if (&connection.constraint == &skeleton.waist && waistGroups) {
            auditOperation("correctJointAngle", [&] { aura::body::correctJointAngle(a, b, connection.constraint); });
            const bool eitherFootTouchesFloor =
                !aura::physics::floorContactPoints(auraBody.leftFoot.body, auraBody.leftFoot.size, 0.0f, 0.01f).empty() ||
                !aura::physics::floorContactPoints(auraBody.rightFoot.body, auraBody.rightFoot.size, 0.0f, 0.01f).empty();
            positionOperation("correctWaistPositionWithFloorContact", [&] { aura::body::correctWaistPositionWithFloorContact(auraBody, connection.constraint, eitherFootTouchesFloor); });
            for (int k = 0; k < aura::body::jointVelocityIterations; ++k) {
                anchorOperation("correctJointVelocity", [&] { aura::body::correctJointVelocity(a, b, connection.constraint); });
                angularOperation("correctJointAngularVelocity", [&] { aura::body::correctJointAngularVelocity(a, b, connection.constraint); });
            }
            return;
        }
        if (neckSubtree && &connection.constraint == &skeleton.neck) {
            const bool log = headWindow && diagnosticTime >= 0.70 - 1e-9 && diagnosticTime <= 0.82 + 1e-9;
            if (log) neckSample("A");
            auditOperation("correctNeckAngleAroundPivot", [&] { aura::body::correctNeckAngleAroundPivot(auraBody, skeleton, connection.constraint); });
            if (log) neckSample("B");
            positionOperation("correctNeckPositionAsSubtree", [&] { aura::body::correctNeckPositionAsSubtree(auraBody, connection.constraint); });
            if (log) neckSample("C");
            for (int i = 1; i <= aura::body::jointVelocityIterations; ++i) {
                anchorOperation("correctNeckAnchorVelocityAsSubtree", [&] { aura::body::correctNeckAnchorVelocityAsSubtree(auraBody, connection.constraint); });
                if (log) neckSample(("D" + std::to_string(i)).c_str());
                angularOperation("correctNeckAngularVelocityAsSubtree", [&] { aura::body::correctNeckAngularVelocityAsSubtree(auraBody, connection.constraint); });
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
            if (headPivot) auditOperation("correctHeadAngleAroundPivot", [&] { aura::body::correctHeadAngleAroundPivot(auraBody, skeleton, connection.constraint); });
            else auditOperation("correctJointAngle", [&] { aura::body::correctJointAngle(a, b, connection.constraint); });
            if (log) headSample("B");
            if (headLeafPosition) positionOperation("correctHeadPositionAsLeaf", [&] { aura::body::correctHeadPositionAsLeaf(auraBody, connection.constraint); });
            else positionOperation("correctJointPosition", [&] { aura::body::correctJointPosition(a, b, connection.constraint); });
            if (log) headSample("C");
            for (int velocityIteration = 1; velocityIteration <= aura::body::jointVelocityIterations; ++velocityIteration) {
                anchorOperation("correctJointVelocity", [&] { aura::body::correctJointVelocity(a, b, connection.constraint); });
                if (log) headSample(("D" + std::to_string(velocityIteration)).c_str());
                angularOperation("correctJointAngularVelocity", [&] { aura::body::correctJointAngularVelocity(a, b, connection.constraint); });
                if (log) headSample(("E" + std::to_string(velocityIteration)).c_str());
            }
            return;
        }
        if (headPivot && &connection.constraint == &skeleton.head) {
            auditOperation("correctHeadAngleAroundPivot", [&] { aura::body::correctHeadAngleAroundPivot(auraBody, skeleton, connection.constraint); });
            if (headLeafPosition) positionOperation("correctHeadPositionAsLeaf", [&] { aura::body::correctHeadPositionAsLeaf(auraBody, connection.constraint); });
            else positionOperation("correctJointPosition", [&] { aura::body::correctJointPosition(a, b, connection.constraint); });
            for (int k = 0; k < aura::body::jointVelocityIterations; ++k) {
                anchorOperation("correctJointVelocity", [&] { aura::body::correctJointVelocity(a, b, connection.constraint); });
                angularOperation("correctJointAngularVelocity", [&] { aura::body::correctJointAngularVelocity(a, b, connection.constraint); });
            }
            return;
        }
        if (leftShoulderSubtree && &connection.constraint == &skeleton.leftShoulder) {
            const bool log = headWindow && diagnosticTime >= 0.70 - 1e-9 && diagnosticTime <= 0.82 + 1e-9;
            if (log) shoulderSample("A");
            auditOperation("correctLeftShoulderAngleAroundPivot", [&] { aura::body::correctLeftShoulderAngleAroundPivot(auraBody, skeleton, connection.constraint); });
            if (log) shoulderSample("B");
            positionOperation("correctLeftShoulderPositionAsSubtree", [&] { aura::body::correctLeftShoulderPositionAsSubtree(auraBody, connection.constraint); });
            if (log) shoulderSample("C");
            for (int i = 1; i <= aura::body::jointVelocityIterations; ++i) {
                anchorOperation("correctLeftShoulderAnchorVelocityAsSubtree", [&] { aura::body::correctLeftShoulderAnchorVelocityAsSubtree(auraBody, connection.constraint); });
                if (log) shoulderSample(("D" + std::to_string(i)).c_str());
                angularOperation("correctLeftShoulderAngularVelocityAsSubtree", [&] { aura::body::correctLeftShoulderAngularVelocityAsSubtree(auraBody, connection.constraint); });
                if (log) shoulderSample(("E" + std::to_string(i)).c_str());
            }
            return;
        }
        if (majorBranches && (&connection.constraint == &skeleton.rightShoulder || &connection.constraint == &skeleton.leftHip || &connection.constraint == &skeleton.rightHip)) {
            const auto branch = &connection.constraint == &skeleton.rightShoulder
                ? aura::body::MajorBodyBranch3D::RightShoulder
                : &connection.constraint == &skeleton.leftHip
                    ? aura::body::MajorBodyBranch3D::LeftHip : aura::body::MajorBodyBranch3D::RightHip;
            auditOperation("correctBranchAngleAroundPivot", [&] { aura::body::correctBranchAngleAroundPivot(auraBody, skeleton, connection.constraint, branch); });
            positionOperation("correctBranchPositionAsSubtree", [&] { aura::body::correctBranchPositionAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            for (int k = 0; k < aura::body::jointVelocityIterations; ++k) {
                if (!(rightLegLocalAnchors && (branch == aura::body::MajorBodyBranch3D::RightHip || branch == aura::body::MajorBodyBranch3D::RightKnee)))
                    anchorOperation("correctBranchAnchorVelocityAsSubtree", [&] { aura::body::correctBranchAnchorVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
                angularOperation("correctBranchAngularVelocityAsSubtree", [&] { aura::body::correctBranchAngularVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            }
            return;
        }
        if (rightKneeSubtree && &connection.constraint == &skeleton.rightKnee) {
            constexpr auto branch = aura::body::MajorBodyBranch3D::RightKnee;
            const bool log = tracingAnkle();
            if (log) kneeSample("before");
            auditOperation("correctRightKneeAngleAroundPivot", [&] { aura::body::correctRightKneeAngleAroundPivot(auraBody, skeleton, connection.constraint, skeleton.rightAnkle); });
            if (log) kneeSample("afterAngle");
            positionOperation("correctBranchPositionAsSubtree", [&] { aura::body::correctBranchPositionAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            if (log) kneeSample("afterPosition");
            for (int k = 0; k < aura::body::jointVelocityIterations; ++k) {
                if (!(rightLegLocalAnchors && (branch == aura::body::MajorBodyBranch3D::RightHip || branch == aura::body::MajorBodyBranch3D::RightKnee)))
                    anchorOperation("correctBranchAnchorVelocityAsSubtree", [&] { aura::body::correctBranchAnchorVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
                angularOperation("correctBranchAngularVelocityAsSubtree", [&] { aura::body::correctBranchAngularVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            }
            if (log) kneeSample("afterVelocity");
            return;
        }
        if (tracingWrist() && (&connection.constraint == &skeleton.rightElbow ||
                               &connection.constraint == &skeleton.rightWrist)) {
            const bool elbow = &connection.constraint == &skeleton.rightElbow;
            wristSample(elbow ? "A" : "E");
            constexpr auto branch = aura::body::MajorBodyBranch3D::RightElbow;
            if (elbow && rightElbowSubtree)
                auditOperation("correctRightElbowAngleAroundPivot", [&] { aura::body::correctRightElbowAngleAroundPivot(auraBody, skeleton, connection.constraint, skeleton.rightWrist); });
            else auditOperation("correctJointAngle", [&] { aura::body::correctJointAngle(a, b, connection.constraint); });
            wristSample(elbow ? "B" : "F");
            if (elbow && rightElbowSubtree)
                positionOperation("correctBranchPositionAsSubtree", [&] { aura::body::correctBranchPositionAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            else positionOperation("correctJointPosition", [&] { aura::body::correctJointPosition(a, b, connection.constraint); });
            wristSample(elbow ? "C" : "G");
            // Expand the same velocity loop to measure each pair without changing it.
            for (int i = 1; i <= aura::body::jointVelocityIterations; ++i) {
                if (elbow && rightElbowSubtree) {
                    if (!(rightLegLocalAnchors && (branch == aura::body::MajorBodyBranch3D::RightHip || branch == aura::body::MajorBodyBranch3D::RightKnee)))
                    anchorOperation("correctBranchAnchorVelocityAsSubtree", [&] { aura::body::correctBranchAnchorVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
                    angularOperation("correctBranchAngularVelocityAsSubtree", [&] { aura::body::correctBranchAngularVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
                } else {
                    anchorOperation("correctJointVelocity", [&] { aura::body::correctJointVelocity(a, b, connection.constraint); });
                    angularOperation("correctJointAngularVelocity", [&] { aura::body::correctJointAngularVelocity(a, b, connection.constraint); });
                }
                wristSample(((elbow ? "D" : "H") + std::to_string(i)).c_str());
            }
            if (elbow && std::string(diagnosticSweep) == "backward") wristSample("I");
            return;
        }
        if (rightElbowSubtree && &connection.constraint == &skeleton.rightElbow) {
            constexpr auto branch = aura::body::MajorBodyBranch3D::RightElbow;
            auditOperation("correctRightElbowAngleAroundPivot", [&] { aura::body::correctRightElbowAngleAroundPivot(auraBody, skeleton, connection.constraint, skeleton.rightWrist); });
            positionOperation("correctBranchPositionAsSubtree", [&] { aura::body::correctBranchPositionAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            for (int k = 0; k < aura::body::jointVelocityIterations; ++k) {
                if (!(rightLegLocalAnchors && (branch == aura::body::MajorBodyBranch3D::RightHip || branch == aura::body::MajorBodyBranch3D::RightKnee)))
                    anchorOperation("correctBranchAnchorVelocityAsSubtree", [&] { aura::body::correctBranchAnchorVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
                angularOperation("correctBranchAngularVelocityAsSubtree", [&] { aura::body::correctBranchAngularVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            }
            return;
        }
        if (leftLimbComponents && (&connection.constraint == &skeleton.leftElbow || &connection.constraint == &skeleton.leftKnee)) {
            const bool elbow = &connection.constraint == &skeleton.leftElbow;
            const auto branch = elbow ? aura::body::MajorBodyBranch3D::LeftElbow : aura::body::MajorBodyBranch3D::LeftKnee;
            const auto &descendant = elbow ? skeleton.leftWrist : skeleton.leftAnkle;
            auditOperation("correctLimbAngleAroundPivot", [&] { aura::body::correctLimbAngleAroundPivot(auraBody, skeleton, connection.constraint, descendant, branch); });
            positionOperation("correctBranchPositionAsSubtree", [&] { aura::body::correctBranchPositionAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            for (int k = 0; k < aura::body::jointVelocityIterations; ++k) {
                if (!(rightLegLocalAnchors && (branch == aura::body::MajorBodyBranch3D::RightHip || branch == aura::body::MajorBodyBranch3D::RightKnee)))
                    anchorOperation("correctBranchAnchorVelocityAsSubtree", [&] { aura::body::correctBranchAnchorVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
                angularOperation("correctBranchAngularVelocityAsSubtree", [&] { aura::body::correctBranchAngularVelocityAsSubtree(auraBody, skeleton, connection.constraint, branch); });
            }
            return;
        }
        if (!connection.floorAware) {
            auditOperation("correctJointAngle", [&] { aura::body::correctJointAngle(a, b, connection.constraint); });
            positionOperation("correctJointPosition", [&] { aura::body::correctJointPosition(a, b, connection.constraint); });
            for (int k = 0; k < aura::body::jointVelocityIterations; ++k) {
                anchorOperation("correctJointVelocity", [&] { aura::body::correctJointVelocity(a, b, connection.constraint); });
                angularOperation("correctJointAngularVelocity", [&] { aura::body::correctJointAngularVelocity(a, b, connection.constraint); });
            }
            return;
        }
        const bool logAnkle = tracingAnkle() && &connection.constraint == &skeleton.rightAnkle;
        auditOperation("correctJointAngle", [&] { aura::body::correctJointAngle(a, b, connection.constraint); });
        if (logAnkle) ankleSample("E");
        const bool touchingFloor = !aura::physics::floorContactPoints(b.body, b.size, 0.0f, 0.01f).empty();
        positionOperation("correctJointPositionWithFloorContact", [&] { aura::body::correctJointPositionWithFloorContact(a, b, connection.constraint, touchingFloor); });
        if (logAnkle) ankleSample("F");
        // Same four corrections as solveJointVelocityConstraints; sample each pair.
        for (int i = 1; i <= aura::body::jointVelocityIterations; ++i) {
            if (!(rightLegLocalAnchors && &connection.constraint == &skeleton.rightAnkle))
                anchorOperation("correctJointVelocity", [&] { aura::body::correctJointVelocity(a, b, connection.constraint); });
            if (logAnkle) ankleSample(("G" + std::to_string(i) + "anchor").c_str());
            angularOperation("correctJointAngularVelocity", [&] { aura::body::correctJointAngularVelocity(a, b, connection.constraint); });
            if (logAnkle) ankleSample(("G" + std::to_string(i)).c_str());
        }
    };

    constexpr float gapThreshold = 0.001f;
    // Numerical tolerance, not an allowed anatomical range extension.
    constexpr float limitTolerance = 0.00001f;
    const int steps = contactWindow ? 48 : headWindow ? 98 : ankleWindow ? 156 : wristWindow ? 172 : energyWindow ? 532 : 1200;
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
                  << "Left elbow/knee correction=" << (leftLimbComponents ? "graph components" : "pairwise baseline") << "\n"
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
                  << "Left elbow/knee correction=" << (leftLimbComponents ? "graph components" : "pairwise baseline") << "\n"
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
    if (linearMassReplay || legVelocityReplay || localLegVelocityReplay || skeletonVelocityReplay) std::cout << std::scientific << std::setprecision(9);
    if (energyWindow) {
        std::cout << std::scientific << std::setprecision(9)
                  << "Energy window " << energyStart << "-4.43s; current left/right hierarchy, unchanged 120Hz/16 outer/4 inner and hip/knee motors. Step-end times; A-D each outer iteration; E each step. Threshold crossings audited from startup.\n"
                  << "energyGlobal,time,iteration,stage,linearOwner,maxLinearSpeed,angularOwner,maxAngularSpeed\n"
                  << "energyJoint,time,iteration,stage,joint,gap,angle,hingeOmega,anchorRelativeSpeed,relativeOmega\n"
                  << "energyContact,time,iteration,part,beforeY,afterY,beforeCount,afterCount,beforeV,afterV,beforeOmega,afterOmega,dx,dy,dz,dvx,dvy,dvz,dwx,dwy,dwz\n"
                  << "energyOperation,time,iteration,sweep,object,operation,part,beforeV,afterV,beforeOmega,afterOmega\n"
                  << "energyCrossing,time,iteration,sweep,object,operation,part,kind,threshold,before,after\n";
        std::cout << "energyBalance,time,iteration,sweep,object,operation,beforeKinetic,afterKinetic\n";
        std::cout << "energyDecomposition,time,iteration,sweep,joint,beforeKinetic,afterParentRootImpulse,afterDescendantPropagation\n";
    }
    bool componentPositionReplayDone = false;
    const auto replayComponentPositions = [&] {
        auto copy = auraBody;
        const auto old = copy;
        const auto sample = [&](const char *stage) {
            const auto gap = [&](auto &a, auto &b, const auto &joint) {
                return speed(aura::body::localToWorldPoint(b, joint.localAnchorB) - aura::body::localToWorldPoint(a, joint.localAnchorA));
            };
            std::cout << "componentPositionReplay," << stage << ','
                      << aura::physics::lowestPoint(copy.rightHand.body, copy.rightHand.size).y << ','
                      << gap(copy.rightForearm, copy.rightHand, skeleton.rightWrist) << ','
                      << gap(copy.rightUpperArm, copy.rightForearm, skeleton.rightElbow) << '\n';
        };
        sample("afterFloor");
        const auto wrist = aura::body::correctJointPositionWithComponents(copy, skeleton, copy.rightForearm, copy.rightHand, skeleton.rightWrist);
        sample("afterWrist");
        const auto elbow = aura::body::correctJointPositionWithComponents(copy, skeleton, copy.rightUpperArm, copy.rightForearm, skeleton.rightElbow);
        sample("afterElbow");
        const std::array beforeParts{&old.head,&old.neck,&old.torso,&old.pelvis,&old.leftUpperArm,&old.leftForearm,&old.leftHand,
            &old.rightUpperArm,&old.rightForearm,&old.rightHand,&old.leftThigh,&old.leftShin,&old.leftFoot,&old.rightThigh,&old.rightShin,&old.rightFoot};
        const std::array afterParts{&copy.head,&copy.neck,&copy.torso,&copy.pelvis,&copy.leftUpperArm,&copy.leftForearm,&copy.leftHand,
            &copy.rightUpperArm,&copy.rightForearm,&copy.rightHand,&copy.leftThigh,&copy.leftShin,&copy.leftFoot,&copy.rightThigh,&copy.rightShin,&copy.rightFoot};
        bool velocitiesUnchanged = true;
        for (std::size_t i=0;i<beforeParts.size();++i)
            velocitiesUnchanged = velocitiesUnchanged && (beforeParts[i]->body.velocity-afterParts[i]->body.velocity).lengthSquared()==0 &&
                (beforeParts[i]->body.angularVelocity-afterParts[i]->body.angularVelocity).lengthSquared()==0;
        std::cout << "componentPositionReplayChoices," << static_cast<int>(wrist.choice) << ',' << static_cast<int>(elbow.choice)
                  << ",velocitiesUnchanged=" << velocitiesUnchanged << '\n';
    };
    struct FloorFailureCheckpoint {
        std::array<float, 16> lowest{};
        aura::math::Vec3 comPosition{};
        aura::math::Vec3 comVelocity{};
    };
    struct FloorFailureIteration {
        std::array<FloorFailureCheckpoint, 16> afterOwnFloor;
        FloorFailureCheckpoint afterForward;
        FloorFailureCheckpoint afterBackward;
    };
    bool floorFailureReported = false;
    FloorFailureCheckpoint beginningOfStep;
    std::array<FloorFailureIteration, jointIterations> floorFailureIterations;
    const auto floorFailureCheckpoint = [&] {
        FloorFailureCheckpoint sample;
        for (std::size_t i = 0; i < parts.size(); ++i)
            sample.lowest[i] = aura::physics::lowestPoint(parts[i]->body, parts[i]->size).y;
        sample.comPosition = aura::body::componentCenterOfMass(parts);
        sample.comVelocity = aura::body::componentCenterOfMassVelocity(parts);
        return sample;
    };
    if (floorFailureDiagnostic)
        std::cout << std::scientific << std::setprecision(9)
                  << "Floor failure diagnostic: completed-step tolerance=-0.001, floor=0, contact tolerance=0.01; unchanged all-local 120Hz/16 outer/4 inner, gravity/floor and hip/knee motors ON. A=before integration, B=immediately after this body's floor solve, C/D=after full forward/backward geometry+velocity sweeps, E=end step.\n"
                  << "record,time,iteration,stage,part,lowestY,comX,comY,comZ,comVX,comVY,comVZ\n";
    if (handGeometryMomentumDiagnostic)
        std::cout << std::scientific << std::setprecision(9)
                  << "Hand geometry/momentum audit: unchanged all-local baseline; trace t=1.508333333 iteration=16; earliest completed-step horizontal momentum change >=0.1 kg m/s. Geometry list names the bodies actually moved.\n";
    // Sample after every complete step, not only at the once-per-second log interval.
    for (int frame = 0; frame < steps; ++frame) {
        diagnosticTime = (frame + 1) * FIXED_DT;
        const auto dt = static_cast<float>(FIXED_DT);
        diagnosticIteration = 0;
        diagnosticSweep = "integration";
        energyStage("stepStart");
        if (handGeometryMomentumDiagnostic) {
            momentumStages.clear(); momentumOperations.clear(); stepMomentumBefore = wholeMomentum();
        }
        const auto beforeMotorsMomentum = handGeometryMomentumDiagnostic ? wholeMomentum() : WholeMomentum{};
        if (floorFailureDiagnostic && !floorFailureReported) beginningOfStep = floorFailureCheckpoint();
        for (auto &joint: joints) {
            if (joint.motor) aura::body::applyJointMotor(*joint.partA, *joint.partB, joint.constraint);
        }
        momentumStage(0, "motors", beforeMotorsMomentum);
        const auto beforeIntegrationMomentum = handGeometryMomentumDiagnostic ? wholeMomentum() : WholeMomentum{};
        for (auto *part: parts) {
            aura::physics::applyForce(part->body, aura::math::Vec3{0.0f, -9.81f, 0.0f} * part->body.mass);
            aura::physics::updateLinearAcceleration(part->body);
            aura::physics::updateAngularAcceleration(part->body);
            energyObject = part->name.c_str();
            auditOperation("integrateLinearMotion", [&] { aura::physics::integrateLinearMotion(part->body, dt); });
            auditOperation("integrateAngularMotion", [&] { aura::physics::integrateAngularMotion(part->body, dt); });
        }
        momentumStage(0, "integrationGravity", beforeIntegrationMomentum);
        const double stepTime = (frame + 1) * FIXED_DT;
        const bool trace = contactWindow && stepTime >= 0.35 - 1e-9 && stepTime <= 0.40 + 1e-9;
        for (int iteration = 0; iteration < jointIterations; ++iteration) {
            diagnosticIteration = iteration + 1;
            diagnosticSweep = "floor";
            const auto beforeFloorMomentum = handGeometryMomentumDiagnostic ? wholeMomentum() : WholeMomentum{};
            energyStage("A_beforeFloor");
            if (tracingWrist()) wristSample("start");
            if (tracingAnkle()) ankleSample("A");
            const auto gapsA = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowA = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countA = trace ? contactCounts() : std::array<std::size_t, 16>{};
            std::array<aura::math::Vec3, 16> floorDelta{};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                auto *part = parts[i];
                const auto before = part->body.position;
                const auto floorBefore = part->body;
                const auto floorBeforeY = tracingEnergy() ? aura::physics::lowestPoint(part->body, part->size).y : 0.0f;
                const auto floorBeforeCount = tracingEnergy() ? aura::physics::floorContactPoints(part->body, part->size, 0.0f, 0.01f).size() : 0;
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
                    aura::body::correctBranchPositionAsSubtree(replay, skeleton, skeleton.rightElbow,
                        aura::body::MajorBodyBranch3D::RightElbow);
                    handPositionSample(replay, "after_elbow_position");
                    aura::body::correctBranchPositionAsSubtree(replay, skeleton, skeleton.rightShoulder,
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
                        aura::body::correctBranchPositionAsSubtree(replay, skeleton, skeleton.rightElbow,
                            aura::body::MajorBodyBranch3D::RightElbow);
                        aura::body::solveBranchSubtreeVelocityConstraints(replay, skeleton, skeleton.rightElbow,
                            aura::body::MajorBodyBranch3D::RightElbow);
                        aura::body::correctBranchAngleAroundPivot(replay, skeleton, skeleton.rightShoulder,
                            aura::body::MajorBodyBranch3D::RightShoulder);
                        aura::body::correctBranchPositionAsSubtree(replay, skeleton, skeleton.rightShoulder,
                            aura::body::MajorBodyBranch3D::RightShoulder);
                        aura::body::solveBranchSubtreeVelocityConstraints(replay, skeleton, skeleton.rightShoulder,
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
                energyObject = part->name.c_str();
                auditOperation("floorResponse", [&] {
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
                });
                if (floorFailureDiagnostic && !floorFailureReported)
                    floorFailureIterations[iteration].afterOwnFloor[i] = floorFailureCheckpoint();
                if (tracingEnergy()) {
                    const auto floorAfterY = aura::physics::lowestPoint(part->body, part->size).y;
                    const auto floorAfterCount = aura::physics::floorContactPoints(part->body, part->size, 0.0f, 0.01f).size();
                    if (floorBeforeCount || floorAfterCount) {
                        const auto dp = part->body.position - floorBefore.position;
                        const auto dv = part->body.velocity - floorBefore.velocity;
                        const auto dw = part->body.angularVelocity - floorBefore.angularVelocity;
                        std::cout << "energyContact," << diagnosticTime << ',' << diagnosticIteration << ',' << part->name
                                  << ',' << floorBeforeY << ',' << floorAfterY << ',' << floorBeforeCount << ',' << floorAfterCount
                                  << ',' << speed(floorBefore.velocity) << ',' << speed(part->body.velocity)
                                  << ',' << speed(floorBefore.angularVelocity) << ',' << speed(part->body.angularVelocity)
                                  << ',' << dp.x << ',' << dp.y << ',' << dp.z
                                  << ',' << dv.x << ',' << dv.y << ',' << dv.z
                                  << ',' << dw.x << ',' << dw.y << ',' << dw.z << '\n';
                    }
                }
                if (tracingWrist() && part == &auraBody.rightForearm) wristSample("floorAfterForearm");
                if (tracingWrist() && part == &auraBody.rightHand) wristSample("floorAfterHand");
                if (tracingAnkle() && part == &auraBody.rightShin) ankleSample("AshinAfter");
                if (tracingAnkle() && part == &auraBody.rightFoot) ankleSample("B");
                if (trace) floorDelta[i] = part->body.position - before;
            }
            momentumStage(iteration + 1, "floor", beforeFloorMomentum);
            const auto beforeForwardMomentum = handGeometryMomentumDiagnostic ? wholeMomentum() : WholeMomentum{};
            if (componentPositionReplay && !componentPositionReplayDone && diagnosticIteration == 16 &&
                std::abs(diagnosticTime - 1.5083333333333333) < 1e-9) {
                std::cout << std::scientific << std::setprecision(9);
                replayComponentPositions(); componentPositionReplayDone = true;
            }
            const auto gapsB = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowB = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countB = trace ? contactCounts() : std::array<std::size_t, 16>{};
            energyStage("B_afterFloor");
            diagnosticSweep = "forward";
            for (auto &joint: joints) {
                if (tracingAnkle() && &joint.constraint == &skeleton.rightHip) ankleSample("Cbefore");
                solveConnection(joint);
                if (tracingAnkle() && &joint.constraint == &skeleton.rightHip) ankleSample("C");
                if (tracingAnkle() && &joint.constraint == &skeleton.rightKnee) ankleSample("D");
            }
            momentumStage(iteration + 1, "jointForwardGeometryVelocity", beforeForwardMomentum);
            const auto beforeBackwardMomentum = handGeometryMomentumDiagnostic ? wholeMomentum() : WholeMomentum{};
            const auto gapsC = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowC = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countC = trace ? contactCounts() : std::array<std::size_t, 16>{};
            if (floorFailureDiagnostic && !floorFailureReported)
                floorFailureIterations[iteration].afterForward = floorFailureCheckpoint();
            energyStage("C_afterForward");
            velocitySweepSample("afterForwardGeometryVelocity");
            diagnosticSweep = "backward";
            std::array<float, 15> beforeLastWaist{};
            for (int i = static_cast<int>(joints.size()) - 2; i >= 0; --i) {
                if (trace && i == 0) beforeLastWaist = anchorGaps();
                solveConnection(joints[i]);
                if (tracingAnkle() && &joints[i].constraint == &skeleton.rightKnee) ankleSample("H");
                if (tracingAnkle() && &joints[i].constraint == &skeleton.rightHip) ankleSample("I");
            }
            if (rightLegLocalAnchors) {
                diagnosticSweep = "rightLegLocal";
                for (int pass = 0; pass < aura::body::jointVelocityIterations; ++pass) {
                    for (int index : {12, 13, 14, 14, 13, 12}) {
                        auto &joint = joints[index];
                        energyObject = joint.name;
                        auditOperation("correctLocalJointVelocity", [&] {
                            aura::body::correctLocalJointVelocity(*joint.partA, *joint.partB, joint.constraint);
                        });
                    }
                }
            }
            if (floorFailureDiagnostic && !floorFailureReported)
                floorFailureIterations[iteration].afterBackward = floorFailureCheckpoint();
            momentumStage(iteration + 1, "jointBackwardGeometryVelocity", beforeBackwardMomentum);
            energyStage("D_afterBackward");
            velocitySweepSample("afterBackwardGeometryVelocity");
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

        energyStage("E_endStep");
        observeVelocityMetrics();
        const double time = (frame + 1) * FIXED_DT;
        if (handGeometryMomentumDiagnostic && !firstMomentumReported) {
            const auto after = wholeMomentum();
            const double change = std::hypot(after[0] - stepMomentumBefore[0], after[2] - stepMomentumBefore[2]);
            if (change >= horizontalMomentumThreshold) {
                firstMomentumReported = true;
                std::cout << "firstHorizontalMomentumStep," << time << ',' << change << '\n';
                for (const auto &row : momentumStages) {
                    std::cout << "momentumStage," << time << ',' << row.iteration << ',' << row.stage;
                    for (double x : row.before) std::cout << ',' << x;
                    for (double x : row.after) std::cout << ',' << x;
                    std::cout << ',' << row.after[0] - row.before[0] << ',' << row.after[2] - row.before[2] << '\n';
                }
                for (const auto &row : momentumOperations) {
                    std::cout << "momentumOperation," << time << ',' << row.iteration << ',' << row.sweep << ',' << row.object << ',' << row.operation;
                    for (double x : row.before) std::cout << ',' << x;
                    for (double x : row.after) std::cout << ',' << x;
                    std::cout << ',' << row.after[0] - row.before[0] << ',' << row.after[2] - row.before[2] << '\n';
                }
            }
        }
        if (floorFailureDiagnostic) {
            const auto end = floorFailureCheckpoint();
            std::cout << "bodyCOM," << time << ',' << end.comPosition.x << ',' << end.comPosition.y << ',' << end.comPosition.z
                      << ',' << end.comVelocity.x << ',' << end.comVelocity.y << ',' << end.comVelocity.z << '\n';
            if (!floorFailureReported && *std::min_element(end.lowest.begin(), end.lowest.end()) < -0.001f) {
                floorFailureReported = true;
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    if (end.lowest[i] >= -0.001f) continue;
                    const auto *part = parts[i];
                    std::cout << "firstFloorFailure," << time << ',' << part->name << ',' << end.lowest[i]
                              << ',' << aura::physics::floorContactPoints(part->body, part->size, 0.0f, 0.01f).size() << '\n';
                    const auto emit = [&](int iteration, const char *stage, const FloorFailureCheckpoint &sample) {
                        std::cout << "floorFailureStage," << time << ',' << iteration << ',' << stage << ',' << part->name
                                  << ',' << sample.lowest[i] << ',' << sample.comPosition.x << ',' << sample.comPosition.y
                                  << ',' << sample.comPosition.z << ',' << sample.comVelocity.x << ',' << sample.comVelocity.y
                                  << ',' << sample.comVelocity.z << '\n';
                    };
                    emit(0, "A_beginStep", beginningOfStep);
                    for (int iteration = 0; iteration < jointIterations; ++iteration) {
                        const auto &samples = floorFailureIterations[iteration];
                        emit(iteration + 1, "B_afterOwnFloor", samples.afterOwnFloor[i]);
                        emit(iteration + 1, "C_afterForward", samples.afterForward);
                        emit(iteration + 1, "D_afterBackward", samples.afterBackward);
                    }
                    emit(jointIterations, "E_endStep", end);
                }
            }
        }
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
                  << "Left elbow/knee correction=" << (leftLimbComponents ? "graph components" : "pairwise baseline") << "\n"
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
    if (liveVelocityMetrics) {
        std::cout << "Velocity experiment: allLocalAnchors=" << allLocalAnchors << " rightLegLocalAnchors=" << rightLegLocalAnchors
                  << " peakKinetic=" << peakKinetic << " peakLinearSpeed=" << peakLinear
                  << " peakAngularSpeed=" << peakAngular << " maxAnchorSpeed=" << peakAnchor << " maxAnchorJoint=" << peakAnchorJoint << " rightHipMaxAnchorSpeed=" << rightLegPeakAnchor[0]
                  << " rightKneeMaxAnchorSpeed=" << rightLegPeakAnchor[1] << " rightAnkleMaxAnchorSpeed=" << rightLegPeakAnchor[2]
                  << " auditedLocalAnchors=" << auditedLocalAnchors << " maxLocalWorkDisagreement=" << maxLocalWorkDisagreement
                  << " maxLocalEnergyDisagreement=" << maxLocalEnergyDisagreement << " suspiciousAnchorImpulses=" << suspiciousAnchorImpulses
                  << " suspiciousLocalImpulses=" << suspiciousLocalImpulses << " worstAnchorEnergyRise=" << worstAnchorEnergyRise << '\n';
    }
    if (handGeometryMomentumDiagnostic) {
        const std::array categories{"integrationGravity", "floor", "localAnchor", "angularLimitVelocity", "geometryVelocityCompensation"};
        for (std::size_t i = 0; i < categories.size(); ++i)
            std::cout << "momentumCumulative," << categories[i] << ',' << cumulativeMomentum[i][0] << ','
                      << cumulativeMomentum[i][1] << ',' << cumulativeMomentum[i][2] << ',' << cumulativeAbsoluteHorizontal[i] << '\n';
    }
    if (angularMomentumDiagnostic && !firstAngularMomentumReported)
        std::cout << "angularMomentumDiagnostic,no operation exceeded 0.01 kg m/s horizontal momentum change\n";
    std::cout << "Position/angular policy: componentPositions=" << componentPositions << " localAngularLimits=" << localAngularLimits << '\n';
    // This diagnostic reports threshold violations without declaring the baseline stable.
    return finite ? 0 : 1;
}
