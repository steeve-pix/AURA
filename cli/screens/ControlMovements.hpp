#pragma once
#include "screens/ControlVectors.hpp"

namespace aura::cli {
    // Movement groups reuse joint coordinates; bilateral groups mirror Y/Z across
    // the body's sagittal plane. They command the root joint, preserving child targets.
    struct ControlMovement : JointVectorControl {
        std::string description;
        std::vector<std::size_t> joints;
        std::array<bool, sim::actionSpecs.size()> affected{};
    };

    inline const std::vector<ControlMovement> &control_movements() {
        static const auto groups = [] {
            std::vector<ControlMovement> result;
            const auto add = [&
                    ](const char *name, const char *primary, const char *secondary, const char *description) {
                ControlMovement group;
                group.name = name;
                group.key = primary;
                group.description = description;
                const auto &joints = joint_vector_controls();
                for (const auto *key: {primary, secondary})
                    if (*key) {
                        const auto joint = std::find_if(joints.begin(), joints.end(),
                                                        [&](const auto &j) { return j.key == key; });
                        if (joint == joints.end()) throw std::logic_error("Unknown movement joint");
                        if (group.joints.empty()) group.slots = joint->slots;
                        group.joints.push_back(static_cast<std::size_t>(joint - joints.begin()));
                        for (const auto slot: joint->slots) if (slot >= 0) group.affected[slot] = true;
                    }
                result.push_back(group);
            };
            add("Both arms", "leftShoulder", "rightShoulder", "Shoulders / elbow and wrist targets stay independent");
            add("Left arm", "leftShoulder", "", "Left shoulder / elbow and wrist targets stay independent");
            add("Right arm", "rightShoulder", "", "Right shoulder / elbow and wrist targets stay independent");
            add("Both legs", "leftHip", "rightHip", "Hips / knee and ankle targets stay independent");
            add("Left leg", "leftHip", "", "Left hip / knee and ankle targets stay independent");
            add("Right leg", "rightHip", "", "Right hip / knee and ankle targets stay independent");
            add("Torso", "waist", "", "Waist / torso relative to pelvis");
            add("Head", "neck", "", "Neck / head direction; separate head motor stays unchanged");
            add("Both hands", "leftWrist", "rightWrist", "Wrists / hands relative to forearms");
            add("Left hand", "leftWrist", "", "Left wrist / hand relative to forearm");
            add("Right hand", "rightWrist", "", "Right wrist / hand relative to forearm");
            add("Both feet", "leftAnkle", "rightAnkle", "Ankles / feet relative to shins");
            add("Left foot", "leftAnkle", "", "Left ankle / foot relative to shin");
            add("Right foot", "rightAnkle", "", "Right ankle / foot relative to shin");
            return result;
        }();
        return groups;
    }

    inline const std::vector<JointVectorControl> &movement_vector_controls() {
        static const auto groups = [] {
            std::vector<JointVectorControl> result;
            for (const auto &movement: control_movements()) result.push_back(movement);
            return result;
        }();
        return groups;
    }

    inline const std::vector<std::size_t> &movement_joints(const ControlMovement &movement) { return movement.joints; }

    inline bool movement_mixed_axis(const JointControls &controls, const ControlMovement &movement, std::size_t axis) {
        if (movement.joints.size() < 2 || movement.slots[axis] < 0) return false;
        const auto slot = joint_vector_controls()[movement.joints[1]].slots[axis];
        return std::abs(controls[movement.slots[axis]].value - controls[slot].value * (axis == 0 ? 1 : -1)) > .001;
    }

    inline JointControls with_movement_input(JointControls controls, const ControlMovement &movement,
                                             std::size_t axis, const std::string &text) {
        const auto values = parse_joint_values(text);
        const auto primary = with_joint_input(controls, movement, axis, text);
        // A scalar changes only the selected axis, on each participating side.
        for (std::size_t index = 0; index < movement.joints.size(); ++index) {
            const auto &joint = joint_vector_controls()[movement.joints[index]];
            for (std::size_t component = 0; component < 3; ++component)
                if (joint.slots[component] >= 0 && (values.size() != 1 || component == axis))
                    controls[joint.slots[component]].value =
                            primary[movement.slots[component]].value * (index && component > 0 ? -1 : 1);
        }
        return controls;
    }

    struct MovementFeedback {
        int coordinates = 0, disabledJoints = 0;
        std::optional<double> meanError, maxError;
    };

    inline MovementFeedback movement_feedback(const JointControls &controls, const ControlMovement &movement,
                                              const std::optional<ControlObservation> &observation) {
        MovementFeedback result;
        const bool live = observation && observation->age_seconds <= control_freshness_seconds;
        double sum = 0, maximum = 0;
        bool measured = live;
        for (std::size_t i = 0; i < controls.size(); ++i)
            if (movement.affected[i]) {
                ++result.coordinates;
                if (!live || !std::isfinite(observation->joint_angles[i])) {
                    measured = false;
                    continue;
                }
                const double error = std::abs(controls[i].value - observation->joint_angles[i]);
                sum += error;
                maximum = std::max(maximum, error);
            }
        if (measured && result.coordinates) {
            result.meanError = sum / result.coordinates;
            result.maxError = maximum;
        }
        if (live && observation->has_motor_states)
            for (const auto index: movement_joints(movement)) {
                const auto &joint = joint_vector_controls()[index];
                const auto slot = std::find_if(joint.slots.begin(), joint.slots.end(), [&](int slot) {
                    return slot >= 0 && movement.affected[slot];
                });
                if (slot != joint.slots.end() && !observation->motor_enabled[*slot]) ++result.disabledJoints;
            }
        return result;
    }
}
