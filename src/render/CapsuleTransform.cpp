#include "aura/render/CapsuleTransform.hpp"

#include <algorithm>

namespace aura::render {
    CapsuleTransforms capsuleTransforms(const body::BodyPart3D &part, float topInset, float bottomInset) {
        const float radius = 0.5f * std::min(part.size.x, part.size.z);
        const float visibleHeight = part.size.y - topInset - bottomInset;
        const float localCenterY = (bottomInset - topInset) * 0.5f;
        const float cylinderHeight = std::max(0.0f, visibleHeight - 2.0f * radius);
        const float sphereOffset = cylinderHeight * 0.5f;
        const float diameter = radius * 2.0f;
        const auto base = math::Mat4::translation(part.body.position) *
                          math::Mat4::rotation(part.body.orientation) *
                          math::Mat4::translation({0.0f, localCenterY, 0.0f});
        const auto sphereScale = math::Mat4::scale({diameter, diameter, diameter});

        CapsuleTransforms result;
        result.cylinder = base * math::Mat4::scale({diameter, cylinderHeight, diameter});
        result.topSphere = base * math::Mat4::translation({0.0f, sphereOffset, 0.0f}) * sphereScale;
        result.bottomSphere = base * math::Mat4::translation({0.0f, -sphereOffset, 0.0f}) * sphereScale;
        return result;
    }
}
