#include "screens/ModelsScreen.hpp"
#include "ui/ScreenCanvas.hpp"
#include <chrono>
#include <charconv>
#include <array>
#include <ctime>
#include <cctype>
#include <regex>
#include <sstream>

namespace aura::cli {
    namespace {
        std::string trimmed(const std::string &value) {
            const auto first = value.find_first_not_of(' '), last = value.find_last_not_of(' ');
            return first == std::string::npos ? "" : value.substr(first, last - first + 1);
        }

        std::vector<std::string> network_wrap(const std::string &text, int width) {
            std::vector<std::string> rows;
            std::istringstream words(text);
            std::string word, line;
            while (words >> word) {
                if (!line.empty() && screen::textCells(line + " " + word) > width) {
                    rows.push_back(line);
                    line.clear();
                }
                if (!line.empty()) line += ' ';
                line += word;
            }
            if (!line.empty()) rows.push_back(line);
            return rows;
        }
    }

    std::vector<std::string> model_network_frame(const SavedModel &model, int width, int height, std::size_t &offset) {
        using namespace screen;
        const Palette p;
        width = std::max(1, width);
        height = std::max(1, height);
        const int w = std::max(1, std::min(156, width - 4)), x = std::max(0, (width - w) / 2);
        std::vector<std::string> network, observations, outputs;
        std::string section, observationTitle = "OBSERVATIONS", outputTitle = model.kind == "actor"
                                                                                  ? "MOVEMENTS"
                                                                                  : "PREDICTION";
        for (const auto &line: model.networkDetails) {
            if (line == "ROLE" || line == "NETWORK") {
                section = line;
                continue;
            }
            if (line.starts_with("OBSERVATIONS /")) {
                section = "OBSERVATIONS";
                observationTitle = line;
                continue;
            }
            if (line.starts_with("MOVEMENTS /") || line.starts_with("PREDICTION /")) {
                section = "OUTPUTS";
                outputTitle = line;
                continue;
            }
            if (line.empty()) continue;
            if (section == "NETWORK") network.push_back(line);
            if (section == "OBSERVATIONS") observations.push_back(line);
            if (section == "OUTPUTS") outputs.push_back(line);
        }
        // The catalog carries the saved architecture, including archived models.
        // Activations follow their linear layer; never substitute today's dimensions.
        std::vector<std::string> sizes, activations;
        if (!network.empty() && network[0].find(" -> ") != std::string::npos) {
            std::size_t at = 0;
            while (at < network[0].size()) {
                const auto end = network[0].find(" -> ", at);
                const auto token = network[0].substr(at, end == std::string::npos ? end : end - at);
                if (!token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char c) {
                    return std::isdigit(c);
                })) {
                    sizes.push_back(token);
                    activations.push_back("");
                } else if (!activations.empty()) activations.back() = token;
                if (end == std::string::npos) break;
                at = end + 4;
            }
            if (network.size() > 1 && network[1] == "Tanh hidden layers / linear value output")
                for (std::size_t i = 1; i < sizes.size(); ++i) activations[i] = i + 1 == sizes.size()
                                                                   ? "Linear"
                                                                   : "Tanh";
        }
        const bool columns = w >= 112;
        const int left = columns ? (w - 4) * 3 / 5 : w, right = columns ? w - left - 4 : w;
        // Allocate for wrapped metadata, then crop to the measured content height.
        int capacity = 30 + static_cast<int>(sizes.size()) * 6;
        for (const auto &row: model.networkDetails) capacity += static_cast<int>(row.size()) / std::max(
                                                        1, std::min(left, right) - 14) + 3;
        Canvas body(w, capacity);
        int y = 0;
        body.put(0, y, "NETWORK", p.purple + p.caps.strong, w);
        y += 2;
        if (!sizes.empty()) {
            const int across = std::min(static_cast<int>(sizes.size()), w >= 73 ? 4 : w >= 37 ? 2 : 1);
            const int card = (w - (across - 1) * 3) / across;
            for (std::size_t i = 0; i < sizes.size(); ++i) {
                const int col = static_cast<int>(i) % across, row = static_cast<int>(i) / across;
                const int cx = col * (card + 3), cy = y + row * 6;
                const auto label = i == 0
                                       ? "OBSERVE"
                                       : i + 1 == sizes.size()
                                             ? (model.kind == "actor" ? "ACT" : "PREDICT")
                                             : "HIDDEN " + std::to_string(i);
                body.box(cx, cy, card, 5, label, p);
                body.put(cx + 2, cy + 2,
                         sizes[i] + (i == 0
                                         ? " inputs"
                                         : i + 1 == sizes.size()
                                               ? (sizes[i] == "1" ? " output" : " outputs")
                                               : " units"),
                         (i + 1 == sizes.size() ? p.green : p.purple) + p.caps.strong, card - 4);
                body.put(cx + 2, cy + 3,
                         i == 0 ? "State vector" : activations[i].empty() ? "Unspecified" : activations[i], p.dim,
                         card - 4);
                if (col + 1 < across && i + 1 < sizes.size()) body.rule(cx + card, cy + 2, 3, p.dim);
                else if (i + 1 < sizes.size() && across == 1) body.
                        put(cx + card / 2, cy + 5, glyph("│", "|"), p.dim, 1);
            }
            y += static_cast<int>((sizes.size() + across - 1) / across) * 6;
        } else {
            for (const auto &row: network_wrap(network.empty()
                                                   ? "Architecture unavailable in this checkpoint"
                                                   : network.front(), w))
                body.put(0, y++, row, p.gold, w);
            ++y;
        }
        const int sectionsY = y;
        auto title = [&](int cx, int cy, int cw, const std::string &text) {
            body.put(cx, cy, text, p.purple + p.caps.strong, cw);
            body.rule(cx, cy + 1, cw, p.dim);
        };
        title(0, y, left, observationTitle);
        y += 3;
        body.put(0, y, "INDEX", p.dim, 10);
        body.put(11, y, "PHYSICAL SIGNAL", p.dim, left - 18);
        if (left >= 40) body.put(left - 5, y, "SIZE", p.dim, 5);
        y += 2;
        const std::regex observationPattern(R"(^\s*([0-9]+)-([0-9]+)\s+(.+) \(([0-9]+)\)$)");
        for (const auto &text: observations) {
            std::smatch match;
            if (std::regex_match(text, match, observationPattern)) {
                body.put(0, y, match[1].str() + glyph("–", "-") + match[2].str(), p.dim, 10);
                const auto rows = network_wrap(match[3], std::max(1, left - 18));
                for (std::size_t i = 0; i < rows.size(); ++i) body.put(11, y + static_cast<int>(i), rows[i], p.text,
                                                                       std::max(1, left - 18));
                if (left >= 40) body.put(left - 5, y, match[4], p.green, 5);
                y += std::max(1, static_cast<int>(rows.size()));
            } else {
                ++y;
                for (const auto &row: network_wrap(text, left)) body.put(0, y++, row, p.dim, left);
            }
        }
        if (observations.empty()) body.put(0, y++, "Observation metadata unavailable", p.gold, left);
        const int outputX = columns ? left + 4 : 0, outputW = columns ? right : w;
        int oy = columns ? sectionsY : y + 2;
        title(outputX, oy, outputW, outputTitle);
        oy += 3;
        const std::regex movementPattern(R"(^\s*([0-9]+)\s+(.+)$)");
        const auto metric = [&](const std::string &text, const std::string &prefix)-> std::optional<double> {
            if (!text.starts_with(prefix)) return std::nullopt;
            const auto raw = trimmed(text.substr(prefix.size()));
            try {
                std::size_t used = 0;
                const double result = std::stod(raw, &used);
                if (used == raw.size() && std::isfinite(result) && result >= 0) return result;
            } catch (...) {
            }
            return std::nullopt;
        };
        std::optional<double> validation, baseline;
        // Fill the output panel horizontally before making it taller. Keep output
        // numbers in column order and wrap long archived labels without dropping them.
        if (model.kind == "actor") {
            std::vector<std::pair<std::string, std::string> > movements;
            int widest = 0;
            for (const auto &text: outputs) {
                std::smatch match;
                if (std::regex_match(text, match, movementPattern)) {
                    movements.emplace_back(match[1], match[2]);
                    widest = std::max(widest, textCells(match[2]) + 5);
                }
            }
            const int gap = 3, minCell = std::clamp(widest, 24, 32);
            const int across = std::max(1, std::min(4, (outputW + gap) / (minCell + gap)));
            const int cell = std::max(1, (outputW - (across - 1) * gap) / across);
            const int perColumn = (static_cast<int>(movements.size()) + across - 1) / across;
            int bottom = oy;
            for (int col = 0; col < across; ++col) {
                int row = oy;
                for (int index = col * perColumn; index < std::min(static_cast<int>(movements.size()),
                                                                   (col + 1) * perColumn); ++index) {
                    const int cx = outputX + col * (cell + gap);
                    body.put(cx, row, movements[index].first, p.purple, 4);
                    for (const auto &line: network_wrap(movements[index].second, std::max(1, cell - 5)))
                        body.put(cx + 5, row++, line, p.text, std::max(1, cell - 5));
                }
                bottom = std::max(bottom, row);
            }
            oy = bottom;
        }
        for (const auto &text: outputs) {
            std::smatch match;
            if (model.kind == "actor" && std::regex_match(text, match, movementPattern)) {
                continue;
            } else if (text.starts_with("Saved validation MSE:") || text.starts_with("Constant baseline MSE:")) {
                const bool isValidation = text.starts_with("Saved validation MSE:");
                const auto value = metric(text, isValidation ? "Saved validation MSE:" : "Constant baseline MSE:");
                (isValidation ? validation : baseline) = value;
                ++oy;
                body.put(outputX, oy++, isValidation ? "HELD-OUT ERROR / MSE" : "CONSTANT BASELINE / MSE", p.dim,
                         outputW);
                body.put(outputX, oy++, value ? number(*value, 5) : "Unavailable",
                         isValidation ? p.green + p.caps.strong : p.gold, outputW);
            } else {
                const bool note = text.starts_with("Standalone diagnostic critic") || text.starts_with("These measurements") ||
                                  text.starts_with("Outputs set") || text.starts_with("The separate");
                if (note) ++oy;
                for (const auto &row: network_wrap(text, outputW)) body.put(
                    outputX, oy++, row, note ? p.dim : p.green, outputW);
            }
        }
        if (validation && baseline && *baseline > 0) {
            ++oy;
            const double difference = 100 * (*validation / *baseline - 1);
            body.put(outputX, oy++,
                     number(std::abs(difference), 1) + "% " + (difference <= 0 ? "lower" : "higher") + " error",
                     (difference <= 0 ? p.green : p.gold) + p.caps.strong, outputW);
            body.put(outputX, oy++, "Versus the saved constant predictor", p.dim, outputW);
        }
        if (outputs.empty()) body.put(outputX, oy++, "Output metadata unavailable", p.gold, outputW);
        const int contentHeight = std::max(y, oy), available = std::max(1, height - 10);
        const std::size_t limit = contentHeight > available ? contentHeight - available : 0;
        offset = std::min(offset, limit);
        Canvas frame(width, height);
        frame.put(x, 0, "AURA / MODEL NETWORK", p.bold, w);
        frame.put(x, 3, model.catalogKey, p.dim, w);
        const auto role = model.kind == "actor"
                              ? "ACTOR / motor targets"
                              : model.kind == "critic"
                                    ? "CRITIC / return prediction"
                                    : "RESIDUAL CRITIC / return prediction";
        const auto status = model.body;
        const int statusWidth = textCells(status);
        frame.put(x, 2, ellipsize(model.path.filename().string(), w >= 60 ? w - statusWidth - 3 : w),
                  p.purple + p.caps.strong, w);
        if (w >= 60) frame.put(x + w - statusWidth, 2, status, model.body == "Current body" ? p.green : p.gold,
                               statusWidth);
        else frame.put(x, 4, status, model.body == "Current body" ? p.green : p.gold, w);
        frame.put(x, 5, role, p.green, w);
        frame.copy(body, x, 7, static_cast<int>(offset), available);
        frame.rule(x, height - 3, w, p.dim);
        const std::string back = "[Q] Back   [Esc] Home";
        if (w >= 64) {
            const int backWidth = shortcut_width(back);
            frame.shortcuts(x, height - 2, "Up/Down Scroll   PgUp/PgDn Page", p, w - backWidth - 2);
            frame.shortcuts(x + w - backWidth, height - 2, back, p, backWidth);
        } else frame.shortcuts(x, height - 2, (w >= 42 ? "Up/Down Scroll   " : "") + back, p, w);
        frame.put(x, height - 1,
                  limit
                      ? "Rows " + std::to_string(offset + 1) + glyph("–", "-") +
                        std::to_string(std::min(contentHeight, static_cast<int>(offset) + available)) + " / " +
                        std::to_string(contentHeight) + "   |   Saved interface / read only"
                      : "Saved model interface / read only", p.dim, w);
        return frame.lines();
    }

    bool model_is_best(const SavedModel &model) {
        auto stem = model.path.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return std::tolower(c); });
        return stem.ends_with("_best");
    }

    std::string model_family(const SavedModel &model) {
        auto name = model.path.stem().string();
        if (model_is_best(model)) name.resize(name.size() - 5);
        return model.kind == "actor" ? name : model.path.parent_path().string() + "/" + name;
    }

    std::optional<std::size_t> best_model_index(const std::vector<SavedModel> &models, std::size_t selected) {
        if (selected >= models.size()) return std::nullopt;
        for (std::size_t i = 0; i < models.size(); ++i)
            if (model_is_best(models[i]) && model_family(models[i]) == model_family(models[selected])) return i;
        return std::nullopt;
    }

    std::string model_size(std::uintmax_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        double value = static_cast<double>(bytes);
        const char *units[]{"B", "KiB", "MiB", "GiB", "TiB"};
        int unit = 0;
        while (value >= 1024 && unit < 4) {
            value /= 1024;
            ++unit;
        }
        return screen::number(value, 1) + " " + units[unit];
    }

    std::string model_updated(std::filesystem::file_time_type modified, bool dateOnly) {
        const auto systemTime = std::chrono::file_clock::to_sys(modified);
        const auto time = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(systemTime));
        std::tm local{};
        if (!localtime_r(&time, &local)) return "Unavailable";
        std::ostringstream text;
        text << std::put_time(&local, dateOnly ? "%Y-%m-%d" : "%Y-%m-%d %H:%M");
        return text.str();
    }

    std::vector<SavedModel> saved_models(const std::filesystem::path &directory, std::string &error,
                                         bool includeCritics) {
        std::vector<SavedModel> models;
        error.clear();
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec) && !ec) return models;
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() == ".pt" && it->is_regular_file(ec)) {
                const auto bytes = it->file_size(ec);
                if (ec) break;
                const auto modified = it->last_write_time(ec);
                if (!ec) models.push_back({it->path(), bytes, modified});
            }
        }
        if (includeCritics && std::filesystem::exists(directory / "critics", ec)) {
            for (std::filesystem::recursive_directory_iterator it(directory / "critics", ec), end; !ec && it != end; it.
                 increment(ec)) {
                if (it->path().extension() != ".pt" || !it->is_regular_file(ec)) continue;
                const auto bytes = it->file_size(ec);
                if (ec) break;
                const auto modified = it->last_write_time(ec);
                if (ec) break;
                SavedModel model{it->path(), bytes, modified};
                model.kind = "pending";
                models.push_back(std::move(model));
            }
        }
        if (std::filesystem::exists(directory / "experiments",ec)) {
            for (std::filesystem::recursive_directory_iterator it(directory/"experiments",ec),end;!ec && it!=end;it.increment(ec)) {
                if(it->path().extension()!=".pt" || it->path().filename()=="source_snapshot.pt" || !it->is_regular_file(ec)) continue;
                const auto bytes=it->file_size(ec); if(ec) break;
                const auto modified=it->last_write_time(ec); if(ec) break;
                models.push_back({it->path(),bytes,modified});
            }
        }
        for (auto &model: models) model.catalogKey = model.path.lexically_relative(directory).generic_string();
        if (ec) error = "Cannot read models folder: " + ec.message();
        std::sort(models.begin(), models.end(), [](const auto &a, const auto &b) {
            if ((a.kind == "actor") != (b.kind == "actor")) return a.kind == "actor";
            const auto familyA = model_family(a), familyB = model_family(b);
            if (familyA != familyB) return familyA < familyB;
            if (model_is_best(a) != model_is_best(b)) return !model_is_best(a);
            return a.path.filename() < b.path.filename();
        });
        for (std::size_t i = 0; i < models.size(); ++i) models[i].hasBestCopy = best_model_index(models, i).has_value();
        return models;
    }

    std::vector<SavedModel> filter_models(const std::vector<SavedModel> &models, const std::string &query) {
        const auto lower = [](std::string text) {
            for (auto &ch: text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return text;
        };
        std::istringstream words(lower(query));
        std::vector<std::string> terms;
        for (std::string term; words >> term;) terms.push_back(term);
        std::vector<SavedModel> matches;
        for (const auto &model: models) {
            const auto name = lower(
                (model.catalogKey.empty() ? model.path.filename().string() : model.catalogKey) + " " + model.kind + " "
                + model.body);
            if (std::all_of(terms.begin(), terms.end(), [&](const auto &term) {
                return name.find(term) != std::string::npos;
            }))
                matches.push_back(model);
        }
        return matches;
    }

    void apply_model_catalog(std::vector<SavedModel> &models, const std::string &catalog) {
        const auto filenameHex = [](const std::string &name) {
            constexpr char digits[] = "0123456789abcdef";
            std::string result;
            for (unsigned char c: name) {
                result += digits[c >> 4];
                result += digits[c & 15];
            }
            return result;
        };
        const auto decode = [](const std::string &hex) {
            std::string result;
            if (hex.size() % 2) return result;
            for (std::size_t i = 0; i < hex.size(); i += 2) {
                const auto digit = [](char c)-> int {
                    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                };
                const int a = digit(hex[i]), b = digit(hex[i + 1]);
                if (a < 0 || b < 0) return std::string{};
                const unsigned char ch = a * 16 + b;
                result += ch < 32 || ch == 127 ? ' ' : static_cast<char>(ch);
            }
            return result;
        };
        const auto matches = [&](const SavedModel &model, const std::string &name) {
            return filenameHex(model.catalogKey.empty() ? model.path.filename().string() : model.catalogKey) == name;
        };
        for (auto &model: models) {
            model.networkDetails.clear();
            model.bestBalanced = -1;
            model.bestBalancedSeconds = 0;
        }
        std::istringstream lines(catalog);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.starts_with("AURA_MODEL_KIND\t") || line.starts_with("AURA_MODEL_DETAIL\t")) {
                const bool kind = line.starts_with("AURA_MODEL_KIND\t");
                std::istringstream fields(line.substr(kind ? 16 : 18));
                std::string name, value, body;
                fields >> name >> value >> body;
                for (auto &model: models)
                    if (matches(model, name)) {
                        if (kind) {
                            model.kind = decode(value);
                            model.body = decode(body);
                        } else model.networkDetails.push_back(decode(value));
                    }
                continue;
            }
            if (line.starts_with("AURA_MODEL_BALANCE\t")) {
                std::istringstream info(line.substr(19));
                std::string name;
                int balanced;
                double seconds;
                if (info >> name >> balanced >> seconds && balanced >= 0 && std::isfinite(seconds) && seconds >= 0)
                    for (auto &model: models)
                        if (matches(model, name)) {
                            model.bestBalanced = balanced;
                            model.bestBalancedSeconds = seconds;
                        }
                continue;
            }
            if (line.starts_with("AURA_MODEL_INFO\t")) {
                std::istringstream info(line.substr(16));
                std::string name;
                int episodes = -1, updates = -1, best = -1;
                if (info >> name >> episodes >> updates >> best && episodes >= -1 && updates >= -1 && best >= -1)
                    for (auto &model: models)
                        if (matches(model, name)) {
                            model.episodes = episodes;
                            model.updates = updates;
                            model.bestEvaluation = best;
                        }
                continue;
            }
            if (!line.starts_with("AURA_MODEL\t")) continue;
            std::istringstream fields(line.substr(11));
            std::string name, status, detail;
            int inputs = 0, outputs = 0;
            if (!(fields >> name >> status >> inputs >> outputs) || inputs < 0 || outputs < 0 ||
                (status != "compatible" && status != "historical" && status != "experimental" && status != "incompatible" && status != "unreadable"))
                continue;
            std::getline(fields >> std::ws, detail);
            for (auto &model: models)
                if (matches(model, name)) {
                    model.contractState = status;
                    model.contractDetail = detail;
                    model.inputs = inputs;
                    model.outputs = outputs;
                }
        }
        std::erase_if(models, [](const auto &model) { return model.kind == "pending"; });
        for (std::size_t i = 0; i < models.size(); ++i)
            if (models[i].kind == "actor" && !model_is_best(models[i]))
                if (const auto best = best_model_index(models, i); best && models[*best].bestBalanced >= 0) {
                    models[i].bestBalanced = models[*best].bestBalanced;
                    models[i].bestBalancedSeconds = models[*best].bestBalancedSeconds;
                }
    }

    static std::string saved_best_summary(const SavedModel &model) {
        using namespace screen;
        if (model.bestBalanced >= 0)
            return "Best balance: " + integer(model.bestBalanced) + " steps / " + number(model.bestBalancedSeconds, 2) + " s";
        if (model.bestEvaluation >= 0)
            return "Saved best survival: " + integer(model.bestEvaluation) + " steps / diagnostic";
        return "No best balance score stored";
    }

    bool model_selectable(const SavedModel &model, const std::string &action) {
        if ((action == "resume" || action == "restart") && model_is_best(model)) return false;
        if (action == "report") return model.kind == "actor";
        if (model.kind == "actor" && (model.contractState == "historical" || model.contractState == "experimental"))
            return action == "fork" || action == "watch" || action == "evaluate" ||
                   action == "inspect" || action == "network" || action == "compare" || action == "diagnose";
        return model.kind == "actor" && (model.contractState == "compatible" || model.
                                         contractState == "unknown");
    }

    std::string model_action_error(const SavedModel &model, const std::string &action) {
        if ((action == "resume" || action == "restart") && model_is_best(model))
            return "Best checkpoint is read-only / train the latest model instead";
        return model.contractDetail;
    }

    std::vector<std::string> center_models_frame(const std::vector<SavedModel> &models, std::size_t selected,
                                                 const std::string &error, int width, int height,
                                                 const std::string &query,
                                                 bool searching, std::size_t total, bool canRestore) {
        using namespace screen;
        const Palette p;
        Canvas c(width, height);
        const int x = 2, w = std::max(1, width - 4);
        c.put(x, 0, "AURA / COMMAND CENTER", p.text, w);
        center_tabs(c,2,width,0);
        if (width < 60 || height < 20) {
            c.put(x, 4, "Enlarge terminal: 60 columns / 20 rows", p.dim, w);
            c.shortcuts(x, height - 1, "Q back / Esc home", p, w);
            return c.lines();
        }
        const auto count = query.empty()
                               ? std::to_string(models.size()) + " checkpoints"
                               : std::to_string(models.size()) + " / " + std::to_string(total ? total : models.size()) +
                                 " checkpoints";
        const int countWidth = textCells(count);
        if (query.empty() && !searching) c.shortcuts(x, 4, "Search  [/] Filter models", p,
                                                     std::max(1, w - countWidth - 3));
        else c.put(x, 4, "Search  " + query + (searching ? glyph("▏", "_") : ""), searching ? p.text : p.dim,
                   std::max(1, w - countWidth - 3));
        c.put(x + w - countWidth, 4, count, p.dim, countWidth);
        const bool split = width >= 100 && height >= 24;
        const int libraryWidth = split ? w * 3 / 5 : w;
        const int detailX = x + libraryWidth + 3, detailWidth = width - detailX - 2;
        const int rows = std::max(1, height - 15);
        c.put(x, 6, "TYPE / MODEL", p.dim, libraryWidth - 26);
        c.put(x + libraryWidth - 25, 6, "STATE", p.dim, 14);
        c.put(x + libraryWidth - 10, 6, "SIZE", p.dim, 10);
        if (models.empty()) c.put(
            x, 8, query.empty()
                      ? "No saved models. Use /train to create one."
                      : "No matching models. Press / to edit your search.", p.dim, libraryWidth);
        else {
            selected = std::min(selected, models.size() - 1);
            const auto first = selected >= static_cast<std::size_t>(rows) ? selected - rows + 1 : 0;
            for (std::size_t i = first; i < models.size() && i < first + rows; ++i) {
                const int y = 8 + static_cast<int>(i - first);
                const auto style = i == selected ? p.selected : p.text;
                if (i == selected) c.put(x, y, std::string(libraryWidth, ' '), p.selected, libraryWidth);
                const bool best = model_is_best(models[i]);
                c.put(x, y, i == selected ? glyph("▸ ", "> ") : "  ", style, 2);
                c.put(x + 2, y, models[i].kind != "actor" ? "Critic " : "Actor  ",
                      i == selected ? p.selected : models[i].kind != "actor" ? p.purple : p.dim, 7);
                const auto name = models[i].kind != "actor" && models[i].path.filename() == "critic.pt"
                                      ? models[i].path.parent_path().filename().string()
                                      : models[i].path.filename().string();
                c.put(x + 9, y, ellipsize((best ? "Best / " : std::string{}) + name, libraryWidth - 35),
                      i == selected ? p.selected : best ? p.green : p.text, libraryWidth - 35);
                const bool incompatible = models[i].contractState == "incompatible";
                const auto state = models[i].body == "Older body" ? std::string("older body") : models[i].contractState;
                c.put(x + libraryWidth - 25, y, (incompatible ? glyph("⚠ ", "! ") : std::string{}) + state,
                      incompatible ? style + p.gold + p.caps.strong : style, 14);
                c.put(x + libraryWidth - 10, y, model_size(models[i].bytes), style, 10);
            }
            const auto &model = models[selected];
            if (split) {
                c.put(detailX, 6, "MODEL DETAILS", p.dim, detailWidth);
                c.put(detailX, 8, ellipsize(model.path.filename().string(), detailWidth), p.text, detailWidth);
                c.put(detailX, 9, model.kind != "actor"
                                      ? "Standalone critic / inspect only"
                                      : model.contractState == "incompatible" || model.contractState == "unreadable"
                                            ? "Incompatible checkpoint / inspect only"
                                      : model.contractState == "experimental"
                                            ? "Experimental / evaluate or Fork actor"
                                      : model.contractState == "historical"
                                            ? "Historical collector/reward / Fork actor"
                                      : model_is_best(model)
                                            ? "Best checkpoint / read-only / watch or evaluate"
                                            : (model.hasBestCopy || best_model_index(models, selected))
                                                  ? "Latest checkpoint / resume training"
                                                  : "Saved checkpoint / no best copy",
                      model_is_best(model) ? p.green : p.dim, detailWidth);
                c.put(detailX, 10, (model.kind == "actor" ? "Actor / motor control" : "Critic / return prediction"),
                      p.purple, detailWidth);
                c.put(detailX, 11, (model.body != "Current body" ? glyph("⚠ ", "! ") : std::string{}) + model.body,
                      model.body == "Current body" && model.contractState == "compatible"
                          ? p.green
                          : p.gold + p.caps.strong, detailWidth);
                c.put(detailX, 12,
                      model.kind != "actor"
                          ? (model.updates >= 0
                                 ? integer(model.updates) + " critic updates / actor frozen"
                                 : "Saved critic / actor frozen")
                          : model.episodes >= 0
                                ? integer(model.episodes) + " saved episodes / " + integer(model.updates) + " updates"
                                : "Training history unavailable", p.dim, detailWidth);
                if (model.kind != "actor") c.shortcuts(detailX, 13,
                                                       "Enter inspects network and saved prediction results", p,
                                                       detailWidth);
                else c.put(detailX, 13,
                           saved_best_summary(model), p.green, detailWidth);
                c.put(detailX, 15, "Updated " + model_updated(model.modified), p.dim, detailWidth);
            }
        }
        std::vector<std::pair<std::string, bool> > hints;
        const auto hint = [&](const std::string &key, const std::string &action, bool enabled = true) {
            const auto label = "[" + key + "] " + action;
            hints.emplace_back(label,enabled);
        };
        if (searching) {
            hints.emplace_back("Type to filter",true);
            hint("Enter", "Finish");
            hint("Ctrl+U", "Clear");
            hint("Esc", "Cancel search");
        } else {
            const SavedModel *selectedModel = nullptr;
            if (!models.empty()) selectedModel = &models[std::min(selected, models.size() - 1)];
            hint("/", "Search");
            hint(glyph("↑↓", "Up/Down"), "Choose", selectedModel != nullptr);
            const bool critic = selectedModel && selectedModel->kind != "actor";
            hint("Enter", "Inspect", selectedModel != nullptr);
            if (!critic) {
                hint("W", "Watch", selectedModel && model_selectable(*selectedModel, "watch"));
                hint("E", w<100 ? "Eval" : "Evaluate", selectedModel && model_selectable(*selectedModel, "evaluate"));
                hint("T", "Resume", selectedModel && model_selectable(*selectedModel, "resume"));
                hint("B", "Best",
                     selectedModel && best_model_index(models, std::min(selected, models.size() - 1)).has_value());
            }
            hint("U", "Restore", canRestore);
            hint("Q", "Back");
            hint("Esc", "Home");
            hint("D", "Delete", selectedModel != nullptr);
        }
        std::vector<Canvas::FooterAction> actions;
        for (const auto &[label, enabled] : hints)
            actions.push_back({label, enabled, label.starts_with("[D]")});
        c.actionFooter(x, w, actions, error, p);
        return c.lines();
    }

    std::vector<std::string> models_frame(const std::vector<SavedModel> &models, std::size_t selected,
                                          const std::string &error, int width, int height, const std::string &action) {
        if (action == "center") return center_models_frame(models, selected, error, width, height);
        using namespace screen;
        const Palette p;
        Canvas c(width, height);
        const int w = std::max(1, std::min(120, width - 4)), x = (width - w) / 2;
        c.heading(x, w, action == "compare" ? "COMPARE POLICIES" : action == "diagnose" ? "DIAGNOSE MODEL" : action == "report" ? "EXPORT TRAINING REPORT" : action == "critic"
                            ? "FROZEN ACTOR"
                            : action == "center"
                                  ? "COMMAND CENTER"
                                  : action == "watch"
                                        ? "SAVED MODELS"
                                        : action == "evaluate"
                                              ? "EVALUATE MODEL"
                                              : action == "resume"
                                                    ? "RESUME TRAINING"
                                                    : "RESTART TRAINING", p);
        if (width < 50 || height < 14) {
            c.put(x, 3, "Enlarge terminal: 50 columns / 14 rows", p.dim, w);
            c.shortcuts(x, height - 2, "Q back / Esc home", p, w);
            return c.lines();
        }
        c.put(x, 3, action == "center"
                        ? "1 Models   2 Activity  /  " + std::to_string(models.size()) + " saved"
                        : "" + std::to_string(models.size()) + " models  /  models/", p.dim, w);
        const int libraryHeight = std::min(height - 10, models.empty() ? 8 : static_cast<int>(models.size()) + 4);
        c.box(x, 5, w, libraryHeight, "CHECKPOINT LIBRARY", p);
        if (models.empty()) {
            c.put(x + 2, 7, "No saved models yet.", p.text, w - 4);
            if (height > 15) c.put(x + 2, 9, "Run /train to save the first checkpoint.", p.dim, w - 4);
        } else {
            selected = std::min(selected, models.size() - 1);
            const bool dateOnly = width < 76;
            const int dateWidth = dateOnly ? 10 : 16;
            const int dateX = x + w - dateWidth - 3, sizeX = dateX - 12;
            c.put(x + 4, 6, "Model", p.dim, sizeX - x - 6);
            c.put(sizeX, 6, "Size", p.dim, 10);
            c.put(dateX, 6, dateOnly ? "Updated" : "Updated (local)", p.dim, dateWidth);
            const auto visible = static_cast<std::size_t>(std::max(1, height - 14));
            const auto start = selected >= visible ? selected - visible + 1 : 0;
            for (std::size_t i = start; i < models.size() && i < start + visible; ++i) {
                const int y = 7 + static_cast<int>(i - start);
                const bool usable = model_selectable(models[i], action);
                const auto style = i == selected
                                       ? (usable ? p.selected : p.caps.shell_style + p.dim)
                                       : usable
                                             ? p.text
                                             : p.dim;
                if (i == selected) c.put(x + 1, y, std::string(w - 2, ' '), p.selected, w - 2);
                c.put(x + 2, y, i == selected ? glyph("▸", ">") : " ", p.green);
                c.put(x + 4, y, ellipsize(models[i].path.filename().string(), sizeX - x - 6), style, sizeX - x - 6);
                c.put(sizeX, y, model_size(models[i].bytes), style, 10);
                c.put(dateX, y, model_updated(models[i].modified, dateOnly), style, dateWidth);
            }
            const auto &model = models[selected];
            const std::string contract = model.contractState == "unknown"
                                             ? "Compatibility will be checked on open"
                                             : integer(model.inputs) + " inputs " + glyph("→", "->") + " " + integer(
                                                   model.outputs) + " actions  " + glyph("·", "/") + "  " +
                                               (model.contractState == "compatible"
                                                    ? "Compatible"
                                                    : model.contractState == "incompatible"
                                                          ? "Incompatible"
                                                          : "Unreadable");
            c.put(x, std::min(height - 5, 6 + libraryHeight), contract,
                  model_selectable(model, action) ? p.dim : p.gold, w);
            const auto position = integer(static_cast<int>(selected + 1)) + " / " + integer(
                                      static_cast<int>(models.size()));
            c.put(x + w - textCells(position), 3, position, p.dim, textCells(position));
        }
        const bool blocked = !models.empty() && !
                             model_selectable(models[std::min(selected, models.size() - 1)], action);
        const auto *current = models.empty() ? nullptr : &models[std::min(selected, models.size() - 1)];
        const std::string progress = current && current->episodes >= 0
                                         ? integer(current->episodes) + " saved episodes / " + (
                                               current->updates >= 0
                                                   ? integer(current->updates) + " updates"
                                                   : "updates unknown") + " / " + saved_best_summary(*current)
                                         : "Training history unavailable";
        const std::string note = !error.empty()
                                     ? error
                                     : blocked
                                           ? model_action_error(models[std::min(selected, models.size() - 1)], action)
                                           : action == "center"
                                                 ? progress
                                                 : action == "fork"
                                                       ? "Copy actor into a new model / fresh critic and Adam / source preserved"
                                                 : action == "restart"
                                                       ? "Restart replaces the selected model and its best with fresh state"
                                                       : "Compatibility checks the interface / evaluate separately to measure performance";
        c.footer(x, w, "Up/Down select   R refresh", "Q back / Esc home", note, p);
        const auto select = "Enter " + (action == "critic" ? std::string("use actor") : action);
        const int selectX = x + shortcut_width("Up/Down select   R refresh") + 3;
        c.shortcuts(selectX, height - 2, select, p,
                    std::max(0, w - (selectX - x) - shortcut_width("Q back / Esc home") - 2), "", "",
                    !blocked && !models.empty());
        if (!error.empty()) c.put(x, height - 1, note, p.gold, w);
        return c.lines();
    }
    void center_tabs(screen::Canvas &canvas,int row,int width,int active) {
        using namespace screen;const Palette p;
        std::array<std::string,3> labels{"[1] Model library","[2] Experiments","[3] Designs"};
        int required=8;
        for(const auto &label:labels)required+=textCells(label);
        if(required>width-4) {labels[0]="[1] Models";}
        int x=2;
        for(int i=0;i<3;++i) {
            canvas.shortcuts(x,row,labels[i],p,std::max(0,width-x-2),i==active?p.selected:"",i==active?p.caps.shell_style:"");
            x+=textCells(labels[i])+4;
        }
    }

    std::vector<SavedEntry> parse_saved_catalog(const std::string &catalog) {
        const auto decode=[](const std::string &hex) {
            std::string text;if(hex.size()%2)return text;
            for(std::size_t i=0;i<hex.size();i+=2) {
                unsigned value=0;const auto parsed=std::from_chars(hex.data()+i,hex.data()+i+2,value,16);
                if(parsed.ec!=std::errc{} || parsed.ptr!=hex.data()+i+2)return std::string{};
                text+=value<32||value==127?' ':char(value);
            }return text;
        };
        std::vector<SavedEntry> items;std::istringstream lines(catalog);std::string line;
        while(std::getline(lines,line)) {
            std::istringstream fields(line);std::string tag,path,kind,name,status,detail;
            fields>>tag>>path;
            if(tag=="AURA_SAVED" && fields>>kind>>name>>status && (kind=="experiment"||kind=="design"))
                items.push_back({decode(path),kind,decode(name),decode(status),{}});
            else if(tag=="AURA_SAVED_DETAIL" && fields>>detail)for(auto &item:items)if(item.path==decode(path))item.details.push_back(decode(detail));
        }return items;
    }

    std::vector<std::string> center_saved_frame(const std::vector<SavedEntry> &items,std::size_t selected,int tab,
        int width,int height,const std::string &message,bool inspecting,std::size_t offset,bool canRestore) {
        using namespace screen;const Palette p;Canvas c(width,height);const int w=std::max(1,width-4);
        c.put(2,0,"AURA / COMMAND CENTER",p.bold,w);center_tabs(c,2,width,tab);
        if(width<60||height<20) {c.put(2,5,"Enlarge terminal: 60 columns / 20 rows",p.dim,w);return c.lines();}
        const std::string type=tab==1?"experiments":"model designs";
        c.put(2,4,"Saved "+type+" / "+std::to_string(items.size())+" items",p.dim,w);
        const SavedEntry *item=items.empty()?nullptr:&items[std::min(selected,items.size()-1)];
        const bool split=!inspecting && width>=100;
        const int left=split?w/2:w;
        if(!inspecting) {
            c.put(2,6,"NAME",p.dim,left-18);c.put(left-15,6,"STATE",p.dim,17);
            const auto capacity=std::size_t(std::max(1,height-13));const auto first=selected>=capacity?selected-capacity+1:0;
            for(std::size_t i=first;i<items.size()&&i<first+capacity;++i) {
                int y=8+int(i-first);if(i==selected)c.put(2,y,std::string(left,' '),p.selected,left);
                c.put(2,y,(i==selected?glyph("▸ ","> "):"  ")+ellipsize(items[i].name,left-20),i==selected?p.selected:p.text,left-18);
                c.put(left-15,y,items[i].status,i==selected?p.selected:items[i].status=="failed"?p.caps.error_color:p.dim,17);
            }
            if(!item)c.put(2,8,"No saved "+type+". Use "+(tab==1?"/experiment":"/create")+" to create one.",p.dim,w);
        }
        if(item&&(split||inspecting)) {
            const int x=split?left+5:2,space=width-x-3;
            c.put(x,6,tab==1?"EXPERIMENT DETAILS":"DESIGN DETAILS",p.dim,space);
            c.put(x,8,ellipsize(item->name,space),p.text,space);
            struct DetailRow { std::string label, value, style; int indent; };
            std::vector<DetailRow> details;
            for (const auto &line : item->details) {
                const auto colon = line.find(": ");
                const bool labeled = colon != std::string::npos;
                const std::string label = labeled ? line.substr(0,colon+1) : "";
                std::string value = labeled ? line.substr(colon+2) : line;
                std::string style = p.text;
                if (label == "Actor:" || label == "Critic:" || label == "Reward:") style = p.purple;
                else if (label == "Status:")
                    style = value == "completed" || value == "validated" ? p.green
                          : value == "failed" || value == "unreadable" ? p.caps.error_color : p.gold;
                else if (label == "Error:") style = p.caps.error_color;
                else if (label == "Location:") {
                    value = std::filesystem::path(value).lexically_normal().string();
                    style = p.dim;
                } else if (value.starts_with("Open /")) style = p.dim;
                // Keep values aligned within the pane, including wrapped continuations.
                const int indent = labeled ? std::min(20,std::max(0,space-20)) : 0;
                bool first = true;
                for (const auto &part : network_wrap(value,std::max(1,space-indent))) {
                    details.push_back({first ? label : "",part,style,indent});
                    first = false;
                }
            }
            const int capacity=height-14;const auto first=std::min(offset,details.size()>std::size_t(capacity)?details.size()-capacity:0);
            for(int i=0;i<capacity&&first+i<details.size();++i) {
                const auto &row=details[first+i];
                c.put(x,9+i,ellipsize(row.label,std::max(1,row.indent-1)),p.dim,std::max(1,row.indent-1));
                c.put(x+row.indent,9+i,row.value,row.style,space-row.indent);
            }
            if(inspecting)c.put(2,height-5,"Details / Up/Down scroll / Q returns to saved list",p.dim,w);
        }
        c.actionFooter(2,w,{
            {"Enter Inspect",item!=nullptr},{"U Restore",canRestore},{"R Refresh"},
            {"Q Back"},{"Esc Home"},{"[D] Delete",item!=nullptr,true}
        },message,p);
        return c.lines();
    }

}
