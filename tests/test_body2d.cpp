#include <cassert>

#include <aura/physics/Body2D.hpp>

int main()
{
    aura::physics::Body2D body{
        .position = {2.0f, 5.0f},
        .velocity = {3.0f, -1.0f}
    };

    assert(body.position.x == 2.0f);
    assert(body.position.y == 5.0f);

    assert(body.velocity.x == 3.0f);
    assert(body.velocity.y == -1.0f);

    return 0;
}
