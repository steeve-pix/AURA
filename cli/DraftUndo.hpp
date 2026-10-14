#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>

namespace aura::cli {
class DraftUndo {
    struct State { std::string text; std::size_t cursor; };
    enum class Kind { atomic, typing, erasing };
    std::deque<State> undo_, redo_;
    State current_{};
    Kind previous_ = Kind::atomic;
    std::chrono::steady_clock::time_point last_{};
    std::uint64_t revision_ = 0;
    static void bound(std::deque<State>& stack) {
        std::size_t bytes = 0;
        for (const auto& item : stack) bytes += item.text.size();
        while (!stack.empty() && (stack.size() > 100 || bytes > 2 * 1024 * 1024)) {
            bytes -= stack.front().text.size(); stack.pop_front();
        }
    }
    void record(State before, const std::string& text, std::size_t cursor, Kind kind) {
        if (before.text == text) { previous_ = Kind::atomic; current_ = {text, cursor}; return; }
        const auto now = std::chrono::steady_clock::now();
        const bool join = kind != Kind::atomic && kind == previous_
            && before.text == current_.text && before.cursor == current_.cursor
            && now - last_ < std::chrono::milliseconds(700);
        if (!join) undo_.push_back(std::move(before));
        bound(undo_); redo_.clear();
        current_ = {text, cursor}; previous_ = kind; last_ = now;
    }
public:
    void reset(const std::string& text, std::size_t cursor) {
        undo_.clear(); redo_.clear(); current_ = {text, cursor}; previous_ = Kind::atomic; ++revision_;
    }
    void sync(const std::string& text, std::size_t cursor) {
        if (current_.text != text) reset(text, cursor);
        else current_.cursor = cursor;
    }
    bool restore(bool redo, std::string& text, std::size_t& cursor) {
        auto& from = redo ? redo_ : undo_;
        auto& to = redo ? undo_ : redo_;
        ++revision_; previous_ = Kind::atomic;
        if (from.empty()) return false;
        to.push_back({text, cursor}); bound(to);
        auto state = std::move(from.back()); from.pop_back();
        text = std::move(state.text); cursor = std::min(state.cursor, text.size());
        current_ = {text, cursor}; return true;
    }
    // Every input event is a transaction, including early returns and paste.
    class Edit {
        DraftUndo& owner_;
        std::string& text_;
        std::size_t& cursor_;
        State before_;
        Kind kind_;
        std::uint64_t revision_;
    public:
        Edit(DraftUndo& owner, std::string& text, std::size_t& cursor, unsigned char key, bool atomic = false)
            : owner_(owner), text_(text), cursor_(cursor), before_{text, cursor},
              kind_(atomic ? Kind::atomic : key == 127 || key == 8 ? Kind::erasing
                  : key > 32 && key != 127 ? Kind::typing : Kind::atomic), revision_(owner.revision_) {}
        ~Edit() {
            if (revision_ == owner_.revision_) owner_.record(std::move(before_), text_, cursor_, kind_);
        }
        Edit(const Edit&) = delete;
        Edit& operator=(const Edit&) = delete;
    };
};
}
