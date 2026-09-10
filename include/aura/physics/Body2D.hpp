#pragma once

#include <aura/math/Vec2.hpp>

namespace aura::physics {
    struct Body2D {
        math::Vec2 position{};
        math::Vec2 velocity{};
        math::Vec2 acceleration{};

        math::Vec2 force{};

        math::Vec2 size{1.0, 1.0f};
        float mass{1.0f};

        float angle = 0.0f;
        float angularVelocity = 0.0f;
    };
}
