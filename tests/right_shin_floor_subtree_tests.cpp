#include <cmath>
#include <iostream>

#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"

int main() {
    bool passed = true;
    constexpr float tolerance = 2e-6f;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    for (bool tilted: {false, true}) for (float footY: {0.15f, -0.4f}) {
        auto body = aura::body::createAuraBody3D();
        const auto skeleton = aura::body::createAuraSkeleton3D(body);
        body.rightShin.body.position.y = 0.6f;
        body.rightFoot.body.position.y = footY;
        if (tilted) body.rightShin.body.orientation = aura::math::Quaternion::fromAxisAngle({1, 2, 3}, 0.5f);
        body.rightShin.body.velocity = {1.0f, -2.0f, 0.5f};
        body.rightShin.body.angularVelocity = {0.2f, 0.3f, 0.4f};
        body.rightFoot.body.velocity = {-0.2f, 0.4f, 0.1f};
        body.rightFoot.body.angularVelocity = {0.25f, 0.28f, 0.41f};
        auto motion = body;
        auto reference = body.rightShin.body;
        const auto oldFoot = body.rightFoot.body;
        const auto oldShinPosition = body.rightShin.body.position;
        const auto leftShinPosition = body.leftShin.body.position;
        const auto ankleGap = [&] {
            return aura::body::localToWorldPoint(body.rightFoot, skeleton.rightAnkle.localAnchorB) -
                   aura::body::localToWorldPoint(body.rightShin, skeleton.rightAnkle.localAnchorA);
        };
        const auto beforeGap = ankleGap();
        const float beforeAngle = aura::body::relativeJointAngle(body.rightShin, body.rightFoot, skeleton.rightAnkle);
        constexpr float dt = 1.0f / 120.0f / 16.0f;
        aura::physics::resolveFloorCollision(reference, body.rightShin.size, 0.0f, dt);
        aura::body::resolveRightShinFloorWithFootTranslation(body, dt);
        check((ankleGap() - beforeGap).length() < tolerance &&
              std::abs(aura::body::relativeJointAngle(body.rightShin, body.rightFoot, skeleton.rightAnkle) - beforeAngle) < tolerance,
              "shin floor translation preserves ankle gap and relative angle");
        check(((body.rightShin.body.position - oldShinPosition) - (body.rightFoot.body.position - oldFoot.position)).length() < tolerance,
              "shin and foot receive identical translations, including floor guard");
        check(aura::physics::lowestPoint(body.rightShin.body, body.rightShin.size).y >= -tolerance &&
              aura::physics::lowestPoint(body.rightFoot.body, body.rightFoot.size).y >= -tolerance,
              "floor guard keeps both parts above Y=0");
        check((body.rightShin.body.velocity - reference.velocity).length() == 0.0f &&
              (body.rightShin.body.angularVelocity - reference.angularVelocity).length() == 0.0f,
              "shin impulse and damping response exactly matches existing resolver");
        check((body.rightFoot.body.velocity - oldFoot.velocity).length() == 0.0f &&
              (body.rightFoot.body.angularVelocity - oldFoot.angularVelocity).length() == 0.0f,
              "floor impulse is not propagated to foot");
        check((body.leftShin.body.position - leftShinPosition).length() == 0.0f,
              "left leg stays unchanged");
        const auto relativeVelocity = [&](const auto &state) {
            return aura::physics::velocityAtWorldPoint(state.rightFoot.body,
                    aura::body::localToWorldPoint(state.rightFoot, skeleton.rightAnkle.localAnchorB)) -
                   aura::physics::velocityAtWorldPoint(state.rightShin.body,
                    aura::body::localToWorldPoint(state.rightShin, skeleton.rightAnkle.localAnchorA));
        };
        const auto beforeRelativeV = relativeVelocity(motion);
        const auto beforeRelativeOmega = motion.rightFoot.body.angularVelocity - motion.rightShin.body.angularVelocity;
        const auto beforeShinOmega = motion.rightShin.body.angularVelocity;
        aura::body::resolveRightShinFloorWithFootMotion(motion, dt);
        const auto deltaOmega = motion.rightShin.body.angularVelocity - beforeShinOmega;
        const auto expectedRelativeV = beforeRelativeV + deltaOmega.cross(beforeGap);
        check((relativeVelocity(motion) - expectedRelativeV).length() < 1e-5f,
              "anchor relative velocity changes only by deltaOmega cross existing anchor gap");
        check((motion.rightFoot.body.angularVelocity - motion.rightShin.body.angularVelocity - beforeRelativeOmega).length() < tolerance,
              "floor velocity propagation preserves relative angular velocity");
        check((motion.rightShin.body.velocity - reference.velocity).length() == 0.0f &&
              (motion.rightShin.body.angularVelocity - reference.angularVelocity).length() == 0.0f &&
              (motion.rightShin.body.position - body.rightShin.body.position).length() == 0.0f &&
              (motion.rightFoot.body.position - body.rightFoot.body.position).length() == 0.0f,
              "motion propagation keeps existing shin response and positional correction unchanged");
    }
    auto body = aura::body::createAuraBody3D();
    body.rightShin.body.position.y += 2.0f;
    const auto beforeFoot = body.rightFoot.body;
    aura::body::resolveRightShinFloorWithFootTranslation(body, 1.0f / 1920.0f);
    check((body.rightFoot.body.position - beforeFoot.position).length() == 0.0f,
          "foot is untouched when shin has no positional floor correction");
    // Touching without penetration: position-only helper returns early, but floor
    // damping/impulses still change velocity and must reach the descendant.
    body.rightShin.body.position.y = 0.8f;
    body.rightShin.body.velocity = {0.2f, -1.0f, 0.3f};
    body.rightShin.body.angularVelocity = {0.1f, 0.2f, 0.3f};
    const auto skeleton = aura::body::createAuraSkeleton3D(body);
    body.rightFoot.body.orientation = body.rightShin.body.orientation;
    body.rightFoot.body.position = aura::body::localToWorldPoint(body.rightShin, skeleton.rightAnkle.localAnchorA) -
                                  body.rightFoot.body.orientation.rotate(skeleton.rightAnkle.localAnchorB);
    const auto anchorRelativeV = [&] {
        return aura::physics::velocityAtWorldPoint(body.rightFoot.body,
                aura::body::localToWorldPoint(body.rightFoot, skeleton.rightAnkle.localAnchorB)) -
               aura::physics::velocityAtWorldPoint(body.rightShin.body,
                aura::body::localToWorldPoint(body.rightShin, skeleton.rightAnkle.localAnchorA));
    };
    const auto beforeRelativeV = anchorRelativeV();
    const auto beforeRelativeOmega = body.rightFoot.body.angularVelocity - body.rightShin.body.angularVelocity;
    const auto beforeShinV = body.rightShin.body.velocity;
    const auto beforeShinPosition = body.rightShin.body.position;
    aura::body::resolveRightShinFloorWithFootMotion(body, 1.0f / 1920.0f);
    check((body.rightShin.body.position - beforeShinPosition).length() == 0.0f &&
          (body.rightShin.body.velocity - beforeShinV).length() > 0.01f &&
          (anchorRelativeV() - beforeRelativeV).length() < tolerance &&
          (body.rightFoot.body.angularVelocity - body.rightShin.body.angularVelocity - beforeRelativeOmega).length() < tolerance,
          "matched anchors preserve velocity through floor contact with no positional correction");
    return passed ? 0 : 1;
}
