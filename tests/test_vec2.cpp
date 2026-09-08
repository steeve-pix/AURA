#include <cassert>

#include <aura/math/Vec2.hpp>

int main() {
    aura::math::Vec2 initialized_vector{2.5f, 4.0f};

    assert(initialized_vector.x == 2.5f);
    assert(initialized_vector.y == 4.0f);

    aura::math::Vec2 first_addend{2.0f, 3.0f};
    aura::math::Vec2 second_addend{4.0f, 1.0f};

    aura::math::Vec2 sum = first_addend + second_addend;

    assert(sum.x == 6.0f);
    assert(sum.y == 4.0f);

    aura::math::Vec2 target_position{7.0f, 5.0f};
    aura::math::Vec2 current_position{2.0f, 3.0f};

    aura::math::Vec2 offset_to_target =
            target_position - current_position;

    assert(offset_to_target.x == 5.0f);
    assert(offset_to_target.y == 2.0f);

    aura::math::Vec2 velocity{4.0f, 2.0f};
    aura::math::Vec2 movement =
            velocity * 0.5f;

    assert(movement.x == 2.0f);
    assert(movement.y == 1.0f);

    aura::math::Vec2 accumulated_vector{2.0f,3.0f};

    accumulated_vector += aura::math::Vec2{4.0f,1.0f};

    assert(accumulated_vector.x == 6.0f);
    assert(accumulated_vector.y == 4.0f);

    accumulated_vector *= 0.5f;

    assert(accumulated_vector.x == 3.0f);
    assert(accumulated_vector.y == 2.0f);
}
