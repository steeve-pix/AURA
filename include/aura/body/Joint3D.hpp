#pragma once
#include "aura/math/Vec3.hpp"

namespace aura::body {
    enum class JointType { Hinge, SwingTwist };

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

        // Appended to retain existing aggregate initialization of anchors/gains.
        // Metadata for now: legacy solving still enforces only axial twist.
        JointType type = JointType::Hinge;
        // First SwingTwist model: Z component of log(swing), in part A's
        // local frame (radians). Used for X-axis hips; Y swing is unbounded.
        float minSwingZ = -0.4f;
        float maxSwingZ = 0.4f;
    };
}
