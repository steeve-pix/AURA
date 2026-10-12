#pragma once

#include <array>
#include <string_view>

namespace aura::cli {
enum class CommandId {
    help, shortcuts, status, files, cd, history, queue, unqueue, version, clear, voice,
    aura_control, train_balance, exit
};
struct Command {
    CommandId id;
    std::string_view name;
    std::string_view description;
    bool takes_path = false;
};
inline constexpr std::array commands{
    Command{CommandId::help, "/help", "Explore commands and input modes"},
    Command{CommandId::shortcuts, "/shortcuts", "Show keyboard shortcuts"},
    Command{CommandId::status, "/status", "Show the current workspace"},
    Command{CommandId::files, "/files", "List files in this directory"},
    Command{CommandId::cd, "/cd", "Change workspace: /cd <directory>", true},
    Command{CommandId::history, "/history", "Show inputs from this session"},
    Command{CommandId::queue, "/queue", "Show inputs waiting to run"},
    Command{CommandId::unqueue, "/unqueue", "Restore the last queued input to your draft"},
    Command{CommandId::version, "/version", "Show the CLI version"},
    Command{CommandId::clear, "/clear", "Start a fresh screen"},
    Command{CommandId::voice, "/voice", "Dictate into an editable prompt (on-device)"},
    Command{CommandId::aura_control, "/control", "Edit all four joint targets in a table"},
    Command{CommandId::train_balance, "/train_balance", "Search balance feedback gains"},
    Command{CommandId::exit, "/exit", "Close Aura CLI"},
};
inline const Command* find_command(std::string_view name) {
    if (name == "-h" || name == "--help") name = "/help";
    if (name == "?" ) name = "/shortcuts";
    if (name == "/pwd") name = "/status";
    if (name == "-v" || name == "--version") name = "/version";
    if (name == "/quit" || name == "quit") name = "/exit";
    for (const auto& command : commands)
        if (name == command.name || name == command.name.substr(1)) return &command;
    return nullptr;
}
}
