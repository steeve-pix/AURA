#include <cassert>

#include <aura/body/BodyPart.hpp>

int main()
{
    aura::body::BodyPart torso{
        .type = aura::body::BodyPartType::Torso,
        .name = "torso",
        .body = {
            .position = {1.0f, 4.0f},
            .size = {1.0f, 2.0f},
            .mass = 3.0f
        }
    };

    assert(torso.type == aura::body::BodyPartType::Torso);
    assert(torso.name == "torso");
    assert(torso.body.position.x == 1.0f);
    assert(torso.body.mass == 3.0f);

    return 0;
}
