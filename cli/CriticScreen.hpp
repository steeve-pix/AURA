#pragma once
#include <string>
#include <vector>

namespace aura::cli {
class CriticScreen {
    std::string source_, phase_="loading", error_, report_, checkpoint_;
    int inputs_=0, actions_=0, trainingEpisodes_=32, validationEpisodes_=16, budget_=20, seed_=43;
    int collectedTraining_=0, collectedValidation_=0, steps_=0, trainingCount_=0, validationCount_=0;
    double rate_=.001, discount_=.995, scale_=100, baseline_=-1, constant_=0, actorChange_=0, criticChange_=0;
    bool unchanged_=false;
    std::vector<double> training_, validation_;
    std::vector<int> updates_;
    int scroll_=0, viewport_=20, scrollMax_=0;
    double elapsed_=0, progressAt_=0;
public:
    void receive(const std::string& line);
    std::string key(const std::string& key);
    std::vector<std::string> frame(int width,int rows,bool exited,double elapsedSeconds=0);
    const std::string& error() const { return error_; }
};
}
