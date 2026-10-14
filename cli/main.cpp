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
#include "ModelsScreen.hpp"
#include "TerminalCapabilities.hpp"

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
    const auto& c=aura::cli::terminal_capabilities();
    const auto heading=[&](const std::string& title) {
        std::cout << "\n  " << c.context_mode << c.strong << title << c.normal << "\n\n";
    };
    const auto row=[&](const std::string& key,const std::string& description) {
        std::cout << "    " << c.context_activity << c.strong << std::left << std::setw(29)
                  << ("["+key+"]") << c.normal << c.muted_color << description << c.normal << '\n';
    };
    heading("Input modes");
    row("/","Browse local commands");
    row("!command","Run a shell command with live output");
    row("@path","Browse files and preview text");
    row("?","Open this shortcut guide");
    row("/voice","Dictate a draft; Enter edits, Esc discards");
    heading("Editing");
    row("Shift+Enter","New line; input expands (Aura iTerm2 profile)");
    row("Ctrl+J","New line on any terminal");
    row("Cmd+Z","Undo draft edit (Ctrl+Z also works)");
    row("Cmd+Shift+Z","Redo draft edit (Esc, then Shift+Z also works)");
    heading("Menus and history");
    row("Tab","Complete a menu item; otherwise run / queue");
    row("Up/Down","Choose a menu item; otherwise recall previous inputs");
    row("Cmd+C/V","Copy selection / paste into draft (macOS)");
    row("Ctrl+R","Search inputs; Enter recalls for editing, Esc cancels");
    row("Ctrl+P/N","Previous / next input, preserving your draft");
    row("PgUp/PgDn","Scroll output by a page");
    heading("Cursor and text editing");
    row("Home/End","Start / end of current line");
    row("Ctrl+U/K","Erase before / after the cursor");
    row("Ctrl+Y","Restore text removed with Ctrl+U/K/W");
    row("Ctrl+A/E","Start / end of current line");
    row("Ctrl+B/F","Move backward / forward one character");
    row("Option+Left/Right","Move by word");
    row("Option+Shift+Left/Right","Select by word");
    row("Shift+Left/Right","Select by character");
    row("Option+Delete","Erase the previous word");
    row("Option+Forward Delete","Erase the next word");
    row("Cmd+Left/Right","Start / end of current line");
    row("Cmd+Shift+Left/Right","Select to start / end of line");
    row("Cmd+Delete","Erase to start of line");
    row("Cmd+A","Select the draft");
    row("Alt+B/F","Move by word (or Esc, B/F)");
    row("Ctrl+W","Erase the previous word");
    heading("Navigation and session");
    row("Ctrl+C","Cancel a draft or stop a running command");
    row("Ctrl+D","Exit when the draft is empty");
    row("Left Arrow","Open the command center when the draft is empty");
    row("Ctrl+O","Expand / collapse the latest file / command output");
    row("Ctrl+L","Clear the screen");
    row("Esc","Dismiss menus and return to latest output");
}

std::filesystem::path aura_project_directory();
void print_help() {
    const auto& colors=aura::cli::terminal_capabilities();
    const auto key=[&](const std::string& text) { return colors.context_activity+colors.strong+text+colors.normal; };
    for (const bool utilities : {false, true}) {
        std::cout << colors.context_mode << colors.strong
                  << (utilities ? "\n  Console utilities\n\n" : "  AURA project actions\n\n") << colors.normal;
        for (const auto& command : aura::cli::commands) {
            if (command.secondary != utilities) continue;
            const bool primary=command.id==aura::cli::CommandId::run || command.id==aura::cli::CommandId::aura_control ||
                command.id==aura::cli::CommandId::train_nn || command.id==aura::cli::CommandId::center;
            std::cout << "    " << (primary?colors.context_activity+colors.strong:colors.syntax_command)
                      << std::left << std::setw(22) << command.name << colors.normal
                      << colors.muted_color << command.description << colors.normal << '\n';
        }
    }
    std::cout << colors.context_mode << colors.strong << "\n  Quick use\n\n" << colors.normal
              << colors.muted_color << "    Type " << key("run, build, control, train or center") << colors.muted_color << ", with or without " << key("/") << colors.muted_color << ".\n"
              << "    " << key("!command") << colors.muted_color << " runs a shell command in the current directory.\n"
              << "    " << key("@") << colors.muted_color << " opens the file picker; " << key("@src/main.cpp") << colors.muted_color << " previews a file.\n"
              << "    " << key("?") << colors.muted_color << " shows shortcuts. " << key("Left Arrow") << colors.muted_color << " on an empty prompt opens the model library and model logs.\n\n"
              << "    Shell commands use /bin/sh with no interactive input.\n" << colors.normal;
}

