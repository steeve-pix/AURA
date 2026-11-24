#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aura::cli {
    namespace screen { class Canvas; }
    struct SavedEntry {
        std::filesystem::path path;
        std::string kind,name,status;
        std::vector<std::string> details;
    };
    std::vector<SavedEntry> parse_saved_catalog(const std::string &catalog);
    void center_tabs(screen::Canvas &canvas,int row,int width,int active);
    std::vector<std::string> center_saved_frame(const std::vector<SavedEntry> &items,std::size_t selected,int tab,
        int width,int height,const std::string &message,bool inspecting,std::size_t offset,bool canRestore);

    struct SavedModel {
        std::filesystem::path path;
        std::uintmax_t bytes;
        std::filesystem::file_time_type modified;
        std::string contractState = "unknown", contractDetail;
        int inputs = 0, outputs = 0;
        bool hasBestCopy = false;
        int episodes = -1, updates = -1, bestEvaluation = -1;
        int bestBalanced = -1;
        double bestBalancedSeconds = 0;
        std::string kind = "actor", body = "Unverified body", catalogKey;
        std::vector<std::string> networkDetails;
    };

    std::string model_family(const SavedModel &model);

    bool model_is_best(const SavedModel &model);

    std::optional<std::size_t> best_model_index(const std::vector<SavedModel> &models, std::size_t selected);

    std::string model_size(std::uintmax_t bytes);

    std::string model_updated(std::filesystem::file_time_type modified, bool dateOnly = false);

    std::vector<SavedModel> saved_models(const std::filesystem::path &directory, std::string &error,
                                         bool includeCritics = false);

    std::vector<SavedModel> filter_models(const std::vector<SavedModel> &models, const std::string &query);

    void apply_model_catalog(std::vector<SavedModel> &models, const std::string &catalog);

    bool model_selectable(const SavedModel &model, const std::string &action);

    std::string model_action_error(const SavedModel &model, const std::string &action);

    // Read-only, responsive network sheet. Offset is clamped after resizing.
    std::vector<std::string> model_network_frame(const SavedModel &model, int width, int height, std::size_t &offset);

    std::vector<std::string> center_models_frame(const std::vector<SavedModel> &models, std::size_t selected,
                                                 const std::string &error, int width, int height,
                                                 const std::string &query = "",
                                                 bool searching = false, std::size_t total = 0, bool canRestore = false);

    std::vector<std::string> models_frame(const std::vector<SavedModel> &models, std::size_t selected,
                                          const std::string &error, int width, int height,
                                          const std::string &action = "watch");
}
