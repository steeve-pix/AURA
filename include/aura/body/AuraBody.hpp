#pragma once
#include <vector>

#include "BodyPart.hpp"
#include "aura/physics/Joint2D.hpp"

namespace aura::body {
    struct AuraBody {
        std::vector<BodyPart> parts;
        std::vector<physics::Joint2D> joints;
    };

    inline BodyPart *findPart(AuraBody &aura, BodyPartType type) noexcept {
        for (BodyPart &part: aura.parts) {
            if (part.type == type) {
                return &part;
            }
        }

        return nullptr;
    }
}
