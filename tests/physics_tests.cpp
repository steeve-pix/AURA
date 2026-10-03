#include <cmath>
#include <iostream>
#include <numbers>

#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Motion.hpp"
#include "aura/physics/RigidBody3D.hpp"

int main() {
    aura::physics::RigidBody3D body;
    body.position = {0.0f, 0.0f, 0.0f};
    body.velocity = {2.0f, 0.0f, 0.0f};

    aura::physics::integrateLinearMotion(body, 0.5f);

    std::cout << body.position.x << ", " << body.position.y
              << ", " << body.position.z << '\n';

    constexpr float tolerance = 1e-6f;
    bool passed = std::abs(body.position.x - 1.0f) <= tolerance &&
                        std::abs(body.position.y) <= tolerance &&
                        std::abs(body.position.z) <= tolerance;
    if (!passed) {
        std::cerr << "FAIL linear motion: expected (1, 0, 0)\n";
    }

    const auto check = [&](bool condition, const char *name) {
        if (!condition) {
            std::cerr << "FAIL " << name << '\n';
            passed = false;
        }
    };
    const auto checkVec = [&](const aura::math::Vec3 &actual,
                              const aura::math::Vec3 &expected, const char *name) {
        check((actual - expected).length() <= tolerance, name);
    };

    // The same force translates both bodies; only the offset force turns one.
    for (const bool aboveCenter : {false, true}) {
        aura::physics::RigidBody3D pushed;
        pushed.position = {0.0f, 2.0f, 0.0f};
        pushed.mass = 2.0f;
        pushed.momentOfInertia = {2.0f, 2.0f, 2.0f};
        const auto point = pushed.position +
                           aura::math::Vec3{0.0f, aboveCenter ? 0.5f : 0.0f, 0.0f};

        aura::physics::applyForceAtPoint(pushed, {10.0f, 0.0f, 0.0f}, point);
        checkVec(pushed.torque, {0.0f, 0.0f, aboveCenter ? -5.0f : 0.0f},
                 "force at point produces expected torque");
        aura::physics::updateLinearAcceleration(pushed);
        aura::physics::updateAngularAcceleration(pushed);
        aura::physics::integrateLinearMotion(pushed, 0.2f);
        aura::physics::integrateAngularMotion(pushed, 0.2f);

        checkVec(pushed.velocity, {1.0f, 0.0f, 0.0f}, "force accelerates translation");
        checkVec(pushed.position, {0.2f, 2.0f, 0.0f}, "force moves body");
        checkVec(pushed.angularVelocity, {0.0f, 0.0f, aboveCenter ? -0.5f : 0.0f},
                 "offset controls angular motion");
        const auto rotated = pushed.orientation.rotate({1.0f, 0.0f, 0.0f});
        if (aboveCenter) {
            check(rotated.y < -0.09f, "offset force visibly changes orientation");
        } else {
            checkVec(rotated, {1.0f, 0.0f, 0.0f}, "center force preserves orientation");
        }

        aura::physics::clearForce(pushed);
        aura::physics::clearTorque(pushed);
        aura::physics::updateLinearAcceleration(pushed);
        aura::physics::updateAngularAcceleration(pushed);
        checkVec(pushed.force, {}, "force is cleared");
        checkVec(pushed.torque, {}, "torque is cleared");
        checkVec(pushed.acceleration, {}, "cleared force stops acceleration");
        checkVec(pushed.angularAcceleration, {}, "cleared torque stops angular acceleration");
        checkVec(pushed.velocity, {1.0f, 0.0f, 0.0f}, "clearing force preserves velocity");
    }

    aura::physics::RigidBody3D accumulated;
    aura::physics::applyForceAtPoint(accumulated, {10.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f});
    aura::physics::applyForceAtPoint(accumulated, {-10.0f, 0.0f, 0.0f}, {0.0f, -0.5f, 0.0f});
    checkVec(accumulated.force, {}, "opposing forces cancel translation");
    checkVec(accumulated.torque, {0.0f, 0.0f, -10.0f}, "torques accumulate");

    aura::physics::RigidBody3D falling;
    falling.mass = 2.0f;
    aura::physics::applyForce(falling, aura::math::Vec3{0.0f, -9.81f, 0.0f} * falling.mass);
    aura::physics::updateLinearAcceleration(falling);
    checkVec(falling.acceleration, {0.0f, -9.81f, 0.0f}, "gravity force accounts for mass");
    checkVec(falling.torque, {}, "gravity at center adds no torque");

    aura::physics::RigidBody3D box;
    box.position = {3.0f, -4.0f, 5.0f};
    checkVec(aura::physics::lowestPoint(box, {2.0f, 4.0f, 6.0f}),
             {2.0f, -6.0f, 2.0f}, "lowest corner uses world position and size");
    box.orientation = aura::math::Quaternion::fromAxisAngle(
        {0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 4.0f
    );
    checkVec(aura::physics::lowestPoint(box, {1.0f, 1.0f, 1.0f}),
             {3.0f, -4.0f - std::sqrt(0.5f), 4.5f},
             "lowest corner accounts for orientation");

    // Isolate the normal impulse from friction by using dt = 0.
    for (const float mass : {1.0f, 2.0f}) {
        for (const float restitution : {0.0f, 0.5f, 1.0f}) {
            aura::physics::RigidBody3D landing;
            landing.position = {2.0f, 0.4f, 3.0f};
            landing.velocity = {2.0f, -6.0f, 4.0f};
            landing.angularVelocity = {1.0f, 2.0f, 3.0f};
            landing.mass = mass;
            landing.restitution = restitution;

            aura::physics::resolveFloorCollision(landing, 0.5f, 0.0f, 0.0f);

            checkVec(landing.position, {2.0f, 0.5f, 3.0f}, "floor corrects penetration");
            checkVec(landing.velocity, {2.0f, 6.0f * restitution, 4.0f},
                     "floor impulse produces expected bounce for different masses");
            const float angularChange = 3.0f * (1.0f + restitution) * mass;
            checkVec(landing.angularVelocity, {1.0f + angularChange, 2.0f, 3.0f - angularChange},
                     "normal impulse at the selected corner changes angular velocity");
        }
    }

    aura::physics::RigidBody3D rising;
    rising.position = {0.0f, 0.4f, 0.0f};
    rising.velocity = {0.0f, 3.0f, 0.0f};
    aura::physics::resolveFloorCollision(rising, 0.5f, 0.0f, 0.0f);
    checkVec(rising.position, {0.0f, 0.5f, 0.0f}, "rising body is moved out of floor");
    checkVec(rising.velocity, {0.0f, 3.0f, 0.0f}, "rising body receives no bounce impulse");

    aura::physics::RigidBody3D airborne;
    airborne.position = {0.0f, 2.0f, 0.0f};
    airborne.velocity = {2.0f, -6.0f, 4.0f};
    aura::physics::resolveFloorCollision(airborne, 0.5f, 0.0f, 0.1f);
    checkVec(airborne.position, {0.0f, 2.0f, 0.0f}, "airborne position is unchanged");
    checkVec(airborne.velocity, {2.0f, -6.0f, 4.0f}, "airborne body receives no impulse or friction");

    return passed ? 0 : 1;
}
