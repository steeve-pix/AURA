#include <cassert>

#include <aura/math/Vec2.hpp>

int main() {
    aura::math::Vec2 value{2.5f, 4.0f};

    assert(value.x == 2.5f);
    assert(value.y == 4.0f);
}
