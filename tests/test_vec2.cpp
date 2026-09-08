#include <cassert>

#include <aura/math/Vec2.hpp>

int main() {
    aura::math::Vec2 spawn_position{2.5f, 4.0f};

    assert(spawn_position.x == 2.5f);
    assert(spawn_position.y == 4.0f);

    aura::math::Vec2 starting_position{2.0f, 3.0f};
    aura::math::Vec2 movement_offset{4.0f, 1.0f};

    aura::math::Vec2 moved_position = starting_position + movement_offset;

    assert(moved_position.x == 6.0f);
    assert(moved_position.y == 4.0f);

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

    aura::math::Vec2 combined_movement{2.0f, 3.0f};

    combined_movement += aura::math::Vec2{4.0f, 1.0f};

    assert(combined_movement.x == 6.0f);
    assert(combined_movement.y == 4.0f);

    combined_movement *= 0.5f;

    assert(combined_movement.x == 3.0f);
    assert(combined_movement.y == 2.0f);

    aura::math::Vec2 travel_offset{3.0f, 4.0f};
    assert(travel_offset.length() == 5.0f);

    aura::math::Vec2 no_movement{};
    assert(no_movement.length() == 0.0f);

    aura::math::Vec2 heading_offset{
        3.0f,
        4.0f
    };

    aura::math::Vec2 unit_direction =
            heading_offset.normalized();

    assert(unit_direction.x == 0.6f);
    assert(unit_direction.y == 0.8f);

    aura::math::Vec2 zero_direction{};

    aura::math::Vec2 normalized_zero_direction =
            zero_direction.normalized();

    assert(normalized_zero_direction.x == 0.0f);
    assert(normalized_zero_direction.y == 0.0f);

    aura::math::Vec2 right_direction{1.0f, 0.0f};

    aura::math::Vec2 left_direction{-1.0f, 0.0f};

    aura::math::Vec2 up_direction{0.0f, 1.0f};

    assert(right_direction.dot(right_direction) == 1.0f);
    assert(right_direction.dot(left_direction) == -1.0f);
    assert(right_direction.dot(up_direction) == 0.0f);

    aura::math::Vec2 displacement{3.0f, 4.0f};

    assert(displacement.lengthSquared() == 25.0f);
    assert(displacement.length() == 5.0f);
}
