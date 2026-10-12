#include "Terminal.hpp"
#include "BodyPreview.hpp"
#include "Commands.hpp"
#include "TerminalCapabilities.hpp"
#include "VoiceDictation.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <charconv>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <deque>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <clocale>
#include <cwchar>
#include <memory>
#include <sstream>
#include <poll.h>
#include <cerrno>
#include <csignal>
#include <string_view>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace aura::cli {
namespace {
const std::string& normal() { return terminal_capabilities().normal; }
const std::string& faint() { return terminal_capabilities().faint; }
const std::string& strong() { return terminal_capabilities().strong; }
const std::string& inverse() { return terminal_capabilities().inverse; }
std::string_view prompt_mark() { return terminal_capabilities().utf8 ? "› " : "> "; }
const std::string& accent() { return terminal_capabilities().accent; }
const std::string& muted() { return terminal_capabilities().muted_color; }
const std::string& error_color() { return terminal_capabilities().error_color; }
std::string_view separator() { return terminal_capabilities().utf8 ? " · " : " / "; }
bool styled() { return terminal_capabilities().fullscreen; }
bool clickable_details() {
    const auto* mode = std::getenv("AURA_MOUSE");
    return !mode || std::string_view(mode) == "click";
}

int terminal_width() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
        return static_cast<int>(size.ws_col);
    return 80;
}

int terminal_height() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) return size.ws_row;
    return 24;
}

// Byte positions always stop at a UTF-8 character boundary.
std::size_t previous(const std::string& value, std::size_t pos) {
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(value[pos]) & 0xc0) == 0x80) --pos;
    return pos;
}
std::size_t next(const std::string& value, std::size_t pos) {
    if (pos == value.size()) return pos;
    ++pos;
    while (pos < value.size() && (static_cast<unsigned char>(value[pos]) & 0xc0) == 0x80) ++pos;
    return pos;
}
int cell_width(std::string_view character) {
    std::mbstate_t state{};
    wchar_t decoded = 0;
    const auto consumed = std::mbrtowc(&decoded, character.data(), character.size(), &state);
    if (consumed == static_cast<std::size_t>(-1) || consumed == static_cast<std::size_t>(-2)) return 1;
    return std::max(0, wcwidth(decoded));
}
std::size_t characters(std::string_view value) {
    const std::string text{value};
    std::size_t width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto end = next(text, i);
        width += cell_width(value.substr(i, end - i));
        i = end;
    }
    return width;
}
std::string clipped(std::string_view value, int limit) {
    const std::string text{value};
    std::string result;
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto end = next(text, i);
        const auto character = value.substr(i, end - i);
        width += cell_width(character);
        if (width > limit) break;
        result += character;
        i = end;
    }
    return result;
}

struct Reference { std::size_t begin, end; std::string path; };
std::vector<Reference> references_in(const std::string& text) {
    std::vector<Reference> result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '@' || (i > 0 && !std::isspace(static_cast<unsigned char>(text[i - 1])))) continue;
        const auto start = i++;
        std::string path;
        if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
            const char quote = text[i++];
            const auto content = i;
            while (i < text.size() && text[i] != quote) ++i;
            path = text.substr(content, i - content);
            if (i < text.size()) ++i;
        } else {
            const auto content = i;
            while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            path = text.substr(content, i - content);
        }
        if (!path.empty()) result.push_back({start, i, path});
        if (i > 0) --i;
    }
    return result;
}
std::optional<Reference> active_reference(const std::string& text, std::size_t cursor) {
    if (text.starts_with('!') || text.starts_with('/')) return std::nullopt;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '@' || (i && !std::isspace(static_cast<unsigned char>(text[i - 1])))) continue;
        const auto begin = i++;
        if (i < text.size() && (text[i] == '\"' || text[i] == '\'')) {
            const auto quote = text[i++];
            while (i < text.size() && text[i] != quote) ++i;
            if (i < text.size()) ++i;
        } else while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (cursor > begin && cursor <= i) return Reference{begin, i, text.substr(begin + 1, cursor - begin - 1)};
        if (i) --i;
    }
    return std::nullopt;
}
std::string style_references(const std::string& text, std::size_t offset = 0,
                             const std::vector<Reference>* references = nullptr) {
    const auto local = references ? std::vector<Reference>{} : references_in(text);
    const auto& spans = references ? *references : local;
    std::string result;
    bool gray = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool reference = std::any_of(spans.begin(), spans.end(), [&](const auto& span) {
            return i + offset >= span.begin && i + offset < span.end;
        });
        if (reference != gray) { result += reference ? muted() + faint() : normal(); gray = reference; }
        result += text[i];
    }
    if (gray) result += normal();
    return result;
}

// Keep code formatting while discarding terminal control bytes from pasted data.
std::string clean_paste(std::string_view text) {
    std::string result;
    bool carriage_return = false;
    for (unsigned char c : text) {
        if (c == '\r') result += '\n';
        else if (c == '\n') { if (!carriage_return) result += '\n'; }
        else if (c == '\t') result += "    ";
        else if (c >= 32 && c != 127) result += static_cast<char>(c);
        carriage_return = c == '\r';
    }
    return result;
}

// Measure styled rows by visible cells, then paint the gray composer in either mode.
std::string composer_row(std::string text, int width) {
    const auto& caps = terminal_capabilities();
    if (caps.shell_style.empty()) return text;
    std::string plain;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != '\033') { plain += text[i++]; continue; }
        ++i;
        if (i < text.size() && text[i] == '[') {
            ++i;
            while (i < text.size()) { const auto c = text[i++]; if (c >= '@' && c <= '~') break; }
        } else {
            if (i < text.size() && (text[i] == '(' || text[i] == ')')) ++i;
            if (i < text.size()) ++i;
        }
    }
    const auto reset_style = normal();
    if (!reset_style.empty()) {
        for (std::size_t at = 0; (at = text.find(reset_style, at)) != std::string::npos;) {
            const auto replacement = reset_style + caps.shell_style;
            text.replace(at, reset_style.size(), replacement);
            at += replacement.size();
        }
    }
    return caps.shell_style + text + std::string(std::max(0, width - static_cast<int>(characters(plain))), ' ') + normal();
}

std::string workspace_path() {
    std::error_code error;
    const auto directory = std::filesystem::current_path(error);
    std::string path = error ? "Directory unavailable" : directory.string();
    if (const char* home = std::getenv("HOME")) {
        const std::string prefix = std::string(home) + "/";
        if (path == home) path = "~";
        else if (path.starts_with(prefix)) path.replace(0, prefix.size(), "~/");
    }
    return path;
}

// Read Git metadata directly: drawing the prompt never launches a shell command.
std::string workspace_branch() {
    static std::filesystem::path cached_directory;
    static std::string cached_branch;
    static auto checked = std::chrono::steady_clock::time_point{};
    std::error_code error;
    auto directory = std::filesystem::current_path(error);
    if (error) return {};
    const auto now = std::chrono::steady_clock::now();
    if (directory == cached_directory && now - checked < std::chrono::seconds(1)) return cached_branch;
    cached_directory = directory;
    cached_branch.clear();
    checked = now;
    auto read_line = [](const std::filesystem::path& path) {
        std::ifstream file(path);
        std::array<char, 4096> buffer{};
        if (!file.getline(buffer.data(), buffer.size())) return std::string{};
        std::string value(buffer.data());
        if (!value.empty() && value.back() == '\r') value.pop_back();
        return value;
    };
    while (!directory.empty()) {
        auto metadata = directory / ".git";
        if (std::filesystem::is_regular_file(metadata, error)) {
            const auto link = read_line(metadata);
            if (!link.starts_with("gitdir: ")) break;
            std::filesystem::path target(link.substr(8));
            metadata = target.is_absolute() ? target : directory / target;
        }
        error.clear();
        if (std::filesystem::is_directory(metadata, error)) {
            auto head = read_line(metadata / "HEAD");
            if (head.starts_with("ref: refs/heads/")) cached_branch = head.substr(16);
            else if (head.size() >= 7 && head.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
                cached_branch = "detached " + head.substr(0, 7);
            break;
        }
        const auto parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
        error.clear();
    }
    for (char& c : cached_branch) if (static_cast<unsigned char>(c) < 32 || c == 127) c = '?';
    return cached_branch;
}

class RawInput {
    termios saved_{};
    bool active_ = false;
public:
    RawInput() {
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) return;
        termios raw = saved_;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        active_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }
    bool active() const { return active_; }
    ~RawInput() {
        if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    }
};

// Command output is retained independently of the bottom composer, so resizing
// and command menus can redraw the viewport without losing transcript lines.
struct SessionState {
    struct sigaction saved_interrupt{}, saved_terminate{};
    std::ostringstream pending;
    std::streambuf* saved_out = std::cout.rdbuf();
    std::streambuf* saved_error = std::cerr.rdbuf();
    std::ostream terminal{saved_out};
    std::string restore_iterm_profile;
    std::string transcript;
    struct Message { std::size_t begin, end; std::string text; };
    std::vector<Message> messages;
    void record_message(const std::string& input, bool queued = false) {
        collect();
        const auto begin = transcript.size();
        transcript += "  " + std::string(prompt_mark()) + input + "\n\n";
        messages.push_back({begin, transcript.size(), input + (queued ? "  [from queue]" : "")});
        wrapped_width = 0;
    }
    struct OutputBlock {
        std::size_t begin, end;
        bool expanded = false;
        std::string command, directory;
        std::time_t finished = 0;
        double seconds = 0;
        int code = 0;
        bool stopped = false;
        bool file = false;
        pid_t process = 0;
    };
    std::vector<OutputBlock> outputs;
    bool collecting_output = false;

    void begin_output(const std::string& command, bool file = false) {
        collect();
        std::error_code error;
        OutputBlock block{transcript.size(), transcript.size()};
        block.command = command;
        block.file = file;
        block.directory = std::filesystem::current_path(error).string();
        if (error) block.directory = "Unavailable";
        outputs.push_back(std::move(block));
        collecting_output = true;
        wrapped_width = 0;
    }
    void end_output(int code, double seconds, bool stopped) {
        collect();
        auto& block = outputs.back();
        block.code = code;
        block.seconds = seconds;
        block.stopped = stopped;
        block.finished = std::time(nullptr);
        collecting_output = false;
        wrapped_width = 0;
    }
    void toggle_output() {
        collect();
        if (outputs.empty()) return;
        outputs.back().expanded = !outputs.back().expanded;
        wrapped_width = 0;
    }
    std::vector<std::string> lines;
    std::vector<std::pair<std::size_t, std::size_t>> control_lines;
    std::vector<std::size_t> block_lines;
    struct DetailTarget { int row; std::size_t output; };
    std::vector<DetailTarget> detail_targets;
    int wrapped_width = 0;
    std::vector<std::string> frame;
    int frame_width = 0;
    bool show_welcome = true;

    void collect() {
        auto text = pending.str();
        if (!text.empty()) {
            transcript += text;
            if (collecting_output) outputs.back().end = transcript.size();
            pending.str("");
            pending.clear();
            wrapped_width = 0;
        }
    }
    static std::vector<std::string> wrap_text(const std::string& text, int width, int continuation_indent = 0) {
        std::vector<std::string> rows;
        std::string line, style;
        width = std::max(1, width);
        // Leave room for a wide character even in a narrow terminal.
        continuation_indent = std::clamp(continuation_indent, 0, std::max(0, width - 2));
        int columns = 0;
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '\033') {
                const auto end = text.find('m', i);
                if (end == std::string::npos) { ++i; continue; }
                const auto sequence = text.substr(i, end - i + 1);
                line += sequence;
                if (sequence == normal()) style.clear();
                else style += sequence;
                i = end + 1;
                continue;
            }
            if (text[i] == '\n') {
                rows.push_back(line);
                line = style;
                columns = 0;
                ++i;
                continue;
            }
            if (static_cast<unsigned char>(text[i]) < 32) { ++i; continue; }
            const auto end = next(text, i);
            const int cells = cell_width(std::string_view(text).substr(i, end - i));
            if (columns + cells > width) {
                rows.push_back(line);
                line = std::string(continuation_indent, ' ') + style;
                columns = continuation_indent;
            }
            line += text.substr(i, end - i);
            columns += cells;
            i = end;
        }
        if (columns > 0) rows.push_back(line);
        return rows;
    }
    void wrap(int width) {
        if (wrapped_width == width) return;
        lines.clear();
        control_lines.clear(); block_lines.clear();
        auto append = [&](const std::vector<std::string>& rows) {
            lines.insert(lines.end(), rows.begin(), rows.end());
        };
        auto append_transcript = [&](std::size_t begin, std::size_t end) {
            for (const auto& message : messages) {
                if (message.begin < begin || message.end > end) continue;
                append(wrap_text(transcript.substr(begin, message.begin - begin), width));
                lines.push_back(composer_row("", width));
                const bool shell = message.text.starts_with('!');
                const auto text = shell ? message.text.substr(1) : message.text;
                const auto rows = wrap_text(shell ? text : style_references(text), std::max(1, width - 4));
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    const auto prefix = i ? std::string("  ") : shell
                        ? terminal_capabilities().shell_marker + "! " + normal() : std::string(prompt_mark());
                    lines.push_back(composer_row(prefix + rows[i], width));
                }
                lines.push_back(composer_row("", width));
                lines.push_back("");
                begin = message.end;
            }
            append(wrap_text(transcript.substr(begin, end - begin), width));
        };
        std::size_t position = 0;
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            const auto& block = outputs[i];
            append_transcript(position, block.begin);
            block_lines.push_back(lines.size());
            const bool running = collecting_output && i + 1 == outputs.size();
            const bool failed = block.code != 0 && !block.stopped;
            const std::string dot = terminal_capabilities().utf8 ? "• " : "* ";
            const std::string label = block.file ? "Opened " : running ? "Started "
                : block.stopped ? "Stopped " : failed ? "Failed " : "Ran ";
            append(wrap_text("  " + muted() + dot + normal()
                + strong() + label + normal() + (block.file ? muted() : terminal_capabilities().context_activity)
                + block.command + normal() + "\n", width, block.file ? 11 : 12));
            const auto rows = wrap_text(transcript.substr(block.begin, block.end - block.begin), std::max(1, width - 6));
            const std::size_t visible = block.expanded ? rows.size() : block.file ? 0 : std::min<std::size_t>(6, rows.size());
            const auto first_row = !block.file && !block.expanded ? rows.size() - visible : 0;
            for (std::size_t row = 0; row < visible; ++row) {
                const auto branch = row == 0 && terminal_capabilities().utf8 ? "  └ " : "    ";
                lines.push_back(muted() + branch + rows[first_row + row] + normal());
            }
            if (rows.empty()) append(wrap_text(muted() + (terminal_capabilities().utf8 ? "  └ " : "    ")
                + (running ? "Waiting for output…" : "(no output)") + normal() + "\n", width));
            const auto details_label = block.expanded ? (terminal_capabilities().utf8 ? "− Show less" : "- Show less") : "+ Show details";
            const auto hidden = block.expanded || rows.size() <= visible ? std::string{} : " / " + std::to_string(rows.size() - visible)
                + (block.file ? " lines" : " earlier lines");
            control_lines.push_back({lines.size(), i});
            const auto detail_rows = wrap_text(muted() + "    " + details_label + hidden
                + (clickable_details() ? " / click or Ctrl+O" : " / Ctrl+O") + normal() + "\n", width, 4);
            // Every wrapped part of the Details label is the same click target.
            for (std::size_t row = 1; row < detail_rows.size(); ++row)
                control_lines.push_back({lines.size() + row, i});
            append(detail_rows);
            if (!running) {
                std::ostringstream result;
                result << "    " << (failed ? error_color() : muted())
                       << (block.stopped ? "Stopped" : failed ? "Failed" : "Completed")
                       << separator() << std::fixed << std::setprecision(1) << block.seconds << 's';
                if (!block.file) result << separator() << "exit " << block.code;
                result << normal() << "\n\n";
                append(wrap_text(result.str(), width, 4));
            }
            position = block.end;
        }
        append_transcript(position, transcript.size());
        wrapped_width = width;
    }
};
std::unique_ptr<SessionState> session;
std::vector<std::string> history;
std::deque<std::string> queued_inputs;
std::string killed_text;
struct InputHistory {
    struct Draft { std::string text; std::size_t cursor; };
    std::vector<Draft> drafts;
    std::size_t index;
    explicit InputHistory(const std::string& input, std::size_t cursor) {
        for (const auto& entry : history) drafts.push_back({entry, entry.size()});
        index = drafts.size();
        drafts.push_back({input, cursor});
    }
    void recall(bool up, std::string& input, std::size_t& cursor) {
        drafts[index] = {input, cursor};
        if (up && index > 0) --index;
        else if (!up && index + 1 < drafts.size()) ++index;
        input = drafts[index].text;
        cursor = drafts[index].cursor;
    }
};

