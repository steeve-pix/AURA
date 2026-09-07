#include <cassert>

#include <aura/math/Vec2.hpp>

int main() {
    aura::math::Vec2 value{2.5f, 4.0f};

    assert(value.x == 2.5f);
    assert(value.y == 4.0f);

    aura::math::Vec2 a{2.0f, 3.0f};
    aura::math::Vec2 b{4.0f, 1.0f};

    aura::math::Vec2 result = a + b;

    assert(result.x == 6.0f);
    assert(result.y == 4.0f);

    aura::math::Vec2 target{7.0f, 5.0f};
    aura::math::Vec2 position{2.0f, 3.0f};

    aura::math::Vec2 difference =
            target - position;

    assert(difference.x == 5.0f);
    assert(difference.y == 2.0f);
}
