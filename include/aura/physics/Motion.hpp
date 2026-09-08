#pragma once
#include "Body2D.hpp"

namespace aura::physics {
    inline void updatePosition(Body2D &body, float deltaTime) noexcept {
        body.position += body.velocity * deltaTime;
    }
}