std::size_t word_before(const std::string& text, std::size_t cursor) {
    auto is_word = [&](std::size_t at) { const auto c = static_cast<unsigned char>(text[at]); return c >= 128 || std::isalnum(c); };
    while (cursor > 0 && !is_word(previous(text, cursor))) cursor = previous(text, cursor);
    while (cursor > 0 && is_word(previous(text, cursor))) cursor = previous(text, cursor);
    return cursor;
}
std::size_t word_after(const std::string& text, std::size_t cursor) {
    auto is_word = [&](std::size_t at) { const auto c = static_cast<unsigned char>(text[at]); return c >= 128 || std::isalnum(c); };
    while (cursor < text.size() && !is_word(cursor)) cursor = next(text, cursor);
    while (cursor < text.size() && is_word(cursor)) cursor = next(text, cursor);
    return cursor;
}
// SGR wheel events are distinct from arrow keys, which recall editable inputs.
int wheel_direction(std::string_view sequence) {
    if (!sequence.starts_with("[<") || !sequence.ends_with('M')) return 0;
    const auto end = sequence.find(';', 2);
    if (end == std::string_view::npos) return 0;
    unsigned button = 0;
    const auto parsed = std::from_chars(sequence.data() + 2, sequence.data() + end, button);
    if (parsed.ec != std::errc{} || parsed.ptr != sequence.data() + end) return 0;
    const auto base = button & 0x43;
    return base == 64 ? -1 : base == 65 ? 1 : 0;
}
bool wheel_reporting() {
    const auto* mode = std::getenv("AURA_MOUSE");
    return !mode || std::string_view(mode) == "wheel" || std::string_view(mode) == "click";
}
std::optional<std::size_t> clicked_details(std::string_view sequence) {
    if (!clickable_details() || !session
        || !sequence.starts_with("[<") || !sequence.ends_with('M')) return std::nullopt;
    int values[3]{};
    std::size_t start = 2;
    for (int i = 0; i < 3; ++i) {
        const auto end = i == 2 ? sequence.size() - 1 : sequence.find(';', start);
        if (end == std::string_view::npos) return std::nullopt;
        const auto parsed = std::from_chars(sequence.data() + start, sequence.data() + end, values[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != sequence.data() + end) return std::nullopt;
        start = end + 1;
    }
    if (values[0] != 0 || values[1] < 3) return std::nullopt;
    for (const auto& target : session->detail_targets) {
        if (target.row != values[2] - 1) continue;
        session->outputs[target.output].expanded = !session->outputs[target.output].expanded;
        session->wrapped_width = 0;
        return target.output;
    }
    return std::nullopt;
}

std::string suspended_draft;
std::size_t suspended_cursor = 0;
struct Suggestion { std::string name, description; bool directory = false; };
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }

class Composer {
    std::size_t scroll_offset_ = 0;
    int page_size_ = 1;
public:
    void scroll(bool up, int lines = 0) {
        const auto amount = static_cast<std::size_t>(lines > 0 ? lines : page_size_);
        if (up) scroll_offset_ += amount;
        else scroll_offset_ = scroll_offset_ > amount ? scroll_offset_ - amount : 0;
    }
    void follow_output() { scroll_offset_ = 0; }
    void reveal_output(std::size_t index) {
        session->wrap(std::max(1, terminal_width() - 1));
        if (index >= session->block_lines.size()) return;
        const auto maximum = session->lines.size() > static_cast<std::size_t>(page_size_)
            ? session->lines.size() - page_size_ : 0;
        scroll_offset_ = maximum > session->block_lines[index] ? maximum - session->block_lines[index] : 0;
    }
    void paint(const std::vector<std::string>& frame, int width, int row, int column) {
        const auto& caps = terminal_capabilities();
        auto& out = session->terminal;
        out << caps.hide_cursor;
        // Resizing may reflow even unchanged text. The old frame is no longer
        // a reliable picture of the screen, so repaint it completely once.
        const bool resized = session->frame.size() != frame.size() || session->frame_width != width;
        if (resized) out << caps.erase_screen;
        for (std::size_t i = 0; i < frame.size(); ++i) {
            if (resized || session->frame[i] != frame[i])
                out << caps.move_to(static_cast<int>(i), 0) << normal() << caps.erase_line << frame[i] << normal();
        }
        session->frame = frame;
        session->frame_width = width;
        out << caps.move_to(std::clamp(row, 0, static_cast<int>(frame.size()) - 1),
                            std::clamp(column, 0, width - 1)) << caps.show_cursor << std::flush;
    }
    void draw(const std::string& input, std::size_t cursor,
              const std::vector<Suggestion>& matches, std::size_t selected,
              const std::string& activity = "", int tick = 0, bool stopping = false, int file_filter = 0, bool suggestions_open = true, const std::string& hint = "", const std::string& voice_banner = "") {
        session->collect();
        const int width = std::max(1, terminal_width() - 1);
        const int height = terminal_height();
        const bool shell_mode = input.starts_with('!');
        const std::string editor_input = shell_mode ? input.substr(1) : input;
        const auto editor_cursor = shell_mode && cursor > 0 ? cursor - 1 : cursor;
        session->wrap(width);
        session->detail_targets.clear();
        // Small panes still get a visible editor. Omit chrome before sacrificing
        // the prompt or addressing cells outside the actual terminal dimensions.
        if (height < 6 || width < 20) {
            std::vector<std::string> frame(height);
            const int prefix = width >= 3 ? 2 : 0;
            const int available = std::max(1, width - prefix - 1);
            std::size_t start = editor_cursor;
            int left_cells = 0;
            while (start > 0) {
                const auto before = previous(editor_input, start);
                const int cells = cell_width(std::string_view(editor_input).substr(before, start - before));
                if (left_cells + cells > available) break;
                left_cells += cells;
                start = before;
            }
            page_size_ = std::max(1, height - 1);
            const auto maximum = session->lines.size() > static_cast<std::size_t>(page_size_)
                ? session->lines.size() - page_size_ : 0;
            scroll_offset_ = std::min(scroll_offset_, maximum);
            for (int row = 0; row < height - 1; ++row) {
                const auto index = maximum - scroll_offset_ + row;
                if (index < session->lines.size()) {
                    frame[row] = session->lines[index];
                    for (const auto& [control, output] : session->control_lines)
                        if (control == index) session->detail_targets.push_back({row, output});
                }
            }
            frame.back() = (prefix ? (shell_mode ? "! " : std::string(prompt_mark())) : "")
                + clipped(std::string_view(editor_input).substr(start), std::max(0, width - prefix));
            frame.back() = composer_row(frame.back(), width);
            paint(frame, width, height - 1, prefix + left_cells);
            return;
        }
        // Wrap the draft into terminal cells, retaining the editor_cursor's row/column.
        // The composer grows upward to six lines; its bottom edge never moves.
        const int draft_width = std::max(1, width - 4);
        std::vector<std::string> draft_lines(1);
        std::vector<std::size_t> draft_offsets{0};
        int draft_column = 0, cursor_row = 0, cursor_column = 0;
        for (std::size_t i = 0; i <= editor_input.size();) {
            if (draft_column >= draft_width) {
                draft_lines.emplace_back();
                draft_offsets.push_back(i);
                draft_column = 0;
            }
            if (i == editor_cursor) {
                cursor_row = static_cast<int>(draft_lines.size()) - 1;
                cursor_column = draft_column;
            }
            if (i == editor_input.size()) break;
            if (editor_input[i] == '\n') {
                draft_lines.emplace_back();
                draft_offsets.push_back(++i);
                draft_column = 0;
                continue;
            }
            const auto end = next(editor_input, i);
            const auto character = std::string_view(editor_input).substr(i, end - i);
            const int cells = cell_width(character);
            if (draft_column + cells > draft_width) {
                draft_lines.emplace_back();
                draft_offsets.push_back(i);
                draft_column = 0;
                if (i == editor_cursor) {
                    cursor_row = static_cast<int>(draft_lines.size()) - 1;
                    cursor_column = 0;
                }
            }
            draft_lines.back() += character;
            draft_column += cells;
            i = end;
        }
        const auto references = shell_mode ? std::vector<Reference>{} : references_in(editor_input);
        std::vector<std::string> styled_draft;
        for (std::size_t row = 0; row < draft_lines.size(); ++row)
            styled_draft.push_back(style_references(draft_lines[row], draft_offsets[row], &references));
        const int header_rows = height >= 10 && !session->show_welcome ? 2 : 0;
        const int visible_draft = std::min(static_cast<int>(draft_lines.size()),
                                         std::max(1, std::min(6, height - header_rows - 6)));
        const int draft_first = std::clamp(cursor_row - visible_draft + 1, 0,
                                           static_cast<int>(draft_lines.size()) - visible_draft);
        const int composer_rows = std::min(height - 1, visible_draft + 4);
        const bool file_menu = active_reference(input, cursor).has_value();
        const bool panel_open = input == "?" || !matches.empty() || (file_menu && suggestions_open);
        const int menu_heading = file_menu ? 4 : 3;
        const auto available = std::max(0, height - header_rows - composer_rows - menu_heading - 2);
        const std::size_t count = std::min(matches.size(), static_cast<std::size_t>(std::min(10, available)));
        const int shortcut_rows = input == "?" ? std::min(10, std::max(0, height - header_rows - composer_rows - 2)) : 0;
        const int menu_rows = shortcut_rows > 0 ? shortcut_rows
            : (panel_open ? std::min(height - header_rows - composer_rows - 1, menu_heading + std::max(1, static_cast<int>(count))) : 0);
        const int output_rows = std::max(1, height - header_rows - composer_rows - menu_rows);
        page_size_ = output_rows;
        const auto total = session->lines.size();
        const auto maximum = total > static_cast<std::size_t>(output_rows) ? total - output_rows : 0;
        scroll_offset_ = std::min(scroll_offset_, maximum);
        const auto first_line = maximum - scroll_offset_;
        std::vector<std::string> frame(height);
        auto line = [&](int row, const std::string& text) {
            if (row >= 0 && row < height) frame[row] = text;
        };
        if (header_rows > 0) {
            const std::string title = "  AURA  /  " + std::string(version);
            const std::string status = !voice_banner.empty() ? "dictation  " : activity.empty() ? "ready  " : (stopping ? "stopping  " : "working  ");
            std::string heading = accent() + strong() + clipped(title, width) + std::string(normal());
            if (width >= 40) heading += std::string(width - characters(title) - characters(status), ' ') + std::string(faint()) + status;
            line(0, heading);
            line(1, std::string(faint()) + clipped("  " + workspace_path(), width));
        }
        for (int row = 0; row < output_rows; ++row) {
            if (first_line + row < total) {
                line(header_rows + row, session->lines[first_line + row]);
                for (const auto& [control, output] : session->control_lines)
                    if (control == first_line + row) session->detail_targets.push_back({header_rows + row, output});
            }
        }
        if (session->show_welcome && output_rows >= 5) {
            const int start = header_rows + (output_rows >= 12 ? 1 : 0);
            const int card_width = std::min(66, width - 4);
            const bool rounded = terminal_capabilities().utf8 && card_width >= 28 && output_rows >= 8;
            if (rounded) {
                const std::string label = " Aura CLI  v" + std::string(version) + " ";
                std::string top = "  ╭";
                top += "─";
                top += label;
                for (int i = 0; i < card_width - 3 - static_cast<int>(characters(label)); ++i) top += "─";
                top += "╮";
                line(start, accent() + top);
                auto card_row = [&](int row, std::string_view key, std::string_view value) {
                    const int inner = card_width - 4;
                    const auto content = clipped(std::string(key) + std::string(value), inner);
                    const auto key_text = clipped(key, inner);
                    const auto value_text = clipped(value, std::max(0, inner - static_cast<int>(characters(key_text))));
                    line(row, accent() + "  │ " + normal() + faint() + key_text + normal() + value_text
                        + std::string(std::max(0, inner - static_cast<int>(characters(content))), ' ') + accent() + " │");
                };
                card_row(start + 1, "", "");
                card_row(start + 2, "workspace  ", workspace_path());
                card_row(start + 3, "tools      ", "Shell, files, commands");
                card_row(start + 4, "session    ", "Local / no model connected");
                std::string bottom = "  ╰";
                for (int i = 0; i < card_width - 2; ++i) bottom += "─";
                line(start + 5, accent() + bottom + "╯");
                if (output_rows >= 10) {
                    line(start + 7, strong() + "  Start with " + accent() + "/" + normal() + " commands, " + accent() + "!" + normal()
                        + " shell, " + accent() + "@" + normal() + " files, or " + accent() + "?" + normal() + " shortcuts.");
                    line(start + 8, faint() + "  Tip: !git status runs here. @ browses your workspace.");
                }
            } else {
                line(start, accent() + strong() + "  Aura CLI" + normal() + faint() + "  v" + version);
                line(start + 2, faint() + clipped("  " + workspace_path(), width));
                line(start + 3, faint() + "  Local tools / no model connected");
                line(start + 4, accent() + "  /" + normal() + " commands   " + accent() + "!" + normal() + " shell   " + accent() + "@" + normal() + " files   " + accent() + "?" + normal() + " shortcuts");
            }
        }
        if (menu_rows > 0 && shortcut_rows == 0) {
            int row = header_rows + output_rows;
            const bool files = file_menu;
            line(row++, strong() + clipped(files ? "  Mentions" : "  Commands", width));
            line(row++, faint() + clipped(files ? "  Find files and directories in your workspace" : "  Search local commands", width));
            if (files) {
                const std::array<std::string_view, 3> tabs{" All ", " Files ", " Directories "};
                std::string tab_line = "  ";
                for (int i = 0; i < 3; ++i) tab_line += (i == file_filter ? inverse() : faint()) + std::string(tabs[i]) + normal() + "  ";
                line(row++, tab_line);
            }
            ++row;
            if (matches.empty()) line(row, faint() + "    No matching entries in this filter.");
            const std::size_t first = selected >= count ? selected - count + 1 : 0;
            const int name_column = std::clamp(width / 3, 16, 32);
            for (std::size_t i = first; i < first + count; ++i) {
                const std::string prefix = i == selected ? "  " + std::string(prompt_mark()) : "    ";
                const auto display_name = files && matches[i].name.starts_with('@') ? matches[i].name.substr(1) : matches[i].name;
                const auto label = clipped(prefix + display_name, name_column - 2);
                const auto description = clipped(matches[i].description, std::max(0, width - name_column));
                const auto padding = std::string(std::max(2, name_column - static_cast<int>(characters(label))), ' ');
                std::string text;
                if (i == selected) text = inverse() + label + padding + description
                    + std::string(std::max(0, width - name_column - static_cast<int>(characters(description))), ' ') + normal();
                else text = accent() + label + normal() + padding + faint() + description;
                line(row++, text);
            }
        } else if (shortcut_rows > 0) {
            const int start = header_rows + output_rows;
            auto panel_line = [&](int offset, const std::string& text) {
                if (offset < shortcut_rows) line(start + offset, text);
            };
            panel_line(0, strong() + "  Keyboard shortcuts");
            const int columns = width >= 108 ? 3 : width >= 72 ? 2 : 1;
            const int cell = (width - 4) / columns;
            const std::array<std::string_view, 3> titles{"Compose", "Session", "Output"};
            using Key = std::pair<std::string_view, std::string_view>;
            const std::array<std::array<Key, 5>, 3> groups{{
                {{{"/", "Commands"}, {"@", "Mention files"}, {"!", "Shell command"}, {"Tab", "Complete"}, {"Esc", "Close menu"}}},
                {{{"Up/Down", "Input history"}, {"Ctrl+C", "Stop / cancel"}, {"Ctrl+D", "Exit"}, {"Ctrl+L", "Clear screen"}, {"Tab/Enter", "Send draft"}}},
                {{{"PgUp/PgDn", "Scroll pages"}, {"Ctrl+R", "Search history"}, {"Ctrl+O", "Output details"}, {"/clear", "Fresh screen"}, {"Left Arrow", "Center (empty draft)"}}}
            }};
            std::string headings = "  ";
            for (int column = 0; column < columns; ++column)
                headings += strong() + std::string(titles[column]) + normal() + std::string(std::max(0, cell - static_cast<int>(titles[column].size())), ' ');
            panel_line(2, headings);
            for (int row = 0; row < 5; ++row) {
                std::string text = "  ";
                for (int column = 0; column < columns; ++column) {
                    const auto& [key, description] = groups[column][row];
                    const int gap = std::max(2, 12 - static_cast<int>(key.size()));
                    const auto detail = clipped(description, std::max(0, cell - static_cast<int>(key.size()) - gap - 2));
                    text += accent() + std::string(key) + normal() + std::string(gap, ' ') + detail;
                    text += std::string(std::max(0, cell - static_cast<int>(key.size()) - gap - static_cast<int>(characters(detail))), ' ');
                }
                panel_line(3 + row, text);
            }
            panel_line(9, faint() + (columns < 3 ? "  Resize for more shortcuts / Enter for the full guide" : "  Enter full guide / Esc close"));
        } else if (input.starts_with('/') && input.find(' ') == std::string::npos && std::none_of(commands.begin(), commands.end(), [&](const Command& command) { return command.name.starts_with(input); })) {
            line(header_rows + output_rows - 1, std::string(faint()) + clipped("  No matching command. /help lists available commands.", width));
        }
        const int top = height - composer_rows;
        const bool has_rules = composer_rows >= 4;
        const int input_row = top + (has_rules ? 1 : 0);
        if (has_rules) {
            // Keep task activity out of the composer. The running command,
            // streamed output, and status remain visible in the transcript/footer.
            if (!voice_banner.empty()) line(top, voice_banner);
        }
        for (int row = 0; row < visible_draft; ++row) {
            std::string text = row + draft_first == 0
                ? (shell_mode ? terminal_capabilities().shell_marker + "! " + normal() : std::string(prompt_mark())) : "  ";
            if (editor_input.empty()) text += muted() + faint() + clipped(shell_mode ? "Type a command, e.g. pwd" : activity.empty()
                ? "What would you like to do?" : "Type next input / Tab to queue", draft_width);
            else {
                const bool command_mode = input.starts_with('/') || input == "?";
                text += (command_mode ? accent() + strong() : "") + styled_draft[draft_first + row] + normal();
            }
            line(input_row + row, text);
        }
        // Keep useful local context below the editor, outside its shell background.
        std::string context = "  ";
        int context_cells = 2;
        auto context_item = [&](const std::string& value, const std::string& style, int maximum) {
            const std::string gap = context_cells > 2 ? std::string(separator()) : "";
            const int remaining = width - context_cells - static_cast<int>(characters(gap));
            if (remaining < 4) return;
            const auto text = clipped(value, std::min(remaining, maximum));
            if (text.empty()) return;
            context += faint() + gap + normal() + style + text + normal();
            context_cells += static_cast<int>(characters(gap) + characters(text));
        };
        context_item(!voice_banner.empty() ? "Voice" : !activity.empty() ? (stopping ? "Stopping" : "Running") : shell_mode ? "Shell" : "Ready",
                     strong() + terminal_capabilities().context_mode, 8);
        if (!activity.empty()) context_item(std::to_string(tick / 10) + "s", muted(), 12);
        if (voice_banner.empty() && !activity.empty() && !session->outputs.empty() && session->outputs.back().process > 0)
            context_item("PID " + std::to_string(session->outputs.back().process), muted(), 18);
        if (!queued_inputs.empty()) context_item(std::to_string(queued_inputs.size()) + " queued",
                                               terminal_capabilities().context_activity, 20);
        std::size_t done = 0, failed = 0, stopped = 0;
        for (const auto& output : session->outputs) {
            if (!output.finished) continue;
            if (output.stopped) ++stopped;
            else if (output.code != 0) ++failed;
            else ++done;
        }
        if (failed) context_item(std::to_string(failed) + " failed", error_color(), 20);
        if (done) context_item(std::to_string(done) + " done", muted(), 20);
        if (stopped) context_item(std::to_string(stopped) + " stopped", muted(), 20);
        const auto branch = workspace_branch();
        if (!branch.empty()) context_item(branch, muted(), 32);
        line(height - 2, context);
        std::string footer;
        bool show_legend = false;
        if (!hint.empty()) footer = "  " + hint;
        else if (!activity.empty()) {
            std::string latest;
            if (!session->outputs.empty()) {
                const auto& block = session->outputs.back();
                const auto end = session->transcript.find_last_not_of("\r\n \t", block.end ? block.end - 1 : 0);
                if (end != std::string::npos && end >= block.begin && end < block.end) {
                    const auto newline = session->transcript.rfind('\n', end);
                    const auto begin = std::max(block.begin, newline == std::string::npos ? 0 : newline + 1);
                    latest = session->transcript.substr(begin, end - begin + 1);
                }
            }
            const std::string controls = "   Tab queue / Ctrl+C stop";
            footer = "  " + clipped(latest.empty() ? "No output yet" : "Output: " + latest,
                                     std::max(1, width - static_cast<int>(controls.size()) - 2)) + controls;
        }
        else if (input.starts_with('!')) footer = input == "!"
            ? "  Try !pwd to show this folder   Enter run   Esc cancel"
            : "  Enter / Tab run shell   Esc cancel";
        else if (input == "?") footer = "  Shortcuts   Enter full guide   Esc close";
        else if (scroll_offset_ > 0) footer = "  History" + std::string(separator()) + std::to_string(scroll_offset_) + " lines above latest" + std::string(separator()) + "PgDn to return";
        else if (!matches.empty()) footer = file_menu
            ? "  Enter/Tab insert   Esc close   Up/Down select   Left/Right filter"
            : "  Up/Down choose   Tab complete   Enter run   Esc dismiss";
        else {
            show_legend = true;
            const std::string center_key = terminal_capabilities().utf8 ? "←" : "Left";
            const std::string center_hint = input.empty() ? "  " + center_key + " Center   " : "  ";
            const std::string left = center_hint + (width >= 90
                ? "/ Commands   ! Run shell   @ Browse files   ? Shortcuts"
                : width >= 68 ? "/ Commands   ! Shell   @ Files   ? Shortcuts"
                : "/ commands  ! shell  @ files  ? help");
            const std::string right = "Tab / Enter send  ";
            const int space = width - static_cast<int>(characters(left) + characters(right));
            if (width < static_cast<int>(characters(left))) footer = "  Type ? to see all shortcuts";
            else footer = left + (space >= 3 ? std::string(space, ' ') + right : "");
        }
        auto footer_text = clipped(footer, width);
        if (show_legend) {
            // Measure plain text first, then separate white icons from gray labels.
            const std::string center_key = terminal_capabilities().utf8 ? "←" : "Left";
            for (const std::string key : {center_key, std::string("/"), std::string("!"), std::string("@"), std::string("?")}) {
                const auto at = footer_text.find(key);
                if (at != std::string::npos)
                    footer_text.replace(at, key.size(), normal() + terminal_capabilities().white_color + strong() + key + normal() + muted());
            }
            line(height - 1, muted() + footer_text + normal());
        } else line(height - 1, faint() + footer_text);

        for (int row = top; row < height - 2; ++row) frame[row] = composer_row(frame[row], width);
        paint(frame, width, input_row + cursor_row - draft_first, cursor_column + 2);
    }
};

