#include <cassert>

#include <aura/body/AuraBody.hpp>

int main()
{
    aura::body::AuraBody aura{
        .parts = {
            {
                .type = aura::body::BodyPartType::Torso,
                .name = "torso",
                .body = {
                    .position = {0.0f, 4.0f},
                    .size = {1.0f, 2.5f},
                    .mass = 4.0f
                }
            },
            {
                .type = aura::body::BodyPartType::Head,
                .name = "head",
                .body = {
                    .position = {0.0f, 6.0f},
                    .size = {0.8f, 0.8f},
                    .mass = 1.0f
                }
            }
        }
    };

    assert(aura.parts.size() == 2);
    assert(aura.parts[0].type == aura::body::BodyPartType::Torso);
    assert(aura.parts[1].type == aura::body::BodyPartType::Head);

    auto *torso = aura::body::findPart(
            aura,
            aura::body::BodyPartType::Torso);
    assert(torso != nullptr);
    assert(torso->name == "torso");

    auto *head = aura::body::findPart(
            aura,
            aura::body::BodyPartType::Head);
    assert(head != nullptr);
    assert(head->name == "head");

    auto *arm = aura::body::findPart(
            aura,
            aura::body::BodyPartType::UpperArm);
    assert(arm == nullptr);

    return 0;
}
