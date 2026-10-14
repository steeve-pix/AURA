#pragma once

#include <array>
#include <string>
#include <vector>

namespace aura::cli {
// Read-only presentation of the CLI worker's private AURA_NN telemetry.
class NNTrainingScreen {
    struct Tensor {
        std::string name, shape, label;
        int count = 0;
        double minimum = 0, maximum = 0, norm = 0, gradient = 0, change = 0;
    };
    struct Objective { bool valid = false, hasReturn = false; int steps = 0; double advantage = 0, logProbability = 0, totalReturn = 0; };
    std::vector<Tensor> tensors_;
    std::vector<double> scores_, losses_, means_, evaluations_;
    std::vector<int> evaluationUpdates_;
    std::vector<double> rewardReturns_, rewardRates_;
    std::vector<int> rewardIndices_;
    int rewardBatch_=0, rewardEpisode_=0, rewardSteps_=0;
    int completedRewardBatch_=0, completedRewardEpisode_=0;
    double stepReward_=0, episodeReward_=0, rewardBatchReturn_=0;
    int rewardBatchEpisodes_=0;
    bool rewardSeen_=false, learningDetails_=false;
    struct Layer { int inputs=0, outputs=0; std::string activation; };
    struct Action { std::string key, label; double scale=0, value=0; bool nonnegative=false, motorKnown=false, enabled=true; };
    std::vector<Objective> objectives_;
    std::vector<Layer> layers_;
    std::vector<Action> actions_;
    std::string device_="pending";
    int inputs_=0, outputs_=0, activeMotors_=-1, totalMotors_=0;
    std::string disabledMotors_;
    std::string checkpoint_;
    std::string model_, trainingMode_;
    int completedEpisodes_ = 0;
    int episodeOffset_ = 0;
    int evaluationUpdate_ = 0, evaluationSteps_ = 0, previousEvaluationSteps_ = -1;
    bool evaluationSeen_ = false;
    int bestEvaluationSteps_ = -1;
    std::string bestCheckpoint_;
    bool visual_ = false;
    std::string viewerError_;
    double sigma_=0, sampleLow_=0, sampleHigh_=0;
    double discount_=0;
    std::array<int, 3> scroll_{};
    int scrollMax_ = 0, viewport_ = 20;
    int page_ = 1, selected_ = 0, element_ = 0, inspectedTensor_ = -1, inspectedElement_ = -1;
    int batch_ = 1, episode_ = 0, steps_ = 0, updates_ = 0, inspectedUpdate_ = 0, count_ = 0;
    int episodeLimit_ = 0, batchSize_ = 0, seed_ = 0, objectiveUpdate_ = 0;
    double height_ = 0, upright_ = 0, loss_ = 0, gradient_ = 0, change_ = 0;
    double lr_ = 0, beta1_ = 0, beta2_ = 0, epsilon_ = 0;
    std::array<double, 5> scalar_{}; // before, after, gradient, first moment, second moment
    std::string coordinates_, phase_ = "starting", error_, outcome_;
    bool paused_ = false, expandedMath_ = false, expandedTargets_ = false, lossChart_ = false;
public:
    void receive(const std::string& line);
    std::string key(const std::string& key);
    std::vector<std::string> frame(int width, int rows, bool exited);
    int updates() const { return updates_; }
    const std::string& error() const { return error_; }
};
}
