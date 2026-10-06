#include <cmath>
#include <iostream>

#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/JointGeometry.hpp"

int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "FAIL " << message << '\n';
            passed = false;
        }
    };
    constexpr float tolerance = 1e-5f;
    for (bool tiltedNeck: {false, true}) {
        for (float angle: {-0.8f, -0.3f, 0.0f, 0.3f, 0.8f}) {
            auto body = aura::body::createAuraBody3D();
            const auto skeleton = aura::body::createAuraSkeleton3D(body);
            if (tiltedNeck)
                body.neck.body.orientation = aura::math::Quaternion::fromAxisAngle({1, 2, 3}, 0.7f);
            // Include swing away from the hinge to exercise the local/world multiplication convention.
            body.head.body.orientation = body.neck.body.orientation *
                aura::math::Quaternion::fromAxisAngle({1, 0, 0}, angle) *
                aura::math::Quaternion::fromAxisAngle({0, 1, 0}, 0.2f);
            body.head.body.position = aura::body::localToWorldPoint(body.neck, skeleton.head.localAnchorA) -
                body.head.body.orientation.rotate(skeleton.head.localAnchorB) + aura::math::Vec3{0.001f, -0.002f, 0.003f};
            body.head.body.velocity = {0.2f, -0.3f, 0.4f};
            body.head.body.angularVelocity = {0.1f, 0.2f, 0.3f};
            const auto beforeHead = body.head.body;
            const auto beforeNeck = body.neck.body;
            const auto headAnchor = aura::body::localToWorldPoint(body.head, skeleton.head.localAnchorB);
            const auto neckAnchor = aura::body::localToWorldPoint(body.neck, skeleton.neck.localAnchorB);
            aura::body::correctHeadAngleAroundPivot(body, skeleton, skeleton.head);
            const float expected = angle < -0.5f ? -0.5f : angle > 0.5f ? 0.5f : angle;
            check(std::abs(aura::body::relativeJointAngle(body.neck, body.head, skeleton.head) - expected) < tolerance,
                  "head reaches either limit and keeps allowed angles with a rotated parent");
            check((aura::body::localToWorldPoint(body.head, skeleton.head.localAnchorB) - headAnchor).length() < tolerance,
                  "pivot correction preserves the head's world anchor despite an existing gap");
            check((aura::body::localToWorldPoint(body.neck, skeleton.neck.localAnchorB) - neckAnchor).length() < tolerance &&
                  (body.neck.body.position - beforeNeck.position).length() == 0.0f &&
                  body.neck.body.orientation.w == beforeNeck.orientation.w &&
                  body.neck.body.orientation.x == beforeNeck.orientation.x &&
                  body.neck.body.orientation.y == beforeNeck.orientation.y &&
                  body.neck.body.orientation.z == beforeNeck.orientation.z,
                  "head leaf correction leaves the neck and its torso anchor unchanged");
            check((body.head.body.velocity - beforeHead.velocity).length() == 0.0f &&
                  (body.head.body.angularVelocity - beforeHead.angularVelocity).length() == 0.0f,
                  "geometric pivot correction leaves velocities unchanged");
            if (std::abs(angle) <= 0.5f)
                check((body.head.body.position - beforeHead.position).length() == 0.0f,
                      "allowed angle does not move the head");
            const auto angleBeforePosition = aura::body::relativeJointAngle(body.neck, body.head, skeleton.head);
            aura::body::correctHeadPositionAsLeaf(body, skeleton.head);
            check((aura::body::localToWorldPoint(body.head, skeleton.head.localAnchorB) -
                   aura::body::localToWorldPoint(body.neck, skeleton.head.localAnchorA)).length() < tolerance,
                  "leaf position correction closes a three-dimensional anchor error");
            check((body.neck.body.position - beforeNeck.position).length() == 0.0f &&
                  (aura::body::localToWorldPoint(body.neck, skeleton.neck.localAnchorB) - neckAnchor).length() == 0.0f,
                  "head position repair preserves the neck and its torso anchor exactly");
            check(std::abs(aura::body::relativeJointAngle(body.neck, body.head, skeleton.head) - angleBeforePosition) < tolerance &&
                  (body.head.body.velocity - beforeHead.velocity).length() == 0.0f &&
                  (body.head.body.angularVelocity - beforeHead.angularVelocity).length() == 0.0f,
                  "head position repair leaves orientations and velocities unchanged");
        }
    }
    return passed ? 0 : 1;
}
