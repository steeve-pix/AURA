#include "screens/ControlScreen.hpp"
#include "screens/ControlMovements.hpp"
#include "screens/ControlVectors.hpp"
#include "ui/ScreenCanvas.hpp"
#include <cassert>
#include <clocale>
#include <fstream>
#include <iostream>
#include <regex>

namespace {
    std::string plain(const std::string &line) {
        static const std::regex escapes("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])");
        return std::regex_replace(line, escapes, "");
    }
}

int main(int argc, char **argv) {
    std::setlocale(LC_CTYPE, "");
    aura::cli::JointControls controls{};
    for (std::size_t i = 0; i < aura::sim::actionSpecs.size(); ++i) {
        const auto &spec = aura::sim::actionSpecs[i];
        controls[i] = {spec.label, 0.0, spec.minimum, spec.maximum};
    }
    const auto &joints = aura::cli::joint_vector_controls();
    assert(joints.size() == 15);
    std::array < int, aura::sim::actionSpecs.size() > coverage{};
    for (const auto &joint: joints) {
        for (int slot: joint.slots)
            if (slot >= 0) {
                ++coverage[slot];
                assert(joint.key == aura::sim::actionSpecs[slot].jointName);
            }
    }
    assert(std::all_of(coverage.begin(), coverage.end(), [](int count) { return count == 1; }));
    const auto shoulder = std::find_if(joints.begin(), joints.end(), [](const auto &joint) {
        return joint.key == "leftShoulder";
    });
    const aura::cli::JointTargetVector requested{-.3, .2, -.1};
    const auto grouped = aura::cli::with_joint_target(controls, *shoulder, requested);
    assert(aura::cli::joint_target_vector(grouped, *shoulder) == requested);
    for (std::size_t slot = 0; slot < controls.size(); ++slot)
        if (std::find(shoulder->slots.begin(), shoulder->slots.end(), slot) == shoulder->slots.end())
            assert(grouped[slot].value == controls[slot].value);
    assert(aura::cli::parse_joint_vector("(-0.3, 0.2, -0.1)") == requested);
    assert(aura::cli::parse_joint_vector("-0.3 0.2 -0.1") == requested);
    for (const auto *invalid: {"", "1 2", "1 2 3 4", "1,,2", "1,2,3,", "nan 0 0", "1e999 0 0"}) {
        bool rejected = false;
        try { aura::cli::parse_joint_vector(invalid); } catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
    }
    const auto knee = std::find_if(joints.begin(), joints.end(), [](const auto &joint) {
        return joint.key == "leftKnee";
    });
    const auto ankle = std::find_if(joints.begin(), joints.end(), [](const auto &joint) {
        return joint.key == "leftAnkle";
    });
    assert(knee->slots[1] == -1 && knee->slots[2] == -1);
    assert(aura::cli::next_joint_axis(*knee, 0) == 0);
    assert(aura::cli::next_joint_axis(*ankle, 0) == 2);
    bool rejected = false;
    try { aura::cli::with_joint_target(controls, *knee, {.2, .1, 0}); } catch (const std::invalid_argument &) {
        rejected = true;
    }
    assert(rejected && controls[knee->slots[0]].value == 0);
    const auto scalar = aura::cli::with_joint_input(grouped, *shoulder, 1, ".4");
    assert((aura::cli::joint_target_vector(scalar, *shoulder) == aura::cli::JointTargetVector{-.3, .4, -.1}));
    const auto full = aura::cli::with_joint_input(scalar, *shoulder, 2, ".2 .2 .2");
    assert((aura::cli::joint_target_vector(full, *shoulder) == aura::cli::JointTargetVector{.2, .2, .2}));
    const auto two = aura::cli::with_joint_input(controls, *ankle, 2, ".15 -.1");
    assert((aura::cli::joint_target_vector(two, *ankle) == aura::cli::JointTargetVector{.15, 0, -.1}));
    const auto ankleScalar = aura::cli::with_joint_input(two, *ankle, 2, ".1");
    assert((aura::cli::joint_target_vector(ankleScalar, *ankle) == aura::cli::JointTargetVector{.15, 0, .1}));
    assert(aura::cli::with_joint_input(controls, *knee, 0, ".2")[knee->slots[0]].value == .2);
    assert(aura::cli::with_joint_input(controls, *knee, 0, "(.2,0,0)")[knee->slots[0]].value == .2);
    for (const auto *invalid: {".2 .3", "nan", "inf", "1e999", "1,,2", "1,2,3,", "1 2 3 4"}) {
        bool bad = false;
        try { aura::cli::with_joint_input(grouped, *shoulder, 0, invalid); } catch (const std::invalid_argument &) {
            bad = true;
        }
        assert(bad);
    }
    bool badCount = false;
    try { aura::cli::with_joint_input(controls, *knee, 0, ".2 .3"); } catch (const std::invalid_argument &) {
        badCount = true;
    }
    assert(badCount && controls[knee->slots[0]].value == 0);
    const auto &movements = aura::cli::control_movements();
    auto preserved = controls;
    preserved[11].value = .35; // Elbow and wrist targets survive shoulder-group edits.
    preserved[13].value = .15;
    preserved[0].value = .2;
    preserved[8].value = .1; // Stored disabled head target is also preserved.
    const auto paired = aura::cli::with_movement_input(preserved, movements[0], 0, "-.5 .2 -.3");
    assert(paired[9].value == -.5 && paired[10].value == -.5);
    assert(paired[15].value == .2 && paired[16].value == -.2);
    assert(paired[17].value == -.3 && paired[18].value == .3);
    for (std::size_t slot = 0; slot < controls.size(); ++slot)
        if (!movements[0].affected[slot]) assert(paired[slot].value == preserved[slot].value);
    assert(!aura::cli::movement_mixed_axis(paired, movements[0], 1));
    const auto left = aura::cli::with_movement_input(paired, movements[1], 1, ".4");
    assert(left[15].value == .4 && left[16].value == -.2);
    assert(aura::cli::movement_mixed_axis(left, movements[0], 1));
    const auto linked = aura::cli::with_movement_input(left, movements[0], 2, ".1");
    assert(linked[17].value == .1 && linked[18].value == -.1);
    assert(linked[15].value == .4 && linked[16].value == -.2); // Scalar preserves other axes, even mixed ones.
    assert(aura::cli::with_movement_input(controls, movements[11], 2, ".15 .1")[31].value == .1);
    assert(aura::cli::with_movement_input(controls, movements[11], 2, ".15 .1")[32].value == -.1);
    bool invalidMovement = false;
    try { aura::cli::with_movement_input(controls, movements[11], 0, ".1 .2 .3"); } catch (const std::invalid_argument &
    ) { invalidMovement = true; }
    assert(invalidMovement);
    controls[0].value = 0.15;
    controls[1].value = -0.10;
    controls[2].value = 0.25;
    controls[3].value = 0.30;
    aura::cli::ControlObservation observation;
    observation.age_seconds = 0.03;
    observation.pelvis_height = 3.4;
    observation.pelvis_uprightness = 0.98;
    observation.is_standing = true;
    observation.has_motion = true;
    observation.simulation_time = 8.5;
    observation.steps = 1020;
    observation.joint_angles = {0.123, -0.08, 0.2, 0.28};
    // Static schematic fixture; this does not claim physical stability.
    const std::array<std::array < double, 3>,16 > positions{
                                                 {
                                                     {0, 3, 0}, {0, 3.6, 0}, {0, 4.1, 0}, {0, 4.5, 0}, {0, 4.9, 0},
                                                     {-.7, 4, 0}, {.7, 4, 0}, {-1, 3.3, 0}, {1, 3.3, 0}, {-1, 2.8, 0},
                                                     {1, 2.8, 0},
                                                     {-.3, 2.4, 0}, {.3, 2.4, 0}, {-.3, 1.4, 0}, {.3, 1.4, 0},
                                                     {0, .6, 0}
                                                 }
                                             };
    for (const auto &position: positions)
        observation.body_pose.push_back({position[0], position[1], position[2], 1, 0, 0, 0, .45, .7, .4});
    auto render = [&](int width, int height, int mode, bool editing = false) {
        std::optional<aura::cli::ControlObservation> data = observation;
        if (mode == 0) data.reset();
        if (mode == 2) data->age_seconds = 2.0;
        return aura::cli::vector_control_frame(controls, data, 0, 0, .05, editing, false, "(-0.125, 0, 0)",
                                               "Targets applied to viewer.", false, .5, width, height);
    };
    for (int width: {1, 20, 59, 75, 76, 80, 112, 120, 160, 220})
        for (int height: {1, 8, 16, 17, 24, 25, 28, 34, 40, 60})
            for (int mode = 0; mode < 3; ++mode)
                for (bool editing: {false, true}) {
                    const auto frame = render(width, height, mode, editing);
                    const auto movementFrame = aura::cli::movement_frame(
                        controls, mode == 0 ? std::nullopt : std::optional{observation}, 7, "Ready", false, .5, width,
                        height);
                    assert(static_cast<int>(movementFrame.size()) == height);
                    for (const auto &row: movementFrame) assert(aura::cli::screen::textCells(plain(row)) <= width);
                    const auto vectorFrame = aura::cli::vector_control_frame(
                        controls, mode == 0 ? std::nullopt : std::optional{observation}, 9, 2, .05, editing, false,
                        "(-.3, .2, 0)", "Ready", false, .5, width, height);
                    assert(static_cast<int>(vectorFrame.size()) == height);
                    for (const auto &row: vectorFrame) assert(aura::cli::screen::textCells(plain(row)) <= width);
                    assert(static_cast<int>(frame.size()) == height);
                    for (const auto &row: frame) assert(aura::cli::screen::textCells(plain(row)) <= width);
                }
    for (int mode = 0; mode < 3; ++mode) {
        std::string frame;
        for (const auto &row: render(120, 40, mode)) frame += plain(row) + '\n';
        assert((frame.find("+0.123") != std::string::npos) == (mode == 1));
        assert(frame.find("+0.150") != std::string::npos);
        if (mode == 2) assert(frame.find("STALE") != std::string::npos);
    }
    const auto shortFrame = render(80, 17, 1);
    assert(plain(shortFrame[11]).find("Targets applied") == std::string::npos);
    assert(plain(shortFrame[12]).find("Targets applied") != std::string::npos);
    std::string lastJointFrame;
    for (const auto &row: aura::cli::vector_control_frame(controls, observation, joints.size() - 1, 2, .05,
                                                          false, false, "", "Ready", false, .5, 80, 17))
        lastJointFrame += plain(row) + '\n';
    assert(lastJointFrame.find("Right wrist") != std::string::npos);
    assert(lastJointFrame.find("Single angles") == std::string::npos);
    observation.has_motor_states = true;
    observation.motor_enabled.fill(true);
    const auto head = std::find_if(joints.begin(), joints.end(), [](const auto &joint) { return joint.key == "head"; });
    observation.motor_enabled[head->slots[0]] = false;
    std::string motorFrame;
    for (const auto &row: aura::cli::vector_control_frame(controls, observation, head - joints.begin(), 0, .05,
                                                          false, false, "", "Ready", false, .5, 120, 40))
        motorFrame += plain(row) + '\n';
    assert(motorFrame.find("Motor off / target stored") != std::string::npos);
    assert(motorFrame.find("Actual") != std::string::npos && motorFrame.find("Error") != std::string::npos);
    assert(motorFrame.find("Locked at zero") != std::string::npos);
    assert(motorFrame.find("[E] edit axis") == std::string::npos);
    assert(motorFrame.find("[Enter] edit") != std::string::npos);
    std::string scalarFrame;
    for (const auto &row: aura::cli::vector_control_frame(controls, observation, 0, 1, .05,
                                                          true, true, ".2", "Ready", false, .5, 120, 40))
        scalarFrame += plain(row) + '\n';
    assert(scalarFrame.find("Turn target: .2_") != std::string::npos);
    // Range endpoints and markers must follow each axis independently.
    auto scaleControls = controls;
    auto scaleObservation = observation;
    const auto shoulderIndex = static_cast<std::size_t>(shoulder - joints.begin());
    for (int component = 0; component < 3; ++component) {
        const int slot = shoulder->slots[component];
        scaleControls[slot].value = scaleControls[slot].maximum;
        scaleObservation.joint_angles[slot] = scaleControls[slot].minimum;
    }
    const auto scaleFrame = aura::cli::vector_control_frame(scaleControls, scaleObservation, shoulderIndex, 0, .05,
                                                            false, false, "", "Ready", false, .5, 120, 40);
    const auto actualMarker = aura::cli::screen::glyph("●", "o"), targetMarker = aura::cli::screen::glyph("◆", "^");
    for (int component = 0; component < 3; ++component) {
        const auto row = plain(scaleFrame[30 + component]);
        const auto &control = scaleControls[shoulder->slots[component]];
        assert(row.find("[" + actualMarker) != std::string::npos);
        assert(row.find(targetMarker + "]") != std::string::npos);
        assert(row.find(aura::cli::screen::number(control.minimum, 2)) != std::string::npos);
        assert(row.find(aura::cli::screen::number(control.maximum, 2)) != std::string::npos);
    }
    scaleObservation.joint_angles[shoulder->slots[0]] = scaleControls[shoulder->slots[0]].value;
    auto overlap = aura::cli::vector_control_frame(scaleControls, scaleObservation, shoulderIndex, 0, .05,
                                                   false, false, "", "Ready", false, .5, 120, 40);
    assert(plain(overlap[30]).find(aura::cli::screen::glyph("◈", "@")) != std::string::npos);
    scaleObservation.joint_angles[shoulder->slots[0]] = scaleControls[shoulder->slots[0]].maximum + .1;
    const auto outside = aura::cli::vector_control_frame(scaleControls, scaleObservation, shoulderIndex, 0, .05,
                                                         false, false, "", "Ready", false, .5, 120, 40);
    assert(plain(outside[30]).find(targetMarker + ">") != std::string::npos);
    scaleObservation.age_seconds = 2;
    const auto stale = aura::cli::vector_control_frame(scaleControls, scaleObservation, shoulderIndex, 0, .05,
                                                       false, false, "", "Ready", false, .5, 120, 40);
    assert(plain(stale[30]).find(actualMarker) == std::string::npos);
    assert(plain(stale[30]).find(targetMarker) != std::string::npos);
    scaleObservation.age_seconds = .03;
    for (int component = 0; component < 3; ++component)
        scaleObservation.joint_angles[shoulder->slots[component]] = scaleControls[shoulder->slots[component]].minimum;
    const auto compactScales = aura::cli::vector_control_frame(scaleControls, scaleObservation, shoulderIndex, 0, .05,
                                                               false, false, "", "Ready", false, .5, 80, 17);
    for (int row = 9; row <= 11; ++row) {
        assert(plain(compactScales[row]).find(actualMarker) != std::string::npos);
        assert(plain(compactScales[row]).find(targetMarker) != std::string::npos);
    }
    const auto &armMovement = movements[0];
    const auto commanded = aura::cli::with_movement_input(controls, armMovement, 0, "-.5 .2 -.3");
    auto tracking = observation;
    for (std::size_t slot = 0; slot < controls.size(); ++slot) tracking.joint_angles[slot] = commanded[slot].value;
    auto feedback = aura::cli::movement_feedback(commanded, armMovement, tracking);
    assert(feedback.coordinates == 6 && aura::cli::movement_joints(armMovement).size() == 2);
    assert(feedback.meanError && *feedback.meanError == 0 && *feedback.maxError == 0);
    tracking.joint_angles[9] += .28;
    feedback = aura::cli::movement_feedback(commanded, armMovement, tracking);
    assert(std::abs(*feedback.meanError - .28 / 6) < 1e-8 && std::abs(*feedback.maxError - .28) < 1e-8);
    for (const auto slot: shoulder->slots) tracking.motor_enabled[slot] = false;
    feedback = aura::cli::movement_feedback(commanded, armMovement, tracking);
    assert(feedback.disabledJoints == 1); // Count motors, not three shoulder coordinates.
    std::string movementDetails;
    for (const auto &row: aura::cli::movement_frame(commanded, tracking, 0, "Ready", false, .5, 160, 40))
        movementDetails += plain(row) + '\n';
    assert(movementDetails.find("MOVEMENT VECTORS") != std::string::npos);
    assert(movementDetails.find("Both arms") != std::string::npos);
    assert(movementDetails.find("Bend") != std::string::npos && movementDetails.find("Turn") != std::string::npos);
    assert(movementDetails.find("Shoulders / elbow and wrist targets stay independent") != std::string::npos);
    assert(movementDetails.find("Motor off in selected group") != std::string::npos);
    assert(movementDetails.find("[Tab] axis") != std::string::npos);
    assert(movementDetails.find("BODY / TELEMETRY") != std::string::npos);
    assert(movementDetails.find("Time 8.5 s / 1,020 steps") != std::string::npos);
    assert(movementDetails.find("13/15 motors on") != std::string::npos); // Existing head off + shoulder off.
    tracking.age_seconds = 2;
    feedback = aura::cli::movement_feedback(commanded, armMovement, tracking);
    assert(!feedback.meanError && !feedback.maxError && feedback.disabledJoints == 0);
    std::string staleMovement;
    for (const auto &row: aura::cli::movement_frame(commanded, tracking, 0, "Ready", false, .5, 160, 40))
        staleMovement += plain(row) + '\n';
    assert(staleMovement.find("live measurements unavailable") != std::string::npos);
    assert(staleMovement.find("Mean error 0.020") == std::string::npos);
    assert(staleMovement.find("no live data") != std::string::npos);
    assert(!aura::cli::movement_feedback(commanded, armMovement, std::nullopt).meanError);
    if (argc == 2) {
        std::ofstream out(argv[1]);
        for (const auto &row: render(120, 40, 1)) out << row << '\n';
        std::ofstream movementOut(std::string(argv[1]) + "-movement.ansi");
        tracking.age_seconds = .03;
        for (const auto &row: aura::cli::movement_frame(commanded, tracking, 0, "Ready", false, .5, 160, 40))
            movementOut << row << '\n';
    }
    std::cout << "Control layout, resize, editing, and missing/stale telemetry checks passed.\n";
}
