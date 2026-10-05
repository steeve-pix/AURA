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
    for (bool touchingFloor: {false, true}) {
        for (float sign: {-1.0f, 1.0f}) {
            auto body = aura::body::createAuraBody3D();
            const auto skeleton = aura::body::createAuraSkeleton3D(body);
            body.torso.body.mass = 2.0f;
            body.pelvis.body.mass = 3.0f;
            // Create a known waist error, keeping the original joint anchors.
            body.torso.body.position -= aura::math::Vec3{0.3f, sign * 0.2f, -0.1f};
            const std::array<aura::body::BodyPart3D *, 16> parts{
                &body.head, &body.neck, &body.torso, &body.pelvis,
                &body.leftUpperArm, &body.leftForearm, &body.leftHand,
                &body.rightUpperArm, &body.rightForearm, &body.rightHand,
                &body.leftThigh, &body.leftShin, &body.leftFoot,
                &body.rightThigh, &body.rightShin, &body.rightFoot
            };
            std::array<aura::math::Vec3, 16> before{};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                before[i] = parts[i]->body.position;
                parts[i]->body.velocity = {1.0f, 2.0f, 3.0f};
                parts[i]->body.angularVelocity = {0.5f, 0.0f, 0.0f};
            }
            const float leftFootY = aura::physics::lowestPoint(body.leftFoot.body, body.leftFoot.size).y;
            const float rightFootY = aura::physics::lowestPoint(body.rightFoot.body, body.rightFoot.size).y;
            aura::body::correctWaistPositionWithFloorContact(body, skeleton.waist, touchingFloor);
            const auto waistGap = aura::body::localToWorldPoint(body.pelvis, skeleton.waist.localAnchorB) -
                                  aura::body::localToWorldPoint(body.torso, skeleton.waist.localAnchorA);
            check(waistGap.length() < tolerance, "group correction closes the waist gap");
            const bool blocked = touchingFloor && sign > 0.0f;
            const aura::math::Vec3 expectedUpper{0.18f, sign * (blocked ? 0.2f : 0.12f), -0.06f};
            const aura::math::Vec3 expectedLower{-0.12f, blocked ? 0.0f : -sign * 0.08f, 0.04f};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const bool upper = i < 3 || (i >= 4 && i < 10);
                check((parts[i]->body.position - before[i] - (upper ? expectedUpper : expectedLower)).length() < tolerance,
                      "every group member translates equally, preserving internal anchor offsets");
                check((parts[i]->body.velocity - aura::math::Vec3{1, 2, 3}).length() < tolerance &&
                      (parts[i]->body.angularVelocity - aura::math::Vec3{0.5f, 0, 0}).length() < tolerance,
                      "position correction leaves velocities unchanged");
                const auto &q = parts[i]->body.orientation;
                check(q.w == 1.0f && q.x == 0.0f && q.y == 0.0f && q.z == 0.0f,
                      "group translation leaves orientations unchanged");
            }
            if (touchingFloor) {
                check(aura::physics::lowestPoint(body.leftFoot.body, body.leftFoot.size).y >= leftFootY - tolerance &&
                      aura::physics::lowestPoint(body.rightFoot.body, body.rightFoot.size).y >= rightFootY - tolerance,
                      "contact blocks downward group motion and permits upward motion");
            }
        }
    }
    return passed ? 0 : 1;
}
