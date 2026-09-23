#pragma once

#include <aura/math/Vec2.hpp>

namespace aura::physics {
    struct World2D {
        math::Vec2 gravity{0.0f, -9.81f};

        float floorHeight{0.0f};
    };
}
