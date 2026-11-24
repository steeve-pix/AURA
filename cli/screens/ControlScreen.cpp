#include "screens/ControlScreen.hpp"
#include "screens/ControlMovements.hpp"
#include "screens/ControlVectors.hpp"
#include "ui/BodyPreview.hpp"
#include "ui/ScreenCanvas.hpp"
#include <initializer_list>
#include <utility>

namespace {
    using ControlHint = std::pair<std::string, std::string>;

    void control_hints(aura::cli::screen::Canvas &canvas, int x, int y, int width,
                       std::initializer_list<ControlHint> hints, const aura::cli::screen::Palette &p) {
        using namespace aura::cli::screen;
        std::string text;
        for (const auto &[key,description]: hints) {
            if (!text.empty()) text += "   ";
            text += key + " " + description;
        }
        canvas.shortcuts(x, y, text, p, std::max(0, width));
    }

    void control_footer(aura::cli::screen::Canvas &canvas, int x, int width, int height,
                        bool editing, bool movement, const std::string &note, const aura::cli::screen::Palette &p) {
        using namespace aura::cli::screen;
        canvas.footer(x, width, "", "", note, p);
        const int backWidth = shortcut_width(editing ? "Esc cancel edit" : "Q back   Esc home");
        if (editing) control_hints(canvas, x, height - 2, width - backWidth - 2,
                                   {{"Enter", "apply"}, {"Ctrl+U", "clear"}}, p);
        else
            control_hints(canvas, x, height - 2, width - backWidth - 2,
                          {
                              {glyph("↑↓", "Up/Down"), movement ? "part" : "joint"},
                              {glyph("←→", "Left/Right"), "adjust"},
                              {"Enter", "edit"}
                          }, p);
        if (editing) control_hints(canvas, x + width - backWidth, height - 2, backWidth, {{"Esc", "cancel edit"}}, p);
        else control_hints(canvas, x + width - backWidth, height - 2, backWidth, {{"Q", "back"}, {"Esc", "home"}}, p);
    }

    bool fresh(const std::optional<aura::cli::ControlObservation> &observation) {
        return observation && observation->age_seconds <= aura::cli::control_freshness_seconds;
    }

    std::string motor_summary(const std::optional<aura::cli::ControlObservation> &observation) {
        if (!fresh(observation) || !observation->has_motor_states) return "Motors unknown";
        int active = 0;
        for (const auto &joint: aura::cli::joint_vector_controls()) {
            const auto slot = std::find_if(joint.slots.begin(), joint.slots.end(), [](int slot) { return slot >= 0; });
            if (slot != joint.slots.end() && observation->motor_enabled[*slot]) ++active;
        }
        return std::to_string(active) + "/" + std::to_string(aura::cli::joint_vector_controls().size()) + " motors on";
    }

    void body_summary(aura::cli::screen::Canvas &canvas, int x, int width,
                      const std::optional<aura::cli::ControlObservation> &observation,
                      const aura::cli::screen::Palette &p, bool sidebar) {
        using namespace aura::cli::screen;
        if (!observation) {
            canvas.put(x, 3, "Waiting for viewer telemetry / targets remain editable", p.dim, width);
            return;
        }
        if (!fresh(observation)) {
            canvas.put(
                x, 3, "Last sample " + number(observation->age_seconds, 1) + " s old / live measurements unavailable",
                p.gold, width);
            return;
        }
        canvas.put(x, 3, observation->balance_state +
                         (observation->has_motion
                              ? (observation->paused ? " / paused" : " / running")
                              : " / motion unknown") +
                         " / pelvis " + number(observation->pelvis_height, 2) + " m / upright " + number(
                             observation->pelvis_uprightness, 3) +
                         (sidebar ? std::string{} : " / " + motor_summary(observation)), p.dim, width);
    }

    void body_panel(aura::cli::screen::Canvas &canvas, int x, int y, int width, int height,
                    const std::optional<aura::cli::ControlObservation> &observation, double yaw,
                    const aura::cli::screen::Palette &p) {
        using namespace aura::cli::screen;
        const bool live = fresh(observation);
        canvas.box(x, y, width, height, "BODY / TELEMETRY", p);
        canvas.put(x + 2, y + 1, motor_summary(observation), live ? p.text : p.dim, width - 4);
        canvas.put(x + 2, y + 2,
                   live && observation->has_motion
                       ? "Time " + number(observation->simulation_time, 1) + " s / " +
                         integer(static_cast<int>(observation->steps)) + " steps"
                       : "Simulation time unavailable", p.dim, width - 4);
        canvas.put(x + 2, y + 3,
                   live && observation->has_motion
                       ? "Speed " + number(observation->speed, 2) + " m/s / spin " +
                         number(observation->angular_speed, 2) + " rad/s"
                       : "Motion measurements unavailable", p.dim, width - 4);
        canvas.put(x + 2, y + 4,
                   observation
                       ? "Sample age " + number(observation->age_seconds, 2) + " s"
                       : "Waiting for viewer telemetry", live ? p.dim : p.gold, width - 4);
        if (observation && !observation->body_pose.empty()) {
            const auto pose = aura::cli::body_preview(*observation, width - 4, height - 8, yaw, p.caps.utf8);
            for (int row = 0; row < static_cast<int>(pose.size()); ++row)
                canvas.put(x + 2, y + 6 + row, pose[row], live ? p.purple : p.dim, width - 4);
        }
        canvas.put(x + 2, y + height - 2, live ? "Live pose / read-only" : "Last pose / awaiting live data", p.dim,
                   width - 4);
    }

