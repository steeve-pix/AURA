#include <cassert>

#include <aura/math/Math.hpp>
#include <aura/math/Rotation.hpp>
#include <aura/math/Vec2.hpp>

namespace {
    void test_rotation_turns_right_vector_up() {
        const float halfPi = 3.1415926535f * 0.5f;
        const aura::math::Vec2 right{1.0f, 0.0f};

        const auto rotated = aura::math::rotate(right, halfPi);

        assert(aura::math::nearlyEqual(rotated.x, 0.0f));
        assert(aura::math::nearlyEqual(rotated.y, 1.0f));
    }

    void test_nearly_equal_accepts_close_values() {
        assert(aura::math::nearlyEqual(0.1f + 0.2f, 0.3f));
    }

    void test_nearly_equal_rejects_different_values() {
        assert(!aura::math::nearlyEqual(1.0f, 2.0f));
    }

    void test_vector_stores_initial_components() {
        aura::math::Vec2 spawn_position{2.5f, 4.0f};

        assert(spawn_position.x == 2.5f);
        assert(spawn_position.y == 4.0f);
    }

    void test_addition_applies_movement_offset() {
        aura::math::Vec2 starting_position{2.0f, 3.0f};
        aura::math::Vec2 movement_offset{4.0f, 1.0f};

        aura::math::Vec2 moved_position = starting_position + movement_offset;

        assert(aura::math::nearlyEqual(moved_position.x, 6.0f));
        assert(aura::math::nearlyEqual(moved_position.y, 4.0f));
    }

    void test_subtraction_finds_offset_to_target() {
        aura::math::Vec2 target_position{7.0f, 5.0f};
        aura::math::Vec2 current_position{2.0f, 3.0f};

        aura::math::Vec2 offset_to_target =
                target_position - current_position;

        assert(aura::math::nearlyEqual(offset_to_target.x, 5.0f));
        assert(aura::math::nearlyEqual(offset_to_target.y, 2.0f));
    }

    void test_scalar_multiplication_scales_velocity() {
        aura::math::Vec2 velocity{4.0f, 2.0f};
        aura::math::Vec2 movement =
                velocity * 0.5f;

        assert(aura::math::nearlyEqual(movement.x, 2.0f));
        assert(aura::math::nearlyEqual(movement.y, 1.0f));
    }

    void test_add_assign_accumulates_movement() {
        aura::math::Vec2 combined_movement{2.0f, 3.0f};

        combined_movement += aura::math::Vec2{4.0f, 1.0f};

        assert(aura::math::nearlyEqual(combined_movement.x, 6.0f));
        assert(aura::math::nearlyEqual(combined_movement.y, 4.0f));
    }

    void test_multiply_assign_scales_movement() {
        aura::math::Vec2 combined_movement{6.0f, 4.0f};

        combined_movement *= 0.5f;

        assert(aura::math::nearlyEqual(combined_movement.x, 3.0f));
        assert(aura::math::nearlyEqual(combined_movement.y, 2.0f));
    }

    void test_length_measures_displacement() {
        aura::math::Vec2 travel_offset{3.0f, 4.0f};
        assert(aura::math::nearlyEqual(travel_offset.length(), 5.0f));
    }

    void test_zero_vector_has_zero_length() {
        aura::math::Vec2 no_movement{};
        assert(aura::math::nearlyEqual(no_movement.length(), 0.0f));
    }

    void test_normalization_produces_unit_direction() {
        aura::math::Vec2 heading_offset{
            3.0f,
            4.0f
        };

        aura::math::Vec2 unit_direction =
                heading_offset.normalized();

        assert(aura::math::nearlyEqual(unit_direction.x, 0.6f));
        assert(aura::math::nearlyEqual(unit_direction.y, 0.8f));
    }

    void test_normalization_preserves_zero_vector() {
        aura::math::Vec2 zero_direction{};

        aura::math::Vec2 normalized_zero_direction =
                zero_direction.normalized();

        assert(aura::math::nearlyEqual(normalized_zero_direction.x, 0.0f));
        assert(aura::math::nearlyEqual(normalized_zero_direction.y, 0.0f));
    }

    void test_dot_product_compares_directions() {
        aura::math::Vec2 right_direction{1.0f, 0.0f};

        aura::math::Vec2 left_direction{-1.0f, 0.0f};

        aura::math::Vec2 up_direction{0.0f, 1.0f};

        assert(aura::math::nearlyEqual(right_direction.dot(right_direction), 1.0f));
        assert(aura::math::nearlyEqual(right_direction.dot(left_direction), -1.0f));
        assert(aura::math::nearlyEqual(right_direction.dot(up_direction), 0.0f));
    }

    void test_squared_length_matches_length_squared() {
        aura::math::Vec2 displacement{3.0f, 4.0f};

        assert(aura::math::nearlyEqual(displacement.lengthSquared(), 25.0f));
        assert(aura::math::nearlyEqual(displacement.length(), 5.0f));
    }
}

int main() {
    test_rotation_turns_right_vector_up();
    test_nearly_equal_accepts_close_values();
    test_nearly_equal_rejects_different_values();
    test_vector_stores_initial_components();
    test_addition_applies_movement_offset();
    test_subtraction_finds_offset_to_target();
    test_scalar_multiplication_scales_velocity();
    test_add_assign_accumulates_movement();
    test_multiply_assign_scales_movement();
    test_length_measures_displacement();
    test_zero_vector_has_zero_length();
    test_normalization_produces_unit_direction();
    test_normalization_preserves_zero_vector();
    test_dot_product_compares_directions();
    test_squared_length_matches_length_squared();

    return 0;
}
