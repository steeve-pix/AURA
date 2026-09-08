#include <cassert>

#include <aura/math/Math.hpp>
#include <aura/physics/Collision.hpp>
#include <aura/physics/Physics.hpp>
#include <aura/physics/World2D.hpp>

namespace {
    void test_world_starts_with_floor_at_zero() {
        const aura::physics::World2D world{};

        assert(world.floorHeight == 0.0f);
    }

    void test_world_starts_with_default_gravity() {
        aura::physics::World2D const world{};

        assert(aura::math::nearlyEqual(world.gravity.x, 0.0f));
        assert(aura::math::nearlyEqual(world.gravity.y, -9.81f));
    }

    void test_body_above_floor_does_not_intersect() {
        const aura::physics::World2D world{};
        const aura::physics::Body2D above{
            .position = {0.0f, 2.0f},
            .size = {1.0f, 1.0f}
        };

        // Bottom is 2.0 - 0.5 = 1.5, above the floor at 0.
        assert(!aura::physics::intersectsFloor(above, world));
    }

    void test_body_penetrating_floor_intersects() {
        const aura::physics::World2D world{};
        const aura::physics::Body2D penetrating{
            .position = {0.0f, 0.2f},
            .size = {1.0f, 1.0f}
        };

        // Bottom is 0.2 - 0.5 = -0.3, below the floor at 0.
        assert(aura::physics::intersectsFloor(penetrating, world));
    }

    void test_floor_correction_places_body_on_floor() {
        const aura::physics::World2D world{};
        aura::physics::Body2D body{
            .position = {0.0f, 0.2f},
            .size = {1.0f, 1.0f}
        };

        assert(aura::math::nearlyEqual(aura::physics::bottom(body), -0.3f));

        aura::physics::correctFloorPenetration(body, world);

        assert(aura::math::nearlyEqual(body.position.y, 0.5f));
        assert(aura::math::nearlyEqual(
            aura::physics::bottom(body), world.floorHeight
        ));
    }

    void test_floor_correction_preserves_body_above_floor() {
        const aura::physics::World2D world{};
        aura::physics::Body2D above{
            .position = {0.0f, 3.0f},
            .size = {1.0f, 1.0f}
        };

        aura::physics::correctFloorPenetration(above, world);

        assert(aura::math::nearlyEqual(above.position.y, 3.0f));
    }

    void test_stop_downward_velocity_preserves_horizontal_motion() {
        aura::physics::Body2D body{
            .velocity = {2.0f, -6.0f}
        };

        aura::physics::stopDownwardVelocity(body);

        assert(aura::math::nearlyEqual(body.velocity.x, 2.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, 0.0f));
    }

    void test_stop_downward_velocity_preserves_upward_motion() {
        aura::physics::Body2D rising{
            .velocity = {0.0f, 3.0f}
        };

        aura::physics::stopDownwardVelocity(rising);

        assert(aura::math::nearlyEqual(rising.velocity.x, 0.0f));
        assert(aura::math::nearlyEqual(rising.velocity.y, 3.0f));
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
    test_world_starts_with_floor_at_zero();
    test_world_starts_with_default_gravity();
    test_body_above_floor_does_not_intersect();
    test_body_penetrating_floor_intersects();
    test_floor_correction_places_body_on_floor();
    test_floor_correction_preserves_body_above_floor();
    test_stop_downward_velocity_preserves_horizontal_motion();
    test_stop_downward_velocity_preserves_upward_motion();
    test_step_body_applies_gravity_and_moves_body();

    return 0;
}