// Reverse search recalls a line for editing; it never executes the match.
void search_input_history(std::string& input, std::size_t& cursor) {
    const auto saved = input;
    const auto saved_cursor = cursor;
    std::string query;
    std::size_t match = history.size();
    bool found = false;
    Composer screen;
    auto find = [&](std::size_t before) {
        found = false;
        for (auto i = before; i > 0; --i) {
            if (history[i - 1].find(query) == std::string::npos) continue;
            match = i - 1;
            input = history[match]; cursor = input.size();
            found = true;
            break;
        }
    };
    find(history.size());
    while (!stop_requested) {
        const auto hint = std::string(found ? "Search: " : "No match: ") + query + " / Ctrl+R older / Enter edit / Esc cancel";
        screen.draw(input, cursor, {}, 0, "", 0, false, 0, false, hint);
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        if (poll(&descriptor, 1, 150) <= 0) continue;
        unsigned char key = 0;
        if (read(STDIN_FILENO, &key, 1) != 1) break;
        if (key == '\r' || key == '\n') { if (found) return; break; }
        if (key == 3 || key == 7) break;
        if (key == 27) {
            std::string sequence;
            char byte = 0;
            while (sequence.size() < 32 && poll(&descriptor, 1, 40) > 0 && read(STDIN_FILENO, &byte, 1) == 1) {
                sequence += byte;
                if (sequence.size() > 1 && byte >= '@' && byte <= '~') break;
            }
            if (sequence.starts_with("[<")) {
                const auto direction = wheel_direction(sequence);
                if (direction) screen.scroll(direction < 0, 3);
                else if (const auto output = clicked_details(sequence)) screen.reveal_output(*output);
                continue;
            }
            if (sequence == "[200~") {
                std::string paste, tail;
                while (!stop_requested) {
                    const auto ready = poll(&descriptor, 1, 150);
                    if (ready < 0 && errno != EINTR) break;
                    if (ready <= 0) continue;
                    if (read(STDIN_FILENO, &byte, 1) != 1) break;
                    tail += byte;
                    if (tail.size() > 6) tail.erase(0, tail.size() - 6);
                    if (paste.size() < 65542) paste += byte;
                    if (tail == "\033[201~") {
                        if (paste.ends_with(tail)) paste.resize(paste.size() - 6);
                        break;
                    }
                }
                for (unsigned char c : paste) if (c >= 32 && c != 127) query += static_cast<char>(c);
                find(history.size());
                continue;
            }
            break;
        }
        if (key == 18) find(found ? match : history.size());
        else if (key == 127 || key == 8) { query.erase(previous(query, query.size())); find(history.size()); }
        else if (key >= 32) {
            std::string character(1, static_cast<char>(key));
            const int length = key >= 0xf0 ? 4 : key >= 0xe0 ? 3 : key >= 0xc0 ? 2 : 1;
            for (int i = 1; i < length; ++i) {
                char byte = 0;
                if (poll(&descriptor, 1, 100) <= 0 || read(STDIN_FILENO, &byte, 1) != 1) break;
                character += byte;
            }
            query += character;
            find(history.size());
        }
    }
    input = saved; cursor = saved_cursor;
}

std::vector<Suggestion> matching(const std::string& input, std::size_t cursor, bool dismissed, int file_filter) {
    std::vector<Suggestion> result;
    if (dismissed) return result;
    if (input.starts_with('/') && input.find(' ') == std::string::npos) {
        for (const auto& command : commands)
            if (command.name.starts_with(input)) result.push_back({std::string(command.name), std::string(command.description)});
    } else if (const auto reference = active_reference(input, cursor)) {
        std::string query = reference->path;
        if (query.starts_with('"')) query.erase(0, 1);
        if (query.ends_with('"')) query.pop_back();
        const auto slash = query.find_last_of('/');
        const auto prefix = slash == std::string::npos ? std::string{} : query.substr(0, slash + 1);
        const auto leaf = slash == std::string::npos ? query : query.substr(slash + 1);
        std::error_code error;
        auto directory = prefix.empty() ? std::string(".") : prefix;
        if (directory.starts_with("~/"))
            if (const auto* home = std::getenv("HOME")) directory.replace(0, 1, home);
        std::filesystem::directory_iterator entries(directory, error);
        std::size_t inspected = 0;
        for (std::filesystem::directory_iterator end; !error && entries != end; entries.increment(error)) {
            if (++inspected > 4096) break;
            const auto& entry = *entries;
            const auto name = entry.path().filename().string();
            if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; })) continue;
            if (!name.starts_with(leaf) || (name.starts_with('.') && !leaf.starts_with('.'))) continue;
            // Bound filesystem work and menu size even in enormous directories.
            if (result.size() >= 200) break;
            const bool directory = entry.is_directory(error);
            error.clear();
            if ((file_filter == 1 && directory) || (file_filter == 2 && !directory)) continue;
            std::string path = prefix + name + (directory ? "/" : "");
            if (path.find(' ') != std::string::npos) path = "\"" + path + "\"";
            result.push_back({"@" + path, directory ? "Browse directory" : "Insert file reference", directory});
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.directory != b.directory ? a.directory : a.name < b.name; });
    }
    return result;
}
} // namespace

