#include <cmath>
#include <iostream>
#include <numbers>

#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"

int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *name) {
        if (!condition) {
            std::cerr << "FAIL " << name << '\n';
            passed = false;
        }
    };

    aura::physics::RigidBody3D body;
    const aura::math::Vec3 size{1.0f, 1.0f, 1.0f};
    body.position = {3.0f, 1.5f, 5.0f};
    auto contacts = aura::physics::floorContactPoints(body, size, 1.0f, 0.0f);
    check(contacts.size() == 4, "upright cube has four floor contacts");
    for (const auto &contact: contacts) {
        check(contact.y == 1.0f &&
              std::abs(contact.x - 3.0f) == 0.5f &&
              std::abs(contact.z - 5.0f) == 0.5f,
              "contacts are bottom corners in world space");
    }

    body.position.y = 1.505f;
    check(aura::physics::floorContactPoints(body, size, 1.0f, 0.0f).empty(),
          "corners above threshold are excluded");
    check(aura::physics::floorContactPoints(body, size, 1.0f, 0.01f).size() == 4,
          "tolerance includes nearby corners");

    body.position.y = 1.4f;
    check(aura::physics::floorContactPoints(body, size, 1.0f, 0.01f).size() == 4,
          "penetrating corners are included");
    body.position.y = 2.0f;
    check(aura::physics::floorContactPoints(body, size, 1.0f, 0.01f).empty(),
          "airborne cube has no contacts");

    body.position.y = 1.0f + std::sqrt(0.5f);
    body.orientation = aura::math::Quaternion::fromAxisAngle(
        {0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 4.0f
    );
    contacts = aura::physics::floorContactPoints(body, size, 1.0f, 1e-5f);
    check(contacts.size() == 2, "cube tilted onto an edge has two contacts");
    for (const auto &contact: contacts) {
        check(std::abs(contact.x - 3.0f) < 1e-5f &&
              std::abs(contact.y - 1.0f) < 1e-5f,
              "edge contacts account for orientation");
    }

    body.orientation = aura::math::Quaternion::fromAxisAngle({1.0f, 1.0f, 1.0f}, 0.5f);
    const auto lowest = aura::physics::lowestPoint(body, size);
    contacts = aura::physics::floorContactPoints(body, size, lowest.y, 1e-5f);
    check(contacts.size() == 1, "cube tilted onto a corner has one contact");
    if (contacts.size() == 1) {
        check((contacts[0] - lowest).length() < 1e-5f, "single contact is the lowest corner");
    }

    aura::physics::RigidBody3D nearFloor;
    nearFloor.position = {0.0f, 0.505f, 0.0f};
    nearFloor.velocity = {2.0f, 0.0f, 0.0f};
    aura::physics::resolveFloorCollision(nearFloor, {1.0f, 1.0f, 1.0f}, 0.0f, 0.1f);
    check(std::abs(nearFloor.velocity.x - 0.8f) < 1e-5f,
          "resolver recognizes contact within 0.01 units");
    check(nearFloor.position.y == 0.505f, "contact tolerance does not pull body downward");

    nearFloor.position.y = 0.511f;
    nearFloor.velocity = {2.0f, 0.0f, 0.0f};
    aura::physics::resolveFloorCollision(nearFloor, {1.0f, 1.0f, 1.0f}, 0.0f, 0.1f);
    check(nearFloor.velocity.x == 2.0f, "resolver excludes contacts beyond tolerance");

    aura::physics::RigidBody3D chest;
    chest.position = {0.0f, 0.5f, 0.0f};
    chest.orientation = aura::math::Quaternion::fromAxisAngle(
        {1.0f, 0.0f, 0.0f}, std::numbers::pi_v<float> * 0.5f);
    const aura::math::Vec3 torsoSize{1.5f, 1.45f, 0.8f};
    const aura::physics::CollisionShape chestShape{aura::physics::CollisionShapeType::Sphere, 0.725f};
    check(std::abs(aura::physics::lowestPoint(chest, torsoSize, chestShape).y + 0.225f) < 1e-5f,
          "sphere floor support uses its radius after rotation");
    aura::physics::resolveFloorCollision(chest, torsoSize, chestShape, 0.0f, 1.0f / 120.0f);
    check(std::abs(chest.position.y - 0.725f) < 1e-5f &&
          std::abs(aura::physics::lowestPoint(chest, torsoSize, chestShape).y) < 1e-5f,
          "sphere floor correction keeps the visible sphere above ground");

    return passed ? 0 : 1;
}
