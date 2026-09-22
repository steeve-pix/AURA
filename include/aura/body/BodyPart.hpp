#pragma once

#include <string>

#include "BodyPartType.hpp"
#include "aura/physics/Body2D.hpp"

namespace aura::body {
    struct BodyPart {
        BodyPartType type;
        std::string name;
        physics::Body2D body;
    };
}