TerminalSession::TerminalSession() {
    std::setlocale(LC_CTYPE, "");
    if (!styled()) return;
    session = std::make_unique<SessionState>();
    stop_requested = 0;
    struct sigaction action{};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &session->saved_interrupt);
    sigaction(SIGTERM, &action, &session->saved_terminate);
    // Never translate wheel gestures into arrow keys: arrows edit history.
    // Enable SGR wheel and Details click reports for ordinary launches too.
    const auto& caps = terminal_capabilities();
    const auto* terminal_program = std::getenv("TERM_PROGRAM");
    if (wheel_reporting() && terminal_program && std::string_view(terminal_program) == "iTerm.app") {
        // These are session overrides, not changes to saved or global profiles.
        // Mouse reporting must be permitted before enabling the terminal modes;
        // otherwise iTerm2 may deliver wheel gestures as indistinguishable arrows.
        if (const auto* profile = std::getenv("ITERM_PROFILE")) {
            const std::string name(profile);
            if (std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                session->restore_iterm_profile = name;
        }
        if (!std::getenv("NO_COLOR"))
            session->terminal << "\033]1337;SetColors=selbg=c4c4cc\a\033]1337;SetColors=selfg=171719\a";
        session->terminal << "\033]1337;SetProfileProperty=Mouse Reporting=dHJ1ZQ=="
                          << ";Mouse Reporting allow mouse wheel=dHJ1ZQ=="
                          << ";Mouse Reporting allow clicks and drags=" << (clickable_details() ? "dHJ1ZQ==" : "ZmFsc2U=")
                          << ";Allow Alternate Mouse Scroll=ZmFsc2U="
                          << ";Automatically Enable Alternate Mouse Scroll=ZmFsc2U="
                          << "\a";
    }
    session->terminal << caps.enter_screen << "\033[?2004h"
                      << "\033[?1000l\033[?1002l\033[?1003l\033[?1006l\033[?1007l"
                      << (wheel_reporting() ? "\033[?1000h\033[?1006h" : "")
                      << caps.erase_screen << std::flush;
    std::cout.rdbuf(session->pending.rdbuf());
    std::cerr.rdbuf(session->pending.rdbuf());
}

TerminalSession::~TerminalSession() {
    if (!session) return;
    std::cout.rdbuf(session->saved_out);
    std::cerr.rdbuf(session->saved_error);
    const auto& caps = terminal_capabilities();
    session->terminal << normal() << "\033[?1000l\033[?1006l\033[?1007l\033[?2004l"
                      << caps.show_cursor << caps.leave_screen << std::flush;
    if (!session->restore_iterm_profile.empty())
        session->terminal << "\033]1337;SetProfile=" << session->restore_iterm_profile << '\a' << std::flush;
    sigaction(SIGINT, &session->saved_interrupt, nullptr);
    sigaction(SIGTERM, &session->saved_terminate, nullptr);
    session.reset();
}

std::vector<std::string> file_references(const std::string& input) {
    std::vector<std::string> result;
    for (const auto& reference : references_in(input)) result.push_back(reference.path);
    return result;
}
FilePreview::FilePreview(const std::string& path) {
    if (session) { session->begin_output(path, true); active_ = true; }
    else std::cout << "  File  " << path << "\n\n";
}
FilePreview::~FilePreview() {
    if (active_ && session) session->end_output(0, 0, false);
}

void print_error(const std::string& message) {
    std::cerr << "  " << (styled() ? error_color() : "") << "! " << message
              << (styled() ? normal() : "") << '\n';
}

void clear_transcript() {
    if (session) {
        session->pending.str("");
        session->pending.clear();
        session->transcript.clear();
        session->messages.clear();
        session->outputs.clear();
        session->control_lines.clear(); session->block_lines.clear(); session->detail_targets.clear();
        session->collecting_output = false;
        session->lines.clear();
        session->wrapped_width = 0;
    }
}

void edit_joint_controls(JointControls controls,
                         const std::function<std::string(const JointControls&)>& save,
                         const std::function<std::optional<ControlObservation>()>& observe) {
    std::string status = "Targets are in radians.";
    bool failed = false;
    auto apply = [&](JointControls candidate) {
        bool anyClamped = false;
        for (auto& joint : candidate) {
            if (!std::isfinite(joint.value)) {
                status = "Enter a finite number.";
                failed = true;
                return false;
            }
            joint.clamped = joint.value < joint.minimum || joint.value > joint.maximum;
            anyClamped = anyClamped || joint.clamped;
            joint.value = std::clamp(joint.value, joint.minimum, joint.maximum);
        }
        const auto error = save(candidate);
        failed = !error.empty();
        status = failed ? error : anyClamped
            ? "Limit reached; target was clamped and applied."
            : "Applied to viewer_action.json.";
        if (!failed) controls = std::move(candidate);
        return !failed;
    };
    if (!session) {
        // Redirected input and terminals without cursor addressing keep a usable fallback.
        while (true) {
            const auto live = observe();
            std::cout << "\n  AURA / Control (radians)\n";
            if (!live) std::cout << "  o Waiting for viewer data\n";
            else if (live->age_seconds > 0.5) std::cout << "  ! Viewer data is stale\n";
            else std::cout << "  * Live  pelvis " << std::fixed << std::setprecision(2)
                           << live->pelvis_height << " m  "
                           << (live->is_standing ? "standing" : "fallen") << '\n';
            for (std::size_t i = 0; i < controls.size(); ++i) {
                std::cout << "  " << i + 1 << "  " << std::left << std::setw(12) << controls[i].name
                          << std::right << " actual ";
                if (live && live->age_seconds <= 0.5)
                    std::cout << std::fixed << std::setprecision(3) << live->joint_angles[i];
                else std::cout << "--";
                std::cout << "  target " << std::fixed << std::setprecision(3) << controls[i].value
                          << (controls[i].clamped ? "  ! LIMIT" : "")
                          << "  [" << controls[i].minimum << ", " << controls[i].maximum << "]\n";
            }
            std::cout << "  " << status << "\n  Enter <row> <value>, reset, or q: " << std::flush;
            std::string input;
            if (!std::getline(std::cin, input) || input == "q" || input == "quit") return;
            auto candidate = controls;
            if (input == "reset") {
                for (auto& joint : candidate) joint.value = 0.0;
            } else {
                std::istringstream fields(input);
                int row = 0;
                double value = 0.0;
                std::string extra;
                if (!(fields >> row >> value) || (fields >> extra) || row < 1 || row > 4) {
                    status = "Use a row from 1 to 4 and one finite value.";
                    continue;
                }
                candidate[row - 1].value = value;
            }
            apply(candidate);
        }
    }

    session->collect();
    RawInput raw;
    if (!raw.active()) { print_error("Could not open joint controls."); return; }
    Composer screen;
    std::size_t selected = 0;
    constexpr std::array<double, 3> stepSizes{0.01, 0.05, 0.20};
    std::size_t stepIndex = 1;
    double stepSize = stepSizes[stepIndex];
    bool editing = false;
    std::string draft;
    double previewYaw = 0.0;
    auto previousPreview = std::chrono::steady_clock::now();
    while (!stop_requested) {
        const int width = std::max(1, terminal_width() - 1);
        const int height = std::max(1, terminal_height());
        std::vector<std::string> frame(height);
        const bool wide = width >= 120 && height >= 28;
        std::vector<int> occupiedColumns(height, 0);
        auto put = [&](int row, const std::string& text, const std::string& style = "") {
            if (row >= 0 && row < height) {
                const auto visible = clipped(text, wide ? 76 : width);
                frame[row] = style + visible + normal();
                occupiedColumns[row] = static_cast<int>(characters(visible));
            }
        };
        const auto observation = observe();
        const bool compact = width < 76 || height < 17;
        const bool utf8 = terminal_capabilities().utf8;
        const auto up = utf8 ? "↑" : "^";
        const auto down = utf8 ? "↓" : "v";
        const auto left = utf8 ? "←" : "<";
        const auto right = utf8 ? "→" : ">";
        const auto liveIcon = utf8 ? "●" : "*";
        const auto waitingIcon = utf8 ? "◌" : "o";
        std::ostringstream stepLabel;
        stepLabel << std::fixed << std::setprecision(2) << stepSize;
        put(1, "  AURA  /  CONTROL CENTER", strong());
        if (compact) {
            put(3, "  Enlarge terminal to 77 columns / 17 rows.");
            put(height - 1, "  Esc / q: back");
        } else {
            if (!observation)
                put(3, "  " + std::string(waitingIcon) + " WAITING FOR VIEWER DATA", faint());
            else if (observation->age_seconds > 0.5)
                put(3, "  ! STALE   viewer data is " + std::to_string(observation->age_seconds).substr(0, 4)
                           + "s old", error_color());
            else put(3, "  " + std::string(liveIcon) + " LIVE / viewer connected", strong());
            put(5, "    Joint          Actual    Target     Error       Range position", faint());
            for (std::size_t i = 0; i < controls.size(); ++i) {
                const auto& joint = controls[i];
                std::ostringstream row;
                row << (i == selected ? (utf8 ? "  ▸ " : "  > ") : "    ")
                    << std::left << std::setw(14) << joint.name << std::right;
                const bool live = observation && observation->age_seconds <= 0.5;
                const double actual = live ? observation->joint_angles[i] : joint.value;
                row << std::fixed << std::showpos << std::setprecision(2)
                    << std::setw(7) << (live ? actual : 0.0) << " "
                    << std::setw(7) << joint.value << " "
                    << std::setw(7) << (live ? joint.value - actual : 0.0) << std::noshowpos << "  ";
                if (!live) row << "-- waiting --";
                else {
                    constexpr int segments = 10;
                    const double fraction = std::clamp(
                        (actual - joint.minimum) / (joint.maximum - joint.minimum), 0.0, 1.0);
                    const int marker = static_cast<int>(std::lround(fraction * segments));
                    row << '[';
                    for (int tick = 0; tick <= segments; ++tick) {
                        if (tick == marker) row << (utf8 ? "●" : "|");
                        else if (utf8) row << "─";
                        else row << '-';
                    }
                    row << "] " << std::fixed << std::showpos << std::setprecision(1)
                        << joint.minimum << ".." << joint.maximum << std::noshowpos;
                    const double margin = 0.05 * (joint.maximum - joint.minimum);
                    if (actual < joint.minimum || actual > joint.maximum) row << " !OUT";
                    else if (actual - joint.minimum <= margin || joint.maximum - actual <= margin)
                        row << " !NEAR";
                }
                if (joint.clamped) row << " !CMD";
                put(7 + static_cast<int>(i), row.str(), i == selected ? inverse() : "");
            }
            if (height >= 25) {
                put(13, observation && observation->age_seconds > 0.5
                    ? "  OBSERVATIONS / last known state" : "  OBSERVATIONS", strong());
                if (observation) {
                    const auto metric = [](const std::string& label, double number,
                                           const std::string& unit, int precision = 2) {
                        std::ostringstream text;
                        text << label << "  " << std::fixed << std::setprecision(precision) << number;
                        if (!unit.empty()) text << " " << unit;
                        return text.str();
                    };
                    const auto pair = [&](int row,const std::string& left,const std::string& right) {
                        put(row,"  " + left + std::string(std::max(2,35-static_cast<int>(left.size())),' ') + right);
                    };
                    pair(15,metric("Pelvis height",observation->pelvis_height,"m"),
                         observation->is_standing ? "Standing" : "Fallen");
                    pair(16,metric("Uprightness",observation->pelvis_uprightness,""),
                         observation->has_motion ? (observation->paused ? "Paused" : "Running") : "State unavailable");
                    pair(17,metric("Data age",observation->age_seconds,"s"),"");
                    if (observation->has_motion) {
                        pair(18,metric("Simulation time",observation->simulation_time,"s"),
                             metric("Steps",observation->steps,"",0));
                        pair(19,metric("Pelvis speed",observation->speed,"m/s"),
                             metric("Pelvis spin",observation->angular_speed,"rad/s"));
                    }
                } else put(15,"  Waiting for viewer data",faint());
            }
            const auto now = std::chrono::steady_clock::now();
            if (observation && observation->age_seconds <= 0.5)
                previewYaw += std::min(0.3,std::chrono::duration<double>(now-previousPreview).count())*0.35;
            previousPreview = now;
            if (wide) {
                constexpr int panel = 78;
                auto append = [&](int row,const std::string& text,const std::string& style) {
                    if (row < 0 || row >= height-6) return;
                    frame[row] += std::string(std::max(0,panel-occupiedColumns[row]),' ')
                        + style + clipped(text,width-panel) + normal();
                };
                append(1,"BODY POSE",strong());
                append(3,observation && observation->age_seconds <= 0.5
                    ? "Live pose / orbiting camera" : "Preview frozen / waiting for live data",faint());
                const int previewHeight = std::min(22,height-12);
                if (observation && !observation->body_pose.empty()) {
                    const auto rows=body_preview(*observation,width-panel-2,previewHeight,previewYaw,utf8);
                    for (std::size_t row=0;row<rows.size();++row)
                        append(5+static_cast<int>(row),rows[row],accent());
                } else append(7,"Waiting for body pose telemetry",faint());
                append(5+previewHeight,"Schematic / actual part transforms",faint());
            }
            const int statusRow = height - 5;
            put(statusRow, editing ? "  Target: " + draft + "_" : "  " + status,
                failed && !editing ? error_color() : "");
            const std::string navigation = "  " + std::string(up) + "/" + down + " select    "
                + left + "/" + right + " ±" + stepLabel.str() + "    S step    "
                + (utf8 ? "↵" : "Enter") + " edit    1-4 joint";
            put(height - 3, editing ? "  Type value  ·  " + std::string(utf8 ? "↵" : "Enter")
                                    + " apply  ·  Esc cancel" : navigation, faint());
            put(height - 2, "  R neutral  ·  Esc/Q back  ·  angles: rad", faint());
            put(height - 1, "  Targets save immediately and apply in the viewer.", faint());
        }
        screen.paint(frame, width, height - 1, 0);
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&input, 1, 150);
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        unsigned char key = 0;
        if (read(STDIN_FILENO, &key, 1) != 1) break;
        if (key == 3 || key == 4) break;
        if (key == 27) {
            std::string sequence;
            char byte = 0;
            while (sequence.size() < 64 && poll(&input, 1, 30) > 0
                   && read(STDIN_FILENO, &byte, 1) == 1) {
                sequence += byte;
                if (sequence.size() > 1 && byte >= '@' && byte <= '~') break;
            }
            if (sequence.empty()) {
                if (editing) { editing = false; draft.clear(); }
                else break;
            } else if (sequence == "[200~") {
                // Pasted text is an editable draft; it never applies automatically.
                std::string pasted, tail;
                while (!stop_requested) {
                    const int available = poll(&input, 1, 150);
                    if (available < 0 && errno != EINTR) break;
                    if (available <= 0) continue;
                    if (read(STDIN_FILENO, &byte, 1) != 1) break;
                    tail += byte;
                    if (tail.size() > 6) tail.erase(0, tail.size() - 6);
                    if (pasted.size() < 134) pasted += byte;
                    if (tail == "\033[201~") {
                        if (pasted.ends_with(tail)) pasted.resize(pasted.size() - 6);
                        break;
                    }
                }
                draft = clean_paste(pasted).substr(0, 128);
                editing = true;
            } else if (!editing && !compact) {
                if (sequence == "[A" || sequence == "OA") selected = (selected + 3) % 4;
                else if (sequence == "[B" || sequence == "OB") selected = (selected + 1) % 4;
                else if (sequence == "[C" || sequence == "OC" || sequence == "[D" || sequence == "OD") {
                    auto candidate = controls;
                    candidate[selected].value += sequence.back() == 'C' ? stepSize : -stepSize;
                    apply(candidate);
                }
            }
            continue;
        }
        if (!editing && (key == 'q' || key == 'Q')) break;
        if (compact) continue;
        if (!editing && key >= '1' && key <= '4') {
            selected = static_cast<std::size_t>(key - '1');
            continue;
        }
        if (!editing && (key == 's' || key == 'S')) {
            stepIndex = (stepIndex + 1) % stepSizes.size();
            stepSize = stepSizes[stepIndex];
            std::ostringstream stepText;
            stepText << std::fixed << std::setprecision(2) << stepSize;
            status = "Arrow step: " + stepText.str() + " rad.";
            failed = false;
            continue;
        }
        if (key == '\r' || key == '\n') {
            if (!editing) {
                editing = true;
                draft.clear();
            } else {
                try {
                    std::size_t consumed = 0;
                    const double value = std::stod(draft, &consumed);
                    if (consumed != draft.size()) throw std::invalid_argument("Trailing text");
                    auto candidate = controls;
                    candidate[selected].value = value;
                    apply(candidate);
                    editing = false;
                } catch (const std::exception&) {
                    status = "Invalid value; press Enter to edit again.";
                    failed = true;
                    editing = false;
                }
            }
        } else if (!editing && (key == 'r' || key == 'R')) {
            auto candidate = controls;
            for (auto& joint : candidate) joint.value = 0.0;
            apply(candidate);
        } else if (editing && (key == 127 || key == 8)) {
            if (!draft.empty()) draft.pop_back();
        } else if (editing && key == 21) draft.clear();
        else if (editing && ((key >= '0' && key <= '9') || key == '-' || key == '+' || key == '.'
                 || key == 'e' || key == 'E')) {
            if (draft.size() < 128) draft += static_cast<char>(key);
        }
    }
    // Invalidate this screen so the next prompt restores the original transcript.
    session->frame.clear();
    session->detail_targets.clear();
}

