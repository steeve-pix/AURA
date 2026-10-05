#pragma once

#include "aura/body/BodyPart3D.hpp"
#include "aura/math/Mat4.hpp"

namespace aura::render {
    struct CapsuleTransforms {
        math::Mat4 cylinder;
        math::Mat4 topSphere;
        math::Mat4 bottomSphere;
    };

    // Render a Y-aligned capsule using unit-height/diameter cylinder and sphere meshes.
    // Insets shorten the visual ends in local Y; zero preserves the full-height capsule.
    CapsuleTransforms capsuleTransforms(const body::BodyPart3D &part,
                                        float topInset = 0.0f, float bottomInset = 0.0f);
}
