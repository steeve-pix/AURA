#pragma once
#include <string>
#include <vector>

namespace aura::cli {
class EvaluationScreen {
    std::string model_, phase_="loading", outcome_, error_;
    int episodes_=0, updates_=0, limit_=0, steps_=0, previous_=0;
    double seconds_=0;
    bool hasPrevious_=false;
    int inputs_=0, outputs_=0, activeMotors_=-1, totalMotors_=0;
    std::string disabledMotors_;
public:
    void receive(const std::string& line);
    std::string key(const std::string&) { return ""; }
    std::vector<std::string> frame(int width,int rows,bool exited);
    const std::string& error() const { return error_; }
};
}
