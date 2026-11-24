#include "aura/physics/BodyGeometry.hpp"

#include <algorithm>
#include <cmath>

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
        return lowestPoint(body, size, CollisionShape{});
    }

    math::Vec3 lowestPoint(const RigidBody3D &body, const math::Vec3 &size, CollisionShape shape) {
        if (shape.type != CollisionShapeType::Box) {
            const float radius = collisionShapeRadius(size, shape);
            float segmentHalfLength = 0.0f;
            if (shape.type == CollisionShapeType::Capsule)
                segmentHalfLength = std::max(0.0f, 0.5f * size.y - radius);
            const auto axis = body.orientation.normalized().rotate({0.0f, 1.0f, 0.0f});
            return body.position - math::Vec3{0.0f, radius + segmentHalfLength * std::abs(axis.y), 0.0f};
        }
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

    std::vector<math::Vec3> floorContactPoints(const RigidBody3D &body, const math::Vec3 &size, float floorY,
                                               float tolerance) {
        return floorContactPoints(body, size, CollisionShape{}, floorY, tolerance);
    }

    std::vector<math::Vec3> floorContactPoints(const RigidBody3D &body, const math::Vec3 &size,
                                               CollisionShape shape, float floorY, float tolerance) {
        if (shape.type != CollisionShapeType::Box) {
            const float radius = collisionShapeRadius(size, shape);
            float segmentHalfLength = 0.0f;
            if (shape.type == CollisionShapeType::Capsule)
                segmentHalfLength = std::max(0.0f, 0.5f * size.y - radius);
            const auto axis = body.orientation.normalized().rotate({0.0f, 1.0f, 0.0f});
            std::vector<math::Vec3> contacts;
            for (float sign: {-1.0f, 1.0f}) {
                if (shape.type == CollisionShapeType::Sphere && sign > 0.0f) break;
                if (shape.type == CollisionShapeType::Capsule && segmentHalfLength == 0.0f && sign > 0.0f) break;
                const auto endpoint = body.position + axis * (sign * segmentHalfLength);
                const auto point = endpoint - math::Vec3{0.0f, radius, 0.0f};
                if (point.y <= floorY + tolerance) contacts.push_back(point);
            }
            return contacts;
        }
        const auto corners = cubeCorners(body, size);
        std::vector<math::Vec3> contacts;
        contacts.reserve(corners.size());

        for (const auto &corner: corners) {
            if (corner.y <= floorY + tolerance) {
                contacts.push_back(corner);
            }
        }

        return contacts;
    }
}
