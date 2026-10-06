#pragma once
#include "aura/physics/RigidBody3D.hpp"

namespace aura::physics {
    // Principal moments are body-local; velocities and returned momenta are world-space.
    struct MechanicalState {
        double linearKinetic = 0.0;
        double rotationalKinetic = 0.0;
        double potential = 0.0;
        math::Vec3 linearMomentum{};
        math::Vec3 angularMomentum{};
        [[nodiscard]] double energy() const { return linearKinetic + rotationalKinetic + potential; }
    };

    // Potential is zero at world origin. Angular momentum is about worldReference,
    // including both intrinsic spin and orbital r x (m v).
    MechanicalState mechanicalState(const RigidBody3D &body,
                                   const math::Vec3 &worldReference = {},
                                   const math::Vec3 &gravity = {0.0f, -9.81f, 0.0f});
}
