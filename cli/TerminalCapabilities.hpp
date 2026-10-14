#pragma once

#include <string>

namespace aura::cli {
// Keep terminfo's legacy macros inside the implementation file.
struct TerminalCapabilities {
    bool fullscreen = false;
    bool utf8 = false;
    std::string enter_screen, leave_screen, erase_screen, erase_line;
    std::string hide_cursor, show_cursor, normal, strong, faint, inverse, accent, error_color, muted_color, context_mode, context_path, context_activity, shell_style, shell_marker;
    std::string addressing, white_color, highlight_color, syntax_command, syntax_operator;
    std::string move_to(int row, int column) const;
};
const TerminalCapabilities& terminal_capabilities();
}
