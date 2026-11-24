#pragma once
#include "terminal/Terminal.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace aura::cli {
    // Coordinates, not a Cartesian rotation vector: signed hinge bend, then the
    // parent-local Y/Z swing components. Do not normalize or rotate this tuple.
    using JointTargetVector = std::array<double, 3>;

    struct JointVectorControl {
        std::string name, key;
        std::array<int, 3> slots{-1, -1, -1}; // Unsupported coordinates stay locked at zero.
    };

    inline const std::vector<JointVectorControl> &joint_vector_controls() {
        static const auto groups = [] {
            std::vector<JointVectorControl> result;
            for (std::size_t i = 0; i < sim::actionSpecs.size(); ++i) {
                const auto &spec = sim::actionSpecs[i];
                auto group = std::find_if(result.begin(), result.end(), [&](const auto &item) {
                    return item.key == spec.jointName;
                });
                if (group == result.end()) {
                    std::string name = spec.jointName;
                    if (name.starts_with("left")) name = "Left " + name.substr(4);
                    else if (name.starts_with("right")) name = "Right " + name.substr(5);
                    else name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                    if (name.find(' ') != std::string::npos) name[name.find(' ') + 1] = static_cast<char>(std::tolower(
                                                                 static_cast<unsigned char>(name[name.find(' ') + 1])));
                    result.push_back({name, spec.jointName});
                    group = std::prev(result.end());
                }
                const int axis = spec.coordinate == sim::JointCoordinate::Twist
                                     ? 0
                                     : spec.coordinate == sim::JointCoordinate::SwingY
                                           ? 1
                                           : 2;
                group->slots[axis] = static_cast<int>(i);
            }
            return result;
        }();
        return groups;
    }

    inline JointTargetVector joint_target_vector(const JointControls &controls, const JointVectorControl &joint) {
        JointTargetVector value{};
        for (int axis = 0; axis < 3; ++axis) if (joint.slots[axis] >= 0)
            value[axis] = controls[joint.slots[axis]].value;
        return value;
    }

    inline JointControls with_joint_target(JointControls controls, const JointVectorControl &joint,
                                           const JointTargetVector &value) {
        // Validate the entire tuple before writing any component. The existing save
        // path applies each supported coordinate's limits atomically.
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(value[axis])) throw std::invalid_argument("Joint angles must be finite");
            if (joint.slots[axis] < 0 && value[axis] != 0) throw std::invalid_argument("Locked axes must be zero");
        }
        for (int axis = 0; axis < 3; ++axis) if (joint.slots[axis] >= 0)
            controls[joint.slots[axis]].value = value[axis];
        return controls;
    }

    inline std::vector<double> parse_joint_values(std::string text) {
        const auto first = text.find_first_not_of(" \t"), last = text.find_last_not_of(" \t");
        if (first == std::string::npos) throw std::invalid_argument("Enter at least one angle");
        text = text.substr(first, last - first + 1);
        if (text.front() == '(' && text.back() == ')') text = text.substr(1, text.size() - 2);
        std::vector<double> values;
        const auto append = [&](const std::string &token) {
            std::size_t end = 0;
            double value;
            try { value = std::stod(token, &end); } catch (const std::exception &) {
                throw std::invalid_argument("Enter finite angles only");
            }
            if (token.find_first_not_of(" \t", end) != std::string::npos || !std::isfinite(value))
                throw std::invalid_argument("Enter finite angles only");
            values.push_back(value);
            if (values.size() > 3) throw std::invalid_argument("Enter at most three angles");
        };
        std::istringstream fields(text);
        std::string token;
        if (text.find(',') != std::string::npos) {
            if (text.back() == ',') throw std::invalid_argument("Missing angle after comma");
            while (std::getline(fields, token, ',')) append(token);
        } else while (fields >> token) append(token);
        if (values.empty()) throw std::invalid_argument("Enter at least one angle");
        return values;
    }

    inline JointTargetVector parse_joint_vector(const std::string &text) {
        const auto values = parse_joint_values(text);
        if (values.size() != 3) throw std::invalid_argument("Enter exactly three angles");
        return {values[0], values[1], values[2]};
    }

    inline std::string joint_input_hint(const JointVectorControl &joint, std::size_t axis) {
        constexpr std::array<const char *, 3> names{"bend", "turn", "tilt"};
        std::string available;
        int count = 0;
        for (std::size_t i = 0; i < 3; ++i)
            if (joint.slots[i] >= 0) {
                if (count++) available += ", ";
                available += names[i];
            }
        if (count == 1) return "1 value: " + available + " / radians";
        return "1 value: " + std::string(names[axis]) + " / " + std::to_string(count) + " values: " + available +
               " / radians";
    }

    inline JointControls with_joint_input(JointControls controls, const JointVectorControl &joint,
                                          std::size_t axis, const std::string &text) {
        if (axis >= 3 || joint.slots[axis] < 0) throw std::invalid_argument("Select an available axis");
        const auto values = parse_joint_values(text);
        auto target = joint_target_vector(controls, joint);
        const auto count = std::count_if(joint.slots.begin(), joint.slots.end(), [](int slot) { return slot >= 0; });
        if (values.size() == 1) target[axis] = values[0];
        else if (values.size() == static_cast<std::size_t>(count)) {
            std::size_t index = 0;
            for (std::size_t i = 0; i < 3; ++i) if (joint.slots[i] >= 0) target[i] = values[index++];
        } else if (values.size() == 3) target = {values[0], values[1], values[2]};
            // Existing explicit tuples retain locked-axis validation.
        else throw std::invalid_argument(joint_input_hint(joint, axis));
        return with_joint_target(controls, joint, target);
    }

    inline std::size_t next_joint_axis(const JointVectorControl &joint, std::size_t axis, int direction = 1) {
        for (int i = 0; i < 3; ++i) {
            axis = (axis + (direction > 0 ? 1 : 2)) % 3;
            if (joint.slots[axis] >= 0) return axis;
        }
        return axis;
    }
}
