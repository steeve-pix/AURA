#pragma once
#include "Body2D.hpp"

namespace aura::physics {
    inline float rectangleMomentOfInertia(const Body2D &body) noexcept {
        const float widthSquared =
                body.size.x * body.size.x;

        const float heightSquared =
                body.size.y * body.size.y;

        return body.mass * (widthSquared + heightSquared) / 12.0f;
    }
}
