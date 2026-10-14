#pragma once
#include <array>

namespace aura::sim {
    inline constexpr int actionSchemaVersion = 2;

    struct AuraAction {
        float leftHipTarget = 0.0f;
        float rightHipTarget = 0.0f;
        float leftKneeTarget = 0.0f;
        float rightKneeTarget = 0.0f;
        float leftAnkleTarget = 0.0f;
        float rightAnkleTarget = 0.0f;
        float waistTarget = 0.0f;
        float neckTarget = 0.0f;
        float headTarget = 0.0f;
        float leftShoulderTarget = 0.0f;
        float rightShoulderTarget = 0.0f;
        float leftElbowTarget = 0.0f;
        float rightElbowTarget = 0.0f;
        float leftWristTarget = 0.0f;
        float rightWristTarget = 0.0f;
    };

    // Public action contract shared by physics, Python bindings and the CLI.
    struct ActionSpec {
        const char *key;
        const char *angleKey;
        const char *label;
        double minimum, maximum;
        const char *jointName;
        float AuraAction::*target;
    };

    inline constexpr std::array actionSpecs{
        ActionSpec{"left_hip_target", "left_hip_angle", "Left hip", -.8, .8, "leftHip", &AuraAction::leftHipTarget},
        ActionSpec{"right_hip_target", "right_hip_angle", "Right hip", -.8, .8, "rightHip", &AuraAction::rightHipTarget},
        ActionSpec{"left_knee_target", "left_knee_angle", "Left knee", 0., 2.2, "leftKnee", &AuraAction::leftKneeTarget},
        ActionSpec{"right_knee_target", "right_knee_angle", "Right knee", 0., 2.2, "rightKnee", &AuraAction::rightKneeTarget},
        ActionSpec{"left_ankle_target", "left_ankle_angle", "Left ankle", -.35, .85, "leftAnkle", &AuraAction::leftAnkleTarget},
        ActionSpec{"right_ankle_target", "right_ankle_angle", "Right ankle", -.35, .85, "rightAnkle", &AuraAction::rightAnkleTarget},
        ActionSpec{"waist_target", "waist_angle", "Waist", -.5, .5, "waist", &AuraAction::waistTarget},
        ActionSpec{"neck_target", "neck_angle", "Neck", -.5, .5, "neck", &AuraAction::neckTarget},
        ActionSpec{"head_target", "head_angle", "Head", -.5, .5, "head", &AuraAction::headTarget},
        ActionSpec{"left_shoulder_target", "left_shoulder_angle", "Left shoulder", -1.5, 1.5, "leftShoulder", &AuraAction::leftShoulderTarget},
        ActionSpec{"right_shoulder_target", "right_shoulder_angle", "Right shoulder", -1.5, 1.5, "rightShoulder", &AuraAction::rightShoulderTarget},
        ActionSpec{"left_elbow_target", "left_elbow_angle", "Left elbow", 0., 2.4, "leftElbow", &AuraAction::leftElbowTarget},
        ActionSpec{"right_elbow_target", "right_elbow_angle", "Right elbow", 0., 2.4, "rightElbow", &AuraAction::rightElbowTarget},
        ActionSpec{"left_wrist_target", "left_wrist_angle", "Left wrist", -1.2, 1.2, "leftWrist", &AuraAction::leftWristTarget},
        ActionSpec{"right_wrist_target", "right_wrist_angle", "Right wrist", -1.2, 1.2, "rightWrist", &AuraAction::rightWristTarget},
    };
}
