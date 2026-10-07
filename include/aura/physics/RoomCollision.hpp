#pragma once
#include "RigidBody3D.hpp"
#include <span>

namespace aura::physics {
    // Static world-space enclosure. Individual oriented boxes must fit inside it.
    struct RoomBounds {
        math::Vec3 minimum{-60.0f, 0.0f, -60.0f};
        math::Vec3 maximum{60.0f, 30.0f, 60.0f};
    };
    struct RoomBody {
        RigidBody3D *body;
        math::Vec3 size;
    };

    // Project box corners inside six axis-aligned planes, then apply normal
    // contact impulses to this body only. No friction, damping, or pose-to-velocity
    // compensation. The existing floor solver retains its friction policy.
    void resolveRoomCollision(RigidBody3D &body, const math::Vec3 &size, const RoomBounds &room);
    // A common translation keeps connected bodies' geometry intact. Returns
    // false without changing state if the group's bounding box cannot fit.
    bool resolveRoomCollisions(std::span<const RoomBody> bodies, const RoomBounds &room);
    float roomPenetration(const RigidBody3D &body, const math::Vec3 &size, const RoomBounds &room);
}