    // Display-only mapping. Out-of-range measurements appear beyond the end bracket;
    // stale/missing telemetry never produces a measured-angle marker.
    void axis_scale_row(aura::cli::screen::Canvas &canvas, int x, int y, int width,
                        const std::string &name, bool active, int slot, const aura::cli::JointControls &controls,
                        const std::optional<aura::cli::ControlObservation> &observation, bool live,
                        const aura::cli::screen::Palette &p, bool compact = false) {
        using namespace aura::cli::screen;
        canvas.put(x + 2, y, (active ? "> " : "  ") + name, active ? p.purple : p.text, 8);
        if (slot < 0) {
            canvas.put(x + 11, y, "Locked at zero", p.caps.syntax_command + p.caps.strong, width - 13);
            return;
        }
        const auto &target = controls[slot];
        const bool measured = live && std::isfinite(observation->joint_angles[slot]);
        const auto actual = measured ? number(observation->joint_angles[slot], 3, true) : glyph("—", "--");
        canvas.put(x + 11, y, (compact ? "A " : "") + actual, measured ? p.green : p.dim, compact ? 12 : 8);
        canvas.put(x + (compact ? 24 : 20), y, (compact ? "T " : "") + number(target.value, 3, true),
                   target.clamped ? p.gold : p.purple, compact ? 12 : 8);
        if (!compact) canvas.put(
            x + 29, y, measured ? number(target.value - observation->joint_angles[slot], 3, true) : glyph("—", "--"),
            p.dim, 8);
        const int scaleX = x + 38, available = width - 40, track = available - 16;
        if (target.maximum <= target.minimum || track < 2) return;
        const auto position = [&](double value) {
            return static_cast<int>(std::lround(
                std::clamp((value - target.minimum) / (target.maximum - target.minimum), 0.0, 1.0) * (track - 1)));
        };
        const bool below = measured && observation->joint_angles[slot] < target.minimum;
        const bool above = measured && observation->joint_angles[slot] > target.maximum;
        const int actualPosition = measured && !below && !above ? position(observation->joint_angles[slot]) : -1;
        const int targetPosition = position(target.value);
        canvas.put(scaleX, y, number(target.minimum, 2), p.dim, 6);
        canvas.put(scaleX + 7, y, below ? "<" : "[", below ? p.gold : p.dim, 1);
        for (int cell = 0; cell < track; ++cell) {
            const bool actualHere = cell == actualPosition, targetHere = cell == targetPosition;
            canvas.put(scaleX + 8 + cell, y,
                       actualHere && targetHere
                           ? glyph("◈", "@")
                           : actualHere
                                 ? glyph("●", "o")
                                 : targetHere
                                       ? glyph("◆", "^")
                                       : glyph("─", "-"),
                       actualHere ? p.green : targetHere ? p.purple : p.dim, 1);
        }
        canvas.put(scaleX + 8 + track, y, above ? ">" : "]", above ? p.gold : p.dim, 1);
        canvas.put(scaleX + 10 + track, y, number(target.maximum, 2), p.dim, 6);
    }
}

namespace aura::cli {
    std::vector<std::string> movement_frame(
        const JointControls &controls, const std::optional<ControlObservation> &observation,
        std::size_t selected, const std::string &status, bool failed, double previewYaw, int width, int height) {
        return vector_control_frame(controls, observation, selected, 0, .05, false, false, "", status, failed,
                                    previewYaw, width, height, true);
    }
}

