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
    bool headPivot = false;
    bool headLeafPosition = false;
    bool neckSubtree = false;
    bool waistGroups = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--contact-window") contactWindow = true;
        else if (argument == "--waist-groups") waistGroups = true;
        else if (argument == "--head-window") headWindow = true;
        else if (argument == "--head-pivot") headPivot = true;
        else if (argument == "--head-leaf-position") headLeafPosition = true;
        else if (argument == "--neck-subtree") neckSubtree = true;
        else {
            std::cerr << "Usage: aura_full_body_diagnostics [--contact-window | --head-window] [--waist-groups] [--head-pivot] [--head-leaf-position] [--neck-subtree]\n";
            return 2;
        }
    }
    if (contactWindow && headWindow) {
        std::cerr << "Choose one diagnostic window.\n";
        return 2;
    }
    // This experiment measures the waist-group baseline, never the original pairwise waist.
    if (headWindow) waistGroups = true;
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
        std::cout << "neck," << diagnosticTime << ',' << diagnosticIteration << ',' << diagnosticSweep << ',' << stage
                  << ',' << aura::body::relativeJointAngle(auraBody.torso, auraBody.neck, skeleton.neck)
                  << ',' << (neckAnchor - torsoAnchor).length() << ',' << (headAnchor - neckHeadAnchor).length()
                  << ',' << aura::body::relativeJointAngle(auraBody.neck, auraBody.head, skeleton.head) << '\n';
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
            aura::body::correctNeckAngleAroundPivot(auraBody, connection.constraint);
            if (log) neckSample("B");
            aura::body::correctNeckPositionAsSubtree(auraBody, connection.constraint);
            if (log) neckSample("C");
            aura::body::solveJointVelocityConstraints(a, b, connection.constraint);
            if (log) neckSample("E");
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
            if (headPivot) aura::body::correctHeadAngleAroundPivot(auraBody, connection.constraint);
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
            aura::body::correctHeadAngleAroundPivot(auraBody, connection.constraint);
            if (headLeafPosition) aura::body::correctHeadPositionAsLeaf(auraBody, connection.constraint);
            else aura::body::correctJointPosition(a, b, connection.constraint);
            aura::body::solveJointVelocityConstraints(a, b, connection.constraint);
            return;
        }
        if (!connection.floorAware) {
            aura::body::solveJoint(a, b, connection.constraint);
            return;
        }
        aura::body::correctJointAngle(a, b, connection.constraint);
        const bool touchingFloor = !aura::physics::floorContactPoints(b.body, b.size, 0.0f, 0.01f).empty();
        aura::body::correctJointPositionWithFloorContact(a, b, connection.constraint, touchingFloor);
        aura::body::solveJointVelocityConstraints(a, b, connection.constraint);
    };

    constexpr float gapThreshold = 0.001f;
    // Numerical tolerance, not an allowed anatomical range extension.
    constexpr float limitTolerance = 0.00001f;
    const int steps = contactWindow ? 48 : headWindow ? 98 : 1200;
    std::array<JointMeasurements, 15> measurements{};
    bool finite = true;
    float maxDistance = 0.0f;
    float minimumLowestY = 0.0f;
    double firstFloorContact = std::numeric_limits<double>::infinity();
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
                  << "Neck correction=" << (neckSubtree ? "child subtree" : "pairwise") << "\n"
                  << "Head correction window: 0.70-0.82s; waist groups ON; hip/knee motors only; 120Hz; 16 iterations.\n"
                  << "Near-limit band=0.05 rad plus reference step 0.741667s; head limits=[-0.5,+0.5]; step END times; iterations 1-based.\n"
                  << "A=before angle, B=after angle, C=after position, D1..D4=after anchor velocity, E1..E4=after angular velocity; four inner iterations.\n"
                  << "head,time,iteration,sweep,stage,angle,hingeOmega,headGap,neckGap,headSpeed,neckSpeed,anchorRelVx,anchorRelVy,anchorRelVz,anchorRelSpeed\n"
                  << "neck,time,iteration,sweep,stage,neckAngle,neckGap,headGap,headRelativeAngle\n";
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
            const auto gapsA = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowA = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countA = trace ? contactCounts() : std::array<std::size_t, 16>{};
            std::array<aura::math::Vec3, 16> floorDelta{};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                auto *part = parts[i];
                const auto before = part->body.position;
                aura::physics::resolveFloorCollision(part->body, part->size, 0.0f, dt / jointIterations);
                if (trace) floorDelta[i] = part->body.position - before;
            }
            const auto gapsB = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowB = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countB = trace ? contactCounts() : std::array<std::size_t, 16>{};
            diagnosticSweep = "forward";
            for (auto &joint: joints) solveConnection(joint);
            const auto gapsC = trace ? anchorGaps() : std::array<float, 15>{};
            const auto lowC = trace ? lowestPoints() : std::array<float, 16>{};
            const auto countC = trace ? contactCounts() : std::array<std::size_t, 16>{};
            diagnosticSweep = "backward";
            std::array<float, 15> beforeLastWaist{};
            for (int i = static_cast<int>(joints.size()) - 2; i >= 0; --i) {
                if (trace && i == 0) beforeLastWaist = anchorGaps();
                solveConnection(joints[i]);
            }
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
            std::cerr << "Nonfinite state at t=" << time << "s; stopping run.\n";
            break;
        }
    }

    if (contactWindow || headWindow) {
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
                  << std::setw(12) << timeText(m.firstLimitFailure) << std::setw(12) << m.maxGap
                  << std::setw(16) << m.maxOvershoot << std::setw(16) << m.peakRelativeOmega
                  << std::setw(16) << m.peakHingeOmega << '\n';
    }
    // This diagnostic reports threshold violations without declaring the baseline stable.
    return finite ? 0 : 1;
}
