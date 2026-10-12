#include "TerminalCapabilities.hpp"

#include <clocale>
#include <cstdlib>
#include <langinfo.h>
#include <string_view>
#include <unistd.h>
#include <curses.h>
#include <term.h>

namespace aura::cli {
namespace {
std::string capability(const char* name) {
    const char* value = tigetstr(const_cast<char*>(name));
    if (!value || value == reinterpret_cast<const char*>(-1)) return {};
    // terminfo delay markers are interpreted by tputs, not by the terminal.
    // Modern terminals do not need those delays; omit them from buffered frames.
    std::string result(value);
    for (auto at = result.find("$<"); at != std::string::npos; at = result.find("$<")) {
        const auto end = result.find('>', at);
        if (end == std::string::npos) break;
        result.erase(at, end - at + 1);
    }
    return result;
}
}
const TerminalCapabilities& terminal_capabilities() {
    static const auto value = [] {
        TerminalCapabilities result;
        std::setlocale(LC_CTYPE, "");
        const std::string_view encoding(nl_langinfo(CODESET));
        result.utf8 = encoding == "UTF-8" || encoding == "UTF8" || encoding == "utf-8";
        if (const auto* ascii = std::getenv("AURA_ASCII"))
            if (std::string_view(ascii) == "1") result.utf8 = false;
        const char* term = std::getenv("TERM");
        const auto* mode = std::getenv("AURA_TUI");
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || !term || !*term
            || std::string_view(term) == "dumb"
            || (mode && std::string_view(mode) == "plain")) return result;
        int status = 0;
        if (setupterm(const_cast<char*>(term), STDOUT_FILENO, &status) != OK || status != 1)
            return result;
        result.enter_screen = capability("smcup");
        result.leave_screen = capability("rmcup");
        result.erase_screen = capability("clear");
        result.erase_line = capability("el");
        result.addressing = capability("cup");
        result.hide_cursor = capability("civis");
        result.show_cursor = capability("cnorm");
        result.normal = capability("sgr0");
        result.strong = capability("bold");
        result.faint = capability("dim");
        result.inverse = capability("rev");
        const auto foreground = capability("setaf");
        const int color_count = tigetnum(const_cast<char*>("colors"));
        if (!std::getenv("NO_COLOR") && !foreground.empty() && color_count >= 8) {
            auto foreground_color = [&](long index) {
                const char* value = tparm(const_cast<char*>(foreground.c_str()), index);
                return value ? std::string(value) : std::string{};
            };
            // Main interface uses the terminal's own text color. Only the
            // context line gets restrained warm, green, and lavender accents.
            result.muted_color = foreground_color(color_count >= 256 ? 245 : color_count >= 16 ? 8 : 7);
            result.context_mode = foreground_color(color_count >= 256 ? 180 : 3);
            result.context_path = foreground_color(color_count >= 256 ? 151 : 2);
            result.context_activity = foreground_color(color_count >= 256 ? 147 : 5);
            const auto background = capability("setab");
            if (!background.empty() && color_count >= 16) {
                const char* gray = tparm(const_cast<char*>(background.c_str()),
                                         static_cast<long>(color_count >= 256 ? 236 : 8));
                if (gray) result.shell_style = gray;
            }
            result.white_color = foreground_color(color_count >= 256 ? 231 : color_count >= 16 ? 15 : 7);
            result.highlight_color = foreground_color(color_count >= 256 ? 250 : 7);
            result.shell_style += result.white_color;
            const char* marker = tparm(const_cast<char*>(foreground.c_str()),
                                       static_cast<long>(color_count >= 16 ? 9 : 1));
            if (marker) { result.shell_marker = marker; result.error_color = marker; }
        }
        result.fullscreen = !result.enter_screen.empty() && !result.leave_screen.empty()
            && !result.erase_screen.empty() && !result.erase_line.empty()
            && !result.addressing.empty();
        return result;
    }();
    return value;
}
std::string TerminalCapabilities::move_to(int row, int column) const {
    if (addressing.empty()) return {};
    const char* value = tparm(const_cast<char*>(addressing.c_str()),
                              static_cast<long>(row), static_cast<long>(column));
    return value ? value : "";
}
}
