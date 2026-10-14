#pragma once

#include "TerminalCapabilities.hpp"
#include <algorithm>
#include <cctype>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace aura::cli {
// Display-only lexical coloring. The original bytes remain the executed command.
class SyntaxHighlight {
    enum class Kind { plain, command, option, string, variable, number, keyword, punctuation, comment };
    std::vector<Kind> kinds_;
    struct HereDoc { std::string delimiter; bool python, strip_tabs; };
    std::deque<HereDoc> documents_;
    std::string python_string_;
    char shell_string_ = 0;
    static bool identifier(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 128; }
    static bool starts_identifier(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 128; }
    static bool one_of(std::string_view word, std::string_view words) {
        return words.find(" " + std::string(word) + " ") != std::string_view::npos;
    }
    void mark(std::size_t begin, std::size_t end, Kind kind) {
        std::fill(kinds_.begin() + begin, kinds_.begin() + end, kind);
    }
    static std::size_t quoted_end(std::string_view text, std::size_t at, std::size_t end, std::string_view delimiter) {
        while (at < end) {
            if (text[at] == '\\') { at = std::min(end, at + 2); continue; }
            if (text.substr(at, delimiter.size()) == delimiter) return at;
            ++at;
        }
        return end;
    }
    void python(std::string_view text, std::size_t begin, std::size_t end) {
        for (auto at = begin; at < end;) {
            if (!python_string_.empty()) {
                const auto stop = quoted_end(text, at, end, python_string_);
                const auto after = stop == end ? end : stop + python_string_.size();
                mark(at, after, Kind::string); at = after;
                if (stop != end || python_string_.size() == 1) python_string_.clear();
                continue;
            }
            const auto c = static_cast<unsigned char>(text[at]);
            if (c == '#') { mark(at, end, Kind::comment); break; }
            if (c == '\'' || c == '"') {
                const auto start = at;
                python_string_ = text.substr(at, 3) == std::string(3, static_cast<char>(c))
                    ? std::string(3, static_cast<char>(c)) : std::string(1, static_cast<char>(c));
                at += python_string_.size();
                const auto stop = quoted_end(text, at, end, python_string_);
                const auto after = stop == end ? end : stop + python_string_.size();
                mark(start, after, Kind::string); at = after;
                if (stop != end || python_string_.size() == 1) python_string_.clear();
                continue;
            }
            if (std::isdigit(c) || (c == '.' && at + 1 < end && std::isdigit(static_cast<unsigned char>(text[at + 1])))) {
                const auto start = at++;
                while (at < end) {
                    const auto n = static_cast<unsigned char>(text[at]);
                    if (std::isdigit(n) || n == '.' || n == '_' || n == 'x' || n == 'X'
                        || n == 'e' || n == 'E' || n == 'j' || n == 'J'
                        || (n >= 'a' && n <= 'f') || (n >= 'A' && n <= 'F')) ++at;
                    else if ((n == '+' || n == '-') && (text[at - 1] == 'e' || text[at - 1] == 'E')) ++at;
                    else break;
                }
                mark(start, at, Kind::number); continue;
            }
            if (starts_identifier(c)) {
                const auto start = at++;
                while (at < end && identifier(static_cast<unsigned char>(text[at]))) ++at;
                const auto word = text.substr(start, at - start);
                auto following = at;
                while (following < end && std::isspace(static_cast<unsigned char>(text[following]))) ++following;
                if (one_of(word, " import from as for in if else elif while try except finally with def class return assert raise yield lambda pass break continue global nonlocal async await del and or not is True False None "))
                    mark(start, at, Kind::keyword);
                else if (following < end && text[following] == '(') mark(start, at, Kind::command);
                else if (following < end && text[following] == '=' && (following + 1 == end || text[following + 1] != '='))
                    mark(start, at, Kind::variable);
                continue;
            }
            if (std::string_view("()[]{}=+-*/%<>!&|^~:").find(static_cast<char>(c)) != std::string_view::npos)
                mark(at, at + 1, Kind::punctuation);
            ++at;
        }
    }
    void shell_quoted(std::string_view text, std::size_t begin, std::size_t end, char quote) {
        mark(begin, end, Kind::string);
        if (quote != '"') return;
        for (auto at = begin; at < end;) {
            if (text[at] == '\\') { at = std::min(end, at + 2); continue; }
            if (text[at] != '$') { ++at; continue; }
            const auto start = at++;
            if (at < end && text[at] == '{') {
                while (at < end && text[at++] != '}') {}
            } else while (at < end && (identifier(static_cast<unsigned char>(text[at])) || text[at] == '?')) ++at;
            mark(start, at, Kind::variable);
        }
    }
    void shell(std::string_view text, std::size_t begin, std::size_t end) {
        bool command = true, assignment_value = false;
        for (auto at = begin; at < end;) {
            const auto c = static_cast<unsigned char>(text[at]);
            if (shell_string_) {
                const auto start = at;
                const auto quote = shell_string_;
                while (at < end) {
                    if (text[at] == '\\' && shell_string_ != '\'') { at = std::min(end, at + 2); continue; }
                    if (text[at++] == shell_string_) { shell_string_ = 0; break; }
                }
                shell_quoted(text, start, at, quote); continue;
            }
            if (std::isspace(c)) { assignment_value = false; ++at; continue; }
            if (c == '#' && (at == begin || std::isspace(static_cast<unsigned char>(text[at - 1])))) {
                mark(at, end, Kind::comment); break;
            }
            if (c == '<' && at + 1 < end && text[at + 1] == '<' && (at + 2 == end || text[at + 2] != '<')) {
                const auto start = at; at += 2;
                const bool strip_tabs = at < end && text[at] == '-';
                if (strip_tabs) ++at;
                mark(start, at, Kind::punctuation);
                while (at < end && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
                const auto delimiter_start = at;
                const char quote = at < end && (text[at] == '\'' || text[at] == '"') ? text[at++] : 0;
                const auto value_start = at;
                while (at < end && (quote ? text[at] != quote
                    : identifier(static_cast<unsigned char>(text[at])))) ++at;
                const auto delimiter = std::string(text.substr(value_start, at - value_start));
                if (quote && at < end && text[at] == quote) ++at;
                mark(delimiter_start, at, Kind::string);
                if (!delimiter.empty() && delimiter.size() <= 128)
                    documents_.push_back({delimiter, text.substr(begin, end - begin).find("python") != std::string_view::npos, strip_tabs});
                continue;
            }
            if (std::string_view(";&|<>()").find(static_cast<char>(c)) != std::string_view::npos) {
                mark(at, at + 1, Kind::punctuation);
                if (c == ';' || c == '&' || c == '|' || c == '(') { command = true; assignment_value = false; }
                ++at; continue;
            }
            if (c == '\'' || c == '"') {
                const auto start = at++; shell_string_ = static_cast<char>(c);
                while (at < end) {
                    if (text[at] == '\\' && shell_string_ != '\'') { at = std::min(end, at + 2); continue; }
                    if (text[at++] == shell_string_) { shell_string_ = 0; break; }
                }
                shell_quoted(text, start, at, static_cast<char>(c)); if (!assignment_value) command = false; continue;
            }
            if (c == '$') {
                const auto start = at++;
                if (at < end && text[at] == '{') {
                    while (at < end && text[at++] != '}') {}
                } else while (at < end && (identifier(static_cast<unsigned char>(text[at])) || text[at] == '?')) ++at;
                mark(start, at, Kind::variable); if (!assignment_value) command = false; continue;
            }
            const auto start = at;
            while (at < end && !std::isspace(static_cast<unsigned char>(text[at]))
                && std::string_view(";&|<>()'\"$").find(text[at]) == std::string_view::npos) {
                if (text[at] == '\\' && at + 1 < end) at += 2;
                else ++at;
            }
            const auto word = text.substr(start, at - start);
            const auto equal = word.find('=');
            const bool assignment = equal != std::string_view::npos && equal > 0 && starts_identifier(static_cast<unsigned char>(word.front()))
                && std::all_of(word.begin(), word.begin() + equal, [](unsigned char v) { return identifier(v); });
            if (assignment && command) {
                assignment_value = true;
                mark(start, start + equal, Kind::variable); mark(start + equal, start + equal + 1, Kind::punctuation);
                mark(start + equal + 1, at, Kind::string);
            } else if (assignment_value && command) mark(start, at, Kind::string);
            else if (one_of(word, " if then else elif fi for in do done while until case esac function select export local return ")) {
                mark(start, at, Kind::keyword); command = true;
            } else if (word.starts_with('-') && word.size() > 1) mark(start, at, Kind::option);
            else if (command) {
                mark(start, at, Kind::command);
                command = one_of(word, " sudo env command exec ");
            } else if (!word.empty() && std::all_of(word.begin(), word.end(), [](unsigned char v) { return std::isdigit(v) || v == '.'; }))
                mark(start, at, Kind::number);
        }
    }
    static const std::string& color(Kind kind) {
        const auto& caps = terminal_capabilities();
        switch (kind) {
            case Kind::command: return caps.syntax_command;
            case Kind::option: case Kind::number: case Kind::keyword: return caps.context_mode;
            case Kind::string: return caps.context_path;
            case Kind::variable: return caps.context_activity;
            case Kind::punctuation: return caps.syntax_operator;
            case Kind::comment: return caps.muted_color;
            default: return caps.white_color;
        }
    }
public:
    explicit SyntaxHighlight(std::string_view text) : kinds_(text.size(), Kind::plain) {
        for (std::size_t begin = 0; begin < text.size();) {
            const auto newline = text.find('\n', begin);
            const auto end = newline == std::string_view::npos ? text.size() : newline;
            if (!documents_.empty()) {
                const auto& document = documents_.front();
                auto delimiter_begin = begin;
                if (document.strip_tabs) while (delimiter_begin < end && text[delimiter_begin] == '\t') ++delimiter_begin;
                if (text.substr(delimiter_begin, end - delimiter_begin) == document.delimiter) {
                    mark(begin, end, Kind::string); documents_.pop_front(); python_string_.clear();
                } else if (document.python) python(text, begin, end);
                else mark(begin, end, Kind::string);
            } else shell(text, begin, end);
            begin = end == text.size() ? end : end + 1;
        }
    }
    std::string render(std::string_view text, std::size_t offset = 0) const {
        const auto& caps = terminal_capabilities();
        if (caps.white_color.empty()) return std::string(text);
        std::string result;
        Kind current = Kind::plain;
        result += color(current);
        for (std::size_t i = 0; i < text.size();) {
            const auto kind = offset + i < kinds_.size() ? kinds_[offset + i] : Kind::plain;
            if (kind != current) { current = kind; result += color(kind); }
            // Formatting may change only between UTF-8 characters, never between
            // a leading byte and its continuation bytes (including inserted …).
            auto end = i + 1;
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
            result.append(text.substr(i, end - i));
            i = end;
        }
        return result + caps.normal;
    }
};
}
