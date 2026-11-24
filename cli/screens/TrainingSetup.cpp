#include "screens/TrainingSetup.hpp"
#include "ui/ScreenCanvas.hpp"
#include <array>
#include <charconv>
#include <cctype>
#include <thread>

namespace aura::cli {
    int default_worker_count() { return int(std::min(4u, std::max(1u, std::thread::hardware_concurrency()))); }
    int maximum_worker_count() { return int(std::max(4u, std::min(64u, 2u*std::max(1u,std::thread::hardware_concurrency())))); }
    namespace {
        std::vector<std::string> setting_fields(const std::string &value, bool delimited) {
            std::vector<std::string> result;
            if (delimited) {
                std::size_t start = 0;
                while (true) {
                    const auto end = value.find('|', start);
                    result.push_back(value.substr(start, end == std::string::npos ? end : end - start));
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
            } else {
                std::istringstream input(value);
                for (std::string field; input >> field;) result.push_back(field);
            }
            return result;
        }

        bool parse_setting(const std::string &value, int &parsed) {
            if (value.empty()) return false;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            return result.ec == std::errc{} && result.ptr == value.data() + value.size();
        }
    }

    std::optional<std::array<int, 3>> training_settings(const std::string &value, bool newModel) {
        const bool delimited = value.find('|') != std::string::npos;
        const auto fields = setting_fields(value, delimited);
        const auto expected = 3u;
        if (!newModel && !delimited && fields.size()==2) {
            int episodes=0,updates=0;
            if(parse_setting(fields[0],episodes) && parse_setting(fields[1],updates) && episodes>=2 && episodes<=128 && updates>=1 && updates<=1000)
                return std::array{episodes,updates,0};
        }
        if (fields.size() != expected) return {};
        int episodes = 0, batches = 0, agents = 0;
        if (!parse_setting(fields[0], episodes) || !parse_setting(fields[1], batches) ||
            !parse_setting(fields[2], agents)) return {};
        if (episodes < 2 || episodes > 128 || batches < 1 || batches > 1000 ||
            (agents < 1 || agents > maximum_worker_count())) return {};
        return std::array{episodes, batches, agents};
    }
    bool reserved_training_name(std::string name) {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        return name.ends_with("_best");
    }

