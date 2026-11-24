#pragma once
#include "ui/ScreenCanvas.hpp"

namespace aura::cli {
    inline std::vector<std::string> training_exit_frame(int width, int rows) {
        const screen::Palette palette;
        screen::Canvas modal(width, rows);
        const int w = std::max(1, std::min(86, width - 6)), x = (width - w) / 2;
        const int y = std::max(1, (rows - 12) / 2);
        modal.heading(x, w, "LEAVE TRAINING?", palette, "CONFIRM", palette.gold);
        if (width < 55 || rows < 15) {
            modal.put(x, 3, "Exit training?", palette.text, w);
            modal.shortcuts(x, rows - 2, "Q / Esc exit   N stay", palette, w);
            return modal.lines();
        }
        modal.box(x, y, w, 10, "TRAINING SESSION", palette);
        modal.put(x+2, y+2, "Training is still running while you decide.", palette.text, w-4);
        modal.put(x+2, y+4, "Your last saved checkpoint is preserved.", palette.green, w-4);
        modal.put(x+2, y+5, "An unfinished update will be discarded on exit.", palette.dim, w-4);
        modal.shortcuts(x+2, y+7, "Q / Esc confirm exit   N keep training", palette, w-4);
        return modal.lines();
    }
}
