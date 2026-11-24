#pragma once

#include "terminal/Terminal.hpp"

namespace aura::cli {
    std::vector<std::string> vector_control_frame(
        const JointControls &controls, const std::optional<ControlObservation> &observation,
        std::size_t selected, std::size_t axis, double step, bool editing, bool scalarEditing, const std::string &draft,
        const std::string &status, bool failed, double previewYaw, int width, int height, bool movementsMode = false);
}

namespace aura::cli {
    std::vector<std::string> movement_frame(
        const JointControls &controls, const std::optional<ControlObservation> &observation,
        std::size_t selected, const std::string &status, bool failed,
        double previewYaw, int width, int height);
}
