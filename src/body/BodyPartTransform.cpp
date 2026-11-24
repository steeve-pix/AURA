#include "aura/body/BodyPartTransform.hpp"

namespace aura::body {
    math::Mat4 modelMatrix(const BodyPart3D &part) {
        return math::Mat4::translation(part.body.position) * math::Mat4::rotation(part.body.orientation) *
               math::Mat4::scale(part.size);
    }
}
