#include <cmath>
#include <iostream>

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
    const bool passed = std::abs(body.position.x - 1.0f) <= tolerance &&
                        std::abs(body.position.y) <= tolerance &&
                        std::abs(body.position.z) <= tolerance;
    if (!passed) {
        std::cerr << "FAIL linear motion: expected (1, 0, 0)\n";
    }
    return passed ? 0 : 1;
}
