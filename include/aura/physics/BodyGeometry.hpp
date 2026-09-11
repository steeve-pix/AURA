#pragma once
#include "Body2D.hpp"
#include "aura/math/Rotation.hpp"
#include <array>

namespace aura::physics {
    inline float bottom(const Body2D &body) noexcept {
        return body.position.y - body.size.y * 0.5f;
    }

    inline std::array<math::Vec2, 4> corners(const Body2D &body) noexcept {
        const float halfWidth = body.size.x * 0.5f;
        const float halfHeight = body.size.y * 0.5f;

        const std::array<math::Vec2, 4> localCorners{
            math::Vec2{-halfWidth, -halfHeight},
            math::Vec2{halfWidth, -halfHeight},
            math::Vec2{halfWidth, halfHeight},
            math::Vec2{-halfWidth, halfHeight},
        };
        std::array<math::Vec2, 4> worldCorners{};

        for (std::size_t i = 0; i < localCorners.size(); ++i) {
            const math::Vec2 rotated =
                    math::rotate(localCorners[i], body.angle);

            worldCorners[i] = body.position + rotated;
        };

        return worldCorners;
    }
}
