#pragma once
#include <string>
#include <vector>
#include <optional>
#include <array>

namespace aura::cli {
    int default_worker_count();
    bool reserved_training_name(std::string name);
    std::optional<std::array<int, 3>> training_settings(const std::string &value, bool newModel);

    std::vector<std::string> training_setup_frame(int selected, bool hasModels, const std::string &stage,
                                                  const std::string &value, int width, int height,
                                                  bool canTrainSaved = true,
                                                  std::size_t nameCursor = std::string::npos,
                                                  const std::string &device = "cpu",
                                                  int selectedField = 0);
}