namespace aura::cli {
    std::vector<std::string> vector_control_frame(
        const JointControls &controls, const std::optional<ControlObservation> &observation,
        std::size_t selected, std::size_t axis, double step, bool editing, bool scalarEditing, const std::string &draft,
        const std::string &status, bool failed, double previewYaw, int width, int height, bool movementsMode) {
        using namespace screen;
        const Palette p;
        Canvas canvas(width, height);
        const int w = std::max(1, std::min(156, width - 4)), x = (width - w) / 2;
        const bool live = observation && observation->age_seconds <= control_freshness_seconds;
        canvas.heading(x, w, movementsMode ? "MOVEMENT VECTORS" : "JOINT VECTORS", p,
                       live ? "LIVE" : observation ? "STALE" : "WAITING", live ? p.green : p.gold);
        if (width < 76 || height < 17) {
            canvas.put(x, 4, "Enlarge terminal: 76 columns / 17 rows", p.dim, w);
            canvas.shortcuts(x, height - 2, "Q back / Esc home", p, w);
            return canvas.lines();
        }
        canvas.shortcuts(x, 2, "J Joints (" + integer(static_cast<int>(joint_vector_controls().size())) + ")", p, 21,
                         movementsMode ? p.dim : p.selected, movementsMode ? "" : p.caps.shell_style);
        canvas.shortcuts(x + 22, 2, "M Movements (" + integer(static_cast<int>(control_movements().size())) + ")", p,
                         -1, movementsMode ? p.selected : p.dim, movementsMode ? p.caps.shell_style : "");
        body_summary(canvas, x, w, observation, p, w >= 108 && height >= 26);
        const auto &joints = movementsMode ? movement_vector_controls() : joint_vector_controls();
        selected = std::min(selected, joints.size() - 1);
        axis = std::min<std::size_t>(axis, 2);
        if (joints[selected].slots[axis] < 0) axis = next_joint_axis(joints[selected], axis);
        const bool wide = w >= 108 && height >= 26;
        const int tableW = wide ? 72 : w, top = 4;
        const int count = std::min<int>(joints.size(), std::max(1, height - (height >= 24 ? 22 : 17)));
        const int first = std::clamp<int>(static_cast<int>(selected) - count + 1, 0, joints.size() - count);
        canvas.box(x, top, tableW, count + 4, movementsMode ? "MOVEMENT TARGETS / RADIANS" : "JOINT TARGETS / RADIANS",
                   p);
        canvas.put(x + 4, top + 1, movementsMode ? "Body part" : "Joint", p.dim, 20);
        const std::array<std::string, 3> names{"Bend", "Turn", "Tilt"};
        for (int component = 0; component < 3; ++component)
            canvas.put(x + 25 + component * 14, top + 1, names[component],
                       component == static_cast<int>(axis) ? p.purple : p.dim, 12);
        for (int row = 0; row < count; ++row) {
            const int index = first + row, y = top + 2 + row;
            const auto &joint = joints[index];
            const bool active = index == static_cast<int>(selected);
            if (active) canvas.put(x + 1, y, std::string(tableW - 2, ' '), p.selected, tableW - 2);
            canvas.put(x + 2, y, active ? glyph("▸", ">") : " ", p.green);
            canvas.put(x + 4, y, std::to_string(index + 1) + " " + joint.name, active ? p.selected : p.text, 20);
            for (int component = 0; component < 3; ++component) {
                const int slot = joint.slots[component];
                bool off = slot >= 0 && live && observation->has_motor_states && !observation->motor_enabled[slot];
                bool pairedClamped = false;
                if (movementsMode && slot >= 0 && control_movements()[index].joints.size() > 1) {
                    const auto pairedSlot = joint_vector_controls()[control_movements()[index].joints[1]].slots[
                        component];
                    off |= live && observation->has_motor_states && !observation->motor_enabled[pairedSlot];
                    pairedClamped = controls[pairedSlot].clamped;
                }
                const bool mixed =
                        movementsMode && movement_mixed_axis(controls, control_movements()[index], component);
                const auto text = slot < 0
                                      ? "locked"
                                      : mixed
                                            ? "Mixed"
                                            : number(controls[slot].value, 3, true) + (off ? " off" : "");
                const bool changed = slot >= 0 && controls[slot].value != 0.0;
                const bool warning = slot >= 0 && (off || controls[slot].clamped || pairedClamped || mixed);
                const auto background = active ? p.caps.shell_style : std::string{};
                const auto color = slot < 0
                                       ? p.caps.syntax_command + p.caps.strong
                                       : warning
                                             ? p.gold + p.caps.strong
                                             : changed
                                                   ? p.green + p.caps.strong
                                                   : active && component == static_cast<int>(axis)
                                                         ? p.purple + p.caps.strong
                                                         : p.dim;
                canvas.put(x + 25 + component * 14, y, text, background + color, 12);
            }
        }
        canvas.put(x + 2, top + count + 2, "Non-neutral", p.green + p.caps.strong, 11);
        canvas.put(x + 13, top + count + 2, " targets / ", p.dim, 11);
        canvas.put(x + 24, top + count + 2, "Motor / limit", p.gold, tableW - 26);
        canvas.put(x + 37, top + count + 2, " / ", p.dim, tableW - 39);
        canvas.put(x + 40, top + count + 2, "Locked axes", p.caps.syntax_command + p.caps.strong, tableW - 42);
        if (movementsMode) canvas.put(x + 51, top + count + 2, " / Mixed", p.gold, tableW - 53);
        const auto &joint = joints[selected];
        const auto value = joint_target_vector(controls, joint);
        const int slot = joint.slots[axis];
        const auto &component = controls[slot];
        const bool off = live && observation->has_motor_states && !observation->motor_enabled[slot];
        const auto vector = "(" + number(value[0], 3) + ", " + number(value[1], 3) + ", " + number(value[2], 3) + ")";
        if (height >= 24) {
            const int detailY = height - 13;
            canvas.box(x, detailY, tableW, 8, "SELECTED / " + joint.name, p);
            canvas.put(x + 2, detailY + 1,
                       movementsMode ? control_movements()[selected].description : "Target " + vector + " rad", p.bold,
                       tableW - 4);
            canvas.put(x + 2, detailY + 2, "Axis", p.dim, 8);
            canvas.put(x + 11, detailY + 2, "Actual", p.green, 8);
            canvas.put(x + 20, detailY + 2, "Target", p.purple, 8);
            canvas.put(x + 29, detailY + 2, "Error", p.dim, 8);
            canvas.put(x + 38, detailY + 2, "Limits / angle scale", p.dim, tableW - 40);
            for (int componentIndex = 0; componentIndex < 3; ++componentIndex) {
                axis_scale_row(canvas, x, detailY + 3 + componentIndex, tableW, names[componentIndex],
                               componentIndex == static_cast<int>(axis), joint.slots[componentIndex], controls,
                               observation, live, p);
            }
            const bool outside = live && (observation->joint_angles[slot] < component.minimum || observation->
                                          joint_angles[slot] > component.maximum);
            const double margin = .05 * (component.maximum - component.minimum);
            const bool near = live && (observation->joint_angles[slot] - component.minimum <= margin || component.
                                       maximum - observation->joint_angles[slot] <= margin);
            if (movementsMode) {
                const auto &group = control_movements()[selected];
                const auto feedback = movement_feedback(controls, group, observation);
                const auto note = group.joints.size() > 1
                                      ? "Mirrored pair / scales show left side"
                                      : "Single side / same axes as Joints";
                canvas.put(x + 2, detailY + 6,
                           feedback.disabledJoints
                               ? "Motor off in selected group / targets stored"
                               : std::string(note) + (feedback.meanError
                                                          ? " / error " + number(*feedback.meanError, 3) + " rad"
                                                          : " / no live data"),
                           feedback.disabledJoints ? p.gold : p.dim, tableW - 4);
            } else {
                canvas.put(x + 2, detailY + 6,
                           off
                               ? "Motor off / target stored"
                               : component.clamped
                                     ? "Target clamped to limit"
                                     : outside
                                           ? "Actual angle outside limits"
                                           : near
                                                 ? "Actual angle near a limit"
                                                 : live
                                                       ? "Error = target - actual"
                                                       : "Waiting for fresh joint measurements",
                           off || component.clamped || outside || near ? p.gold : p.dim, tableW - 4);
            }
        } else {
            for (int componentIndex = 0; componentIndex < 3; ++componentIndex)
                axis_scale_row(canvas, x, height - 8 + componentIndex, tableW, names[componentIndex],
                               componentIndex == static_cast<int>(axis), joint.slots[componentIndex], controls,
                               observation, live, p, true);
        }
        if (editing) canvas.put(x, height - 5,
                                (scalarEditing ? names[axis] + " target: " : "Target / " + names[axis] + ": ") + draft +
                                "_", p.purple, w);
        else canvas.shortcuts(x, height - 5, status, p, w, failed ? p.caps.error_color : p.text);
        if (editing) canvas.put(x, height - 4, joint_input_hint(joint, axis), p.dim, w);
        else control_hints(canvas, x, height - 4, w, {
                               {"Tab", "axis"}, {"S", "step " + number(step, 2) + " rad"}, {"R", "neutral"},
                               {"1-9", movementsMode ? "part" : "joint"}
                           }, p);
        if (wide) body_panel(canvas, x + tableW + 2, top, w - tableW - 2, height - top - 8, observation, previewYaw, p);
        control_footer(canvas, x, w, height, editing, movementsMode,
                       glyph("● actual   ◆ target   ◈ same cell", "o actual   ^ target   @ same cell") + (movementsMode
                           ? " / Mixed = sides differ / radians"
                           : " / limits in radians / < > outside range"), p);
        return canvas.lines();
    }
}
