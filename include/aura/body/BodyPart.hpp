#pragma once

#include <string>

#include "BodyPartShape.hpp"
#include "BodyPartType.hpp"
#include "aura/physics/Body2D.hpp"

namespace aura::body {
    struct BodyPart {
        BodyPartType type;
        BodyPartShape shape = BodyPartShape::Rectangle;

        std::string name;
        physics::Body2D body;
    };
}
