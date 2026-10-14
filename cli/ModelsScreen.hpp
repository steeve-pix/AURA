#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aura::cli {
struct SavedModel {
    std::filesystem::path path;
    std::uintmax_t bytes;
    std::filesystem::file_time_type modified;
    std::string contractState = "unknown", contractDetail;
    int inputs = 0, outputs = 0;
    bool hasBestCopy = false;
    int episodes = -1, updates = -1, bestEvaluation = -1;
};
std::string model_family(const SavedModel& model);
bool model_is_best(const SavedModel& model);
std::optional<std::size_t> best_model_index(const std::vector<SavedModel>& models, std::size_t selected);
std::string model_size(std::uintmax_t bytes);
std::string model_updated(std::filesystem::file_time_type modified, bool dateOnly=false);
std::vector<SavedModel> saved_models(const std::filesystem::path& directory, std::string& error);
std::vector<SavedModel> filter_models(const std::vector<SavedModel>& models, const std::string& query);
void apply_model_catalog(std::vector<SavedModel>& models, const std::string& catalog);
bool model_selectable(const SavedModel& model, const std::string& action);
std::vector<std::string> center_models_frame(const std::vector<SavedModel>& models, std::size_t selected,
    const std::string& error, int width, int height, const std::vector<std::string>& logs, const std::string& query="", bool searching=false, std::size_t total=0);
std::vector<std::string> models_frame(const std::vector<SavedModel>& models, std::size_t selected,
                                    const std::string& error, int width, int height, const std::string& action="watch");
}
