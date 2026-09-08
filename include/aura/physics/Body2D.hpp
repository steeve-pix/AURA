#pragma once

#include <aura/math/Vec2.hpp>

namespace aura::physics {
    struct Body2D {
        math::Vec2 position{};
        math::Vec2 velocity{};
    };
}
