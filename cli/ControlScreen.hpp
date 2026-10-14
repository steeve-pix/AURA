#pragma once

#include "Terminal.hpp"

namespace aura::cli {
// Presentation only: drawing a frame never saves targets or changes the body.
std::vector<std::string> control_frame(
    const JointControls& controls, const std::optional<ControlObservation>& observation,
    std::size_t selected, double step, bool editing, const std::string& draft,
    const std::string& status, bool failed, double previewYaw, int width, int height);
}