void show_command_center() {
    if (!session) {
        std::cout << "  Open aura-cli interactively, then press Left Arrow on an empty prompt to browse local activity.\n";
        return;
    }
    session->collect();
    RawInput raw;
    if (!raw.active()) { print_error("Could not open the command center."); return; }
    Composer screen;
    std::size_t selected = 0, output_scroll = 0;
    int filter = 0;
    bool viewing_output = false, pasting = false;
    std::vector<std::string> output_rows;
    std::size_t cached_output = static_cast<std::size_t>(-1);
    int cached_width = 0;
    std::string sequence;
    const std::array<std::string_view, 5> filters{"All", "Finished", "Failed", "Stopped", "Queued"};
    auto status_of = [](const SessionState::OutputBlock& block) -> std::string {
        return block.stopped ? "Stopped" : block.code == 0 ? "Finished" : "Failed";
    };
    while (!stop_requested) {
        const int width = std::max(1, terminal_width() - 1), height = std::max(1, terminal_height());
        std::vector<std::string> frame(height);
        std::vector<int> used_columns(height, 0);
        auto put = [&](int row, int col, int space, const std::string& text, const std::string& style = "") {
            if (row < 0 || row >= height - 1 || col >= width || space <= 0) return;
            const auto visible = clipped(text, std::min(space, width - col));
            frame[row] += std::string(std::max(0, col - used_columns[row]), ' ') + style + visible + normal();
            used_columns[row] = col + static_cast<int>(characters(visible));
        };
        // Rows refer to real shell runs and queued inputs from this session.
        std::vector<std::size_t> entries;
        std::array<std::size_t, 5> counts{};
        counts[4] = queued_inputs.size();
        counts[0] = session->outputs.size() + counts[4];
        for (const auto& block : session->outputs) ++counts[block.stopped ? 3 : block.code == 0 ? 1 : 2];
        if (filter == 0 || filter == 4)
            for (std::size_t i = 0; i < queued_inputs.size(); ++i) entries.push_back(session->outputs.size() + i);
        for (std::size_t i = session->outputs.size(); i > 0; --i) {
            const auto& block = session->outputs[i - 1];
            if (filter == 0 || filter == (block.stopped ? 3 : block.code == 0 ? 1 : 2)) entries.push_back(i - 1);
        }
        selected = entries.empty() ? 0 : std::min(selected, entries.size() - 1);
        const auto id = entries.empty() ? session->outputs.size() + queued_inputs.size() : entries[selected];
        const bool queued = !entries.empty() && id >= session->outputs.size();
        const auto* block = !entries.empty() && !queued ? &session->outputs[id] : nullptr;
        const std::string command = block ? (block->file ? "@" : "!") + block->command : queued ? queued_inputs[id - session->outputs.size()] : "";
        put(0, 2, width - 4, "Aura command center", strong() + accent());
        put(1, 2, width - 4, "Local session / " + workspace_path(), faint());
        int tab_col = 2;
        for (std::size_t i = 0; i < filters.size(); ++i) {
            const std::string tab = " " + std::string(filters[i]) + " " + std::to_string(counts[i]) + " ";
            if (tab_col + static_cast<int>(tab.size()) >= width) break;
            put(3, tab_col, static_cast<int>(tab.size()), tab, filter == static_cast<int>(i) ? inverse() : i == 2 && counts[i] > 0 ? error_color() : faint());
            tab_col += static_cast<int>(tab.size()) + 1;
        }
        // The active filter remains readable even on a very narrow terminal.
        if (width < 65) {
            if (height > 4) { frame[3].clear(); used_columns[3] = 0; }
            put(3, 2, width - 4, std::string(filters[filter]) + " / " + std::to_string(counts[filter]) + " items / Tab filter", accent());
        }
        const int body_top = 6, body_bottom = height - 3;
        const bool split = !viewing_output && width >= 100 && height >= 15;
        const int left_width = split ? width * 3 / 5 : width - 2;
        if (!viewing_output) {
            put(5, 2, left_width - 2, "COMMAND", faint());
            const int rows = std::max(0, body_bottom - body_top);
            const auto first = rows > 0 && selected >= static_cast<std::size_t>(rows) ? selected - rows + 1 : 0;
            for (int row = 0; row < rows && first + row < entries.size(); ++row) {
                const auto index = entries[first + row];
                const bool waiting = index >= session->outputs.size();
                const auto label = waiting ? queued_inputs[index - session->outputs.size()] : (session->outputs[index].file ? "@" : "!") + session->outputs[index].command;
                const auto status = waiting ? "Queued" : status_of(session->outputs[index]);
                const int room = std::max(1, left_width - 4);
                const int title_room = std::max(1, room - 11);
                std::string text = (first + row == selected ? "> " : "  ") + clipped(label, std::max(1, title_room - 2));
                if (room >= 24) text += std::string(std::max(1, title_room - static_cast<int>(characters(text))), ' ') + status;
                text += std::string(std::max(0, room - static_cast<int>(characters(text))), ' ');
                const bool failed = !waiting && session->outputs[index].code != 0 && !session->outputs[index].stopped;
                put(body_top + row, 2, room, text, first + row == selected ? inverse() : failed ? error_color() : "");
            }
            if (entries.empty()) {
                put(body_top, 2, width - 4, "No activity in this view.", faint());
                put(body_top + 2, 2, width - 4, "Run !pwd or !git status to start. Esc returns to your prompt.", faint());
            }
        }
        if ((split || viewing_output) && !entries.empty()) {
            const int col = split ? left_width + 2 : 2;
            const int room = width - col - 2;
            put(5, col, room, viewing_output ? "CAPTURED OUTPUT" : "RUN DETAILS", strong());
            put(6, col, room, command, accent());
            put(8, col, room, queued ? "Queued / waiting to run" : status_of(*block), strong() + (block && block->code != 0 && !block->stopped ? error_color() : ""));
            put(9, col, room, block ? "Workspace  " + block->directory : "Workspace  current directory when executed", faint());
            if (block) {
                std::ostringstream detail;
                detail << "Exit " << block->code << " / " << std::fixed << std::setprecision(1) << block->seconds << "s";
                std::tm local{};
                if (localtime_r(&block->finished, &local)) detail << " / " << std::put_time(&local, "%H:%M");
                put(10, col, room, detail.str(), faint());
                if (cached_output != id || cached_width != room) {
                    output_rows = SessionState::wrap_text(session->transcript.substr(block->begin, block->end - block->begin), std::max(1, room));
                    cached_output = id; cached_width = room;
                }
                const auto& rows = output_rows;
                const int available = std::max(0, body_bottom - 12);
                const std::size_t limit = rows.size() > static_cast<std::size_t>(available) ? rows.size() - available : 0;
                output_scroll = std::min(output_scroll, limit);
                for (int row = 0; row < available && output_scroll + row < rows.size(); ++row)
                    put(12 + row, col, room, rows[output_scroll + row]);
                if (rows.empty()) put(12, col, room, "No output captured.", faint());
                if (viewing_output) put(height - 2, 2, width - 4, "Output " + std::to_string(rows.empty() ? 0 : output_scroll + 1) + " / " + std::to_string(rows.size()) + " lines", faint());
            } else put(12, col, room, "Output will appear after this input runs.", faint());
        }
        const std::string footer = viewing_output ? "  Up/Down scroll  PgUp/PgDn page  Esc back"
            : "  Up/Down choose  Tab filter  Enter output  Esc back";
        frame.back() = faint() + clipped(footer, width) + normal();
        screen.paint(frame, width, height - 1, 0);
        pollfd input{STDIN_FILENO, POLLIN, 0};
        if (poll(&input, 1, 150) <= 0) continue;
        unsigned char key = 0;
        if (read(STDIN_FILENO, &key, 1) != 1) break;
        if (pasting) {
            sequence += static_cast<char>(key);
            if (sequence.size() > 6) sequence.erase(0, sequence.size() - 6);
            if (sequence == "\033[201~") { pasting = false; sequence.clear(); }
            continue;
        }
        if (key == 27) {
            sequence.clear();
            pollfd more{STDIN_FILENO, POLLIN, 0};
            while (sequence.size() < 16 && poll(&more, 1, 35) > 0) {
                char byte = 0;
                if (read(STDIN_FILENO, &byte, 1) != 1) break;
                sequence += byte;
                if (sequence.size() > 1 && byte >= '@' && byte <= '~') break;
            }
            if (sequence == "[200~") { pasting = true; sequence.clear(); continue; }
            if (sequence.empty()) {
                if (viewing_output) { viewing_output = false; output_scroll = 0; }
                else break;
            } else if (sequence.starts_with("[<")) {
                const auto direction = wheel_direction(sequence);
                if (viewing_output && direction < 0) output_scroll = output_scroll > 3 ? output_scroll - 3 : 0;
                else if (viewing_output && direction > 0) output_scroll += 3;
                else if (direction < 0 && selected > 0) --selected;
                else if (direction > 0 && selected + 1 < entries.size()) ++selected;
            } else if (sequence == "[A" || sequence == "OA" || sequence == "[5~") {
                if (viewing_output) { const auto amount = sequence == "[5~" ? std::max(1, height - 15) : 1; output_scroll = output_scroll > static_cast<std::size_t>(amount) ? output_scroll - amount : 0; }
                else if (selected > 0) { --selected; output_scroll = 0; }
            } else if (sequence == "[B" || sequence == "OB" || sequence == "[6~") {
                if (viewing_output) output_scroll += sequence == "[6~" ? std::max(1, height - 15) : 1;
                else if (selected + 1 < entries.size()) { ++selected; output_scroll = 0; }
            } else if (!viewing_output && (sequence == "[Z" || sequence == "[D" || sequence == "[C")) {
                filter = (filter + (sequence == "[C" ? 1 : 4)) % 5; selected = output_scroll = 0;
            }
        } else if (key == 3 || key == 4) break;
        else if (key == '\t' && !viewing_output) { filter = (filter + 1) % 5; selected = output_scroll = 0; }
        else if ((key == '\r' || key == '\n') && !entries.empty()) { viewing_output = !viewing_output; output_scroll = 0; }
    }
}

void print_history() {
    if (history.empty()) { std::cout << "  No inputs yet.\n"; return; }
    for (std::size_t i = 0; i < history.size(); ++i)
        std::cout << "  " << i + 1 << "  " << history[i] << '\n';
}

void print_queue() {
    if (queued_inputs.empty()) { std::cout << "  Queue is empty.\n"; return; }
    for (std::size_t i = 0; i < queued_inputs.size(); ++i)
        std::cout << "  " << i + 1 << "  " << queued_inputs[i] << '\n';
}
void unqueue() {
    if (!suspended_draft.empty()) { std::cout << "  Finish or clear your current draft before restoring a queued input.\n"; return; }
    if (queued_inputs.empty()) { std::cout << "  Queue is empty.\n"; return; }
    suspended_draft = queued_inputs.back();
    suspended_cursor = suspended_draft.size();
    queued_inputs.pop_back();
    std::cout << "  Last queued input restored to the draft.\n";
}

