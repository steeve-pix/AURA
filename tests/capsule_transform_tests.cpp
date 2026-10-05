#include <cmath>
#include <iostream>
#include <numbers>

#include "aura/render/CapsuleTransform.hpp"

int main() {
    bool passed = true;
    const auto point = [](const aura::math::Mat4 &matrix, const aura::math::Vec3 &local) {
        const auto *m = matrix.data();
        return aura::math::Vec3{m[0] * local.x + m[4] * local.y + m[8] * local.z + m[12],
                               m[1] * local.x + m[5] * local.y + m[9] * local.z + m[13],
                               m[2] * local.x + m[6] * local.y + m[10] * local.z + m[14]};
    };
    const auto checkPoint = [&](const auto &matrix, const auto &local, const auto &expected, const char *name) {
        if ((point(matrix, local) - expected).length() > 1e-5f) {
            std::cerr << "FAIL " << name << '\n';
            passed = false;
        }
    };
    const aura::math::Vec3 origin{}, top{0.0f, 0.5f, 0.0f}, bottom{0.0f, -0.5f, 0.0f};
    aura::body::BodyPart3D thigh;
    thigh.size = {0.5f, 1.8f, 0.5f};
    const auto thighCapsule = aura::render::capsuleTransforms(thigh);
    checkPoint(thighCapsule.cylinder, top, aura::math::Vec3{0.0f, 0.65f, 0.0f}, "thigh cylinder length");
    checkPoint(thighCapsule.topSphere, top, aura::math::Vec3{0.0f, 0.9f, 0.0f}, "thigh upper extent");
    checkPoint(thighCapsule.bottomSphere, bottom, aura::math::Vec3{0.0f, -0.9f, 0.0f}, "thigh lower extent");
    checkPoint(thighCapsule.topSphere, aura::math::Vec3{0.5f, 0.0f, 0.0f},
               aura::math::Vec3{0.25f, 0.65f, 0.0f}, "thigh radius");

    const auto insetThigh = aura::render::capsuleTransforms(thigh, 0.12f, 0.12f);
    checkPoint(insetThigh.cylinder, origin, origin, "equal insets keep capsule centred");
    checkPoint(insetThigh.topSphere, top, aura::math::Vec3{0.0f, 0.78f, 0.0f}, "top inset shortens upper extent");
    checkPoint(insetThigh.bottomSphere, bottom, aura::math::Vec3{0.0f, -0.78f, 0.0f}, "bottom inset shortens lower extent");

    aura::body::BodyPart3D shin;
    shin.size = {0.45f, 1.6f, 0.45f};
    shin.body.position = {2.0f, 3.0f, 4.0f};
    shin.body.orientation = aura::math::Quaternion::fromAxisAngle(
        {0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> * 0.5f);
    const auto shinCapsule = aura::render::capsuleTransforms(shin);
    checkPoint(shinCapsule.cylinder, origin, shin.body.position, "shin cylinder follows body centre");
    checkPoint(shinCapsule.topSphere, origin, aura::math::Vec3{1.425f, 3.0f, 4.0f}, "rotated top centre");
    checkPoint(shinCapsule.bottomSphere, bottom, aura::math::Vec3{2.8f, 3.0f, 4.0f}, "rotated lower extent");

    const auto insetShin = aura::render::capsuleTransforms(shin, 0.1f, 0.2f);
    checkPoint(insetShin.cylinder, origin, aura::math::Vec3{1.95f, 3.0f, 4.0f}, "asymmetric centre shift follows rotation");
    checkPoint(insetShin.topSphere, top, aura::math::Vec3{1.3f, 3.0f, 4.0f}, "rotated asymmetric upper extent");
    checkPoint(insetShin.bottomSphere, bottom, aura::math::Vec3{2.6f, 3.0f, 4.0f}, "rotated asymmetric lower extent");

    // A short part collapses to coincident spheres rather than a negative-height cylinder.
    aura::body::BodyPart3D shortPart;
    shortPart.size = {0.6f, 0.2f, 0.4f};
    const auto shortCapsule = aura::render::capsuleTransforms(shortPart);
    checkPoint(shortCapsule.cylinder, top, origin, "short cylinder height is zero");
    checkPoint(shortCapsule.topSphere, origin, origin, "short capsule top centre");
    checkPoint(shortCapsule.bottomSphere, origin, origin, "short capsule bottom centre");
    checkPoint(shortCapsule.topSphere, top, aura::math::Vec3{0.0f, 0.2f, 0.0f}, "radius uses smaller width/depth");
    return passed ? 0 : 1;
}
