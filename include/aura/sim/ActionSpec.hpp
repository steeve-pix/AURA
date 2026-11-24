#pragma once
#include <array>

namespace aura::sim {
    inline constexpr int actionSchemaVersion = 4;

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
        float leftShoulderSwingYTarget = 0.0f;
        float rightShoulderSwingYTarget = 0.0f;
        float leftShoulderSwingZTarget = 0.0f;
        float rightShoulderSwingZTarget = 0.0f;
        float leftHipSwingYTarget = 0.0f;
        float rightHipSwingYTarget = 0.0f;
        float leftHipSwingZTarget = 0.0f;
        float rightHipSwingZTarget = 0.0f;
        float waistSwingYTarget = 0.0f;
        float waistSwingZTarget = 0.0f;
        float neckSwingYTarget = 0.0f;
        float neckSwingZTarget = 0.0f;
        float leftWristSwingYTarget = 0.0f;
        float rightWristSwingYTarget = 0.0f;
        float leftWristSwingZTarget = 0.0f;
        float rightWristSwingZTarget = 0.0f;
        float leftAnkleSwingZTarget = 0.0f;
        float rightAnkleSwingZTarget = 0.0f;
    };

    // Public action contract shared by physics, Python bindings and the CLI.
    enum class JointCoordinate { Twist, SwingY, SwingZ };

    struct ActionSpec {
        const char *key;
        const char *angleKey;
        const char *label;
        double minimum, maximum;
        const char *jointName;
        float AuraAction::*target;
        JointCoordinate coordinate = JointCoordinate::Twist;
        const char *directionHint = "Targets apply immediately in the viewer";

        // Generic accessors keep this metadata header usable by the standalone
        // CLI without linking physical body code.
        float &jointTarget(auto &joint) const {
            if (coordinate == JointCoordinate::SwingY) return joint.targetSwingY;
            if (coordinate == JointCoordinate::SwingZ) return joint.targetSwingZ;
            return joint.targetAngle;
        }

        float jointMinimum(const auto &joint) const {
            if (coordinate == JointCoordinate::SwingY) return joint.minSwingY;
            if (coordinate == JointCoordinate::SwingZ) return joint.minSwingZ;
            return joint.minAngle;
        }

        float jointMaximum(const auto &joint) const {
            if (coordinate == JointCoordinate::SwingY) return joint.maxSwingY;
            if (coordinate == JointCoordinate::SwingZ) return joint.maxSwingZ;
            return joint.maxAngle;
        }
    };

    inline constexpr std::array actionSpecs{
        ActionSpec{"left_hip_target", "left_hip_angle", "Left hip", -1.8, .5, "leftHip", &AuraAction::leftHipTarget},
        ActionSpec{
            "right_hip_target", "right_hip_angle", "Right hip", -1.8, .5, "rightHip", &AuraAction::rightHipTarget
        },
        ActionSpec{
            "left_knee_target", "left_knee_angle", "Left knee", 0., 2.2, "leftKnee", &AuraAction::leftKneeTarget
        },
        ActionSpec{
            "right_knee_target", "right_knee_angle", "Right knee", 0., 2.2, "rightKnee", &AuraAction::rightKneeTarget
        },
        ActionSpec{
            "left_ankle_target", "left_ankle_angle", "Left ankle", -.35, .85, "leftAnkle", &AuraAction::leftAnkleTarget
        },
        ActionSpec{
            "right_ankle_target", "right_ankle_angle", "Right ankle", -.35, .85, "rightAnkle",
            &AuraAction::rightAnkleTarget
        },
        ActionSpec{"waist_target", "waist_angle", "Waist", -.5, .5, "waist", &AuraAction::waistTarget},
        ActionSpec{"neck_target", "neck_angle", "Neck", -.5, .5, "neck", &AuraAction::neckTarget},
        ActionSpec{"head_target", "head_angle", "Head", -.5, .5, "head", &AuraAction::headTarget},
        ActionSpec{
            "left_shoulder_target", "left_shoulder_angle", "L arm forward", -3.0, .8, "leftShoulder",
            &AuraAction::leftShoulderTarget, JointCoordinate::Twist,
            "Negative raises forward / -1.57 straight ahead / -3.00 overhead"
        },
        ActionSpec{
            "right_shoulder_target", "right_shoulder_angle", "R arm forward", -3.0, .8, "rightShoulder",
            &AuraAction::rightShoulderTarget, JointCoordinate::Twist,
            "Negative raises forward / -1.57 straight ahead / -3.00 overhead"
        },
        ActionSpec{
            "left_elbow_target", "left_elbow_angle", "Left elbow", 0., 2.4, "leftElbow", &AuraAction::leftElbowTarget
        },
        ActionSpec{
            "right_elbow_target", "right_elbow_angle", "Right elbow", 0., 2.4, "rightElbow",
            &AuraAction::rightElbowTarget
        },
        ActionSpec{
            "left_wrist_target", "left_wrist_angle", "Left wrist", -1.2, 1.2, "leftWrist", &AuraAction::leftWristTarget
        },
        ActionSpec{
            "right_wrist_target", "right_wrist_angle", "Right wrist", -1.2, 1.2, "rightWrist",
            &AuraAction::rightWristTarget
        },
        ActionSpec{
            "left_shoulder_swing_y_target", "left_shoulder_swing_y_angle", "L arm across", -.8, .8, "leftShoulder",
            &AuraAction::leftShoulderSwingYTarget, JointCoordinate::SwingY,
            "With arm forward: positive turns inward / negative turns outward"
        },
        ActionSpec{
            "right_shoulder_swing_y_target", "right_shoulder_swing_y_angle", "R arm across", -.8, .8, "rightShoulder",
            &AuraAction::rightShoulderSwingYTarget, JointCoordinate::SwingY,
            "With arm forward: negative turns inward / positive turns outward"
        },
        ActionSpec{
            "left_shoulder_swing_z_target", "left_shoulder_swing_z_angle", "L arm sideways", -1.8, .2, "leftShoulder",
            &AuraAction::leftShoulderSwingZTarget, JointCoordinate::SwingZ,
            "With arm hanging: negative lifts sideways / -1.57 horizontal"
        },
        ActionSpec{
            "right_shoulder_swing_z_target", "right_shoulder_swing_z_angle", "R arm sideways", -.2, 1.8,
            "rightShoulder", &AuraAction::rightShoulderSwingZTarget, JointCoordinate::SwingZ,
            "With arm hanging: positive lifts sideways / +1.57 horizontal"
        },
        ActionSpec{
            "left_hip_swing_y_target", "left_hip_swing_y_angle", "L hip turn", -0.9, 0.6, "leftHip",
            &AuraAction::leftHipSwingYTarget, JointCoordinate::SwingY,
            "Negative turns left foot outward / positive inward"
        },
        ActionSpec{
            "right_hip_swing_y_target", "right_hip_swing_y_angle", "R hip turn", -0.6, 0.9, "rightHip",
            &AuraAction::rightHipSwingYTarget, JointCoordinate::SwingY,
            "Positive turns right foot outward / negative inward"
        },
        ActionSpec{
            "left_hip_swing_z_target", "left_hip_swing_z_angle", "L leg sideways", -0.9, 0.5, "leftHip",
            &AuraAction::leftHipSwingZTarget, JointCoordinate::SwingZ,
            "Negative moves leg outward / positive across midline"
        },
        ActionSpec{
            "right_hip_swing_z_target", "right_hip_swing_z_angle", "R leg sideways", -0.5, 0.9, "rightHip",
            &AuraAction::rightHipSwingZTarget, JointCoordinate::SwingZ,
            "Positive moves leg outward / negative across midline"
        },
        ActionSpec{
            "waist_swing_y_target", "waist_swing_y_angle", "Torso turn", -1.0, 1.0, "waist",
            &AuraAction::waistSwingYTarget, JointCoordinate::SwingY,
            "Positive turns torso left / negative right, relative to pelvis"
        },
        ActionSpec{
            "waist_swing_z_target", "waist_swing_z_angle", "Torso side bend", -0.35, 0.35, "waist",
            &AuraAction::waistSwingZTarget, JointCoordinate::SwingZ,
            "Positive bends torso right / negative left, relative to pelvis"
        },
        ActionSpec{
            "neck_swing_y_target", "neck_swing_y_angle", "Look left/right", -0.9, 0.9, "neck",
            &AuraAction::neckSwingYTarget, JointCoordinate::SwingY, "Negative looks left / positive looks right"
        },
        ActionSpec{
            "neck_swing_z_target", "neck_swing_z_angle", "Head side tilt", -0.3, 0.3, "neck",
            &AuraAction::neckSwingZTarget, JointCoordinate::SwingZ, "Positive tilts head left / negative right"
        },
        ActionSpec{
            "left_wrist_swing_y_target", "left_wrist_swing_y_angle", "L hand turn", -1.2, 1.2, "leftWrist",
            &AuraAction::leftWristSwingYTarget, JointCoordinate::SwingY,
            "Turn hand around its hanging length; parent-local coordinate"
        },
        ActionSpec{
            "right_wrist_swing_y_target", "right_wrist_swing_y_angle", "R hand turn", -1.2, 1.2, "rightWrist",
            &AuraAction::rightWristSwingYTarget, JointCoordinate::SwingY,
            "Turn hand around its hanging length; parent-local coordinate"
        },
        ActionSpec{
            "left_wrist_swing_z_target", "left_wrist_swing_z_angle", "L wrist sideways", -0.35, 0.35, "leftWrist",
            &AuraAction::leftWristSwingZTarget, JointCoordinate::SwingZ,
            "Positive moves hanging hand right / negative left"
        },
        ActionSpec{
            "right_wrist_swing_z_target", "right_wrist_swing_z_angle", "R wrist sideways", -0.35, 0.35, "rightWrist",
            &AuraAction::rightWristSwingZTarget, JointCoordinate::SwingZ,
            "Positive moves hanging hand right / negative left"
        },
        ActionSpec{
            "left_ankle_swing_z_target", "left_ankle_swing_z_angle", "L ankle tilt", -0.3, 0.3, "leftAnkle",
            &AuraAction::leftAnkleSwingZTarget, JointCoordinate::SwingZ,
            "Tilt foot sideways; forward axis stays with the shin"
        },
        ActionSpec{
            "right_ankle_swing_z_target", "right_ankle_swing_z_angle", "R ankle tilt", -0.3, 0.3, "rightAnkle",
            &AuraAction::rightAnkleSwingZTarget, JointCoordinate::SwingZ,
            "Tilt foot sideways; forward axis stays with the shin"
        },
    };
}
