#pragma once

#include <array>
#include <string>
#include <vector>
#include <map>
#include "ui/BalanceReadout.hpp"

namespace aura::cli {
    // Read-only presentation of the CLI worker's private AURA_NN telemetry.
    class NNTrainingScreen {
        struct Tensor {
            std::string name, shape, label;
            int count = 0;
            double minimum = 0, maximum = 0, norm = 0, gradient = 0, change = 0;
        };

        struct Objective {
            bool valid = false, hasReturn = false;
            int steps = 0;
            double advantage = 0, logProbability = 0, totalReturn = 0;
        };

        std::vector<Tensor> tensors_;
        std::vector<double> scores_, losses_, means_, evaluations_, evaluationBalanced_;
        std::vector<int> lossUpdates_;
        int chartMode_ = 0;
        std::vector<int> evaluationUpdates_;
        std::vector<double> rewardReturns_, rewardRates_, balancedDurations_;
        std::vector<int> balancedIndices_;
        std::vector<int> rewardIndices_;
        int rewardBatch_ = 0, rewardEpisode_ = 0, rewardSteps_ = 0;
        int completedRewardBatch_ = 0, completedRewardEpisode_ = 0;
        double stepReward_ = 0, episodeReward_ = 0, rewardBatchReturn_ = 0;
        int rewardBatchEpisodes_ = 0;
        bool rewardSeen_ = false, learningDetails_ = false;
        BalanceReadout trainingBalance_, deterministicBalance_, stochasticBalance_, bestBalance_;
        double criticLoss_ = 0;
        bool criticLossSeen_ = false;
        bool actorCritic_ = false;
        int agents_ = 1, runBatch_ = 0, runBatches_ = 1;
        int arenaContacts_ = 0, selectedAgent_ = 0;
        double arenaSpacing_ = 12;
        struct ArenaAgent {
            int steps = 0, episode = 0, completed = 0, lastFinished = 0;
            std::string state = "waiting";
            bool done = false;
            double quality = 0, reward = 0, height = 0, upright = 0, stepReward = 0;
            double returnSum = 0, returnMin = 0, returnMax = 0, balancedSum = 0, qualitySum = 0;
            int recoverySum = 0, falls = 0;
            BalanceReadout balance;
            std::vector<double> targets;
        };
        std::vector<ArenaAgent> arenaAgents_{1};
        std::vector<std::vector<double>> workerSteps_, workerSeconds_;
        bool onlySelectedWorker_ = false, independentWorkers_ = false;

        struct Layer {
            int inputs = 0, outputs = 0;
            std::string activation;
        };

        struct Action {
            std::string key, label;
            double scale = 0, value = 0;
            bool nonnegative = false, motorKnown = false, enabled = true;
        };

        std::vector<Objective> objectives_;
        std::vector<Layer> layers_;
        std::vector<Action> actions_;
        std::string device_ = "pending";
        bool deviceInfo_ = false, runTimingSeen_ = false;
        std::map<std::string, std::string> deviceMetrics_;
        int runCompleted_ = 0, runEpisodes_ = 0;
        double runElapsed_ = 0., runEta_ = -1.;
        int inputs_ = 0, outputs_ = 0, activeMotors_ = -1, totalMotors_ = 0;
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
        double sigma_ = 0, sampleLow_ = 0, sampleHigh_ = 0;
        double discount_ = 0;
        std::array<int, 3> scroll_{};
        int scrollMax_ = 0, viewport_ = 20;
        int episodeScroll_ = 0, episodeRows_ = 7;
        bool episodeFocus_ = true;
        int episodeLeft_ = 0, episodeTop_ = 0, episodeWidth_ = 0, episodeHeight_ = 0;
        int page_ = 1, selected_ = 0, element_ = 0, inspectedTensor_ = -1, inspectedElement_ = -1;
        int batch_ = 1, episode_ = 0, steps_ = 0, updates_ = 0, inspectedUpdate_ = 0, count_ = 0;
        int episodeLimit_ = 0, batchSize_ = 0, seed_ = 0, objectiveUpdate_ = 0;
        double height_ = 0, upright_ = 0, loss_ = 0, gradient_ = 0, change_ = 0;
        double lr_ = 0, beta1_ = 0, beta2_ = 0, epsilon_ = 0;
        std::array<double, 5> scalar_{}; // before, after, gradient, first moment, second moment
        std::string coordinates_, phase_ = "starting", error_, diagnosticError_, warning_, outcome_;
        bool paused_ = false, expandedMath_ = false, expandedTargets_ = false, lossChart_ = false;

    public:
        void receive(const std::string &line);

        std::string key(const std::string &key);

        void wheel(int direction, int column, int row);

        std::vector<std::string> frame(int width, int rows, bool exited, bool failed = false);

        int updates() const { return updates_; }
        const std::string &error() const { return error_.empty() ? diagnosticError_ : error_; }
    };
}
