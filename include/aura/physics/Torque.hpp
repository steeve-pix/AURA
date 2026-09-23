#pragma once
#include "Body2D.hpp"

namespace aura::physics {
    inline void updateAngularAccelerationFromTorque(Body2D &body) noexcept {
        body.angularAcceleration =
                body.torque / body.momentOfInertia;
    }

    inline void applyTorque(Body2D &body, float torque) noexcept {
        body.torque += torque;
    }

    inline void clearTorque(Body2D &body) noexcept {
        body.torque = 0.0f;
    }
}