    std::vector<std::string> training_setup_frame(int selected, bool hasModels, const std::string &stage,
                                                  const std::string &value, int width, int height, bool canTrainSaved,
                                                  std::size_t nameCursor, const std::string &device,
                                                  int selectedField) {
        using namespace screen;
        const Palette p;
        Canvas c(width, height);
        const int w = std::max(1, std::min(100, width - 4)), x = (width - w) / 2;
        c.heading(x, w, "TRAINING SETUP", p);
        if (width < 60 || height < 18) {
            c.put(x, 4, "Enlarge terminal: 60 columns / 18 rows", p.dim, w);
            c.shortcuts(x, height - 2, "Q back / Esc home", p, w);
            return c.lines();
        }
        const int spacing = height >= 26 ? 3 : 2;
        c.box(x, 4, w, stage == "mode" ? 4 * spacing + 5 : stage == "critic_source" ? 8 : 12,
              stage == "critic_source"
                  ? "FROZEN ACTOR SOURCE"
                  : stage == "settings" || stage == "saved_settings"
                        ? "TRAINING RUN"
                  : stage == "name"
                        ? "NEW MODEL"
                        : stage == "confirm"
                              ? "RESTART MODEL"
                              : "CHOOSE HOW TO TRAIN", p);
        if (stage == "mode") {
            const std::array<std::string, 5> names{
                "[1] Resume model", "[2] New model", "[3] Restart model", "[4] Critic only", "[5] Fork actor"
            };
            const std::array<std::string, 5> hints{
                "Continue paired actor, critic, Adam states and episode count",
                "Fresh actor and critic / a unique filename / keep existing models",
                "Fresh actor and critic / replace a compatible model after confirmation",
                "Freeze an actor / train a fresh critic / validate predictions",
                "Copy saved actor / current reward / fresh critic and Adam / keep source"
            };
            for (int i = 0; i < 5; ++i) {
                const bool enabled = (hasModels && canTrainSaved) || i == 1 || i == 3 || (i == 4 && hasModels);
                if (selected == i)
                    for (int row = 0; row < 2; ++row)
                        c.put(x + 1, 6 + i * spacing + row, std::string(w - 2, ' '), p.selected, w - 2);
                c.put(x + 2, 6 + i * spacing, selected == i ? glyph("▸", ">") : " ", p.green);
                c.shortcuts(x + 4, 6 + i * spacing,
                            names[i] + (
                                !enabled ? (hasModels ? " / latest checkpoint required" : " / no saved models") : ""),
                            p, w - 6, !enabled ? p.dim : selected == i ? p.selected : p.dim,
                            selected == i ? p.caps.shell_style : "", enabled);
                c.put(x + 4, 7 + i * spacing, hints[i], selected == i ? p.caps.shell_style + p.dim : p.dim, w - 6);
            }
        } else if (stage == "critic_source") {
            const std::array<std::string, 2> names{"[1] Fresh actor / seed 42", "[2] Freeze a saved actor"};
            const std::array<std::string, 2> hints{
                "Evaluate a fresh critic on the current body / actor stays frozen",
                "Read a checkpoint snapshot / leave its weights and Adam state untouched"
            };
            for (int i = 0; i < 2; ++i) {
                if (i == selected) for (int row = 0; row < 2; ++row) c.put(
                    x + 1, 6 + i * 3 + row, std::string(w - 2, ' '), p.selected, w - 2);
                c.put(x + 2, 6 + i * 3, i == selected ? glyph("▸", ">") : " ", p.green);
                c.shortcuts(x + 4, 6 + i * 3, names[i] + (i == 1 && !hasModels ? " / no saved models" : ""), p, w - 6,
                            i == selected ? p.selected : p.dim, i == selected ? p.caps.shell_style : "");
                c.put(x + 4, 7 + i * 3, hints[i], i == selected ? p.caps.shell_style + p.dim : p.dim, w - 6);
            }
            c.put(x, 13, "32 training / 16 validation episodes / 20 critic updates", p.text, w);
            if (height >= 20) c.put(x, 14, "Adam 0.001 / seed 43 / return scale 100 / fresh critic each run", p.dim, w);
        } else if (stage == "settings" || stage == "saved_settings") {
            const bool fresh = stage == "settings";
            const std::array<int, 3> columns{x + 3, x + 36, x + 69};
            const std::array<std::string, 3> labels{"Episodes per update", "Actor-critic updates", "Workers"};
            const int fields = 3;
            auto values = setting_fields(value, value.find('|') != std::string::npos);
            values.resize(static_cast<std::size_t>(fields));
            const int activeField = std::clamp(selectedField, 0, fields - 1);
            const auto field = [&](int i, int labelX, int labelY, int valueX, int valueY, int valueWidth, int labelWidth) {
                c.put(labelX, labelY, labels[i], p.purple, labelWidth);
                std::string shown = values[i];
                if (i == activeField) {
                    const auto offset = std::min(nameCursor, shown.size());
                    shown.insert(offset, glyph("▏", "_"));
                }
                c.put(valueX, valueY, shown.empty() ? " " : shown, i == activeField ? p.selected : p.text, valueWidth);
            };
            if (w >= 88) {
                for (int i = 0; i < fields; ++i)
                    field(i, columns[i], 6, columns[i], 8, 28, i == 0 ? 30 : 28);
            } else {
                for (int i = 0; i < fields; ++i)
                    field(i, x + 3, 6 + i, x + w - 12, 6 + i, 10, w - 17);
            }
            c.put(x + 3, 10, "Episodes: 2–128   Updates: 1–1000   Workers: 1–" + std::to_string(maximum_worker_count()), p.dim, w - 6);
            c.put(x + 3, 11, "Independent worlds / editable workers / no cross-worker collisions", p.text, w - 6);
            c.put(x + 3, 12, "Compute device   [G] " + std::string(device == "gpu" ? "GPU / Apple Metal or CUDA" : "CPU"),
                  p.purple, w - 6);
            const auto settings = training_settings(value, true);
            if (settings && (*settings)[2] > 4) c.put(x+3,14,"Warning: more than four workers can slow collection / benchmark first",p.gold,w-6);
            c.put(x + 3, 13, "Evaluate and save after each update / P pauses, Q stops", p.dim, w - 6);
            if (!training_settings(value, fresh))
                c.put(x + 3, 13, fresh ? "Edit episodes, updates and workers in labeled order" :
                                       "Edit episodes, updates and workers in labeled fields", p.gold, w - 6);
        } else if (stage == "name") {
            c.put(x + 3, 6, "Model name", p.dim, w - 6);
            const auto cursor = std::min(nameCursor, value.size());
            const auto room = static_cast<std::size_t>(w - 6);
            auto first = cursor >= room ? cursor - room + 1 : 0;
            if (cursor < value.size() && cursor - first == room - 1) ++first;
            auto visible = value.substr(first, room - 1);
            visible.insert(cursor - first, glyph("▏", "_"));
            c.put(x + 3, 8, visible, p.selected, w - 6);
            c.put(x + 3, 11, "Letters, numbers, underscores and hyphens", p.dim, w - 6);
            c.put(x + 3, 12, "Existing names receive a unique numeric suffix.", p.dim, w - 6);
        } else {
            c.put(x + 3, 6, value, p.bold, w - 6);
            c.put(x + 3, 9, "Reset weights, Adam state and episode count?", p.gold, w - 6);
            c.put(x + 3, 11, "Starting replaces this checkpoint and its saved best.", p.dim, w - 6);
            c.put(x + 3, 12, "Other models are preserved.", p.dim, w - 6);
        }
        c.footer(x, w, stage == "confirm"
                           ? "Y restart   N cancel"
                           : stage == "settings" || stage == "saved_settings"
                                 ? "[Tab] next input   [G] CPU/GPU   Enter start   Ctrl+U clear field"
                           : stage == "name"
                                 ? "Type to append   Backspace delete   Enter create   Ctrl+U clear"
                                 : stage == "critic_source"
                                       ? glyph("↑↓ choose   Enter start", "Up/Down choose   Enter start")
                                       : glyph("↑↓ choose   Enter continue", "Up/Down choose   Enter continue"),
                 (stage == "name" || stage == "settings" || stage == "saved_settings") ? "Esc cancel edit" : "Q back / Esc home",
                 stage == "critic_source"
                     ? "Critic artifacts are saved separately / actor optimizer updates: 0"
                     : "Choose the model lifecycle before starting training", p);
        if (stage == "name" && reserved_training_name(value)) {
            c.shortcuts(x, height - 2, "Enter create", p, w, "", "", false);
            c.put(x, height - 1, "_best is reserved / choose a latest model name", p.dim, w);
        }
        return c.lines();
    }
}
