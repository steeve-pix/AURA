#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "Terminal.hpp"
#include "Commands.hpp"

namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::filesystem::path local_path(std::string value) {
    value = trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"')
                          || (value.front() == '\'' && value.back() == '\'')))
        value = value.substr(1, value.size() - 2);
    if (value == "~" || value.starts_with("~/"))
        if (const auto* home = std::getenv("HOME")) value.replace(0, 1, home);
    return value;
}
void print_shortcuts() {
    std::cout << "  Input modes\n\n"
              << "    /           Browse local commands\n"
              << "    !command    Run a shell command with live output\n"
              << "    @path       Browse files and preview text\n"
              << "    ?           Open this shortcut guide\n"
              << "    /voice      Dictate a draft; Enter edits, Esc discards\n\n"
              << "  Editing\n\n"
              << "    Tab         Complete a menu item; otherwise send / queue\n"
              << "    Up/Down     Choose a menu item; otherwise recall previous inputs\n"
              << "    Cmd+C/V     Copy selection / paste into draft (macOS)\n"
              << "    Ctrl+R      Search inputs; Enter recalls for editing, Esc cancels\n"
              << "    Ctrl+P/N    Previous / next input, preserving your draft\n"
              << "    PgUp/PgDn   Scroll output by a page\n"
              << "    Home/End    Start / end of draft\n"
              << "    Ctrl+U/K    Erase before / after the cursor\n"
              << "    Ctrl+Y      Restore text removed with Ctrl+U/K/W\n"
              << "    Ctrl+A/E    Start / end of draft\n"
              << "    Ctrl+B/F    Move backward / forward one character\n"
              << "    Alt+B/F     Move backward / forward one word (or Esc, B/F)\n"
              << "    Ctrl+W      Erase the previous word\n"
              << "    Ctrl+C      Cancel a draft or stop a running command\n"
              << "    Ctrl+D      Exit when the draft is empty\n"
              << "    Left Arrow  Open the command center when the draft is empty\n"
              << "    Ctrl+O      Expand / collapse the latest file / command output\n"
              << "    Ctrl+L      Clear the screen\n"
              << "    Esc         Dismiss a menu\n\n"
              << "  In the clickable iTerm2 profile, Option-drag selects; Cmd+C copies; Cmd+V pastes.\n";
}
void print_help() {
    std::cout << "  Local commands\n\n";
    for (const auto& command : aura::cli::commands)
        std::cout << "    " << std::left << std::setw(22) << command.name << command.description << '\n';
    std::cout << "\n  !git status --short --branch  See changed files and branch\n"
              << "  !git diff --stat             Summarize unstaged changes\n"
              << "  @README.md     Preview a file; @ opens the file picker\n"
              << "  ?              Keyboard shortcuts and input modes\n\n"
              << "  Shell commands use /bin/sh with no interactive input.\n";
}
void print_status() {
    std::error_code error;
    const auto directory = std::filesystem::current_path(error);
    std::cout << "  Directory  " << (error ? "unavailable" : directory.string()) << '\n'
              << "  Runtime    Local C++ tools\n  Shell      /bin/sh\n  Model      Not connected\n";
}
void print_files(const std::filesystem::path& directory = ".") {
    std::error_code error;
    std::filesystem::directory_iterator current(directory, error), end;
    if (error) { aura::cli::print_error("Cannot browse directory: " + error.message()); return; }
    std::vector<std::string> names;
    for (; !error && current != end && names.size() < 200; current.increment(error)) {
        auto name = current->path().filename().string();
        const bool folder = current->is_directory(error);
        if (error) break;
        // Filenames are data, never terminal control sequences.
        for (char& c : name) if (static_cast<unsigned char>(c) < 32 || c == 127) c = '?';
        names.push_back(name + (folder ? "/" : ""));
    }
    std::sort(names.begin(), names.end());
    std::cout << "  Files in " << directory.string() << "\n\n";
    for (const auto& name : names) std::cout << "    " << name << '\n';
    if (names.empty()) std::cout << "    Empty directory\n";
    if (names.size() == 200) std::cout << "\n  Showing the first 200 entries.\n";
    if (error) aura::cli::print_error("Directory changed while browsing: " + error.message());
    std::cout << "\n  Use @path to preview a file.\n";
}
void preview_file(const std::string& query) {
    const auto path = local_path(query);
    if (path.empty()) { print_files(); return; }
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) { print_files(path); return; }
    if (!std::filesystem::is_regular_file(path, error)) {
        aura::cli::print_error("Not a readable regular file: " + path.string()); return;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) { aura::cli::print_error("Cannot read file: " + path.string()); return; }
    std::string content(65537, '\0');
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(file.gcount()));
    aura::cli::FilePreview preview(path.string());
    if (content.find('\0') != std::string::npos) {
        std::cout << "  Binary file / text preview unavailable.\n"; return;
    }
    const bool truncated = content.size() > 65536;
    if (truncated) content.resize(65536);
    for (char& c : content)
        if (static_cast<unsigned char>(c) < 32 && c != '\n' && c != '\t') c = ' ';
    std::istringstream lines(content);
    std::string line;
    int number = 0;
    while (number < 120 && std::getline(lines, line))
        std::cout << "  " << std::right << std::setw(4) << ++number << "  " << line << '\n';
    if (truncated || lines.peek() != std::char_traits<char>::eof())
        std::cout << "\n  Preview limited to 120 lines / 64 KiB.\n";
    if (content.empty()) std::cout << "  Empty file.\n";
}
std::filesystem::path aura_project_directory() {
    return std::filesystem::path(AURA_PROJECT_DIR);
}
std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (const char character : value)
        result += character == '\'' ? "'\\''" : std::string(1, character);
    return result + "'";
}
int control_editor() {
    const auto path = aura_project_directory() / "viewer_action.json";
    const auto observationPath = aura_project_directory() / "viewer_observation.json";
    const std::array<std::string, 4> keys{
        "left_hip_target", "right_hip_target", "left_knee_target", "right_knee_target"};
    const std::array<std::string, 4> angleKeys{
        "left_hip_angle", "right_hip_angle", "left_knee_angle", "right_knee_angle"};
    aura::cli::JointControls controls{{
        {"Left hip", 0.0, -0.8, 0.8}, {"Right hip", 0.0, -0.8, 0.8},
        {"Left knee", 0.0, 0.0, 2.2}, {"Right knee", 0.0, 0.0, 2.2}}};
    std::ifstream input(path);
    if (input) {
        const std::string contents(std::istreambuf_iterator<char>(input), {});
        try {
            for (std::size_t i = 0; i < keys.size(); ++i) {
                const std::regex pattern("\\\"" + keys[i] + "\\\"\\s*:\\s*([^,}\\s]+)");
                std::smatch match;
                if (!std::regex_search(contents, match, pattern))
                    throw std::invalid_argument("Missing joint target");
                std::size_t consumed = 0;
                const auto text = match[1].str();
                const double value = std::stod(text, &consumed);
                if (consumed != text.size() || !std::isfinite(value))
                    throw std::invalid_argument("Invalid joint target");
                controls[i].clamped = value < controls[i].minimum || value > controls[i].maximum;
                controls[i].value = std::clamp(value, controls[i].minimum, controls[i].maximum);
            }
        } catch (const std::exception&) {
            aura::cli::print_error("viewer_action.json contains invalid joint targets.");
            return 1;
        }
    } else if (std::filesystem::exists(path)) {
        aura::cli::print_error("Cannot read viewer_action.json.");
        return 1;
    }
    // The terminal owns interaction; the application owns the action file.
    aura::cli::edit_joint_controls(controls, [&](const aura::cli::JointControls& next) -> std::string {
        auto temporary = path;
        temporary += ".cli.tmp";
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return "Cannot write viewer_action.json.";
        output << std::setprecision(9) << "{\n";
        for (std::size_t i = 0; i < keys.size(); ++i)
            output << "  \"" << keys[i] << "\": " << next[i].value
                   << (i + 1 == keys.size() ? "\n" : ",\n");
        output << "}\n";
        output.close();
        std::error_code error;
        if (!output) {
            std::filesystem::remove(temporary, error);
            return "Could not finish writing viewer_action.json.";
        }
        std::filesystem::rename(temporary, path, error);
        if (error) {
            const auto message = "Could not update viewer_action.json: " + error.message();
            std::filesystem::remove(temporary, error);
            return message;
        }
        return {};
    }, [&]() -> std::optional<aura::cli::ControlObservation> {
        std::ifstream source(observationPath);
        if (!source) return std::nullopt;
        const std::string contents(std::istreambuf_iterator<char>(source), {});
        try {
            aura::cli::ControlObservation result;
            const auto number = [&](const std::string& key) {
                const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*([^,}\\s]+)");
                std::smatch match;
                if (!std::regex_search(contents, match, pattern))
                    throw std::invalid_argument("Missing observation field");
                std::size_t consumed = 0;
                const auto text = match[1].str();
                const auto value = std::stod(text, &consumed);
                if (consumed != text.size() || !std::isfinite(value))
                    throw std::invalid_argument("Invalid observation field");
                return value;
            };
            result.pelvis_height = number("pelvis_height");
            result.pelvis_uprightness = number("pelvis_uprightness");
            const std::regex standingPattern("\\\"is_standing\\\"\\s*:\\s*(true|false)");
            std::smatch standing;
            if (!std::regex_search(contents, standing, standingPattern)) return std::nullopt;
            result.is_standing = standing[1].str() == "true";
            for (std::size_t i = 0; i < angleKeys.size(); ++i)
                result.joint_angles[i] = number(angleKeys[i]);
            // Older viewers can still supply basic observations.
            try {
                result.simulation_time = number("simulation_time");
                result.steps = number("steps");
                result.speed = number("pelvis_speed");
                result.angular_speed = number("pelvis_angular_speed");
                result.paused = std::regex_search(contents,std::regex(R"("paused"\s*:\s*true)"));
                result.has_motion = true;
            } catch (const std::exception&) {}
            std::smatch pose;
            if (std::regex_search(contents, pose,
                    std::regex(R"("body_pose"\s*:\s*\[([^\]]*)\])"))) {
                auto values = pose[1].str();
                std::replace(values.begin(), values.end(), ',', ' ');
                std::istringstream input(values);
                std::array<double, 10> part{};
                bool valid = true;
                for (int i = 0; i < 16 && valid; ++i) {
                    for (auto &value : part)
                        if (!(input >> value) || !std::isfinite(value)) { valid = false; break; }
                    if (valid) result.body_pose.push_back(part);
                }
                input >> std::ws;
                if (!valid || !input.eof()) result.body_pose.clear();
            }
            const auto modified = std::filesystem::last_write_time(observationPath);
            result.age_seconds = std::max(0.0, std::chrono::duration<double>(
                std::filesystem::file_time_type::clock::now() - modified).count());
            return result;
        } catch (const std::exception&) { return std::nullopt; }
    });
    return 0;
}
int execute_command(const aura::cli::Command& command, const std::string& argument = "") {
    using aura::cli::CommandId;
    const bool acceptsArgument = command.takes_path;
    if ((!acceptsArgument && !argument.empty()) || (command.takes_path && argument.empty())) {
        std::cout << "  Usage: " << command.name
                  << (command.takes_path ? " <directory>" : " (no arguments)") << '\n';
        return 2;
    }
    switch (command.id) {
        case CommandId::help: print_help(); break;
        case CommandId::shortcuts: print_shortcuts(); break;
        case CommandId::voice: aura::cli::dictate_prompt(); break;
        case CommandId::train_balance: {
            if (!argument.empty()) {
                std::cout << "  Usage: /train_balance\n";
                return 2;
            }
            const auto directory = "cd " + shell_quote(aura_project_directory().string());
            const auto startup = aura::cli::run_shell(directory + " && cmake --build build --target aura_python");
            if (startup != 0) return startup;
            return aura::cli::run_balance_training(
                directory + " && PYTHONPATH=build python3 -m python.train_balance");
        }
        case CommandId::aura_control: {
            const auto startup = aura::cli::run_shell(
                "cd " + shell_quote(aura_project_directory().string())
                + " && python3 -m python.control_aura --prepare");
            if (startup != 0) return startup;
            return control_editor();
        }
        case CommandId::version: std::cout << "  Aura CLI " << aura::cli::version << '\n'; break;
        case CommandId::status: print_status(); break;
        case CommandId::files: print_files(); break;
        case CommandId::history: aura::cli::print_history(); break;
        case CommandId::queue: aura::cli::print_queue(); break;
        case CommandId::unqueue: aura::cli::unqueue(); break;
        case CommandId::cd: {
            std::error_code error;
            std::filesystem::current_path(local_path(argument), error);
            if (error) { aura::cli::print_error("Cannot change directory: " + error.message()); return 1; }
            print_status(); break;
        }
        case CommandId::clear: aura::cli::clear_transcript(); aura::cli::print_header(); break;
        case CommandId::exit: break;
    }
    return 0;
}
int run_interactive() {
    aura::cli::TerminalSession terminal;
    aura::cli::print_header();
    while (const auto draft = aura::cli::read_prompt()) {
        const auto input = trim(*draft);
        if (input.empty()) continue;
        if (input.starts_with('!')) {
            aura::cli::run_shell(trim(input.substr(1)));
            continue;
        }
        const auto references = aura::cli::file_references(input);
        if (!input.starts_with('/') && !references.empty()) {
            for (const auto& path : references) preview_file(path);
            std::cout << '\n'; continue;
        }
        const auto space = input.find_first_of(" \t");
        const auto name = input.substr(0, space);
        const auto argument = space == std::string::npos ? "" : trim(input.substr(space + 1));
        const auto* command = name.starts_with('/') || name == "?" ? aura::cli::find_command(name) : nullptr;
        if (command && argument.empty() && command->id == aura::cli::CommandId::exit) return 0;
        if (command) execute_command(*command, argument);
        else if (name.starts_with('/'))
            aura::cli::print_error("Unknown command: " + name + ". Type / to browse commands.");
        else std::cout << "  No model connected. Local tools are available:\n"
                       << "    !command  run shell     @path  preview file     /  commands\n";
        std::cout << '\n';
    }
    return 0;
}
}
int main(int argc, char** argv) {
    if (argc == 1) return run_interactive();
    const auto* command = aura::cli::find_command(argv[1]);
    if (!command) { aura::cli::print_error("Unknown command. Use aura-cli --help."); return 2; }
    std::string argument;
    for (int i = 2; i < argc; ++i) { if (i > 2) argument += ' '; argument += argv[i]; }
    if (command->id == aura::cli::CommandId::aura_control
        || command->id == aura::cli::CommandId::train_balance) {
        aura::cli::TerminalSession terminal;
        return execute_command(*command, argument);
    }
    return execute_command(*command, argument);
}
