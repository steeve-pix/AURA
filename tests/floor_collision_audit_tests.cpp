#include <cmath>
#include <iostream>
#include "aura/physics/Collision.hpp"

int main() {
    using namespace aura;
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    for (bool tilted : {false, true}) for (bool airborne : {false, true}) {
        physics::RigidBody3D body;
        body.mass = 2.0f;
        body.momentOfInertia = {0.3f, 0.4f, 0.5f};
        body.position = {1.0f, airborne ? 5.0f : 0.4f, -2.0f};
        body.velocity = {3.0f, -2.0f, -4.0f};
        body.angularVelocity = {0.7f, -0.3f, 0.8f};
        if (tilted) body.orientation = math::Quaternion::fromAxisAngle({1, 2, 3}, 0.6f);
        const auto before = body;
        auto unaudited = body;
        physics::FloorCollisionAudit audit;
        audit.contacts.push_back({99, 99, 99}); // Must be reset even when airborne.
        physics::resolveFloorCollision(body, {1, 1, 1}, 0.0f, 1.0f / 1920.0f, &audit);
        physics::resolveFloorCollision(unaudited, {1, 1, 1}, 0.0f, 1.0f / 1920.0f);
        check((body.position-unaudited.position).lengthSquared()==0 &&
              (body.velocity-unaudited.velocity).lengthSquared()==0 &&
              (body.angularVelocity-unaudited.angularVelocity).lengthSquared()==0,
              "auditing leaves the exact response unchanged");
        check((body.velocity-before.velocity-audit.dampingDeltaVelocity-audit.normalDeltaVelocity).length()<1e-5f &&
              (body.angularVelocity-before.angularVelocity-audit.dampingDeltaOmega-audit.normalDeltaOmega).length()<1e-5f,
              "damping and normal contributions account for the full velocity response");
        check(audit.normalDeltaVelocity.x==0 && audit.normalDeltaVelocity.z==0,
              "normal impulses do not change horizontal linear momentum");
        check(audit.dampingDeltaVelocity.x*before.velocity.x<=0 &&
              audit.dampingDeltaVelocity.z*before.velocity.z<=0,
              "horizontal damping opposes the body's horizontal velocity");
        if (!airborne) {
            const float angularFactor = 1.0f - 1.5f / 1920.0f;
            check((audit.dampingDeltaOmega - (before.angularVelocity*angularFactor-before.angularVelocity)).lengthSquared()==0,
                  "angular damping actually mutates angular velocity");
        }
        math::Vec3 impulse{};
        for (const auto &j : audit.normalImpulses) {
            check(j.x==0 && j.z==0 && j.y>=0, "contact impulses point upward only");
            impulse += j;
        }
        check((impulse-audit.normalDeltaVelocity*body.mass).length()<1e-5f,
              "per-contact impulse sums agree with the measured normal momentum change");
        check(airborne ? audit.contacts.empty() : !audit.contacts.empty(), "audit records the actual contacts");
    }
    return passed ? 0 : 1;
}
