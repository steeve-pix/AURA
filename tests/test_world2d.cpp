#include <cassert>

#include <aura/math/Math.hpp>
#include <aura/physics/Physics.hpp>
#include <aura/physics/World2D.hpp>

namespace {
    void test_world_starts_with_default_gravity() {
        aura::physics::World2D const world{};

        assert(aura::math::nearlyEqual(world.gravity.x, 0.0f));
        assert(aura::math::nearlyEqual(world.gravity.y, -9.81f));
    }

    void test_step_body_applies_gravity_and_moves_body() {
        const aura::physics::World2D world{};
        aura::physics::Body2D body{
            .position = {0.0f, 10.0f},
            .velocity = {0.0f, 0.0f}
        };

        aura::physics::stepBody(body, world, 1.0f);

        assert(aura::math::nearlyEqual(body.acceleration.x, 0.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -9.81f));
        assert(aura::math::nearlyEqual(body.velocity.x, 0.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, -9.81f));
        assert(aura::math::nearlyEqual(body.position.x, 0.0f));
        // Position uses the updated velocity: 10.0 + (-9.81 * 1.0).
        assert(aura::math::nearlyEqual(body.position.y, 0.19f));
    }
}

int main() {
    test_world_starts_with_default_gravity();
    test_step_body_applies_gravity_and_moves_body();

    return 0;
}
