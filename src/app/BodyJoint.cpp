#include "aura/app/BodyJoint.hpp"

namespace aura::app {
    std::array<BodyJoint, 15> makeBodyJoints(body::AuraBody3D &auraBody,
                                             body::AuraSkeleton3D &skeleton) {
        return {
            {
                {"waist", &auraBody.torso, &auraBody.pelvis, skeleton.waist, true, 0.0f},
                {"neck", &auraBody.torso, &auraBody.neck, skeleton.neck, true, 0.0f},
                // Both neck/head motors work independently, but their existing
                // coupled gains oscillate at 120 Hz. Keep head opt-in until that
                // integration issue is resolved; no gains or constraints change.
                {"head", &auraBody.neck, &auraBody.head, skeleton.head, false, 0.0f},
                {"leftShoulder", &auraBody.torso, &auraBody.leftUpperArm, skeleton.leftShoulder, true, 0.145f},
                {"leftElbow", &auraBody.leftUpperArm, &auraBody.leftForearm, skeleton.leftElbow, true, 0.14f},
                {"leftWrist", &auraBody.leftForearm, &auraBody.leftHand, skeleton.leftWrist, true, 0.12f},
                {"rightShoulder", &auraBody.torso, &auraBody.rightUpperArm, skeleton.rightShoulder, true, 0.145f},
                {"rightElbow", &auraBody.rightUpperArm, &auraBody.rightForearm, skeleton.rightElbow, true, 0.14f},
                {"rightWrist", &auraBody.rightForearm, &auraBody.rightHand, skeleton.rightWrist, true, 0.12f},
                {"leftHip", &auraBody.pelvis, &auraBody.leftThigh, skeleton.leftHip, true, 0.16f},
                {"leftKnee", &auraBody.leftThigh, &auraBody.leftShin, skeleton.leftKnee, true, 0.14f},
                {"leftAnkle", &auraBody.leftShin, &auraBody.leftFoot, skeleton.leftAnkle, true, 0.12f},
                {"rightHip", &auraBody.pelvis, &auraBody.rightThigh, skeleton.rightHip, true, 0.16f},
                {"rightKnee", &auraBody.rightThigh, &auraBody.rightShin, skeleton.rightKnee, true, 0.14f},
                {"rightAnkle", &auraBody.rightShin, &auraBody.rightFoot, skeleton.rightAnkle, true, 0.12f},
            }
        };
    }
}
