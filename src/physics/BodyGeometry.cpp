#include "aura/physics/BodyGeometry.hpp"

namespace aura::physics {
    std::array<math::Vec3, 8> cubeCorners(const RigidBody3D &body, const math::Vec3 &size) {
        const math::Vec3 half{
            size.x * 0.5f,
            size.y * 0.5f,
            size.z * 0.5f
        };

        std::array localCorners{
            math::Vec3{-half.x, -half.y, -half.z},
            math::Vec3{half.x, -half.y, -half.z},
            math::Vec3{-half.x, half.y, -half.z},
            math::Vec3{half.x, half.y, -half.z},

            math::Vec3{-half.x, -half.y, half.z},
            math::Vec3{half.x, -half.y, half.z},
            math::Vec3{-half.x, half.y, half.z},
            math::Vec3{half.x, half.y, half.z}
        };

        std::array<math::Vec3, 8> worldCorners{};

        for (std::size_t i = 0; i < localCorners.size(); ++i) {
            worldCorners[i] =
                    body.position +
                    body.orientation.rotate(localCorners[i]);
        }

        return worldCorners;
    }

    math::Vec3 lowestPoint(const RigidBody3D &body, const math::Vec3 &size) {
        const auto corners = cubeCorners(body, size);
        math::Vec3 lowest = corners[0];

        for (std::size_t i = 1; i < corners.size(); ++i) {
            if (corners[i].y < lowest.y) {
                lowest = corners[i];
            }
        }

        return lowest;
    }

    math::Vec3 velocityAtWorldPoint(const RigidBody3D &body, const math::Vec3 &worldPoint) {
        const math::Vec3 r =
                worldPoint - body.position;

        const math::Vec3 rotationalVelocity =
                body.angularVelocity.cross(r);

        return body.velocity + rotationalVelocity;
    }
}
