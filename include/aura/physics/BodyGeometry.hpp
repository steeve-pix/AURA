#pragma once
#include "Body2D.hpp"
#include "aura/math/Rotation.hpp"
#include <array>

#include "World2D.hpp"
#include "aura/math/Math.hpp"

namespace aura::physics {
    inline std::array<math::Vec2, 4> corners(const Body2D &body) noexcept {
        const float halfWidth = body.size.x * 0.5f;
        const float halfHeight = body.size.y * 0.5f;

        const std::array localCorners{
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

    inline float bottom(const Body2D &body) noexcept {
        const auto bodyCorners = corners(body);

        float lowest = bodyCorners[0].y;

        for (std::size_t i = 1; i < bodyCorners.size(); ++i)
            if (bodyCorners[i].y < lowest)
                lowest = bodyCorners[i].y;

        return lowest;
    }

    inline math::Vec2 localToWorldPoint(const Body2D &body, const math::Vec2 &localPoint) noexcept {
        return body.position + math::rotate(localPoint, body.angle);
    }

    inline math::Vec2 worldToLocalPoint(const Body2D &body, const math::Vec2 &worldPoint) noexcept {
        const math::Vec2 offset =
                worldPoint - body.position;

        return math::rotate(offset, -body.angle);
    }

    inline math::Vec2 velocityAtWorldPoint(const Body2D &body, const math::Vec2 &worldPoint) noexcept {
        const math::Vec2 offset =
                worldPoint - body.position;

        const math::Vec2 rotationalVelocity{
            -body.angularVelocity * offset.y,
            body.angularVelocity * offset.x
        };

        return body.velocity + rotationalVelocity;
    }

    inline bool isGrounded(const Body2D &body, const World2D &world) noexcept {
        return math::nearlyEqual(bottom(body), world.floorHeight, 0.001f);
    }

    inline float left(const Body2D &body) noexcept {
        return body.position.x - body.size.x * 0.5f;
    }

    inline float right(const Body2D &body) noexcept {
        return body.position.x + body.size.x * 0.5f;
    }

    inline math::Vec2 lowestPoint(const Body2D &body) noexcept {
        const auto bodyCorners = corners(body);

        math::Vec2 lowest = bodyCorners[0];

        for (const auto &corner: bodyCorners) {
            if (corner.y < lowest.y)
                lowest = corner;
        }

        return lowest;
    }
}
