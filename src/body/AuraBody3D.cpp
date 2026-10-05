#include "aura/body/AuraBody3D.hpp"

#include "aura/physics/Inertia.hpp"

namespace aura::body {
    AuraBody3D createAuraBody3D() {
        AuraBody3D result;
        const auto setPart = [](BodyPart3D &part, const char *name,
                                const math::Vec3 &size, const math::Vec3 &position) {
            part.name = name;
            part.size = size;
            part.body.position = position;
            part.body.momentOfInertia = physics::boxMomentOfInertia(part.body.mass, size);
        };
        setPart(result.head, "head", {1.05f, 1.05f, 1.0f}, {0.0f, 7.24f, 0.0f});
        setPart(result.neck, "neck", {0.3f, 0.18f, 0.3f}, {0.0f, 6.76f, 0.0f});
        setPart(result.torso, "torso", {1.5f, 1.45f, 0.8f}, {0.0f, 6.0f, 0.0f});
        setPart(result.pelvis, "pelvis", {1.12f, 1.08f, 0.72f}, {0.0f, 4.17f, 0.0f});

        // Mirrored limbs, with feet on Y=0 and toes pointing toward +Z.
        setPart(result.leftUpperArm, "left_upper_arm", {0.5f, 1.35f, 0.5f}, {-0.9f, 5.725f, 0.0f});
        setPart(result.leftForearm, "left_forearm", {0.44f, 1.25f, 0.44f}, {-0.9f, 4.425f, 0.0f});
        setPart(result.leftHand, "left_hand", {0.32f, 0.55f, 0.26f}, {-0.9f, 3.525f, 0.0f});
        setPart(result.rightUpperArm, "right_upper_arm", {0.5f, 1.35f, 0.5f}, {0.9f, 5.725f, 0.0f});
        setPart(result.rightForearm, "right_forearm", {0.44f, 1.25f, 0.44f}, {0.9f, 4.425f, 0.0f});
        setPart(result.rightHand, "right_hand", {0.32f, 0.55f, 0.26f}, {0.9f, 3.525f, 0.0f});

        setPart(result.leftThigh, "left_thigh", {0.58f, 1.8f, 0.58f}, {-0.34f, 2.92f, 0.0f});
        setPart(result.leftShin, "left_shin", {0.52f, 1.6f, 0.52f}, {-0.34f, 1.22f, 0.0f});
        setPart(result.leftFoot, "left_foot", {0.65f, 0.3f, 1.05f}, {-0.34f, 0.15f, 0.25f});
        setPart(result.rightThigh, "right_thigh", {0.58f, 1.8f, 0.58f}, {0.34f, 2.92f, 0.0f});
        setPart(result.rightShin, "right_shin", {0.52f, 1.6f, 0.52f}, {0.34f, 1.22f, 0.0f});
        setPart(result.rightFoot, "right_foot", {0.65f, 0.3f, 1.05f}, {0.34f, 0.15f, 0.25f});
        return result;
    }
}
