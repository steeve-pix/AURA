#include "BodyJoint.hpp"

namespace aura::app {
    std::array<BodyJoint, 15> makeBodyJoints(body::AuraBody3D& auraBody,
                                           body::AuraSkeleton3D& skeleton) {
        return {
            {
                {"waist", &auraBody.torso, &auraBody.pelvis, skeleton.waist, false, 0.0f},
                {"neck", &auraBody.torso, &auraBody.neck, skeleton.neck, false, 0.0f},
                {"head", &auraBody.neck, &auraBody.head, skeleton.head, false, 0.0f},
                {"leftShoulder", &auraBody.torso, &auraBody.leftUpperArm, skeleton.leftShoulder, false, 0.145f},
                {"leftElbow", &auraBody.leftUpperArm, &auraBody.leftForearm, skeleton.leftElbow, false, 0.14f},
                {"leftWrist", &auraBody.leftForearm, &auraBody.leftHand, skeleton.leftWrist, false, 0.12f},
                {"rightShoulder", &auraBody.torso, &auraBody.rightUpperArm, skeleton.rightShoulder, false, 0.145f},
                {"rightElbow", &auraBody.rightUpperArm, &auraBody.rightForearm, skeleton.rightElbow, false, 0.14f},
                {"rightWrist", &auraBody.rightForearm, &auraBody.rightHand, skeleton.rightWrist, false, 0.12f},
                {"leftHip", &auraBody.pelvis, &auraBody.leftThigh, skeleton.leftHip, true, 0.16f},
                {"leftKnee", &auraBody.leftThigh, &auraBody.leftShin, skeleton.leftKnee, true, 0.14f},
                {"leftAnkle", &auraBody.leftShin, &auraBody.leftFoot, skeleton.leftAnkle, false, 0.12f},
                {"rightHip", &auraBody.pelvis, &auraBody.rightThigh, skeleton.rightHip, true, 0.16f},
                {"rightKnee", &auraBody.rightThigh, &auraBody.rightShin, skeleton.rightKnee, true, 0.14f},
                {"rightAnkle", &auraBody.rightShin, &auraBody.rightFoot, skeleton.rightAnkle, false, 0.12f},
            }
        };

    }
}
