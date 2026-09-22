#pragma once
#include <vector>

#include "BodyPart.hpp"

namespace aura::body {
    struct AuraBody {
        std::vector<BodyPart> parts;
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
