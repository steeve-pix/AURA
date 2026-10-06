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
    auto gap(const BodyPart3D &a, const BodyPart3D &b, const Joint3D &joint) {
        return localToWorldPoint(b, joint.localAnchorB) - localToWorldPoint(a, joint.localAnchorA);
    }
    auto relativeVelocity(const AuraBody3D &body, const Joint3D &ankle) {
        return physics::velocityAtWorldPoint(body.rightFoot.body, localToWorldPoint(body.rightFoot, ankle.localAnchorB)) -
               physics::velocityAtWorldPoint(body.rightShin.body, localToWorldPoint(body.rightShin, ankle.localAnchorA));
    }
    void assemble(BodyPart3D &a, BodyPart3D &b, const Joint3D &joint, float angle) {
        b.body.orientation = a.body.orientation * math::Quaternion::fromAxisAngle({1, 0, 0}, angle);
        b.body.position = localToWorldPoint(a, joint.localAnchorA) - b.body.orientation.rotate(joint.localAnchorB);
    }
}
int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    constexpr float tolerance = 2e-5f;
    constexpr auto branch = MajorBodyBranch3D::RightKnee;
    for (bool tilted: {false, true}) for (float angle: {-0.5f, 0.6f, 2.5f}) {
        auto body = createAuraBody3D();
        const auto skeleton = createAuraSkeleton3D(body);
        body.rightThigh.body.position.y += 10.0f;
        if (tilted) body.rightThigh.body.orientation = math::Quaternion::fromAxisAngle({1, 2, 3}, 0.7f);
        assemble(body.rightThigh, body.rightShin, skeleton.rightKnee, angle);
        body.rightShin.body.position += math::Vec3{0.003f, -0.002f, 0.001f};
        assemble(body.rightShin, body.rightFoot, skeleton.rightAnkle, -0.2f);
        body.rightShin.body.velocity = {0.1f, 0.2f, 0.3f};
        body.rightFoot.body.velocity = {-0.2f, 0.4f, 0.1f};
        body.rightShin.body.angularVelocity = {0.2f, 0.3f, 0.4f};
        body.rightFoot.body.angularVelocity = {0.25f, 0.28f, 0.41f};
        const auto beforeV = relativeVelocity(body, skeleton.rightAnkle);
        const auto beforeOmega = body.rightFoot.body.angularVelocity - body.rightShin.body.angularVelocity;
        const auto thighPosition = body.rightThigh.body.position;
        const auto leftShin = body.leftShin.body;
        correctRightKneeAngleAroundPivot(body, skeleton, skeleton.rightKnee, skeleton.rightAnkle);
        check(std::abs(relativeJointAngle(body.rightThigh, body.rightShin, skeleton.rightKnee) -
                       std::clamp(angle, 0.0f, 2.2f)) < tolerance, "knee enforces both limits with a rotated parent");
        check((relativeVelocity(body, skeleton.rightAnkle) - beforeV).length() < tolerance,
              "angle correction preserves existing ankle anchor-relative velocity");
        correctBranchPositionAsSubtree(body, skeleton, skeleton.rightKnee, branch);
        solveBranchSubtreeVelocityConstraints(body, skeleton, skeleton.rightKnee, branch);
        check(gap(body.rightThigh, body.rightShin, skeleton.rightKnee).length() < tolerance &&
              gap(body.rightShin, body.rightFoot, skeleton.rightAnkle).length() < tolerance,
              "knee repair closes its anchor without separating ankle");
        check(std::abs(relativeJointAngle(body.rightShin, body.rightFoot, skeleton.rightAnkle) + 0.2f) < tolerance,
              "ankle angle survives all knee corrections");
        check((relativeVelocity(body, skeleton.rightAnkle) - beforeV).length() < tolerance &&
              (body.rightFoot.body.angularVelocity - body.rightShin.body.angularVelocity - beforeOmega).length() < tolerance,
              "knee velocity correction preserves ankle linear and angular relative motion");
        check((body.rightThigh.body.position - thighPosition).length() == 0.0f &&
              (body.leftShin.body.position - leftShin.position).length() == 0.0f &&
              (body.leftShin.body.velocity - leftShin.velocity).length() == 0.0f,
              "airborne knee geometry leaves parent fixed and left leg unchanged");
    }
    for (float errorY: {-0.02f, 0.02f}) {
        auto body = createAuraBody3D();
        const auto skeleton = createAuraSkeleton3D(body);
        body.rightThigh.body.position.y -= errorY;
        const auto beforeGap = gap(body.rightShin, body.rightFoot, skeleton.rightAnkle);
        correctBranchPositionAsSubtree(body, skeleton, skeleton.rightKnee, branch);
        check(gap(body.rightThigh, body.rightShin, skeleton.rightKnee).length() < tolerance &&
              (gap(body.rightShin, body.rightFoot, skeleton.rightAnkle) - beforeGap).length() < tolerance,
              "floor-aware knee translation preserves ankle and closes knee");
        check(physics::lowestPoint(body.rightFoot.body, body.rightFoot.size).y >= -tolerance,
              "translation does not push planted foot downward");
    }
    // A bent leg straightens downward; floor protection must preserve all attachments.
    auto body = createAuraBody3D();
    const auto skeleton = createAuraSkeleton3D(body);
    assemble(body.rightThigh, body.rightShin, skeleton.rightKnee, -0.5f);
    assemble(body.rightShin, body.rightFoot, skeleton.rightAnkle, 0.0f);
    const std::array subtree{&body.rightShin, &body.rightFoot};
    const float lowest = physics::lowestPoint(body.rightFoot.body, body.rightFoot.size).y;
    translateSubtree(subtree, {0, -lowest, 0});
    body.rightThigh.body.position.y -= lowest;
    const auto hipGap = gap(body.pelvis, body.rightThigh, skeleton.rightHip);
    correctRightKneeAngleAroundPivot(body, skeleton, skeleton.rightKnee, skeleton.rightAnkle);
    check(physics::lowestPoint(body.rightFoot.body, body.rightFoot.size).y >= -tolerance &&
          gap(body.rightShin, body.rightFoot, skeleton.rightAnkle).length() < tolerance &&
          gap(body.rightThigh, body.rightShin, skeleton.rightKnee).length() < tolerance &&
          (gap(body.pelvis, body.rightThigh, skeleton.rightHip) - hipGap).length() < tolerance,
          "floor-protected angle correction preserves hip, knee and ankle gaps");
    return passed ? 0 : 1;
}
