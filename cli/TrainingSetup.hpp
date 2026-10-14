#pragma once
#include <string>
#include <vector>

namespace aura::cli {
std::vector<std::string> training_setup_frame(int selected, bool hasModels, const std::string& stage,
    const std::string& value, int width, int height);
}