void dictate_prompt() {
    if (!session || !styled()) {
        print_error("/voice needs the interactive terminal. Start aura-cli, then type /voice.");
        return;
    }
    RawInput raw;
    if (!raw.active()) { print_error("Could not open the dictation controls."); return; }
    const auto saved_draft = suspended_draft;
    const auto saved_cursor = suspended_cursor;
    VoiceDictation voice;
    Composer screen;
    const auto started = std::chrono::steady_clock::now();
    auto listening_since = started;
    bool listening = false, cancelled = false, timed_out = false, finishing = false;
    auto finishing_since = started;
    VoiceSnapshot snapshot;
    std::array<float, 12> levels{};
    voice.start();
    while (!stop_requested) {
        voice.poll();
        snapshot = voice.snapshot();
        const auto now = std::chrono::steady_clock::now();
        if (snapshot.phase == VoicePhase::listening && !listening) {
            listening = true;
            listening_since = now;
        }
        if (snapshot.phase == VoicePhase::failed || snapshot.phase == VoicePhase::finished) break;
        if (finishing && now - finishing_since > std::chrono::seconds(2)) break;
        if (now - (listening ? listening_since : started) > std::chrono::seconds(listening ? 60 : 90)) {
            timed_out = true;
            break;
        }
        std::move(levels.begin() + 1, levels.end(), levels.begin());
        levels.back() = snapshot.level;
        const int tick = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count() / 100);
        std::string banner = "  ";
        if (finishing) banner += muted() + "Finishing transcript…" + normal();
        else if (!listening) banner += muted() + "Waiting for macOS permissions…" + normal();
        else {
            banner += strong() + terminal_capabilities().white_color + "Listening  " + normal();
            constexpr std::array<std::string_view, 8> bars{"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
            for (float level : levels) {
                banner += level > 0.15f ? terminal_capabilities().white_color : muted();
                banner += terminal_capabilities().utf8 ? bars[std::clamp(static_cast<int>(level * 7), 0, 7)]
                                                       : level > 0.15f ? "|" : ".";
            }
            banner += normal() + muted() + "  On-device dictation" + normal();
        }
        const auto text = saved_draft + (saved_draft.empty() || snapshot.text.empty() ? "" : " ") + clean_paste(snapshot.text);
        screen.draw(text, text.size(), {}, 0, "Dictation", tick, false, 0, false,
                    finishing ? "Finalizing your draft / Esc discard / Nothing sends automatically"
                              : listening ? "Enter finish & edit / Esc discard / Nothing sends automatically"
                              : "Allow microphone and speech access in macOS / Esc cancel", banner);
        pollfd input{STDIN_FILENO, POLLIN, 0};
        if (::poll(&input, 1, 80) > 0) {
            unsigned char key;
            if (read(STDIN_FILENO, &key, 1) != 1) { cancelled = true; break; }
            if (key == '\r' || key == '\n') {
                if (!listening) { cancelled = true; break; }
                if (!finishing) {
                    finishing = true;
                    finishing_since = now;
                    voice.finish();
                }
            }
            if (key == 27) {
                std::string sequence;
                char byte;
                if (::poll(&input, 1, 30) > 0 && read(STDIN_FILENO, &byte, 1) == 1) {
                    sequence += byte;
                    if (byte == '[' || byte == 'O') {
                        while (sequence.size() < 32 && ::poll(&input, 1, 10) > 0 && read(STDIN_FILENO, &byte, 1) == 1) {
                            sequence += byte;
                            if (byte >= '@' && byte <= '~') break;
                        }
                    }
                }
                if (sequence.empty()) { cancelled = true; break; }
                const auto direction = wheel_direction(sequence);
                if (direction) screen.scroll(direction < 0, 3);
                else if (const auto output = clicked_details(sequence)) screen.reveal_output(*output);
            }
            if (key == 3 || key == 4) {
                cancelled = true;
                break;
            }
        }
    }
    voice.stop();
    if (cancelled || stop_requested) {
        suspended_draft = saved_draft;
        suspended_cursor = saved_cursor;
        std::cout << muted() << "  Dictation cancelled.\n" << normal();
        return;
    }
    const auto text = clean_paste(snapshot.text);
    suspended_draft = saved_draft + (saved_draft.empty() || text.empty() ? "" : " ") + text;
    suspended_cursor = suspended_draft.size();
    if (snapshot.phase == VoicePhase::failed) print_error("Dictation: " + snapshot.error);
    else if (text.empty()) std::cout << muted() << (timed_out ? "  Dictation timed out; no speech captured.\n" : "  No speech captured. Type /voice to try again.\n") << normal();
    else std::cout << muted() << "  Dictation placed in your draft. Edit it, then Enter or Tab to send.\n" << normal();
}

int run_balance_training(const std::string& command) {
    if (!session) return run_shell(command);
    session->collect();
    RawInput raw;
    if (!raw.active()) return run_shell(command);
    int output[2];
    if (pipe(output) != 0) { print_error("Could not open training output."); return 1; }
    int commands[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, commands) != 0) {
        close(output[0]); close(output[1]);
        print_error("Could not open training commands."); return 1;
    }
#ifdef SO_NOSIGPIPE
    const int noSigpipe = 1;
    setsockopt(commands[0], SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
#endif
    const pid_t child = fork();
    if (child < 0) {
        close(output[0]); close(output[1]); close(commands[0]); close(commands[1]);
        print_error("Could not start balance search."); return 1;
    }
    if (child == 0) {
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL);
        dup2(commands[1], STDIN_FILENO);
        close(commands[0]); close(commands[1]);
        dup2(output[1], STDOUT_FILENO); dup2(output[1], STDERR_FILENO);
        close(output[0]); close(output[1]);
        const auto events = command + " --events --interactive";
        execl("/bin/sh", "sh", "-c", events.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    setpgid(child, child);
    close(output[1]); close(commands[1]);
    fcntl(output[0], F_SETFL, fcntl(output[0], F_GETFL) | O_NONBLOCK);

    struct Result { int trial, steps; double seconds, kp, kd; std::string reason; };
    std::vector<Result> results;
    int trial = 0, total = 21, steps = 0, limit = 1200, processStatus = 0;
    int fixedRecord = 346, clamped = 0, resultOffset = 0, selected = -1;
    bool navigated = false, replaying = false, replayComplete = false;
    int replayTrial = 0, replaySteps = 0, replayClamped = 0;
    double replaySeconds = 0, replayHeight = 0, replayUpright = 0, replayCorrection = 0;
    std::string replayOutcome, replayError;
    double seconds = 0, height = 0, upright = 0, kp = 0, kd = 0, correction = 0;
    bool exited = false, stopping = false, leave = false, stopped = false;
    bool finished = false, pipeClosed = false, terminated = false, killed = false;
    const auto started = std::chrono::steady_clock::now();
    auto stoppingAt = started, exitedAt = started;
    std::string pending, errors, lastError;
    const auto sendCommand = [&](const std::string& value) {
        if (exited || commands[0] < 0) return;
        const auto line = value + '\n';
#ifdef MSG_NOSIGNAL
        send(commands[0], line.data(), line.size(), MSG_NOSIGNAL);
#else
        send(commands[0], line.data(), line.size(), 0);
#endif
    };
    const auto cancel = [&] {
        if (stopping || exited) return;
        stopping = true;
        if (commands[0] >= 0) { close(commands[0]); commands[0] = -1; }
        stoppingAt = std::chrono::steady_clock::now();
        kill(-child, SIGINT);
    };
    const auto receive = [&](const std::string& line) {
        // Small private, tab-separated protocol; stderr remains available as an error log.
        if (!line.starts_with("AURA_TRAIN\t")) {
            if (!line.empty()) {
                lastError = clean_paste(line);
                if (errors.size() < 65536) errors += lastError + '\n';
            }
            return;
        }
        std::istringstream fields(line.substr(11));
        std::string kind;
        fields >> kind;
        if (kind == "start") fields >> total >> limit >> fixedRecord;
        else if (kind == "progress") fields >> trial >> steps >> seconds >> height >> upright >> kp >> kd >> correction >> clamped;
        else if (kind == "result") {
            Result result{};
            if (fields >> result.trial >> result.steps >> result.seconds >> result.kp >> result.kd >> result.reason) {
                results.push_back(result);
                if (selected < 0) selected = 0;
                if (resultOffset > 0) ++resultOffset; // Keep the browsed rows in place.
            }
        } else if (kind == "finished" || kind == "stopped") {
            if (kind == "finished") finished = true;
            else if (!finished) stopped = true;
            if (!navigated && !results.empty()) {
                selected = static_cast<int>(std::max_element(results.begin(),results.end(),
                    [](const auto& a,const auto& b) { return a.steps < b.steps; })-results.begin());
            }
        } else if (kind == "replay_start") {
            fields >> replayTrial;
            replaying = true; replayComplete = false; replaySteps = replayClamped = 0;
            replaySeconds = replayHeight = replayUpright = replayCorrection = 0;
            replayOutcome.clear(); replayError.clear();
        } else if (kind == "replay_progress") {
            fields >> replaySteps >> replaySeconds >> replayHeight >> replayUpright >> replayCorrection >> replayClamped;
        } else if (kind == "replay_result") {
            fields >> replaySteps >> replayOutcome;
            replayComplete = true;
        } else if (kind == "replay_end") replaying = false;
        else if (kind == "replay_error") {
            std::getline(fields,replayOutcome);
            lastError = replayOutcome; replayError = replayOutcome;
        }
    };
    Composer screen;
    std::vector<std::string> previousFrame;
    int previousWidth = 0;
    std::string keySequence;
    bool pasting = false;
    auto escapeAt = started;
    const auto selectResult = [&](int amount) {
        if (results.empty() || replaying) return;
        selected = std::clamp(selected+amount,0,static_cast<int>(results.size())-1);
        navigated = true;
    };
    const auto stopOrBack = [&] {
        if (replaying) sendCommand("stop_replay");
        else { leave = true; cancel(); }
    };
    const auto stopSearch = [&] {
        if (replaying) sendCommand("stop_replay");
        else if (!finished && !stopped) sendCommand("stop_search");
    };
    const auto receiveKey = [&](unsigned char key) {
        if (keySequence.empty()) {
            if (key == 27) { keySequence = "\033"; escapeAt = std::chrono::steady_clock::now(); return; }
            if (pasting) return;
            if (key == 'q' || key == 'Q' || key == 4) stopOrBack();
            else if (key == 's' || key == 'S' || key == 3) stopSearch();
            else if (key == 'k' || key == 'K') selectResult(-1);
            else if (key == 'j' || key == 'J') selectResult(1);
            else if ((key == '\r' || key == '\n') && selected >= 0 && !replaying && !exited) {
                replaying = true; replayComplete = false; replayTrial = results[selected].trial;
                replaySteps = replayClamped = 0;
                replaySeconds = replayHeight = replayUpright = replayCorrection = 0;
                sendCommand("replay " + std::to_string(results[selected].trial));
            }
            return;
        }
        keySequence += static_cast<char>(key);
        // An ESC prefix alone is not a quit: mouse and navigation keys share it.
        if (keySequence.size() == 2) {
            if (key != '[' && key != 'O') keySequence.clear();
            return;
        }
        if (keySequence.size() > 64) { keySequence.clear(); return; }
        if (key < '@' || key > '~') return;
        const auto sequence = std::string_view(keySequence).substr(1);
        if (sequence == "[201~") pasting = false;
        else if (sequence == "[200~") pasting = true;
        else if (!pasting) {
            if (sequence.starts_with("[<")) {
                const auto direction = wheel_direction(sequence);
                if (direction) selectResult(direction * 3);
            } else if (sequence == "[A" || sequence == "OA") selectResult(-1);
            else if (sequence == "[B" || sequence == "OB") selectResult(1);
            else if (sequence == "[5~") selectResult(-std::max(1,terminal_height()-19));
            else if (sequence == "[6~") selectResult(std::max(1,terminal_height()-19));
            else if (sequence == "[H" || sequence == "OH" || sequence == "[1~") selectResult(-static_cast<int>(results.size()));
            else if (sequence == "[F" || sequence == "OF" || sequence == "[4~") selectResult(static_cast<int>(results.size()));
        }
        // Consume unknown sequences, clicks and releases too; none reach the prompt.
        keySequence.clear();
    };
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (keySequence == "\033" && now-escapeAt > std::chrono::milliseconds(180)) {
            keySequence.clear();
            if (!pasting) stopOrBack();
        } else if (!keySequence.empty() && now-escapeAt > std::chrono::seconds(1)) keySequence.clear();
        if (stop_requested) { leave = true; cancel(); }
        if (stopping && !exited && now - stoppingAt > std::chrono::seconds(2) && !terminated) {
            kill(-child, SIGTERM); terminated = true;
        }
        if (stopping && !exited && now - stoppingAt > std::chrono::seconds(4) && !killed) {
            kill(-child, SIGKILL); killed = true;
        }
        // Drain before painting so even a very fast headless run keeps its final results.
        if (!pipeClosed) for (int batch = 0; batch < 16; ++batch) {
            char bytes[4096];
            const auto count = read(output[0], bytes, sizeof(bytes));
            if (count == 0) { pipeClosed = true; break; }
            if (count < 0) break;
            pending.append(bytes, static_cast<std::size_t>(count));
            std::size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos) {
                receive(pending.substr(0, newline));
                pending.erase(0, newline + 1);
            }
            if (pending.size() > 65536) pending.clear();
        }
        if (!exited) {
            const auto result = waitpid(child, &processStatus, WNOHANG);
            if (result == child || (result < 0 && errno != EINTR)) {
                exited = true; exitedAt = now;
            }
        }
        if (pipeClosed && !pending.empty()) { receive(pending); pending.clear(); }
        const bool settled = exited && (pipeClosed || now-exitedAt > std::chrono::milliseconds(500));
        if (settled && leave) break;
        const int code = WIFEXITED(processStatus) ? WEXITSTATUS(processStatus)
            : WIFSIGNALED(processStatus) ? 128 + WTERMSIG(processStatus) : 1;
        const bool failed = settled && !stopping && !stopped && (code != 0 || !finished);
        const std::string state = failed ? "FAILED" : stopping ? (settled ? "STOPPED" : "STOPPING")
            : stopped ? "STOPPED" : finished ? "COMPLETE" : trial ? "RUNNING" : "STARTING";
        const int width = std::max(1, terminal_width()-1), rows = std::max(1, terminal_height());
        std::vector<std::string> frame(rows);
        const auto put = [&](int row, const std::string& text, const std::string& style = "") {
            if (row >= 0 && row < rows) frame[row] = style + clipped(text, width) + normal();
        };
        const auto number = [](double value, int precision = 2) {
            std::ostringstream out; out << std::fixed << std::setprecision(precision) << value; return out.str();
        };
        put(1, "  AURA  /  BALANCE LAB", strong());
        put(3, "  " + (replaying ? std::string("REPLAY") : state) + "    /    " + (replaying ? "VISUAL" : "HEADLESS")
            + "    /    " + std::to_string(results.size()) + " of " + std::to_string(total) + " episodes complete",
            failed ? error_color() : accent());
        const auto best = std::max_element(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.steps < b.steps; });
        const auto signedNumber = [&](double value, int precision = 3) {
            return (value >= 0 ? "+" : "") + number(value, precision);
        };
        const auto delta = [](int value) {
            return (value >= 0 ? "+" : "") + std::to_string(value);
        };
        const auto gains = [&](double p, double d) {
            return "Kp " + signedNumber(p) + "   Kd " + signedNumber(d);
        };
        const bool validReplay = replaying && replayTrial > 0 && replayTrial <= static_cast<int>(results.size());
        const auto liveSteps = validReplay ? replaySteps : steps;
        const auto liveSeconds = validReplay ? replaySeconds : seconds;
        const auto liveHeight = validReplay ? replayHeight : height;
        const auto liveUpright = validReplay ? replayUpright : upright;
        const auto liveCorrection = validReplay ? replayCorrection : correction;
        const auto liveClamped = validReplay ? replayClamped : clamped;
        const auto liveKp = validReplay ? results[replayTrial-1].kp : kp;
        const auto liveKd = validReplay ? results[replayTrial-1].kd : kd;
        if (width >= 78 && rows >= 24) {
            // Plain column widths keep ANSI styles out of alignment calculations.
            const int right = std::min(width / 2, 58);
            const auto columns = [&](const std::string& left, const std::string& value) {
                const auto first = clipped(left, right - 2);
                return first + std::string(std::max(2, right - static_cast<int>(characters(first))), ' ') + value;
            };
            put(5, columns((validReplay ? "  REPLAY EPISODE " : "  CURRENT EPISODE ") + std::to_string(validReplay ? replayTrial : trial) + " / " + std::to_string(total),
                           "BEST FEEDBACK"), faint());
            put(7, columns("  " + std::to_string(liveSteps) + " / " + std::to_string(limit) + " steps",
                best == results.end() ? "Awaiting baseline" : std::to_string(best->steps) + " steps"), strong());
            put(8, columns("  " + gains(liveKp,liveKd), best == results.end() ? "--" : gains(best->kp,best->kd)));
            put(9, columns("  Height " + number(liveHeight) + " m   Upright " + number(liveUpright),
                results.empty() ? "Neutral baseline: pending" : "Neutral baseline: " + std::to_string(results.front().steps) + " steps"));
            put(10, columns("  Hip " + signedNumber(liveCorrection) + " rad" + (liveClamped ? "  LIMIT" : "") + "   " + number(liveSeconds) + " s",
                best == results.end() ? "" : delta(best->steps-results.front().steps) + " vs neutral"), liveClamped ? accent() : "");
            put(11, columns("  Correction range: -0.2 to +0.2 rad",
                best == results.end() ? "" : delta(best->steps-fixedRecord) + " vs fixed record (" + std::to_string(fixedRecord) + ")"));
            const int visible = std::max(1, rows-19);
            resultOffset = std::clamp(resultOffset,0,std::max(0,static_cast<int>(results.size())-visible));
            int end = static_cast<int>(results.size())-resultOffset;
            const int firstVisible = std::max(0,end-visible);
            if (selected >= 0 && selected < firstVisible) end = std::min(static_cast<int>(results.size()),selected+visible);
            else if (selected >= end) end = selected+1;
            resultOffset = static_cast<int>(results.size())-end;
            const int first = std::max(0,end-visible);
            put(13, "  TRIALS    " + (results.empty() ? std::string("Awaiting baseline")
                : std::to_string(first+1) + "-" + std::to_string(end) + " of " + std::to_string(results.size()))
                + (resultOffset ? "  /  browsing" : "  /  latest"),strong());
            std::ostringstream heading;
            heading << "    " << std::left << std::setw(11) << "Episode" << std::right
                << std::setw(7) << "Kp" << std::setw(8) << "Kd" << std::setw(7) << "Steps"
                << std::setw(9) << "vs base" << std::setw(10) << "Time" << "  Outcome";
            put(14,heading.str(),faint());
            for (int index = first; index < end; ++index) {
                const auto& result = results[index];
                std::ostringstream row;
                row << (index == selected ? "  > " : "    ") << std::left << std::setw(11) << ((&result == &*best ? "* " : "") + (result.trial == 1 ? std::string("Baseline") : "Trial " + std::to_string(result.trial-1)))
                    << std::right << std::showpos << std::fixed << std::setprecision(3)
                    << std::setw(7) << result.kp << std::setw(8) << result.kd
                    << std::noshowpos << std::setw(7) << result.steps
                    << std::setw(9) << delta(result.steps-results.front().steps)
                    << std::setprecision(2) << std::setw(8) << result.seconds << " s  "
                    << (result.reason == "fallen" ? "Fallen" : result.reason == "time_limit" ? "Time limit" : result.reason);
                put(15+index-first,row.str(), index == selected ? inverse() : &result == &*best ? accent() : "");
            }
            put(rows-4,failed ? "  " + (lastError.empty() ? "Search exited without a completed result." : lastError)
                : replaying ? (replayComplete ? "  Replay: " + std::to_string(replaySteps) + " steps / " + replayOutcome + "  /  Returning to trials"
                    : "  Replaying selected trial. Search paused; Esc returns to trials.")
                : !replayError.empty() ? "  Replay failed:" + replayError
                : replayComplete ? "  Last replay: " + std::to_string(replaySteps) + " steps / " + replayOutcome
                : "  Seed 42  /  20 gain pairs + neutral  /  Knees neutral",failed || !replayError.empty() ? error_color() : faint());
            const std::string navigation = terminal_capabilities().utf8 ? "Wheel / ↑ ↓" : "Wheel / Up Down";
            put(rows-2,"  " + navigation + "  select   Enter  replay   "
                + (replaying ? "Esc  trials" : "S  stop   Q / Esc  back"),faint());
            put(rows-1,"  * Best trial   LIMIT Clamped hip   Record: historical 346 steps   Gains not saved",faint());
        } else {
            put(5,(validReplay ? "  Replay episode " : "  Episode ") + std::to_string(validReplay ? replayTrial : trial)
                + " / " + std::to_string(total) + "   " + std::to_string(liveSteps) + " steps");
            put(6,"  " + gains(liveKp,liveKd));
            put(8,"  Hip " + signedNumber(liveCorrection) + " rad" + (liveClamped ? "  LIMIT" : ""));
            if (best != results.end()) {
                put(10,"  Best " + std::to_string(best->steps) + " steps  " + gains(best->kp,best->kd));
                put(11,"  " + delta(best->steps-results.front().steps) + " vs neutral / "
                    + delta(best->steps-fixedRecord) + " vs historical fixed record");
            }
            if (selected >= 0) {
                const auto& choice = results[selected];
                put(13,"  > " + (choice.trial == 1 ? std::string("Baseline") : "Trial " + std::to_string(choice.trial-1))
                    + (&choice == &*best ? "  * BEST" : "") + "  /  " + std::to_string(choice.steps) + " steps",inverse());
                put(14,"    " + gains(choice.kp,choice.kd));
            }
            if (failed) put(15,"  " + lastError,error_color());
            put(rows-2,replaying ? "  Esc  return to trials" : "  Up/Down select / Enter replay / Q back",faint());
        }
        // Do not send cursor hide/show commands continuously on a static screen.
        if (width != previousWidth || frame != previousFrame) {
            screen.paint(frame,width,rows-1,0);
            previousFrame = frame;
            previousWidth = width;
        }
        pollfd input{STDIN_FILENO,POLLIN,0};
        if (poll(&input,1,50) > 0 && (input.revents & POLLIN)) {
            unsigned char keys[256];
            const auto count = read(STDIN_FILENO,keys,sizeof(keys));
            for (ssize_t index = 0; index < count; ++index) receiveKey(keys[index]);
        }
    }
    close(output[0]);
    if (commands[0] >= 0) close(commands[0]);
    session->frame.clear(); session->detail_targets.clear();
    const int code = WIFEXITED(processStatus) ? WEXITSTATUS(processStatus)
        : WIFSIGNALED(processStatus) ? 128+WTERMSIG(processStatus) : 1;
    std::cout << "  Balance search " << (finished ? "complete" : stopping || stopped ? "stopped" : "failed")
              << " / " << results.size() << " evaluated episodes.\n";
    if (!results.empty()) {
        const auto best = std::max_element(results.begin(),results.end(),
            [](const auto& a,const auto& b) { return a.steps < b.steps; });
        std::cout << "  Best: " << best->steps << " steps / Kp " << std::fixed << std::setprecision(6)
                  << best->kp << " / Kd " << best->kd << ".\n";
        std::cout << "  " << std::showpos << best->steps-results.front().steps << " vs neutral / "
                  << best->steps-fixedRecord << std::noshowpos << " vs historical fixed record.\n";
    }
    if (code != 0 && !stopping && !stopped) std::cout << errors;
    return stopping || stopped ? 0 : code != 0 ? code : finished ? 0 : 1;
}

