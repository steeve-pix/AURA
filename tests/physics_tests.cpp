#include <cassert>
#include <cmath>
#include <iostream>
#include <numbers>

#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Motion.hpp"
#include "aura/physics/RigidBody3D.hpp"
#include "aura/physics/SupportPolygon.hpp"

int main() {
    const aura::math::Vec3 point{2.5f, 0.008f, -1.3f};
    const auto projected = aura::physics::projectToGround(point);
    assert(projected.x == 2.5f);
    assert(projected.z == -1.3f);

    const aura::physics::GroundPoint a{0.0f, 0.0f};
    aura::physics::GroundPoint b{2.0f, 0.0f};
    aura::physics::GroundPoint counterclockwise{2.0f, 2.0f};
    aura::physics::GroundPoint clockwise{2.0f, -2.0f};
    aura::physics::GroundPoint collinear{4.0f, 0.0f};
    assert(aura::physics::cross(a, b, counterclockwise) == 4.0f);
    assert(aura::physics::cross(a, b, clockwise) == -4.0f);
    assert(aura::physics::cross(a, b, collinear) == 0.0f);

    const auto hasVertex = [](const aura::physics::SupportPolygon &hull,
                              const aura::physics::GroundPoint &expected) {
        for (const auto &vertex : hull.vertices) {
            if (vertex.x == expected.x && vertex.z == expected.z) {
                return true;
            }
        }
        return false;
    };

    const auto squareHull = aura::physics::convexHull({
        {0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 2.0f}, {0.0f, 2.0f}, {1.0f, 1.0f}
    });
    assert(squareHull.vertices.size() == 4);
    assert(hasVertex(squareHull, {0.0f, 0.0f}));
    assert(hasVertex(squareHull, {2.0f, 0.0f}));
    assert(hasVertex(squareHull, {2.0f, 2.0f}));
    assert(hasVertex(squareHull, {0.0f, 2.0f}));
    assert(!hasVertex(squareHull, {1.0f, 1.0f}));

    const aura::physics::SupportPolygon square{
        .vertices = {
            {0.0f, 0.0f},
            {2.0f, 0.0f},
            {2.0f, 2.0f},
            {0.0f, 2.0f}
        }
    };
    assert(aura::physics::containsPoint(square, {1.0f, 1.0f}));
    assert(!aura::physics::containsPoint(square, {3.0f, 1.0f}));
    assert(aura::physics::containsPoint(square, {2.0f, 1.0f}));
    const aura::physics::SupportPolygon line{
        .vertices = {{0.0f, 0.0f}, {2.0f, 0.0f}}
    };
    assert(!aura::physics::containsPoint(line, {1.0f, 0.0f}));

    using aura::physics::GroundPoint;
    const GroundPoint edgeStart{0.0f, 0.0f};
    const GroundPoint edgeEnd{4.0f, 0.0f};
    assert(aura::physics::signedDistanceToEdgeLine(edgeStart, edgeEnd, {2.0f, 3.0f}) == 3.0f);
    assert(aura::physics::signedDistanceToEdgeLine(edgeStart, edgeEnd, {2.0f, -3.0f}) == -3.0f);
    assert(aura::physics::signedDistanceToEdgeLine(edgeStart, edgeEnd, {2.0f, 0.0f}) == 0.0f);
    assert(aura::physics::signedDistanceToEdgeLine(edgeStart, edgeStart, {2.0f, 3.0f}) == 0.0f);

    const aura::physics::SupportPolygon marginSquare{
        .vertices = {
            {0.0f, 0.0f},
            {4.0f, 0.0f},
            {4.0f, 4.0f},
            {0.0f, 4.0f}
        }
    };
    assert(aura::physics::supportMargin(marginSquare, {2.0f, 2.0f}) == 2.0f);
    assert(std::abs(aura::physics::supportMargin(marginSquare, {3.9f, 2.0f}) - 0.1f) < 1e-5f);
    assert(aura::physics::supportMargin(marginSquare, {5.0f, 2.0f}) == -1.0f);
    assert(aura::physics::supportMargin(marginSquare, {4.0f, 2.0f}) == 0.0f);
    assert(aura::physics::supportMargin(line, {1.0f, 0.0f}) == 0.0f);

    const auto duplicateHull = aura::physics::convexHull({
        {0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 2.0f},
        {0.0f, 2.0f}, {0.0f, 0.0f}, {2.0f, 2.0f}
    });
    assert(duplicateHull.vertices.size() == 4);

    const auto collinearHull = aura::physics::convexHull({
        {0.0f, 0.0f}, {1.0f, 1.0f}, {2.0f, 2.0f}, {3.0f, 3.0f}
    });
    assert(collinearHull.vertices.size() == 2);
    assert(hasVertex(collinearHull, {0.0f, 0.0f}));
    assert(hasVertex(collinearHull, {3.0f, 3.0f}));

    const auto emptyHull = aura::physics::convexHull({});
    assert(emptyHull.vertices.empty());

    aura::physics::RigidBody3D body;
    body.position = {0.0f, 0.0f, 0.0f};
    body.velocity = {2.0f, 0.0f, 0.0f};

    aura::physics::integrateLinearMotion(body, 0.5f);

    std::cout << body.position.x << ", " << body.position.y
            << ", " << body.position.z << '\n';

    constexpr float tolerance = 1e-6f;
    bool passed = std::abs(body.position.x - 1.0f) <= tolerance &&
                  std::abs(body.position.y) <= tolerance &&
                  std::abs(body.position.z) <= tolerance;
    if (!passed) {
        std::cerr << "FAIL linear motion: expected (1, 0, 0)\n";
    }

    const auto check = [&](bool condition, const char *name) {
        if (!condition) {
            std::cerr << "FAIL " << name << '\n';
            passed = false;
        }
    };
    const auto checkVec = [&](const aura::math::Vec3 &actual,
                              const aura::math::Vec3 &expected, const char *name) {
        check((actual - expected).length() <= tolerance, name);
    };

    aura::physics::RigidBody3D identityInertia;
    identityInertia.momentOfInertia = {0.1f, 0.4f, 0.9f};
    identityInertia.torque = {1.0f, 0.0f, 0.0f};
    aura::physics::updateAngularAcceleration(identityInertia);
    checkVec(identityInertia.angularAcceleration, {10.0f, 0.0f, 0.0f},
             "angular acceleration uses local inertia when orientation is identity");

    aura::physics::RigidBody3D rotatedInertia;
    rotatedInertia.momentOfInertia = {0.1f, 0.4f, 0.9f};
    rotatedInertia.torque = {1.0f, 0.0f, 0.0f};
    rotatedInertia.orientation = aura::math::Quaternion::fromAxisAngle(
        {0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 2.0f
    );
    aura::physics::updateAngularAcceleration(rotatedInertia);
    checkVec(rotatedInertia.angularAcceleration, {2.5f, 0.0f, 0.0f},
             "angular acceleration rotates world torque into local inertia frame");

    // The same force translates both bodies; only the offset force turns one.
    for (const bool aboveCenter: {false, true}) {
        aura::physics::RigidBody3D pushed;
        pushed.position = {0.0f, 2.0f, 0.0f};
        pushed.mass = 2.0f;
        pushed.momentOfInertia = {2.0f, 2.0f, 2.0f};
        const auto point = pushed.position +
                           aura::math::Vec3{0.0f, aboveCenter ? 0.5f : 0.0f, 0.0f};

        aura::physics::applyForceAtPoint(pushed, {10.0f, 0.0f, 0.0f}, point);
        checkVec(pushed.torque, {0.0f, 0.0f, aboveCenter ? -5.0f : 0.0f},
                 "force at point produces expected torque");
        aura::physics::updateLinearAcceleration(pushed);
        aura::physics::updateAngularAcceleration(pushed);
        aura::physics::integrateLinearMotion(pushed, 0.2f);
        aura::physics::integrateAngularMotion(pushed, 0.2f);

        checkVec(pushed.velocity, {1.0f, 0.0f, 0.0f}, "force accelerates translation");
        checkVec(pushed.position, {0.2f, 2.0f, 0.0f}, "force moves body");
        checkVec(pushed.angularVelocity, {0.0f, 0.0f, aboveCenter ? -0.5f : 0.0f},
                 "offset controls angular motion");
        const auto rotated = pushed.orientation.rotate({1.0f, 0.0f, 0.0f});
        if (aboveCenter) {
            check(rotated.y < -0.09f, "offset force visibly changes orientation");
        } else {
            checkVec(rotated, {1.0f, 0.0f, 0.0f}, "center force preserves orientation");
        }

        aura::physics::clearForce(pushed);
        aura::physics::clearTorque(pushed);
        aura::physics::updateLinearAcceleration(pushed);
        aura::physics::updateAngularAcceleration(pushed);
        checkVec(pushed.force, {}, "force is cleared");
        checkVec(pushed.torque, {}, "torque is cleared");
        checkVec(pushed.acceleration, {}, "cleared force stops acceleration");
        checkVec(pushed.angularAcceleration, {}, "cleared torque stops angular acceleration");
        checkVec(pushed.velocity, {1.0f, 0.0f, 0.0f}, "clearing force preserves velocity");
    }

    aura::physics::RigidBody3D accumulated;
    aura::physics::applyForceAtPoint(accumulated, {10.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f});
    aura::physics::applyForceAtPoint(accumulated, {-10.0f, 0.0f, 0.0f}, {0.0f, -0.5f, 0.0f});
    checkVec(accumulated.force, {}, "opposing forces cancel translation");
    checkVec(accumulated.torque, {0.0f, 0.0f, -10.0f}, "torques accumulate");

    aura::physics::RigidBody3D falling;
    falling.mass = 2.0f;
    aura::physics::applyForce(falling, aura::math::Vec3{0.0f, -9.81f, 0.0f} * falling.mass);
    aura::physics::updateLinearAcceleration(falling);
    checkVec(falling.acceleration, {0.0f, -9.81f, 0.0f}, "gravity force accounts for mass");
    checkVec(falling.torque, {}, "gravity at center adds no torque");

    aura::physics::RigidBody3D box;
    box.position = {3.0f, -4.0f, 5.0f};
    checkVec(aura::physics::lowestPoint(box, {2.0f, 4.0f, 6.0f}),
             {2.0f, -6.0f, 2.0f}, "lowest corner uses world position and size");
    box.orientation = aura::math::Quaternion::fromAxisAngle(
        {0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 4.0f
    );
    checkVec(aura::physics::lowestPoint(box, {1.0f, 1.0f, 1.0f}),
             {3.0f, -4.0f - std::sqrt(0.5f), 4.5f},
             "lowest corner accounts for orientation");

    // Isolate the normal impulse from friction by using dt = 0.
    for (const float mass: {1.0f, 2.0f}) {
        float previousBounce = -1.0f;
        for (const float restitution: {0.0f, 0.5f, 1.0f}) {
            aura::physics::RigidBody3D landing;
            landing.position = {2.0f, 0.4f, 3.0f};
            landing.velocity = {2.0f, -6.0f, 4.0f};
            landing.angularVelocity = {1.0f, 2.0f, 3.0f};
            landing.mass = mass;
            landing.restitution = restitution;

            aura::physics::FloorCollisionAudit audit;
            aura::physics::resolveFloorCollision(landing, {1.0f, 1.0f, 1.0f}, 0.0f, 0.0f, &audit);

            checkVec(landing.position, {2.0f, 0.5f, 3.0f}, "floor corrects penetration");
            check(std::abs(landing.velocity.x - 2.0f) < tolerance &&
                  std::abs(landing.velocity.z - 4.0f) < tolerance,
                  "normal impulses preserve horizontal linear momentum");
            check(landing.velocity.y >= previousBounce && landing.velocity.y > 0.0f,
                  "floor contact produces a stronger upward response as restitution increases");
            check(std::isfinite(landing.angularVelocity.x) && std::isfinite(landing.angularVelocity.z) &&
                  std::abs(landing.angularVelocity.y - 2.0f) < tolerance,
                  "floor impulses produce finite angular response around horizontal axes");
            check(!audit.contacts.empty() && !audit.impulseEvents.empty(),
                  "floor audit records contact points and normal impulses");
            previousBounce = landing.velocity.y;
        }
    }

    aura::physics::RigidBody3D rising;
    rising.position = {0.0f, 0.4f, 0.0f};
    rising.velocity = {0.0f, 3.0f, 0.0f};
    aura::physics::resolveFloorCollision(rising, {1.0f, 1.0f, 1.0f}, 0.0f, 0.0f);
    checkVec(rising.position, {0.0f, 0.5f, 0.0f}, "rising body is moved out of floor");
    checkVec(rising.velocity, {0.0f, 3.0f, 0.0f}, "rising body receives no bounce impulse");

    aura::physics::RigidBody3D airborne;
    airborne.position = {0.0f, 2.0f, 0.0f};
    airborne.velocity = {2.0f, -6.0f, 4.0f};
    aura::physics::resolveFloorCollision(airborne, {1.0f, 1.0f, 1.0f}, 0.0f, 0.1f);
    checkVec(airborne.position, {0.0f, 2.0f, 0.0f}, "airborne position is unchanged");
    checkVec(airborne.velocity, {2.0f, -6.0f, 4.0f}, "airborne body receives no impulse or friction");

    return passed ? 0 : 1;
}
