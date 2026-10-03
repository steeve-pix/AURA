#pragma once
#include "BodyPart3D.hpp"
#include "aura/math/Mat4.hpp"

namespace aura::body {
    math::Mat4 modelMatrix(const BodyPart3D &part);
}