void print_status() {
    const auto project = aura_project_directory();
    const auto& c=aura::cli::terminal_capabilities();
    const auto heading=[&](const std::string& title) {
        std::cout << "\n  " << c.context_mode << c.strong << title << c.normal << "\n\n";
    };
    const auto row=[&](const std::string& label,const std::string& value,const std::string& color) {
        std::cout << "    " << c.muted_color << std::left << std::setw(18) << label << c.normal
                  << color << value << c.normal << '\n';
    };
    const auto present = [&](const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    };
    heading("AURA / READINESS");
    const bool configured=present(project / "build-release/CMakeCache.txt");
    row("Release build",configured?"Configured":"Missing / use /build",configured?c.context_path:c.context_mode);
#ifdef __APPLE__
    const auto viewer = project / "build-release/AURA.app/Contents/MacOS/AURA";
#else
    const auto viewer = project / "build-release/aura";
#endif
    const bool viewerBuilt=present(viewer);
    row("Viewer",viewerBuilt?"Built / /run rebuilds before opening":"Missing / use /run",viewerBuilt?c.context_path:c.context_mode);
    std::error_code error;
    bool bridge = false;
    for (std::filesystem::directory_iterator entry(project / "build-release", error), end;
         !error && entry != end; entry.increment(error)) {
        const auto name = entry->path().filename().string();
        if (name.starts_with("aura_python") && (entry->path().extension() == ".so" || entry->path().extension() == ".pyd") && entry->is_regular_file(error))
            bridge = true;
    }
    row("Python bridge",bridge?"Built / available for the neural labs":"Missing / /train builds it",bridge?c.context_path:c.context_mode);
    heading("MODEL LIBRARY");
    std::string libraryError;
    const auto models=aura::cli::saved_models(project / "models",libraryError);
    if (!libraryError.empty()) row("Checkpoints","Unavailable / "+libraryError,c.context_mode);
    else {
        const auto best=std::count_if(models.begin(),models.end(),aura::cli::model_is_best);
        row("Actor checkpoints",std::to_string(models.size())+" saved / "+std::to_string(best)+" best copies",c.context_path);
        if (!models.empty()) {
            const auto latest=std::max_element(models.begin(),models.end(),[](const auto& a,const auto& b) { return a.modified<b.modified; });
            row("Latest save",latest->path.filename().string(),c.context_activity);
            row("Saved",aura::cli::model_updated(latest->modified),c.muted_color);
        }
    }
    row("Open library","/center / inspect compatibility, scores and model logs",c.context_activity);
    heading("VIEWER OBSERVATION");
    error.clear();
    const auto modified = std::filesystem::last_write_time(project / "viewer_observation.json", error);
    if (error) row("Snapshot","None recorded / use /run",c.context_mode);
    else {
        const auto age = std::max(0LL, std::chrono::duration_cast<std::chrono::seconds>(
            std::filesystem::file_time_type::clock::now() - modified).count());
        const auto duration=age<60?std::to_string(age)+"s":age<3600?std::to_string(age/60)+"m "+std::to_string(age%60)+"s":std::to_string(age/3600)+"h "+std::to_string(age%3600/60)+"m";
        row("Snapshot",std::string(age>5?"Stale":"Recent")+" / written "+duration+" ago",age>5?c.context_mode:c.context_path);
    }
    row("Meaning","File freshness only; does not confirm a running viewer",c.muted_color);
    heading("FILE LOCATIONS");
    row("AURA project",project.string(),c.muted_color);
    error.clear();
    const auto directory=std::filesystem::current_path(error);
    row("Shell / files",error?"Unavailable":directory.string(),c.muted_color);
    std::cout << '\n';
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
        aura::cli::print_error("Not a readable regular file: " + path.string());
        std::cout << "  Next: Type @ to browse existing files, or check the current folder with !pwd.\n";
        return;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        aura::cli::print_error("Cannot read file: " + path.string());
        std::cout << "  Next: Check that this file is readable, or use @ to choose another file.\n";
        return;
    }
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
    return std::filesystem::path(AURA_PROJECT_DIR).lexically_normal();
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
    const auto& specs = aura::sim::actionSpecs;
    aura::cli::JointControls controls{};
    for (std::size_t i=0; i<specs.size(); ++i)
        controls[i] = {specs[i].label, 0.0, specs[i].minimum, specs[i].maximum};
    std::ifstream input(path);
    if (input) {
        const std::string contents(std::istreambuf_iterator<char>(input), {});
        try {
            for (std::size_t i = 0; i < specs.size(); ++i) {
                const std::regex pattern("\\\"" + std::string(specs[i].key) + "\\\"\\s*:\\s*([^,}\\s]+)");
                std::smatch match;
                if (!std::regex_search(contents, match, pattern))
                    continue; // Older action files leave appended targets neutral.
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
        for (std::size_t i = 0; i < specs.size(); ++i)
            output << "  \"" << specs[i].key << "\": " << next[i].value
                   << (i + 1 == specs.size() ? "\n" : ",\n");
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
            for (std::size_t i = 0; i < specs.size(); ++i)
                result.joint_angles[i] = number(specs[i].angleKey);
            std::smatch motorBlock;
            if (std::regex_search(contents,motorBlock,std::regex(R"("motor_enabled"\s*:\s*\{([^}]*)\})"))) {
                result.has_motor_states=true;
                const auto states=motorBlock[1].str();
                for (std::size_t i=0;i<specs.size();++i) {
                    std::smatch state;
                    const std::regex pattern("\""+std::string(specs[i].key)+"\"\\s*:\\s*(true|false)");
                    if (!std::regex_search(states,state,pattern)) { result.has_motor_states=false; break; }
                    result.motor_enabled[i]=state[1].str()=="true";
                }
            }
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
                while (valid && (input >> std::ws) && !input.eof()) {
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
int build_project(const std::string& target) {
    const auto directory = "cd " + shell_quote(aura_project_directory().string());
    return aura::cli::run_shell(directory + " && cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release --target " + target,
                                target=="aura_python"?"Neural simulation build / Release":"Simulation build / Release");
}
int execute_command(const aura::cli::Command& command, const std::string& argument = "") {
    using aura::cli::CommandId;
    const bool acceptsArgument = command.takes_path || command.id == CommandId::history;
    if ((!acceptsArgument && !argument.empty()) || (command.takes_path && argument.empty())) {
        aura::cli::print_error("Usage: " + std::string(command.name)
            + (command.takes_path ? " <directory>. Use Tab to complete a folder, including quoted paths with spaces."
                : ". This command takes no arguments. Type /help to see the supported usage."));
        return 2;
    }
    switch (command.id) {
        case CommandId::build: return build_project("aura");
        case CommandId::run: {
            const auto result = build_project("aura");
            if (result != 0) return result;
            const auto directory = "cd " + shell_quote(aura_project_directory().string());
#ifdef __APPLE__
            const auto launched = aura::cli::run_shell(directory + " && /usr/bin/open build-release/AURA.app","Simulation viewer");
            if (launched == 0) std::cout << "  Viewer launch requested. Use control to edit joint targets.\n";
            return launched;
#else
            return aura::cli::run_shell(directory + " && ./build-release/aura");
#endif
        }
        case CommandId::help: print_help(); break;
        case CommandId::shortcuts: print_shortcuts(); break;
        case CommandId::voice: aura::cli::dictate_prompt(); break;
        case CommandId::train_nn: {
            const auto startup = build_project("aura_python");
            if (startup != 0) return startup;
            while (!aura::cli::returning_home()) {
                const auto selection=aura::cli::select_training((aura_project_directory()/"models").string());
                if (!selection) return 0;
                const auto directory = "cd " + shell_quote(aura_project_directory().string());
                const int code=selection->mode=="critic"?aura::cli::run_critic_training(
                    directory+" && PYTHONPATH=build-release python3 -m cli.critic_session"
                    +(selection->checkpoint.empty()?"":" --checkpoint "+shell_quote(selection->checkpoint))):aura::cli::run_nn_training(
                    directory + " && PYTHONPATH=build-release python3 -m cli.nn_session --mode "+shell_quote(selection->mode)
                    +(selection->mode=="new"?" --name "+shell_quote(selection->name):" --checkpoint "+shell_quote(selection->checkpoint)));
                if (code!=0) return code;
            }
            return 0;
        }
        case CommandId::center:
            aura::cli::show_command_center();
            break;
        case CommandId::aura_control: {
            const auto startup = aura::cli::run_shell(
                "cd " + shell_quote(aura_project_directory().string())
                + " && python3 -m python.control_aura --prepare","Joint control setup");
            if (startup != 0) return startup;
            return control_editor();
        }
        case CommandId::version: std::cout << "  Aura CLI " << aura::cli::version << '\n'; break;
        case CommandId::status: print_status(); break;
        case CommandId::files: print_files(); break;
        case CommandId::history:
            if (argument.empty()) aura::cli::print_history();
            else if (argument == "clear") return aura::cli::clear_history() ? 0 : 1;
            else { aura::cli::print_error("Usage: history [clear]. Use history to view commands, or history clear to remove saved and session history."); return 2; }
            break;
        case CommandId::cd: {
            std::error_code error;
            std::filesystem::current_path(local_path(argument), error);
            if (error) {
                aura::cli::print_error("Cannot change directory to " + argument + ": " + error.message());
                std::cout << "  Next: Use cd followed by Tab to choose an existing folder. Quote paths containing spaces.\n";
                return 1;
            }
            print_status(); break;
        }
        case CommandId::clear: aura::cli::clear_transcript(); aura::cli::print_header(); break;
        case CommandId::exit: break;
    }
    return 0;
}
void unknown_command(const std::string& name) {
    aura::cli::print_error("Unknown command: " + name + ".");
    if (const auto* suggestion = aura::cli::close_command(name))
        std::cout << "  Did you mean " << suggestion->name << "? Type it or select it from completion.\n";
    else std::cout << "  Next: Type / to browse AURA commands. Use ! before a shell command, for example !pwd.\n";
}
void configure_command_center() {
    aura::cli::set_command_center_handler([](const std::string& action, const std::string& path) {
        const auto startup = build_project("aura_python");
        if (startup != 0) return startup;
        const auto prefix = "cd " + shell_quote(aura_project_directory().string()) + " && PYTHONPATH=build-release python3 -m ";
        if (action == "evaluate") return aura::cli::run_evaluation(prefix + "cli.evaluate_model " + shell_quote(path));
        if (action == "resume") return aura::cli::run_nn_training(prefix + "cli.nn_session --mode resume --checkpoint " + shell_quote(path));
        return aura::cli::run_shell(prefix + "cli.model_viewer " + shell_quote(path), "Policy playback / " + std::filesystem::path(path).filename().string(),"playback");
    });
}
int run_interactive() {
    aura::cli::TerminalSession terminal;
    configure_command_center();
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
        const auto space = input.find_first_of(" \t\r\n");
        const auto name = input.substr(0, space);
        const auto argument = space == std::string::npos ? "" : trim(input.substr(space + 1));
        const auto* command = aura::cli::find_command(name);
        if (command && argument.empty() && command->id == aura::cli::CommandId::exit) return 0;
        if (command) execute_command(*command, argument);
        else unknown_command(name);
        std::cout << '\n';
    }
    return 0;
}
}
int main(int argc, char** argv) {
    if (argc == 1) return run_interactive();
    const auto* command = aura::cli::find_command(argv[1]);
    if (!command) { unknown_command(argv[1]); return 2; }
    std::string argument;
    for (int i = 2; i < argc; ++i) { if (i > 2) argument += ' '; argument += argv[i]; }
    if (command->id == aura::cli::CommandId::build
        || command->id == aura::cli::CommandId::run
        || command->id == aura::cli::CommandId::aura_control
        || command->id == aura::cli::CommandId::train_nn
        || command->id == aura::cli::CommandId::center) {
        aura::cli::TerminalSession terminal;
        configure_command_center();
        return execute_command(*command, argument);
    }
    return execute_command(*command, argument);
}
