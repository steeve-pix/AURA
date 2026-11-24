#pragma once
#include <string>
#include <chrono>
#include <vector>
#include "ui/BalanceReadout.hpp"

namespace aura::cli {
    class EvaluationScreen {
        std::string model_, phase_ = "loading", outcome_, error_;
        int episodes_ = 0, updates_ = 0, limit_ = 0, steps_ = 0, previous_ = 0;
        double seconds_ = 0;
        bool hasPrevious_ = false;
        int inputs_ = 0, outputs_ = 0, activeMotors_ = -1, totalMotors_ = 0;
        int agents_ = 1;
        std::string disabledMotors_;
        BalanceReadout deterministicBalance_, stochasticBalance_;
        std::string activity_ = "Starting evaluation worker", pass_;
        std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now(), last_event_ = started_;

    public:
        void receive(const std::string &line);

        std::string key(const std::string &) { return ""; }

        std::vector<std::string> frame(int width, int rows, bool exited);

        const std::string &error() const { return error_; }
    };
}
