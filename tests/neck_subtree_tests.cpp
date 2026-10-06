#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"

int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "FAIL " << message << '\n';
            passed = false;
        }
    };
    constexpr float tolerance = 1e-5f;
    for (bool rotatedParent: {false, true}) {
        for (float neckAngle: {-0.8f, -0.3f, 0.3f, 0.8f}) {
            auto body = aura::body::createAuraBody3D();
            const auto skeleton = aura::body::createAuraSkeleton3D(body);
            if (rotatedParent)
                body.torso.body.orientation = aura::math::Quaternion::fromAxisAngle({1, 2, 3}, 0.7f);
            body.neck.body.orientation = body.torso.body.orientation *
                aura::math::Quaternion::fromAxisAngle({1, 0, 0}, neckAngle) *
                aura::math::Quaternion::fromAxisAngle({0, 1, 0}, 0.2f);
            body.neck.body.position = aura::body::localToWorldPoint(body.torso, skeleton.neck.localAnchorA) -
                body.neck.body.orientation.rotate(skeleton.neck.localAnchorB) + aura::math::Vec3{0.003f, -0.002f, 0.001f};
            body.head.body.orientation = body.neck.body.orientation *
                aura::math::Quaternion::fromAxisAngle({1, 0, 0}, 0.3f);
            body.head.body.position = aura::body::localToWorldPoint(body.neck, skeleton.head.localAnchorA) -
                body.head.body.orientation.rotate(skeleton.head.localAnchorB) + aura::math::Vec3{0.002f, 0.001f, -0.001f};
            body.neck.body.velocity = {1, 2, 3};
            body.head.body.velocity = {-1, 2, -3};
            body.neck.body.angularVelocity = {0.1f, 0.2f, 0.3f};
            body.head.body.angularVelocity = {-0.1f, 0.2f, -0.3f};
            const auto torsoBefore = body.torso.body;
            const auto headBefore = body.head.body;
            const auto neckBefore = body.neck.body;
            const auto armBefore = body.leftUpperArm.body;
            const auto neckPivot = aura::body::localToWorldPoint(body.neck, skeleton.neck.localAnchorB);
            const auto headGap = [&] {
                return aura::body::localToWorldPoint(body.head, skeleton.head.localAnchorB) -
                       aura::body::localToWorldPoint(body.neck, skeleton.head.localAnchorA);
            };
            const float beforeHeadGap = headGap().length();
            aura::body::correctNeckAngleAroundPivot(body, skeleton, skeleton.neck);
            check(std::abs(aura::body::relativeJointAngle(body.torso, body.neck, skeleton.neck) -
                           std::clamp(neckAngle, -0.5f, 0.5f)) < tolerance,
                  "neck subtree rotation enforces either limit with a rotated parent");
            check((aura::body::localToWorldPoint(body.neck, skeleton.neck.localAnchorB) - neckPivot).length() < tolerance,
                  "subtree rotation preserves the neck pivot");
            check(std::abs(headGap().length() - beforeHeadGap) < tolerance &&
                  std::abs(aura::body::relativeJointAngle(body.neck, body.head, skeleton.head) - 0.3f) < tolerance,
                  "rigid subtree rotation preserves internal head gap and relative orientation");
            const auto gapAfterRotation = headGap();
            aura::body::correctNeckPositionAsSubtree(body, skeleton.neck);
            check((aura::body::localToWorldPoint(body.neck, skeleton.neck.localAnchorB) -
                   aura::body::localToWorldPoint(body.torso, skeleton.neck.localAnchorA)).length() < tolerance,
                  "subtree translation closes the neck gap");
            check((headGap() - gapAfterRotation).length() < tolerance,
                  "subtree translation preserves the complete internal head gap vector");
            check((body.torso.body.position - torsoBefore.position).length() == 0.0f &&
                  body.torso.body.orientation.w == torsoBefore.orientation.w &&
                  body.torso.body.orientation.x == torsoBefore.orientation.x &&
                  body.torso.body.orientation.y == torsoBefore.orientation.y &&
                  body.torso.body.orientation.z == torsoBefore.orientation.z &&
                  (body.leftUpperArm.body.position - armBefore.position).length() == 0.0f,
                  "neck correction leaves parent and sibling branch fixed");
            check((body.head.body.velocity - headBefore.velocity).length() == 0.0f &&
                  (body.neck.body.velocity - neckBefore.velocity).length() == 0.0f &&
                  (body.head.body.angularVelocity - headBefore.angularVelocity).length() == 0.0f &&
                  (body.neck.body.angularVelocity - neckBefore.angularVelocity).length() == 0.0f,
                  "geometric subtree corrections leave velocities unchanged");

            const auto relativeHeadVelocity = [&] {
                return aura::physics::velocityAtWorldPoint(body.head.body,
                           aura::body::localToWorldPoint(body.head, skeleton.head.localAnchorB)) -
                       aura::physics::velocityAtWorldPoint(body.neck.body,
                           aura::body::localToWorldPoint(body.neck, skeleton.head.localAnchorA));
            };
            const auto headVelocityBefore = relativeHeadVelocity();
            const auto relativeOmegaBefore = body.head.body.angularVelocity - body.neck.body.angularVelocity;
            const auto neckOmegaBefore = body.neck.body.angularVelocity;
            const auto headPositionBeforeVelocity = body.head.body.position;
            const auto neckPositionBeforeVelocity = body.neck.body.position;
            const auto internalGap = headGap();
            aura::body::solveNeckSubtreeVelocityConstraints(body, skeleton.neck);
            const auto deltaOmega = body.neck.body.angularVelocity - neckOmegaBefore;
            check((body.head.body.angularVelocity - body.neck.body.angularVelocity - relativeOmegaBefore).length() < tolerance,
                  "neck velocity corrections preserve descendant relative angular velocity");
            // Rigid velocity fields evaluated at separated anchor points differ by deltaOmega x gap.
            check((relativeHeadVelocity() - headVelocityBefore - deltaOmega.cross(internalGap)).length() < tolerance,
                  "descendant anchor velocity changes only by the rigid field across its existing gap");
            check((body.head.body.position - headPositionBeforeVelocity).length() == 0.0f &&
                  (body.neck.body.position - neckPositionBeforeVelocity).length() == 0.0f,
                  "subtree velocity correction does not change geometry");
        }
    }

    // Pure velocity translation and rotation induce one common rigid velocity field.
    aura::body::BodyPart3D first, second;
    first.body.position = {1, 2, 3};
    second.body.position = {-2, 4, 1};
    const std::array parts{&first, &second};
    const aura::math::Vec3 pivot{0.5f, -1, 2}, deltaV{0.2f, 0.3f, -0.1f}, deltaOmega{0.4f, -0.2f, 0.6f};
    aura::body::translateSubtreeVelocity(parts, deltaV);
    aura::body::rotateSubtreeVelocityAroundWorldPoint(parts, pivot, deltaOmega);
    for (const auto *part: parts) {
        check((part->body.angularVelocity - deltaOmega).length() < tolerance &&
              (part->body.velocity - deltaV - deltaOmega.cross(part->body.position - pivot)).length() < tolerance,
              "subtree helpers add pivot-induced linear velocity and common angular velocity");
    }
    return passed ? 0 : 1;
}
