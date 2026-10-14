#pragma once

#include "TerminalCapabilities.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// Shared cell-aware drawing and visual vocabulary for CLI dashboards.
namespace aura::cli::screen {
inline std::string glyph(const char* unicode, const char* ascii) {
    return terminal_capabilities().utf8 ? unicode : ascii;
}
inline std::string number(double value, int digits = 4, bool sign = false) {
    std::ostringstream out;
    if (sign) out << std::showpos;
    if (value != 0 && (std::abs(value) < .0001 || std::abs(value) >= 1e6)) out << std::scientific << std::setprecision(2);
    else out << std::fixed << std::setprecision(digits);
    out << value;
    return out.str();
}
inline std::string integer(int value) {
    std::string result = std::to_string(value);
    for (int at = static_cast<int>(result.size()) - 3; at > 0; at -= 3) result.insert(at, ",");
    return result;
}
inline std::string utf8(int codepoint) {
    if (codepoint < 128) return std::string(1, static_cast<char>(codepoint));
    if (codepoint < 2048) return {static_cast<char>(0xc0 | codepoint >> 6), static_cast<char>(0x80 | (codepoint & 63))};
    return {static_cast<char>(0xe0 | codepoint >> 12), static_cast<char>(0x80 | ((codepoint >> 6) & 63)), static_cast<char>(0x80 | (codepoint & 63))};
}
inline std::string shapeLabel(std::string shape) {
    if (terminal_capabilities().utf8) {
        for (std::size_t at=0; (at=shape.find('x',at))!=std::string::npos; at+=2)
            shape.replace(at,1,"×");
    }
    return shape;
}
inline int textCells(const std::string& text) {
    int width=0;
    for (std::size_t at=0; at<text.size();) {
        std::mbstate_t state{};
        wchar_t codepoint=0;
        const auto size=std::mbrtowc(&codepoint,text.data()+at,text.size()-at,&state);
        if (size==static_cast<std::size_t>(-1) || size==static_cast<std::size_t>(-2) || size==0) return width;
        width+=std::max(0,wcwidth(codepoint));
        at+=size;
    }
    return width;
}
inline std::string ellipsize(const std::string& text, int cells) {
    if (cells<=0) return {};
    if (textCells(text)<=cells) return text;
    const auto mark=glyph("…","...");
    const int room=std::max(0,cells-textCells(mark));
    std::size_t end=0;
    int used=0;
    while (end<text.size()) {
        std::mbstate_t state{};
        wchar_t codepoint=0;
        const auto bytes=std::mbrtowc(&codepoint,text.data()+end,text.size()-end,&state);
        if (!bytes || bytes==static_cast<std::size_t>(-1) || bytes==static_cast<std::size_t>(-2)) break;
        const int span=std::max(0,wcwidth(codepoint));
        if (used+span>room) break;
        used+=span; end+=bytes;
    }
    return text.substr(0,end)+(textCells(mark)<=cells?mark:std::string(cells,'.'));
}
struct Palette {
    const TerminalCapabilities& caps = terminal_capabilities();
    std::string text = caps.white_color;
    std::string dim = caps.muted_color.empty() ? caps.faint : caps.muted_color;
    std::string green = caps.context_path;
    std::string purple = caps.context_activity;
    std::string gold = caps.context_mode.empty() ? caps.faint : caps.context_mode;
    std::string bold = caps.strong + caps.white_color;
    std::string selected = caps.shell_style + caps.strong;
};
// Place glyphs by terminal cells, not UTF-8 bytes. A frame contains only complete
// glyphs and styles, so resizing cannot split an arrow, formula or border.
class Canvas {
    struct Cell { std::string text = " ", style; };
    std::vector<std::vector<Cell>> cells_;
public:
    int width, height;
    Canvas(int w, int h) : cells_(std::max(1,h), std::vector<Cell>(std::max(1,w))), width(std::max(1,w)), height(std::max(1,h)) {}
    void put(int x, int y, const std::string& text, const std::string& style = "", int limit = -1) {
        if (y < 0 || y >= height) return;
        const int end = limit < 0 ? width : std::min(width, x + limit);
        for (std::size_t at = 0; at < text.size() && x < end;) {
            const unsigned char lead = text[at];
            const int bytes = lead < 128 ? 1 : lead < 224 ? 2 : lead < 240 ? 3 : 4;
            if (at + bytes > text.size()) break;
            std::string character = text.substr(at, bytes);
            at += bytes;
            if (lead < 32 || lead == 127) character = " ";
            std::mbstate_t state{};
            wchar_t decoded = 0;
            const auto consumed = std::mbrtowc(&decoded, character.data(), character.size(), &state);
            const int span = consumed == static_cast<std::size_t>(-1) || consumed == static_cast<std::size_t>(-2)
                ? 1 : std::max(0, wcwidth(decoded));
            if (span == 0) {
                if (x > 0 && x <= width) cells_[y][x-1].text += character;
                continue;
            }
            if (x + span > end) break;
            if (x >= 0) {
                cells_[y][x] = {character, style};
                for (int i = 1; i < span; ++i) cells_[y][x+i] = {"", style};
            }
            x += span;
        }
    }
    void rule(int x, int y, int length, const std::string& style) {
        for (int i = 0; i < length; ++i) put(x+i, y, glyph("─", "-"), style);
    }
    void heading(int x,int w,const std::string& title,const Palette& p,
                 const std::string& status="",const std::string& statusStyle="") {
        const int reserved=status.empty()?0:textCells(status)+2;
        put(x,1,glyph("◇","*")+" AURA",p.bold,w-reserved);
        put(x+8,1,"/ "+title,p.dim,w-8-reserved);
        if (!status.empty() && w>=40) put(x+w-textCells(status),1,status,statusStyle,textCells(status));
    }
    void footer(int x,int w,const std::string& actions,const std::string& back,
                const std::string& note,const Palette& p) {
        rule(x,height-3,w,p.dim);
        const int reserved=textCells(back)+2;
        put(x,height-2,actions,p.text,std::max(0,w-reserved));
        put(x+w-textCells(back),height-2,back,p.purple,textCells(back));
        put(x,height-1,note,p.dim,w);
    }
    void fraction(int x, int y, const std::string& numerator, const std::string& denominator,
                  const std::string& style) {
        const int span=std::max(textCells(numerator),textCells(denominator))+2;
        put(x+(span-textCells(numerator))/2,y,numerator,style);
        rule(x,y+1,span,style);
        put(x+(span-textCells(denominator))/2,y+2,denominator,style);
    }
    void box(int x, int y, int w, int h, const std::string& title, const Palette& p) {
        rule(x+1,y,w-2,p.dim); rule(x+1,y+h-1,w-2,p.dim);
        for (int row = 1; row < h-1; ++row) {
            put(x,y+row,glyph("│","|"),p.dim); put(x+w-1,y+row,glyph("│","|"),p.dim);
        }
        put(x,y,glyph("╭","+"),p.dim); put(x+w-1,y,glyph("╮","+"),p.dim);
        put(x,y+h-1,glyph("╰","+"),p.dim); put(x+w-1,y+h-1,glyph("╯","+"),p.dim);
        put(x+2,y," " + title + " ",p.bold,w-4);
    }
    void copy(const Canvas& source, int x, int y, int start, int count) {
        for (int row = 0; row < count; ++row) {
            if (y+row < 0 || y+row >= height || start+row >= source.height) continue;
            for (int col = 0; col < source.width && x+col < width; ++col)
                if (x+col >= 0) cells_[y+row][x+col] = source.cells_[start+row][col];
        }
    }
    std::vector<std::string> lines() const {
        std::vector<std::string> result(height);
        const auto& reset = terminal_capabilities().normal;
        for (int y = 0; y < height; ++y) {
            int last = width-1;
            while (last >= 0 && cells_[y][last].text == " " && cells_[y][last].style.empty()) --last;
            std::string active;
            for (int x = 0; x <= last; ++x) {
                const auto& cell = cells_[y][x];
                if (cell.style != active) { result[y] += reset + cell.style; active = cell.style; }
                result[y] += cell.text;
            }
            if (!active.empty()) result[y] += reset;
        }
        return result;
    }
};

}
