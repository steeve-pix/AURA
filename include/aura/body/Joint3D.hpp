#pragma once
#include "aura/math/Vec3.hpp"

namespace aura::body {
    struct Joint3D {
        math::Vec3 localAnchorA{};
        math::Vec3 localAnchorB{};

        math::Vec3 minAngles{};
        math::Vec3 maxAngles{};

        // Hinge axis is expressed in part A's local frame; limits are radians.
        math::Vec3 hingeAxis{1.0f, 0.0f, 0.0f};
        float minAngle = -0.8f;
        float maxAngle = 0.8f;

        float targetAngle = 0.0f;

        float motorStiffness = 10.0f;
        float motorDamping = 2.0f;
    };
}
