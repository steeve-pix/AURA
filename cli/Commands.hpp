#pragma once

#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <string_view>

namespace aura::cli {
enum class CommandId {
    help, shortcuts, status, files, cd, history, version, clear, voice,
    build, run, aura_control, train_nn, center, exit
};
struct Command {
    CommandId id;
    std::string_view name;
    std::string_view description;
    bool takes_path = false;
    bool secondary = false;
};
inline constexpr std::array commands{
    Command{CommandId::run, "/run", "Build and open the AURA simulation"},
    Command{CommandId::aura_control, "/control", "Open the viewer and edit joint targets"},
    Command{CommandId::train_nn, "/train", "Train an actor or critic; inspect learning and evaluation"},
    Command{CommandId::center, "/center", "Open the model library and model logs"},
    Command{CommandId::build, "/build", "Configure and build the simulation"},
    Command{CommandId::status, "/status", "Check build artifacts and observation freshness"},
    Command{CommandId::voice, "/voice", "Dictate a command; review before running"},
    Command{CommandId::cd, "/cd", "Change shell/file directory: /cd <directory>", true},
    Command{CommandId::help, "/help", "Project actions and console utilities"},
    Command{CommandId::exit, "/exit", "Close Aura CLI"},
    Command{CommandId::shortcuts, "/shortcuts", "Show keyboard shortcuts", false, true},
    Command{CommandId::files, "/files", "List files in the shell/file directory", false, true},
    Command{CommandId::history, "/history", "Show saved commands; history clear removes history", false, true},
    Command{CommandId::version, "/version", "Show the CLI version", false, true},
    Command{CommandId::clear, "/clear", "Clear the console output", false, true},
};
inline bool quit_alias_prefix(std::string_view name) {
    if (name.starts_with('/')) name.remove_prefix(1);
    return !name.empty() && std::string_view("quit").starts_with(name);
}
inline std::size_t command_distance(std::string_view a, std::string_view b) {
    if (a.size() > 64 || b.size() > 64) return 65;
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto diagonal = row[0]; row[0] = i + 1;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const auto old = row[j + 1];
            row[j + 1] = std::min({row[j + 1] + 1, row[j] + 1, diagonal + (a[i] != b[j])});
            diagonal = old;
        }
    }
    return row.back();
}
inline const Command* close_command(std::string_view name) {
    if (name.starts_with('/')) name.remove_prefix(1);
    if (name.size() < 2 || name.size() > 64) return nullptr;
    const Command* best = nullptr;
    std::size_t score = 65;
    for (const auto& command : commands) {
        const auto candidate = command.name.substr(1);
        if (candidate.front() != name.front()) continue;
        const auto distance = candidate.starts_with(name) ? 0 : command_distance(name, candidate);
        if (distance <= (name.size() >= 4 ? 2u : 1u) && distance < score) { best = &command; score = distance; }
    }
    return best;
}
inline const Command* find_command(std::string_view name) {
    if (name == "/train_nn" || name == "train_nn") name = "/train";
    if (name == "/models" || name == "models" || name == "/model" || name == "model" ||
        name == "/evaluate" || name == "evaluate" || name == "/eval" || name == "eval") name = "/center";
    if (name == "-h" || name == "--help") name = "/help";
    if (name == "?" ) name = "/shortcuts";
    if (name == "-v" || name == "--version") name = "/version";
    if (name == "/quit" || name == "quit") name = "/exit";
    for (const auto& command : commands)
        if (name == command.name || name == command.name.substr(1)) return &command;
    return nullptr;
}
}
