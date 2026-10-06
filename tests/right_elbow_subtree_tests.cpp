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
    auto relativeVelocity(const AuraBody3D &body, const Joint3D &wrist) {
        return physics::velocityAtWorldPoint(body.rightHand.body, localToWorldPoint(body.rightHand, wrist.localAnchorB)) -
               physics::velocityAtWorldPoint(body.rightForearm.body, localToWorldPoint(body.rightForearm, wrist.localAnchorA));
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
    constexpr auto branch = MajorBodyBranch3D::RightElbow;
    for (bool tilted: {false, true}) for (float angle: {-0.5f, 0.6f, 2.7f}) {
        auto body = createAuraBody3D();
        const auto skeleton = createAuraSkeleton3D(body);
        body.rightUpperArm.body.position.y += 10.0f;
        if (tilted) body.rightUpperArm.body.orientation = math::Quaternion::fromAxisAngle({1, 2, 3}, 0.7f);
        assemble(body.rightUpperArm, body.rightForearm, skeleton.rightElbow, angle);
        body.rightForearm.body.position += math::Vec3{0.003f, -0.002f, 0.001f};
        assemble(body.rightForearm, body.rightHand, skeleton.rightWrist, -0.2f);
        body.rightForearm.body.velocity = {0.1f, 0.2f, 0.3f};
        body.rightHand.body.velocity = {-0.2f, 0.4f, 0.1f};
        body.rightForearm.body.angularVelocity = {0.2f, 0.3f, 0.4f};
        body.rightHand.body.angularVelocity = {0.25f, 0.28f, 0.41f};
        const auto beforeV = relativeVelocity(body, skeleton.rightWrist);
        const auto beforeOmega = body.rightHand.body.angularVelocity - body.rightForearm.body.angularVelocity;
        const auto upperArmPosition = body.rightUpperArm.body.position;
        const auto leftForearm = body.leftForearm.body;
        correctRightElbowAngleAroundPivot(body, skeleton.rightElbow, skeleton.rightWrist);
        check(std::abs(relativeJointAngle(body.rightUpperArm, body.rightForearm, skeleton.rightElbow) -
                       std::clamp(angle, 0.0f, 2.4f)) < tolerance, "elbow enforces both limits with a rotated parent");
        check((relativeVelocity(body, skeleton.rightWrist) - beforeV).length() < tolerance,
              "angle correction preserves existing wrist anchor-relative velocity");
        correctBranchPositionAsSubtree(body, skeleton.rightElbow, branch);
        solveBranchSubtreeVelocityConstraints(body, skeleton.rightElbow, branch);
        check(gap(body.rightUpperArm, body.rightForearm, skeleton.rightElbow).length() < tolerance &&
              gap(body.rightForearm, body.rightHand, skeleton.rightWrist).length() < tolerance,
              "elbow repair closes its anchor without separating wrist");
        check(std::abs(relativeJointAngle(body.rightForearm, body.rightHand, skeleton.rightWrist) + 0.2f) < tolerance,
              "wrist angle survives all elbow corrections");
        check((relativeVelocity(body, skeleton.rightWrist) - beforeV).length() < tolerance &&
              (body.rightHand.body.angularVelocity - body.rightForearm.body.angularVelocity - beforeOmega).length() < tolerance,
              "elbow velocity correction preserves wrist linear and angular relative motion");
        check((body.rightUpperArm.body.position - upperArmPosition).length() == 0.0f &&
              (body.leftForearm.body.position - leftForearm.position).length() == 0.0f &&
              (body.leftForearm.body.velocity - leftForearm.velocity).length() == 0.0f,
              "airborne elbow geometry leaves parent fixed and left arm unchanged");
    }
    return passed ? 0 : 1;
}
