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

    }
    auto body = aura::body::createAuraBody3D();
    body.rightShin.body.position.y += 2.0f;
    const auto beforeFoot = body.rightFoot.body;
    aura::body::resolveRightShinFloorWithFootTranslation(body, 1.0f / 1920.0f);
    check((body.rightFoot.body.position - beforeFoot.position).length() == 0.0f,
          "foot is untouched when shin has no positional floor correction");
    // Contact without penetration must also leave descendant velocity untouched.
    body.rightShin.body.position.y = 0.8f;
    body.rightShin.body.velocity = {0.2f, -1.0f, 0.3f};
    body.rightShin.body.angularVelocity = {0.1f, 0.2f, 0.3f};
    const auto oldFoot = body.rightFoot.body;
    const auto beforeShinV = body.rightShin.body.velocity;
    const auto beforeShinPosition = body.rightShin.body.position;
    aura::body::resolveRightShinFloorWithFootTranslation(body, 1.0f / 1920.0f);
    check((body.rightShin.body.position - beforeShinPosition).lengthSquared() == 0 &&
          (body.rightShin.body.velocity - beforeShinV).length() > 0.01f &&
          (body.rightFoot.body.velocity - oldFoot.velocity).lengthSquared() == 0 &&
          (body.rightFoot.body.angularVelocity - oldFoot.angularVelocity).lengthSquared() == 0,
          "contact changes shin velocity without copying motion to foot");
    return passed ? 0 : 1;
}
