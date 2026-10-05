#pragma once

#include "aura/body/BodyPart3D.hpp"

namespace aura::body {
    struct AuraBody3D {
        BodyPart3D head;
        BodyPart3D neck;
        BodyPart3D torso;
        BodyPart3D pelvis;

        BodyPart3D leftUpperArm;
        BodyPart3D leftForearm;
        BodyPart3D leftHand;
        BodyPart3D rightUpperArm;
        BodyPart3D rightForearm;
        BodyPart3D rightHand;

        BodyPart3D leftThigh;
        BodyPart3D leftShin;
        BodyPart3D leftFoot;
        BodyPart3D rightThigh;
        BodyPart3D rightShin;
        BodyPart3D rightFoot;
    };

    // A complete, upright starting pose. Joint constraints are configured separately.
    AuraBody3D createAuraBody3D();

    // Translate each side of the waist without changing the group's internal anchor offsets.
    void translateUpperBody(AuraBody3D &body, const math::Vec3 &delta);
    void translateLowerBody(AuraBody3D &body, const math::Vec3 &delta);
}
