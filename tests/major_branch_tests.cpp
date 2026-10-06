#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"

using namespace aura;
using namespace aura::body;

namespace {
    auto gap(const BodyPart3D &a, const BodyPart3D &b, const Joint3D &j) {
        return localToWorldPoint(b, j.localAnchorB) - localToWorldPoint(a, j.localAnchorA);
    }
    auto relativeVelocity(const BodyPart3D &a, const BodyPart3D &b, const Joint3D &j) {
        return physics::velocityAtWorldPoint(b.body, localToWorldPoint(b, j.localAnchorB)) -
               physics::velocityAtWorldPoint(a.body, localToWorldPoint(a, j.localAnchorA));
    }
    void assemble(BodyPart3D &a, BodyPart3D &b, const Joint3D &j, float angle) {
        b.body.orientation = a.body.orientation * math::Quaternion::fromAxisAngle({1, 0, 0}, angle);
        b.body.position = localToWorldPoint(a, j.localAnchorA) - b.body.orientation.rotate(j.localAnchorB);
    }
}

int main() {
    bool passed = true;
    constexpr float tolerance = 1e-5f;
    for (auto branch: {MajorBodyBranch3D::RightShoulder, MajorBodyBranch3D::LeftHip, MajorBodyBranch3D::RightHip}) {
        const bool shoulder = branch == MajorBodyBranch3D::RightShoulder;
        const bool left = branch == MajorBodyBranch3D::LeftHip;
        const char *name = shoulder ? "right shoulder" : left ? "left hip" : "right hip";
        bool branchPassed = true;
        const auto check = [&](bool condition, const char *message) {
            if (!condition) { std::cerr << "FAIL " << name << ": " << message << '\n'; branchPassed = false; passed = false; }
        };
        for (bool tilted: {false, true}) for (float angle: {-1.8f, -0.4f, 0.4f, 1.8f}) {
            auto body = createAuraBody3D();
            const auto skeleton = createAuraSkeleton3D(body);
            auto &parent = shoulder ? body.torso : body.pelvis;
            auto &root = shoulder ? body.rightUpperArm : left ? body.leftThigh : body.rightThigh;
            auto &middle = shoulder ? body.rightForearm : left ? body.leftShin : body.rightShin;
            auto &end = shoulder ? body.rightHand : left ? body.leftFoot : body.rightFoot;
            const auto &joint = shoulder ? skeleton.rightShoulder : left ? skeleton.leftHip : skeleton.rightHip;
            const auto &first = shoulder ? skeleton.rightElbow : left ? skeleton.leftKnee : skeleton.rightKnee;
            const auto &second = shoulder ? skeleton.rightWrist : left ? skeleton.leftAnkle : skeleton.rightAnkle;
            // Raised fixture isolates hierarchy behavior from floor handling.
            parent.body.position.y += 10.0f;
            if (tilted) parent.body.orientation = math::Quaternion::fromAxisAngle({1, 2, 3}, 0.7f);
            assemble(parent, root, joint, angle);
            root.body.position += math::Vec3{0.003f, -0.002f, 0.001f};
            assemble(root, middle, first, 0.6f);
            assemble(middle, end, second, 0.2f);
            const float gap1 = gap(root, middle, first).length();
            const float gap2 = gap(middle, end, second).length();
            const auto pivot = localToWorldPoint(root, joint.localAnchorB);
            correctBranchAngleAroundPivot(body, joint, branch);
            check(std::abs(relativeJointAngle(parent, root, joint) - std::clamp(angle, joint.minAngle, joint.maxAngle)) < tolerance,
                  "angle correction enforces both limits");
            check((localToWorldPoint(root, joint.localAnchorB) - pivot).length() < tolerance,
                  "rotation preserves the joint pivot");
            check(std::abs(gap(root, middle, first).length() - gap1) < tolerance &&
                  std::abs(gap(middle, end, second).length() - gap2) < tolerance,
                  "rotation preserves descendant gaps");
            correctBranchPositionAsSubtree(body, joint, branch);
            check(gap(parent, root, joint).length() < tolerance &&
                  gap(root, middle, first).length() < tolerance && gap(middle, end, second).length() < tolerance,
                  "position correction repairs parent without separating descendants");
            check(std::abs(relativeJointAngle(root, middle, first) - 0.6f) < tolerance &&
                  std::abs(relativeJointAngle(middle, end, second) - 0.2f) < tolerance,
                  "geometry corrections preserve descendant angles");
            root.body.velocity = {0.1f, 0.2f, 0.3f};
            middle.body.velocity = {0.2f, 0.1f, 0.4f};
            end.body.velocity = {0.3f, -0.1f, 0.2f};
            root.body.angularVelocity = {0.2f, 0.3f, 0.4f};
            middle.body.angularVelocity = {0.25f, 0.28f, 0.41f};
            end.body.angularVelocity = {0.22f, 0.31f, 0.45f};
            const auto v1 = relativeVelocity(root, middle, first), v2 = relativeVelocity(middle, end, second);
            const auto w1 = middle.body.angularVelocity - root.body.angularVelocity;
            const auto w2 = end.body.angularVelocity - middle.body.angularVelocity;
            solveBranchSubtreeVelocityConstraints(body, joint, branch);
            check((relativeVelocity(root, middle, first) - v1).length() < tolerance &&
                  (relativeVelocity(middle, end, second) - v2).length() < tolerance,
                  "velocity corrections preserve descendant anchor-relative velocities");
            check((middle.body.angularVelocity - root.body.angularVelocity - w1).length() < tolerance &&
                  (end.body.angularVelocity - middle.body.angularVelocity - w2).length() < tolerance,
                  "velocity corrections preserve descendant relative angular velocities");
        }
        if (!shoulder) {
            for (float verticalError: {-0.02f, 0.02f}) {
                auto body = createAuraBody3D();
                const auto skeleton = createAuraSkeleton3D(body);
                auto &root = left ? body.leftThigh : body.rightThigh;
                auto &middle = left ? body.leftShin : body.rightShin;
                auto &foot = left ? body.leftFoot : body.rightFoot;
                const auto &joint = left ? skeleton.leftHip : skeleton.rightHip;
                const auto &knee = left ? skeleton.leftKnee : skeleton.rightKnee;
                const auto &ankle = left ? skeleton.leftAnkle : skeleton.rightAnkle;
                // Floor-planted foot; introduce a hip error by moving the parent.
                body.pelvis.body.position.y -= verticalError;
                const auto kneeGap = gap(root, middle, knee), ankleGap = gap(middle, foot, ankle);
                const auto otherFootPosition = left ? body.rightFoot.body.position : body.leftFoot.body.position;
                correctBranchPositionAsSubtree(body, joint, branch);
                check(gap(body.pelvis, root, joint).length() < tolerance && physics::lowestPoint(foot.body, foot.size).y >= -tolerance,
                      "hip closes while planted foot remains above floor");
                check((gap(root, middle, knee) - kneeGap).length() < tolerance &&
                      (gap(middle, foot, ankle) - ankleGap).length() < tolerance,
                      "floor-aware translation preserves knee and ankle");
                const auto otherFootDelta = (left ? body.rightFoot.body.position : body.leftFoot.body.position) - otherFootPosition;
                check(verticalError < 0.0f ? otherFootDelta.length() < tolerance :
                      std::abs(otherFootDelta.y - verticalError) < tolerance,
                      "blocked downward correction moves complementary group upward");
            }
            // An over-bent leg is clamped to upright; this lowers its foot.
            auto body = createAuraBody3D();
            const auto skeleton = createAuraSkeleton3D(body);
            auto &root = left ? body.leftThigh : body.rightThigh;
            auto &middle = left ? body.leftShin : body.rightShin;
            auto &foot = left ? body.leftFoot : body.rightFoot;
            const auto &original = left ? skeleton.leftHip : skeleton.rightHip;
            const auto &knee = left ? skeleton.leftKnee : skeleton.rightKnee;
            const auto &ankle = left ? skeleton.leftAnkle : skeleton.rightAnkle;
            auto joint = original; joint.minAngle = joint.maxAngle = 0.0f;
            assemble(body.pelvis, root, joint, 0.5f);
            assemble(root, middle, knee, 0.0f); assemble(middle, foot, ankle, 0.0f);
            const auto parts = std::array{&body.head, &body.neck, &body.torso, &body.pelvis,
                &body.leftUpperArm, &body.leftForearm, &body.leftHand, &body.rightUpperArm, &body.rightForearm, &body.rightHand,
                &body.leftThigh, &body.leftShin, &body.leftFoot, &body.rightThigh, &body.rightShin, &body.rightFoot};
            translateSubtree(parts, {0, -physics::lowestPoint(foot.body, foot.size).y, 0});
            const auto hipGap = gap(body.pelvis, root, joint);
            const auto waistGap = gap(body.torso, body.pelvis, skeleton.waist);
            correctBranchAngleAroundPivot(body, joint, branch);
            check(physics::lowestPoint(foot.body, foot.size).y >= -tolerance &&
                  std::abs(relativeJointAngle(body.pelvis, root, joint)) < tolerance,
                  "hip limit rotation clears floor without leaving angle error");
            check((gap(body.pelvis, root, joint) - hipGap).length() < tolerance &&
                  (gap(body.torso, body.pelvis, skeleton.waist) - waistGap).length() < tolerance &&
                  gap(root, middle, knee).length() < tolerance && gap(middle, foot, ankle).length() < tolerance,
                  "floor lift preserves hip, waist and descendant attachment");
        }
        std::cout << name << ": " << (branchPassed ? "PASS" : "FAIL") << '\n';
    }
    return passed ? 0 : 1;
}
