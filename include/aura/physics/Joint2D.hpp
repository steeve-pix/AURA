#pragma once
#include "aura/math/Vec2.hpp"

namespace aura::physics {
    struct Joint2D {
        math::Vec2 localAnchorA{};
        math::Vec2 localAnchorB{};
    };
}
