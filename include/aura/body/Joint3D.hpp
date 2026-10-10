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
        // Hip geometry opts into bounded swing. Legacy diagnostics may retain
        // twist-only Hinge solving; enforceHingeAxis adds transverse locking.
        JointType type = JointType::Hinge;
        // Independent Y/Z components of the shortest swing rotation vector,
        // in part A's local frame (radians). Used for X-axis hips.
        float minSwingZ = -0.4f;
        float maxSwingZ = 0.4f;
        float minSwingY = -0.4f;
        float maxSwingY = 0.4f;
        // Enabled by the generalized collision configuration. Older diagnostic
        // trajectories retain their historical twist-only Hinge behaviour.
        bool enforceHingeAxis = false;
    };
}
