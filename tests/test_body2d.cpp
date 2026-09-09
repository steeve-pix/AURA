#include <cassert>

#include <aura/math/Math.hpp>
#include <aura/physics/Body2D.hpp>
#include <aura/physics/BodyGeometry.hpp>
#include <aura/physics/Collision.hpp>
#include <aura/physics/Forces.hpp>
#include <aura/physics/Motion.hpp>
#include <aura/physics/Physics.hpp>
#include <aura/physics/World2D.hpp>

namespace {
    aura::physics::Body2D initialBody = {
        .position = {2.0f, 5.0f},
        .velocity = {3.0f, -1.0f},
        .acceleration = {0.0f, -9.81f}
    };

    void test_body_stores_initial_state() {
        const aura::physics::Body2D body = initialBody;

        assert(body.position.x == 2.0f);
        assert(body.position.y == 5.0f);
        assert(body.velocity.x == 3.0f);
        assert(body.velocity.y == -1.0f);
        assert(body.acceleration.x == 0.0f);
        assert(body.acceleration.y == -9.81f);
    }

    void test_body_stores_initial_size() {
        const aura::physics::Body2D body{
            .position = {2.0f, 5.0f},
            .velocity = {0.0f, 0.0f},
            .acceleration = {0.0f, 0.0f},
            .size = {2.0f, 4.0f}
        };

        assert(body.size.x == 2.0f);
        assert(body.size.y == 4.0f);
    }

    void test_body_stores_initial_mass() {
        const aura::physics::Body2D body{
            .mass = 2.0f
        };

        assert(body.mass == 2.0f);
    }

    void test_body_stores_initial_force() {
        const aura::physics::Body2D body{
            .force = {3.0f, -2.0f}
        };

        assert(body.force.x == 3.0f);
        assert(body.force.y == -2.0f);
    }

    void test_apply_force_accumulates_with_existing_force() {
        aura::physics::Body2D body{
            .force = {2.0f, -5.0f}
        };

        aura::physics::applyForce(body, {3.0f, 1.0f});

        assert(aura::math::nearlyEqual(body.force.x, 5.0f));
        assert(aura::math::nearlyEqual(body.force.y, -4.0f));
    }

    void test_acceleration_equals_force_divided_by_mass() {
        aura::physics::Body2D body{
            .force = {10.0f, -20.0f},
            .mass = 2.0f
        };

        aura::physics::updateAccelerationFromForce(body);

        assert(aura::math::nearlyEqual(body.acceleration.x, 5.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -10.0f));
    }

    void test_clear_forces_resets_both_components() {
        aura::physics::Body2D body{
            .force = {5.0f, -3.0f}
        };

        aura::physics::clearForces(body);

        assert(aura::math::nearlyEqual(body.force.x, 0.0f));
        assert(aura::math::nearlyEqual(body.force.y, 0.0f));
    }

    void test_gravity_produces_same_acceleration_for_different_masses() {
        const aura::physics::World2D world{};
        aura::physics::Body2D light{
            .mass = 1.0f
        };
        aura::physics::Body2D heavy{
            .mass = 10.0f
        };

        aura::physics::applyGravity(light, world.gravity);
        aura::physics::applyGravity(heavy, world.gravity);

        aura::physics::updateAccelerationFromForce(light);
        aura::physics::updateAccelerationFromForce(heavy);

        assert(aura::math::nearlyEqual(light.acceleration.y, -9.81f));
        assert(aura::math::nearlyEqual(heavy.acceleration.y, -9.81f));
    }

    void test_bottom_is_half_height_below_center() {
        const aura::physics::Body2D body{
            .position = {2.0f, 5.0f},
            .size = {2.0f, 4.0f}
        };

        // Center height 5 minus half the height 4 gives bottom height 3.
        assert(aura::math::nearlyEqual(aura::physics::bottom(body), 3.0f));
    }

    void test_floor_collision_corrects_position_and_stops_falling() {
        const aura::physics::World2D world{};
        aura::physics::Body2D body{
            .position = {0.0f, 0.2f},
            .velocity = {1.0f, -5.0f},
            .size = {1.0f, 1.0f}
        };

        aura::physics::resolveFloorCollision(body, world);

        assert(aura::math::nearlyEqual(body.position.y, 0.5f));
        assert(aura::math::nearlyEqual(body.velocity.x, 1.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, 0.0f));
        assert(!aura::physics::intersectsFloor(body, world));
    }

    void test_step_body_lands_falling_body_on_floor() {
        const aura::physics::World2D world{};
        aura::physics::Body2D body{
            .position = {0.0f, 0.6f},
            .velocity = {0.0f, -2.0f},
            .size = {1.0f, 1.0f}
        };

        aura::physics::stepBody(body, world, 0.1f);

        assert(aura::math::nearlyEqual(body.position.y, 0.5f));
        assert(aura::math::nearlyEqual(body.velocity.y, 0.0f));
        assert(!aura::physics::intersectsFloor(body, world));
    }

    void test_update_position_uses_velocity() {
        aura::physics::Body2D body = initialBody;

        aura::physics::updatePosition(body, 0.5f);

        assert(aura::math::nearlyEqual(body.position.x, 3.5f));
        assert(aura::math::nearlyEqual(body.position.y, 4.5f));
        assert(aura::math::nearlyEqual(body.velocity.x, 3.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, -1.0f));
        assert(aura::math::nearlyEqual(body.acceleration.x, 0.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -9.81f));
    }

    void test_update_velocity_uses_acceleration() {
        aura::physics::Body2D body = initialBody;

        aura::physics::updateVelocity(body, 0.5f);

        assert(aura::math::nearlyEqual(body.velocity.x, 3.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, -5.905f));
        assert(aura::math::nearlyEqual(body.position.x, 2.0f));
        assert(aura::math::nearlyEqual(body.position.y, 5.0f));
        assert(aura::math::nearlyEqual(body.acceleration.x, 0.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -9.81f));
    }

    void test_integrate_moves_with_updated_velocity() {
        aura::physics::Body2D body = initialBody;

        aura::physics::integrate(body, 0.5f);

        assert(aura::math::nearlyEqual(body.velocity.x, 3.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, -5.905f));
        assert(aura::math::nearlyEqual(body.position.x, 3.5f));
        // Position uses the new velocity: 5.0 + (-5.905 * 0.5).
        assert(aura::math::nearlyEqual(body.position.y, 2.0475f));
        assert(aura::math::nearlyEqual(body.acceleration.x, 0.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -9.81f));
    }
}

int main() {
    test_body_stores_initial_state();
    test_body_stores_initial_size();
    test_body_stores_initial_mass();
    test_body_stores_initial_force();
    test_apply_force_accumulates_with_existing_force();
    test_acceleration_equals_force_divided_by_mass();
    test_clear_forces_resets_both_components();
    test_gravity_produces_same_acceleration_for_different_masses();
    test_bottom_is_half_height_below_center();
    test_floor_collision_corrects_position_and_stops_falling();
    test_step_body_lands_falling_body_on_floor();
    test_update_position_uses_velocity();
    test_update_velocity_uses_acceleration();
    test_integrate_moves_with_updated_velocity();

    return 0;
}
