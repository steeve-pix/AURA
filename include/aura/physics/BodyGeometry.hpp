#pragma once
#include "Body2D.hpp"

namespace aura::physics {
    inline float bottom(const Body2D &body) noexcept {
        return body.position.y - body.size.y * 0.5f;
    }
}
