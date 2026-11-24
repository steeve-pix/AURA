#pragma once
#include "ui/ScreenCanvas.hpp"
#include <sstream>

namespace aura::cli {
    struct BalanceReadout {
        bool valid = false;
        int balanced = 0, unbalanced = 0, longest = 0, recoveries = 0;
        double seconds = 0, falls = 0, quality = 0, reward = 0;
        std::string reason;

        bool read(std::istream &in) {
            BalanceReadout next;
            if (!(in >> next.balanced >> next.seconds >> next.unbalanced >> next.longest >> next.recoveries
                  >> next.falls >> next.quality >> next.reward >> next.reason)) return false;
            if (next.balanced < 0 || next.unbalanced < 0 || next.longest < 0 || next.recoveries < 0 ||
                !std::isfinite(next.seconds) || !std::isfinite(next.falls) || !std::isfinite(next.quality) ||
                !std::isfinite(next.reward) || next.seconds < 0 || next.falls < 0 || next.falls > 1 ||
                next.quality < 0 || next.quality > 1) return false;
            next.valid = true;
            *this = next;
            return true;
        }

        std::string duration() const {
            using namespace screen;
            return "Balanced " + integer(balanced) + " / " + number(seconds, 2) + " s   Unbalanced " +
                   integer(unbalanced) + "   Longest " + integer(longest) + "   Recoveries " + integer(recoveries);
        }
        std::string outcome() const {
            using namespace screen;
            return "Quality " + number(quality, 3) + "   Reward " + number(reward, 2) + "   Falls " +
                   number(falls * 100, 0) + "%   " + reason;
        }
    };
}
