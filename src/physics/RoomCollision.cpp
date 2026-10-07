#include "aura/physics/RoomCollision.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Impulse.hpp"
#include "aura/physics/Inertia.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace aura::physics {
    namespace {
        struct Plane { math::Vec3 normal; float offset; };
        std::array<Plane, 6> planes(const RoomBounds &room) {
            // Inside means dot(worldPoint, inwardNormal) >= offset.
            return {{{{1,0,0}, room.minimum.x}, {{-1,0,0}, -room.maximum.x},
                     {{0,1,0}, room.minimum.y}, {{0,-1,0}, -room.maximum.y},
                     {{0,0,1}, room.minimum.z}, {{0,0,-1}, -room.maximum.z}}};
        }
    }

    float roomPenetration(const RigidBody3D &body, const math::Vec3 &size, const RoomBounds &room) {
        float result = 0;
        const auto corners = cubeCorners(body, size);
        for (const auto &plane : planes(room)) for (const auto &corner : corners)
            result = std::max(result, plane.offset - corner.dot(plane.normal));
        return result;
    }

    namespace {
    void resolveRoomVelocity(RigidBody3D &body, const math::Vec3 &size, const RoomBounds &room) {
        constexpr float contactTolerance = 1e-4f;
        constexpr int impulseIterations = 4;
        for (const auto &plane : planes(room)) {
            auto corners = cubeCorners(body, size);
            float nearest = std::numeric_limits<float>::infinity();
            for (const auto &corner : corners)
                nearest = std::min(nearest, corner.dot(plane.normal) - plane.offset);
            if (nearest > contactTolerance) continue;

            for (int iteration = 0; iteration < impulseIterations; ++iteration) {
                for (const auto &point : corners) {
                    if (point.dot(plane.normal) - plane.offset > contactTolerance) continue;
                    const float normalSpeed = velocityAtWorldPoint(body, point).dot(plane.normal);
                    if (normalSpeed >= 0) continue;
                    const auto lever = (point - body.position).cross(plane.normal);
                    const float inverseMass = 1.0f / body.mass + lever.dot(applyInverseInertiaWorld(body, lever));
                    const float magnitude = -(1.0f + body.restitution) * normalSpeed / inverseMass;
                    applyImpulseAtPoint(body, plane.normal * magnitude, point);
                }
            }
        }
    }
    }

    bool resolveRoomCollisions(std::span<const RoomBody> bodies, const RoomBounds &room) {
        if (bodies.empty()) return true;
        const float infinity = std::numeric_limits<float>::infinity();
        math::Vec3 low{infinity,infinity,infinity}, high{-infinity,-infinity,-infinity};
        for (const auto &entry : bodies) for (const auto &corner : cubeCorners(*entry.body,entry.size)) {
            low.x=std::min(low.x,corner.x); low.y=std::min(low.y,corner.y); low.z=std::min(low.z,corner.z);
            high.x=std::max(high.x,corner.x); high.y=std::max(high.y,corner.y); high.z=std::max(high.z,corner.z);
        }
        const auto extent=high-low, available=room.maximum-room.minimum;
        if (extent.x>available.x || extent.y>available.y || extent.z>available.z) return false;
        const auto shift=[](float low,float high,float minimum,float maximum) {
            if (low<minimum) return minimum-low;
            if (high>maximum) return maximum-high;
            return 0.0f;
        };
        const math::Vec3 translation{shift(low.x,high.x,room.minimum.x,room.maximum.x),
            shift(low.y,high.y,room.minimum.y,room.maximum.y), shift(low.z,high.z,room.minimum.z,room.maximum.z)};
        // Projection only: no changes to orientation, linear or angular velocity.
        for (const auto &entry : bodies) entry.body->position += translation;
        for (const auto &entry : bodies) resolveRoomVelocity(*entry.body,entry.size,room);
        return true;
    }

    void resolveRoomCollision(RigidBody3D &body, const math::Vec3 &size, const RoomBounds &room) {
        const RoomBody entry{&body,size};
        resolveRoomCollisions(std::span<const RoomBody>{&entry,1},room);
    }
}
