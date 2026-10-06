#include <cmath>
#include <iostream>
#include "aura/physics/MechanicalState.hpp"

int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    aura::physics::RigidBody3D b;
    b.mass = 2; b.position = {3, 4, 0}; b.velocity = {0, 3, 0};
    b.momentOfInertia = {2, 4, 6}; b.angularVelocity = {1, 2, 3};
    auto s = aura::physics::mechanicalState(b);
    check(std::abs(s.linearKinetic - 9) < 1e-6, "translational kinetic energy");
    check(std::abs(s.rotationalKinetic - 36) < 1e-6, "principal rotational kinetic energy");
    check(std::abs(s.potential - 78.48) < 1e-5, "gravitational potential relative to Y=0");
    check((s.linearMomentum - aura::math::Vec3{0, 6, 0}).length() < 1e-6f, "linear momentum");
    check((s.angularMomentum - aura::math::Vec3{2, 8, 36}).length() < 1e-6f, "spin plus orbital momentum");
    auto aboutCenter = aura::physics::mechanicalState(b, b.position);
    check((aboutCenter.angularMomentum - aura::math::Vec3{2, 8, 18}).length() < 1e-6f, "reference at body center removes orbital term");
    b.orientation = aura::math::Quaternion::fromAxisAngle({0, 0, 1}, 1.57079632679f);
    b.angularVelocity = {1, 0, 0};
    s = aura::physics::mechanicalState(b, b.position);
    check(std::abs(s.rotationalKinetic - 2) < 1e-5, "world X uses rotated local Y inertia");
    check((s.angularMomentum - aura::math::Vec3{4, 0, 0}).length() < 1e-5f, "spin transformed back to world");
    b.velocity = {}; b.angularVelocity = {};
    s = aura::physics::mechanicalState(b, {}, {});
    check(s.energy() == 0 && s.linearMomentum.length() == 0 && s.angularMomentum.length() == 0,
          "rest and zero gravity");
    return passed ? 0 : 1;
}
