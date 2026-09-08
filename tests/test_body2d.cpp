#include <cassert>

#include <aura/math/Math.hpp>
#include <aura/physics/Body2D.hpp>
#include <aura/physics/Motion.hpp>

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
    test_update_position_uses_velocity();
    test_update_velocity_uses_acceleration();
    test_integrate_moves_with_updated_velocity();

    return 0;
}