int run_shell(const std::string& command) {
    if (command.empty()) { std::cout << "  Usage: !<shell command>\n"; return 2; }
    // Plain interactive terminals still need interruption forwarded to the
    // separate child process group rather than leaving the command orphaned.
    struct PlainSignals {
        bool active = false;
        struct sigaction interrupt{}, terminate{};
        PlainSignals() {
            if (styled()) return;
            stop_requested = 0;
            struct sigaction action{};
            action.sa_handler = request_stop;
            sigemptyset(&action.sa_mask);
            sigaction(SIGINT, &action, &interrupt);
            sigaction(SIGTERM, &action, &terminate);
            active = true;
        }
        ~PlainSignals() {
            if (active) {
                sigaction(SIGINT, &interrupt, nullptr);
                sigaction(SIGTERM, &terminate, nullptr);
            }
        }
    } signals;
    int output[2];
    if (pipe(output) != 0) { print_error("Could not open command output."); return 1; }
    Composer display;
    const auto started = std::chrono::steady_clock::now();
    if (session) {
        session->begin_output(command);
        // Paint the real start before launching even a very short command.
        display.draw(suspended_draft, suspended_cursor, {}, 0, command, 0, false, 0, false);
    }
    const pid_t child = fork();
    if (child < 0) {
        close(output[0]); close(output[1]);
        print_error("Could not start command.");
        if (session) session->end_output(1, std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(), false);
        return 1;
    }
    if (child == 0) {
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        const int null_input = open("/dev/null", O_RDONLY);
        if (null_input >= 0) { dup2(null_input, STDIN_FILENO); close(null_input); }
        dup2(output[1], STDOUT_FILENO);
        dup2(output[1], STDERR_FILENO);
        close(output[0]); close(output[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    setpgid(child, child);
    if (session) session->outputs.back().process = child;
    close(output[1]);
    fcntl(output[0], F_SETFL, fcntl(output[0], F_GETFL) | O_NONBLOCK);
    std::unique_ptr<RawInput> raw;
    if (styled()) raw = std::make_unique<RawInput>();
    auto cancelled_at = started;
    bool cancelled = false, terminated = false, killed = false, child_exited = false;
    auto exited_at = started;
    int status = 0;
    std::size_t received = 0;
    bool truncated = false;
    enum class Escape { text, start, csi, osc, osc_end } escape = Escape::text;
    std::string key_sequence;
    std::string next_input = std::move(suspended_draft);
    std::size_t next_cursor = std::min(suspended_cursor, next_input.size());
    suspended_draft.clear();
    InputHistory navigator(next_input, next_cursor);
    bool pasting = false;
    std::string paste_buffer, paste_tail;
    auto cancel = [&] {
        if (cancelled) return;
        cancelled = true;
        cancelled_at = std::chrono::steady_clock::now();
        kill(-child, SIGINT);
        if (!queued_inputs.empty()) {
            std::cout << "\n  Cancelled " << queued_inputs.size() << " queued inputs.\n";
            queued_inputs.clear();
        }
    };
    auto receive_key = [&](unsigned char key) {
        if (pasting) {
            paste_tail += static_cast<char>(key);
            if (paste_tail.size() > 6) paste_tail.erase(0, paste_tail.size() - 6);
            if (paste_buffer.size() < 65542) paste_buffer += static_cast<char>(key);
            if (paste_tail == "\033[201~") {
                if (paste_buffer.ends_with("\033[201~")) paste_buffer.resize(paste_buffer.size() - 6);
                auto clean = clean_paste(paste_buffer);
                clean.resize(std::min(clean.size(), 65536 - std::min<std::size_t>(65536, next_input.size())));
                next_input.insert(next_cursor, clean);
                next_cursor += clean.size();
                pasting = false;
                paste_buffer.clear(); paste_tail.clear();
            }
            return;
        }
        if (key == 27) { key_sequence = "\033"; return; }
        if (key_sequence == "\033" && (key == 'b' || key == 'f')) {
            next_cursor = key == 'b' ? word_before(next_input, next_cursor) : word_after(next_input, next_cursor);
            key_sequence.clear(); return;
        }
        if (key_sequence == "\033" && key != '[' && key != 'O') key_sequence.clear();
        if (!key_sequence.empty()) {
            key_sequence += static_cast<char>(key);
            if (key_sequence.size() > 2 && key >= '@' && key <= '~') {
                if (key_sequence == "\033[A" || key_sequence == "\033OA") navigator.recall(true, next_input, next_cursor);
                else if (key_sequence == "\033[B" || key_sequence == "\033OB") navigator.recall(false, next_input, next_cursor);
                else if (key_sequence.starts_with("\033[<")) {
                    const auto direction = wheel_direction(std::string_view(key_sequence).substr(1));
                    if (direction) display.scroll(direction < 0, 3);
                    else if (const auto output = clicked_details(std::string_view(key_sequence).substr(1))) display.reveal_output(*output);
                }
                else if (key_sequence == "\033[5~") display.scroll(true);
                else if (key_sequence == "\033[6~") display.scroll(false);
                else if (key_sequence == "\033[1;3D" || key_sequence == "\033[1;5D") next_cursor = word_before(next_input, next_cursor);
                else if (key_sequence == "\033[1;3C" || key_sequence == "\033[1;5C") next_cursor = word_after(next_input, next_cursor);
                else if (key_sequence == "\033[C" || key_sequence == "\033OC") next_cursor = next(next_input, next_cursor);
                else if (key_sequence == "\033[D" || key_sequence == "\033OD") next_cursor = previous(next_input, next_cursor);
                else if (key_sequence == "\033[H" || key_sequence == "\033OH" || key_sequence == "\033[1~") next_cursor = 0;
                else if (key_sequence == "\033[F" || key_sequence == "\033OF" || key_sequence == "\033[4~") next_cursor = next_input.size();
                else if (key_sequence == "\033[3~") next_input.erase(next_cursor, next(next_input, next_cursor) - next_cursor);
                else if (key_sequence == "\033[200~") { pasting = true; paste_buffer.clear(); paste_tail.clear(); }
                key_sequence.clear();
            }
            if (key_sequence.size() > 32) key_sequence.clear();
            return;
        }
        if (key == 3) { cancel(); return; }
        if (key == 16 || key == 14) { navigator.recall(key == 16, next_input, next_cursor); return; }
        if (key == 15 && session) { session->toggle_output(); if (!session->outputs.empty()) display.reveal_output(session->outputs.size() - 1); return; }
        if (key == '\t' || key == '\r' || key == '\n') {
            if (next_input.find_first_not_of(" \t") == std::string::npos) return;
            if (cancelled) { std::cout << "\n  Wait for the stopping command to finish before queuing.\n"; return; }
            if (queued_inputs.size() >= 32) { print_error("Queue is full (32 inputs); draft kept."); return; }
            queued_inputs.push_back(next_input);
            std::cout << "\n  Queued " << queued_inputs.size() << ": " << next_input << '\n';
            next_input.clear(); next_cursor = 0;
            navigator = InputHistory(next_input, next_cursor);
            display.follow_output();
        } else if (key == 127 || key == 8) {
            const auto before = previous(next_input, next_cursor);
            next_input.erase(before, next_cursor - before); next_cursor = before;
        } else if (key == 2) next_cursor = previous(next_input, next_cursor);
        else if (key == 6) next_cursor = next(next_input, next_cursor);
        else if (key == 1) next_cursor = 0;
        else if (key == 5) next_cursor = next_input.size();
        else if (key == 4) next_input.erase(next_cursor, next(next_input, next_cursor) - next_cursor);
        else if (key == 21) { if (next_cursor) killed_text = next_input.substr(0, next_cursor); next_input.erase(0, next_cursor); next_cursor = 0; }
        else if (key == 11) { if (next_cursor < next_input.size()) killed_text = next_input.substr(next_cursor); next_input.erase(next_cursor); }
        else if (key == 23) {
            const auto end = next_cursor;
            while (next_cursor > 0 && next_input[previous(next_input, next_cursor)] == ' ') next_cursor = previous(next_input, next_cursor);
            while (next_cursor > 0 && next_input[previous(next_input, next_cursor)] != ' ') next_cursor = previous(next_input, next_cursor);
            if (end > next_cursor) killed_text = next_input.substr(next_cursor, end - next_cursor);
            next_input.erase(next_cursor, end - next_cursor);
        } else if (key == 25) { const auto text = killed_text.substr(0, 65536 - std::min<std::size_t>(65536, next_input.size())); next_input.insert(next_cursor, text); next_cursor += text.size(); }
        else if (key >= 32 && next_input.size() < 65536) {
            next_input.insert(next_cursor, 1, static_cast<char>(key)); ++next_cursor;
        }
    };
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (stop_requested) cancel();
        if (cancelled && !terminated && now - cancelled_at > std::chrono::seconds(2)) {
            kill(-child, SIGTERM); terminated = true;
        }
        if (cancelled && !killed && now - cancelled_at > std::chrono::seconds(4)) {
            kill(-child, SIGKILL); killed = true;
        }
        pollfd descriptors[2]{{output[0], POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
        poll(descriptors, raw && raw->active() ? 2 : 1, 100);
        if (descriptors[0].revents & (POLLIN | POLLHUP)) {
            std::array<char, 8192> bytes;
            // Bound each drain so noisy processes cannot starve cancellation or painting.
            for (int batch = 0; batch < 16; ++batch) {
                const auto count = read(output[0], bytes.data(), bytes.size());
                if (count <= 0) break;
                std::string clean;
                for (int i = 0; i < count; ++i) {
                    const unsigned char c = bytes[i];
                    // Child output is data: discard cursor controls and OSC clipboard/title
                    // escapes so programs cannot overwrite Aura's fixed composer.
                    if (escape == Escape::start) {
                        escape = c == '[' ? Escape::csi : (c == ']' ? Escape::osc : Escape::text);
                        continue;
                    }
                    if (escape == Escape::csi) { if (c >= '@' && c <= '~') escape = Escape::text; continue; }
                    if (escape == Escape::osc) { if (c == 7) escape = Escape::text; else if (c == 27) escape = Escape::osc_end; continue; }
                    if (escape == Escape::osc_end) { escape = c == '\\' ? Escape::text : Escape::osc; continue; }
                    if (c == 27) { escape = Escape::start; continue; }
                    if (c == '\n' || (c >= 32 && c != 127)) clean += static_cast<char>(c);
                    else if (c == '\t') clean += "    ";
                }
                constexpr std::size_t limit = 2 * 1024 * 1024;
                if (received < limit) {
                    const auto keep = std::min(clean.size(), limit - received);
                    std::cout << clean.substr(0, keep);
                    received += keep;
                } else if (!truncated) {
                    std::cout << "\n  Output limit reached (2 MiB); command is still running.\n";
                    truncated = true;
                }
            }
        }
        if (raw && raw->active() && descriptors[1].revents & POLLIN) {
            std::array<unsigned char, 1024> keys{};
            const auto count = read(STDIN_FILENO, keys.data(), keys.size());
            for (int i = 0; i < count; ++i) receive_key(keys[i]);
        }
        if (styled()) {
            const int tick = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count() / 100);
            display.draw(next_input, next_cursor, {}, 0, command, tick, cancelled, 0, false);
        } else std::cout << std::flush;
        if (child_exited) {
            // Drain the final output after waitpid reports exit. Background children
            // may inherit the pipe; do not wait indefinitely for them to close it.
            if ((!pasting || stop_requested) && (!(descriptors[0].revents & POLLIN)
                || std::chrono::steady_clock::now() - exited_at > std::chrono::milliseconds(500))) break;
        } else {
            const auto result = waitpid(child, &status, WNOHANG);
            if (result == child || (result < 0 && errno != EINTR)) {
                child_exited = true;
                exited_at = std::chrono::steady_clock::now();
            }
        }
    }
    close(output[0]);
    suspended_draft = std::move(next_input);
    suspended_cursor = next_cursor;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
    if (session) {
        session->end_output(code, seconds, cancelled);
        // Show the completed run before a command switches to a dedicated screen.
        display.draw(suspended_draft, suspended_cursor, {}, 0);
        return code;
    }
    std::ostringstream duration;
    if (seconds < 60) duration << std::fixed << std::setprecision(1) << seconds << 's';
    else {
        const auto total = static_cast<long long>(seconds);
        if (total >= 3600) duration << total / 3600 << "h ";
        duration << (total / 60) % 60 << "m " << total % 60 << 's';
    }
    const auto finished = std::time(nullptr);
    std::tm local{};
    std::ostringstream clock;
    if (localtime_r(&finished, &local)) {
        clock << (local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12)
              << ':' << std::setfill('0') << std::setw(2) << local.tm_min
              << (local.tm_hour < 12 ? " AM" : " PM");
    }
    std::cout << "\n  " << (styled() ? (code != 0 && !cancelled ? error_color() : faint()) : "")
              << (cancelled ? "Stopped" : code == 0 ? "Finished" : "Failed")
              << separator() << duration.str();
    if (!clock.str().empty()) std::cout << separator() << clock.str();
    if (code != 0) std::cout << separator() << "exit " << code;
    std::cout << (styled() ? normal() : "") << "\n\n";
    return code;
}

void print_header() {
    const auto project = workspace_path();
    if (!styled()) {
        std::cout << "Aura CLI (v" << version << ")\nLocal workspace · " << project
                  << "\nType /help for commands.\n\n";
        return;
    }
    session->show_welcome = true;
}

void print_response_label() {
    if (styled()) std::cout << "  " << faint() << "Output" << normal() << '\n';
    else std::cout << "aura: ";
}

std::optional<std::string> read_prompt() {
    if (stop_requested) return std::nullopt;
    if (!queued_inputs.empty()) {
        auto input = std::move(queued_inputs.front());
        queued_inputs.pop_front();
        if (session) session->record_message(input, true);
        else std::cout << "  " << (styled() ? strong() : "") << prompt_mark() << style_references(input)
                  << (styled() ? normal() : "") << "  [from queue]\n\n";
        if (history.empty() || history.back() != input) history.push_back(input);
        return input;
    }
    if (!styled()) {
        std::cout << "> " << std::flush;
        std::string input;
        if (!std::getline(std::cin, input)) return std::nullopt;
        return input;
    }
    RawInput raw;
    if (!raw.active()) return std::nullopt;
    Composer composer;
    std::string input = std::move(suspended_draft);
    std::size_t cursor = std::min(suspended_cursor, input.size()), selected = 0;
    InputHistory navigator(input, cursor);
    suspended_draft.clear();
    bool dismissed = false;
    int file_filter = 0, matched_filter = -1;
    auto recall_history = [&](bool up) {
        navigator.recall(up, input, cursor);
        dismissed = true;
        selected = 0;
        composer.follow_output();
    };
    bool redraw = true;
    int last_width = 0, last_height = 0;
    std::vector<Suggestion> matches;
    std::string matched_input;
    std::size_t matched_cursor = std::string::npos;
    bool matched_dismissed = false, have_matches = false;
    while (true) {
        if (stop_requested) return std::nullopt;
        if (!have_matches || matched_cursor != cursor || matched_input != input || matched_dismissed != dismissed || matched_filter != file_filter) {
            matches = matching(input, cursor, dismissed, file_filter);
            matched_cursor = cursor;
            matched_filter = file_filter;
            matched_input = input;
            matched_dismissed = dismissed;
            have_matches = true;
        }
        if (selected >= matches.size()) selected = 0;
        if (redraw || last_width != terminal_width() || last_height != terminal_height()) {
            composer.draw(input, cursor, matches, selected, "", 0, false, file_filter, !dismissed);
            last_width = terminal_width();
            last_height = terminal_height();
            redraw = false;
        }
        unsigned char key;
        pollfd input_fd{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&input_fd, 1, 150);
        if (ready == 0) continue; // Also refresh the layout after terminal resizes.
        if (ready < 0) { if (errno == EINTR) continue; return std::nullopt; }
        if (read(STDIN_FILENO, &key, 1) != 1) return std::nullopt;
        redraw = true;
        if (key == 4 && input.empty()) return std::nullopt;
        if (key == 3) {
            session->show_welcome = false;
            std::cout << faint() << prompt_mark() << input << " ^C" << normal() << "\n\n";
            return std::string{};
        }
        if (key == '\r' || key == '\n' || (key == '\t' && matches.empty())) {
            if (matches.empty() && input.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            if (!matches.empty()) {
                if (const auto reference = active_reference(input, cursor)) {
                    const auto& choice = matches[selected];
                    input.replace(reference->begin, reference->end - reference->begin, choice.name);
                    cursor = reference->begin + choice.name.size();
                    dismissed = !choice.directory;
                    selected = 0;
                    continue;
                }
                input = matches[selected].name;
                cursor = input.size();
                if (input.starts_with('@') && (input.ends_with('/') || input.ends_with("/\""))) {
                    selected = 0;
                    continue;
                }
                if (input.starts_with('@')) { dismissed = true; continue; }
                if (input == "/cd") { input += ' '; cursor = input.size(); dismissed = true; continue; }
            }
            session->show_welcome = false;
            session->record_message(input);
            if (!input.empty() && (history.empty() || history.back() != input)) history.push_back(input);
            return input;
        }
        if (key == '\t') {
            if (!matches.empty()) {
                if (const auto reference = active_reference(input, cursor)) {
                    const auto& choice = matches[selected];
                    input.replace(reference->begin, reference->end - reference->begin, choice.name);
                    cursor = reference->begin + choice.name.size();
                    dismissed = !choice.directory;
                    selected = 0;
                    continue;
                }
                input = matches[selected].name;
                if (input == "/cd") input += ' ';
                cursor = input.size();
                dismissed = !(input.starts_with('@') && (input.ends_with('/') || input.ends_with("/\"")));
            }
            continue;
        }
        if (key == 27) {
            // Consume the complete sequence, including Delete/Home/End and paste markers.
            std::string sequence;
            pollfd tail{STDIN_FILENO, POLLIN, 0};
            char byte = 0;
            if (poll(&tail, 1, 50) > 0 && read(STDIN_FILENO, &byte, 1) == 1) {
                sequence += byte;
                if (byte == '[' || byte == 'O') {
                    while (sequence.size() < 32 && poll(&tail, 1, 50) > 0 && read(STDIN_FILENO, &byte, 1) == 1) {
                        sequence += byte;
                        if (byte >= '@' && byte <= '~') break;
                    }
                }
            }
            if (sequence == "[200~") {
                std::string pasted, paste_tail;
                while (!stop_requested) {
                    const int ready = poll(&tail, 1, 100);
                    if (ready < 0 && errno != EINTR) break;
                    if (ready <= 0) continue;
                    if (read(STDIN_FILENO, &byte, 1) != 1) break;
                    paste_tail += byte;
                    if (paste_tail.size() > 6) paste_tail.erase(0, paste_tail.size() - 6);
                    if (pasted.size() < 65542) pasted += byte;
                    if (paste_tail == "\033[201~") {
                        if (pasted.ends_with(paste_tail)) pasted.resize(pasted.size() - 6);
                        break;
                    }
                }
                // The closing paste marker never submits the draft.
                auto clean = clean_paste(pasted);
                clean.resize(std::min(clean.size(), 65536 - std::min<std::size_t>(65536, input.size())));
                input.insert(cursor, clean);
                cursor += clean.size();
                dismissed = true;
                composer.follow_output();
            } else if (sequence.starts_with("[<")) {
                const auto direction = wheel_direction(sequence);
                if (direction) composer.scroll(direction < 0, 3);
                else if (const auto output = clicked_details(sequence)) composer.reveal_output(*output);
            } else if (sequence == "b" || sequence == "[1;3D" || sequence == "[1;5D") cursor = word_before(input, cursor);
            else if (sequence == "f" || sequence == "[1;3C" || sequence == "[1;5C") cursor = word_after(input, cursor);
            else if (sequence == "[A" || sequence == "OA" || sequence == "[B" || sequence == "OB") {
                const bool up = sequence.back() == 'A';
                if (!matches.empty()) selected = up ? (selected + matches.size() - 1) % matches.size() : (selected + 1) % matches.size();
                else recall_history(up);
            } else if ((sequence == "[C" || sequence == "OC" || sequence == "[D" || sequence == "OD")
                       && active_reference(input, cursor) && !dismissed) {
                file_filter = (file_filter + (sequence.back() == 'C' ? 1 : 2)) % 3;
                selected = 0;
            } else if (sequence == "[C" || sequence == "OC") cursor = next(input, cursor);
            else if (sequence == "[D" || sequence == "OD") {
                if (input.empty()) show_command_center();
                else cursor = previous(input, cursor);
            }
            else if (sequence == "[5~" || sequence == "[6~") composer.scroll(sequence == "[5~");
            else if (sequence == "[H" || sequence == "OH" || sequence == "[1~" || sequence == "[7~") cursor = 0;
            else if (sequence == "[F" || sequence == "OF" || sequence == "[4~" || sequence == "[8~") cursor = input.size();
            else if (sequence == "[3~") { input.erase(cursor, next(input, cursor) - cursor); dismissed = false; }
            else {
                dismissed = true;
                if (sequence.empty() && (input == "!" || input == "@" || input == "?")) { input.clear(); cursor = 0; }
            }
            continue;
        }
        // History shortcuts are aliases for physical Up/Down, never wheel events.
        if (key == 18) { search_input_history(input, cursor); dismissed = true; continue; }
        if (key == 16 || key == 14) { recall_history(key == 16); continue; }
        if (key == 2) cursor = previous(input, cursor);
        else if (key == 6) cursor = next(input, cursor);
        else if (key == 1) cursor = 0;
        else if (key == 5) cursor = input.size();
        else if (key == 4) { input.erase(cursor, next(input, cursor) - cursor); dismissed = false; }
        else if (key == 21) { if (cursor) killed_text = input.substr(0, cursor); input.erase(0, cursor); cursor = 0; dismissed = false; }
        else if (key == 11) { if (cursor < input.size()) killed_text = input.substr(cursor); input.erase(cursor); dismissed = false; }
        else if (key == 23) {
            const auto end = cursor;
            while (cursor > 0 && input[previous(input, cursor)] == ' ') cursor = previous(input, cursor);
            while (cursor > 0 && input[previous(input, cursor)] != ' ') cursor = previous(input, cursor);
            if (end > cursor) killed_text = input.substr(cursor, end - cursor);
            input.erase(cursor, end - cursor); dismissed = false;
        }
        else if (key == 25) { input.insert(cursor, killed_text); cursor += killed_text.size(); dismissed = false; }
        else if (key == 15) { session->toggle_output(); if (!session->outputs.empty()) composer.reveal_output(session->outputs.size() - 1); }
        else if (key == 12) { clear_transcript(); print_header(); composer.follow_output(); }
        else if (key == 127 || key == 8) {
            const auto before = previous(input, cursor);
            input.erase(before, cursor - before); cursor = before; dismissed = false;
        } else if (key >= 32) {
            std::string character(1, static_cast<char>(key));
            const int length = key >= 0xf0 ? 4 : key >= 0xe0 ? 3 : key >= 0xc0 ? 2 : 1;
            for (int i = 1; i < length; ++i) {
                pollfd continuation{STDIN_FILENO, POLLIN, 0};
                char byte = 0;
                if (poll(&continuation, 1, 100) <= 0 || read(STDIN_FILENO, &byte, 1) != 1) break;
                character += byte;
            }
            input.insert(cursor, character); cursor += character.size();
            dismissed = false; selected = 0; composer.follow_output();
        }
    }
}
} // namespace aura::cli
