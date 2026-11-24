#include "screens/NNTraining.hpp"
#include "ui/ScreenCanvas.hpp"
#include "ui/TrainingCharts.hpp"
#include "terminal/TerminalCapabilities.hpp"
#include "screens/ControlVectors.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>

namespace aura::cli {
    namespace {
        using namespace screen;

        std::string duration(double seconds) {
            if (seconds < 0) return "warming up";
            const auto value = static_cast<long long>(std::min(seconds, 315360000.));
            if (value >= 3600) return std::to_string(value / 3600) + "h " + std::to_string(value / 60 % 60) + "m";
            if (value >= 60) return std::to_string(value / 60) + "m " + std::to_string(value % 60) + "s";
            return std::to_string(value) + "s";
        }

        std::string tensorLabel(const std::string &name) {
            const auto dot = name.rfind('.');
            return dot == std::string::npos ? name : name.substr(0, dot) + " / " + name.substr(dot + 1);
        }

        std::string subscript(int index) {
            const char *digits[]{"₀", "₁", "₂", "₃", "₄", "₅", "₆", "₇", "₈", "₉"};
            std::string result;
            for (char digit: std::to_string(index)) result += terminal_capabilities().utf8
                                                                  ? digits[digit - '0']
                                                                  : std::string(1, digit);
            return result;
        }
    }

    void NNTrainingScreen::receive(const std::string &line) {
        if (!line.starts_with("AURA_NN\t")) {
            // stderr includes warnings, traceback frames and source excerpts. Only
            // structured error events fail a live session; exit status handles crashes.
            const auto warningAt = line.find("Warning:");
            if (warningAt != std::string::npos) {
                warning_ = line.substr(warningAt + 8);
                if (warning_.find("Failed to initialize NumPy") != std::string::npos)
                    warning_ = device_ == "pending" ? "NumPy is unavailable; training can continue." : "NumPy is unavailable; training can continue on " + device_ + ".";
            } else if (!line.empty() && line.front() != ' ' && line.front() != '\t' &&
                       !line.starts_with("Traceback ")) {
                diagnosticError_ = line;
            }
            return;
        }
        std::istringstream input(line.substr(8));
        std::string kind;
        input >> kind;
        if (kind == "error") std::getline(input >> std::ws, error_);
        else if (kind == "collector") {
            std::string mode; input >> mode; independentWorkers_ = mode == "independent";
        }
        else if (kind == "worker_history") {
            int worker, index; double steps, seconds;
            if (input >> worker >> index >> steps >> seconds && worker>=1 && worker<=64 && steps>=0 && seconds>=0) {
                workerSteps_.resize(std::max(worker, int(workerSteps_.size())));
                workerSeconds_.resize(std::max(worker, int(workerSeconds_.size())));
                workerSteps_[worker-1].push_back(steps); workerSeconds_[worker-1].push_back(seconds);
            }
        }
        else if (kind == "warning") std::getline(input >> std::ws, warning_);
        else if (kind == "history_update") {
            int update, steps, hasLoss; double loss, mean, seconds;
            if (input >> update >> loss >> mean >> steps >> seconds >> hasLoss && update >= 0 &&
                std::isfinite(loss) && std::isfinite(mean) && std::isfinite(seconds)) {
                if (update > 0 && hasLoss) { losses_.push_back(loss); lossUpdates_.push_back(update); means_.push_back(mean); }
                if (steps >= 0) {
                    evaluations_.push_back(steps); evaluationUpdates_.push_back(update); evaluationBalanced_.push_back(std::max(0.,seconds));
                    evaluationSeen_ = true; evaluationUpdate_ = update; evaluationSteps_ = steps;
                }
            }
        } else if (kind == "history_episode") {
            int index; double steps, reward, seconds;
            if (input >> index >> steps >> reward >> seconds && index > 0 && steps > 0 &&
                std::isfinite(steps) && std::isfinite(reward) && std::isfinite(seconds)) {
                scores_.push_back(steps); rewardReturns_.push_back(reward); rewardRates_.push_back(reward/steps);
                rewardIndices_.push_back(index);
                if (seconds >= 0) { balancedDurations_.push_back(seconds); balancedIndices_.push_back(index); }
            }
        } else if (kind == "history_state") {
            double loss, critic, gradient, change;
            if (input >> loss >> critic >> gradient >> change && std::isfinite(loss) && std::isfinite(critic) &&
                std::isfinite(gradient) && std::isfinite(change)) {
                loss_ = loss; criticLoss_ = critic; criticLossSeen_ = true; gradient_ = gradient; change_ = change;
            }
        } else if (kind == "history_end") {
            int episodes, count;
            if (input >> episodes >> count) episodeOffset_ = episodes - static_cast<int>(scores_.size());
        } else if (kind == "start") {
            int batchSize, limit, seed, count;
            if (input >> batchSize >> limit >> seed >> count && batchSize >= 2 && limit > 0 && count > 0) {
                batchSize_ = batchSize;
                episodeLimit_ = limit;
                seed_ = seed;
                count_ = count;
                objectives_.assign(batchSize_, {});
            }
        } else if (kind == "schedule") {
            int episodes, batches, agents;
            if (input >> episodes >> batches >> agents && episodes >= 2 && episodes <= 128 &&
                batches >= 1 && batches <= 1000 && agents >= 1 && agents <= 64) {
                agents_ = agents; arenaAgents_.resize(agents); workerSteps_.resize(agents); workerSeconds_.resize(agents); runBatches_ = batches;
            }
        } else if (kind == "run_progress") {
            int current, total, agents;
            if (input >> current >> total >> agents && current >= 1 && current <= total && total <= 1000 && agents >= 1 && agents <= 64) {
                runBatch_ = current; runBatches_ = total; agents_ = agents; arenaAgents_.resize(agents); workerSteps_.resize(agents); workerSeconds_.resize(agents);
            }
        } else if (kind == "run_timing") {
            int completed, total, episodes;
            double elapsed, eta;
            if (input >> completed >> total >> elapsed >> eta >> episodes &&
                total >= 1 && total <= 1000 && completed >= 0 && completed <= total &&
                episodes >= 0 && episodes <= 128 && std::isfinite(elapsed) && elapsed >= 0 &&
                std::isfinite(eta) && eta >= -1) {
                runCompleted_ = completed; runBatches_ = total; runElapsed_ = elapsed;
                runEta_ = eta; runEpisodes_ = episodes; runTimingSeen_ = true;
            }
        } else if (kind == "device_metric") {
            std::string name, value;
            if (input >> name && std::getline(input >> std::ws, value) && value.size() <= 240)
                deviceMetrics_[name] = value;
        } else if (kind == "arena_agent") {
            int agent, done, contacts;
            ArenaAgent value;
            if (input >> agent >> value.steps >> value.state >> value.quality >> value.reward >> done >> contacts &&
                agent >= 1 && agent <= agents_ && value.steps >= 0 && std::isfinite(value.quality) &&
                value.quality >= 0 && value.quality <= 1 && std::isfinite(value.reward) &&
                contacts >= 0 && (done == 0 || done == 1) &&
                (value.state == "BALANCED" || value.state == "UNBALANCED" || value.state == "FALLEN")) {
                auto &slot = arenaAgents_[agent - 1];
                slot.steps = value.steps; slot.state = value.state; slot.done=done;
                slot.quality = value.quality; slot.reward = value.reward;
                arenaContacts_ = contacts;
            }
        } else if (kind == "arena_spacing") {
            double spacing;
            if (input >> spacing && std::isfinite(spacing) && spacing >= 6 && spacing <= 30) arenaSpacing_ = spacing;
        } else if (kind == "agent_detail") {
            int index, episode;
            double height, upright, reward;
            BalanceReadout balance;
            if (!(input >> index >> episode >> height >> upright >> reward) || index < 1 || index > agents_ ||
                episode < 1 || episode > batchSize_ || !std::isfinite(height) || !std::isfinite(upright) ||
                !std::isfinite(reward) || !balance.read(input)) return;
            std::vector<double> values;
            double value;
            while (input >> value) values.push_back(value);
            if (values.size() != actions_.size() || !std::all_of(values.begin(), values.end(),
                    [](double v) { return std::isfinite(v); })) return;
            auto &agent = arenaAgents_[index - 1];
            agent.episode = episode; agent.height = height; agent.upright = upright;
            agent.stepReward = reward; agent.balance = balance; agent.targets = std::move(values);
        } else if (kind == "agent_result") {
            int index, episode;
            BalanceReadout result;
            if (!(input >> index >> episode) || index < 1 || index > agents_ || episode < 1 ||
                episode > batchSize_ || !result.read(input)) return;
            auto &agent = arenaAgents_[index - 1];
            if (episode <= agent.lastFinished) return;
            agent.lastFinished = episode;
            workerSteps_[index-1].push_back(agent.steps); workerSeconds_[index-1].push_back(result.seconds);
            if (!agent.completed) agent.returnMin = agent.returnMax = result.reward;
            agent.returnMin = std::min(agent.returnMin, result.reward);
            agent.returnMax = std::max(agent.returnMax, result.reward);
            ++agent.completed; agent.returnSum += result.reward;
            agent.balancedSum += result.seconds; agent.qualitySum += result.quality;
            agent.recoverySum += result.recoveries; agent.falls += result.falls >= .5;
            balancedDurations_.push_back(result.seconds);
            balancedIndices_.push_back(episodeOffset_ + static_cast<int>(scores_.size()) + 1);
        } else if (kind == "objective_contract") {
            actorCritic_ = true;
        } else if (kind == "critic_update") {
            double loss;
            if (input >> loss && std::isfinite(loss) && loss >= 0) {
                criticLoss_ = loss;
                criticLossSeen_ = true;
            }
        } else if (kind == "balance") {
            std::string label;
            input >> label;
            if (label == "training") {
                if (trainingBalance_.read(input) && agents_ == 1) {
                    balancedDurations_.push_back(trainingBalance_.seconds);
                    balancedIndices_.push_back(episodeOffset_ + static_cast<int>(scores_.size()) + 1);
                }
            }
            if (label == "deterministic") deterministicBalance_.read(input);
            if (label == "stochastic") stochasticBalance_.read(input);
            if (label == "best") bestBalance_.read(input);
        } else if (kind == "interface") {
            int inputs, outputs, observationSchema, actionSchema;
            if (input >> inputs >> outputs >> observationSchema >> actionSchema && inputs > 0 && outputs > 0) {
                inputs_ = inputs;
                outputs_ = outputs;
            }
        } else if (kind == "motors") {
            int active, total;
            if (input >> active >> total && total > 0 && active >= 0 && active <= total) {
                activeMotors_ = active;
                totalMotors_ = total;
                std::getline(input >> std::ws, disabledMotors_);
            }
        } else if (kind == "model") {
            std::getline(input >> std::ws, model_, '\t');
            input >> trainingMode_ >> completedEpisodes_ >> updates_;
            episodeOffset_ = completedEpisodes_;
            batch_ = updates_ + 1;
        } else if (kind == "viewer") {
            int active;
            if (input >> active) {
                visual_ = active != 0;
                viewerError_.clear();
            }
        } else if (kind == "viewer_error") std::getline(input >> std::ws, viewerError_);
        else if (kind == "checkpoint") {
            std::getline(input >> std::ws, checkpoint_, '\t');
            input >> completedEpisodes_;
            model_ = checkpoint_;
        } else if (kind == "optimizer") input >> lr_ >> beta1_ >> beta2_ >> epsilon_;
        else if (kind == "device") {
            input >> device_;
            if (warning_.starts_with("NumPy is unavailable;"))
                warning_ = "NumPy is unavailable; training can continue on " + device_ + ".";
        }
        else if (kind == "layer") {
            Layer layer;
            if (input >> layer.inputs >> layer.outputs >> layer.activation && layer.inputs > 0 && layer.outputs > 0)
                layers_.push_back(layer);
        } else if (kind == "credit") {
            double discount;
            if (input >> discount && std::isfinite(discount) && discount >= 0 && discount <= 1) discount_ = discount;
        } else if (kind == "sampling") input >> sigma_ >> sampleLow_ >> sampleHigh_;
        else if (kind == "action") {
            Action action;
            int nonnegative;
            if (input >> action.key >> action.scale >> nonnegative && std::isfinite(action.scale)) {
                action.nonnegative = nonnegative;
                std::getline(input >> std::ws, action.label);
                actions_.push_back(action);
            }
        } else if (kind == "motor") {
            std::string key;
            int enabled;
            if (input >> key >> enabled && (enabled == 0 || enabled == 1))
                for (auto &action: actions_) if (action.key == key) {
                    action.motorKnown = true;
                    action.enabled = enabled != 0;
                }
        } else if (kind == "tensor") {
            int index;
            Tensor tensor;
            if (input >> index >> tensor.name >> tensor.shape >> tensor.count && index >= 0 && index <= static_cast<int>
                (tensors_.size()) && tensor.count > 0) {
                std::getline(input >> std::ws, tensor.label);
                if (index >= static_cast<int>(tensors_.size())) tensors_.resize(index + 1);
                tensors_[index] = tensor;
            }
        } else if (kind == "phase") {
            input >> phase_;
            if (phase_ == "collecting") {
                episode_ = steps_ = 0;
                batch_ = updates_ + 1;
                rewardSeen_ = false;
                arenaAgents_.assign(agents_, {});
            }
        } else if (kind == "paused") {
            int value = 0;
            if (input >> value) paused_ = value;
        } else if (kind == "progress") {
            int batch, episode, steps;
            double height, upright;
            if (input >> batch >> episode >> steps >> height >> upright
                && batch > 0 && episode > 0 && steps >= 0 && std::isfinite(height) && std::isfinite(upright)) {
                std::vector<double> values;
                double value;
                while (input >> value) values.push_back(value);
                if (values.size() == actions_.size() && std::all_of(values.begin(), values.end(),
                                                                    [](double v) { return std::isfinite(v); })) {
                    batch_ = batch;
                    episode_ = episode;
                    steps_ = steps;
                    height_ = height;
                    upright_ = upright;
                    for (std::size_t i = 0; i < values.size(); ++i) actions_[i].value = values[i];
                }
            }
        } else if (kind == "reward") {
            int batch, episode, steps, finished;
            double reward, total;
            if (input >> batch >> episode >> steps >> reward >> total >> finished && batch > 0 && episode > 0 && episode
                <= batchSize_
                && steps > 0 && steps <= episodeLimit_ && (finished == 0 || finished == 1) && std::isfinite(reward) &&
                std::isfinite(total)
                && (batch > rewardBatch_ || (batch == rewardBatch_ && (
                                                 episode > rewardEpisode_ || (
                                                     episode == rewardEpisode_ && steps >= rewardSteps_))))) {
                rewardSeen_ = true;
                rewardBatch_ = batch;
                rewardEpisode_ = episode;
                rewardSteps_ = steps;
                stepReward_ = reward;
                episodeReward_ = total;
                if (finished && (batch > completedRewardBatch_ || (
                                     batch == completedRewardBatch_ && episode > completedRewardEpisode_))) {
                    if (batch != completedRewardBatch_) {
                        rewardBatchReturn_ = 0;
                        rewardBatchEpisodes_ = 0;
                    }
                    rewardBatchReturn_ += total;
                    ++rewardBatchEpisodes_;
                    completedRewardBatch_ = batch;
                    completedRewardEpisode_ = episode;
                    rewardReturns_.push_back(total);
                    rewardRates_.push_back(total / steps);
                    rewardIndices_.push_back(episodeOffset_ + static_cast<int>(scores_.size()) + 1);
                    if (rewardReturns_.size() > 120) {
                        rewardReturns_.erase(rewardReturns_.begin());
                        rewardRates_.erase(rewardRates_.begin());
                        rewardIndices_.erase(rewardIndices_.begin());
                    }
                }
            }
        } else if (kind == "episode") {
            int batch, episode;
            double score;
            std::string outcome;
            if (input >> batch >> episode >> score >> outcome && std::isfinite(score) && score >= 0) {
                scores_.push_back(score);
                outcome_ = outcome;
            }
        } else if (kind == "update") {
            int updates;
            double loss, mean, best, gradient, change;
            if (input >> updates >> loss >> mean >> best >> gradient >> change && updates > updates_
                && std::isfinite(loss) && std::isfinite(mean) && std::isfinite(gradient) && std::isfinite(change)) {
                updates_ = updates;
                loss_ = loss;
                gradient_ = gradient;
                change_ = change;
                losses_.push_back(loss);
                lossUpdates_.push_back(updates);
                means_.push_back(mean);
            }
        } else if (kind == "evaluation") {
            int update, steps;
            if (input >> update >> steps && update == updates_
                && (update > evaluationUpdate_ || (!evaluationSeen_ && update == 0))
                && steps >= 0 && steps <= episodeLimit_) {
                previousEvaluationSteps_ = evaluationSeen_ ? evaluationSteps_ : -1;
                evaluationUpdate_ = update;
                evaluationSteps_ = steps;
                evaluationSeen_ = true;
                evaluations_.push_back(steps);
                evaluationUpdates_.push_back(update);
                evaluationBalanced_.push_back(deterministicBalance_.valid ? deterministicBalance_.seconds : 0.);
            }
        } else if (kind == "best") {
            int steps;
            std::string checkpoint;
            if (input >> steps >> checkpoint && steps >= 0 && steps <= episodeLimit_) {
                bestEvaluationSteps_ = steps;
                bestCheckpoint_ = checkpoint;
            }
        } else if (kind == "stats") {
            int index = -1;
            input >> index;
            if (index >= 0 && index < static_cast<int>(tensors_.size())) {
                auto &tensor = tensors_[index];
                input >> tensor.minimum >> tensor.maximum >> tensor.norm >> tensor.gradient >> tensor.change;
            }
        } else if (kind == "inspect") {
            input >> inspectedTensor_ >> inspectedElement_ >> coordinates_ >> inspectedUpdate_;
            for (auto &value: scalar_) input >> value;
        } else if (kind == "objective") {
            int episode;
            Objective objective;
            if (input >> episode >> objective.steps >> objective.advantage >> objective.logProbability
                && episode >= 1 && episode <= batchSize_ && std::isfinite(objective.advantage) && std::isfinite(
                    objective.logProbability)) {
                input >> std::ws;
                if (!input.eof()) {
                    if (!(input >> objective.totalReturn) || !std::isfinite(objective.totalReturn)) return;
                    objective.hasReturn = true;
                }
                if (objectiveUpdate_ != updates_) {
                    std::fill(objectives_.begin(), objectives_.end(), Objective{});
                    objectiveUpdate_ = updates_;
                    episodeScroll_ = 0;
                }
                objective.valid = true;
                objectives_[episode - 1] = objective;
            }
        } else if (kind == "stopped") phase_ = "stopped";
    }

    std::string NNTrainingScreen::key(const std::string &key) {
        if (key == "d" || key == "D") {
            deviceInfo_ = !deviceInfo_;
            if (deviceInfo_) deviceMetrics_.clear();
            scroll_[page_ - 1] = 0;
            return deviceInfo_ ? "device_info 1" : "device_info 0";
        }
        if (key == "1" || key == "2" || key == "3") {
            page_ = key[0] - '0';
            if (deviceInfo_) { deviceInfo_ = false; return "device_info 0"; }
            return "";
        }
        if (deviceInfo_) {
            if (key == "\033[A" || key == "\033OA" || key == "scroll_up" || key == "\033[5~")
                scroll_[page_ - 1] = std::max(0, scroll_[page_ - 1] - 3);
            else if (key == "\033[B" || key == "\033OB" || key == "scroll_down" || key == "\033[6~")
                scroll_[page_ - 1] = std::min(scrollMax_, scroll_[page_ - 1] + 3);
            if (key != "p" && key != "P" && key != " " && key != "v" && key != "V" && key != "n" && key != "N")
                return "";
        }
        if (agents_ > 1 && (page_ == 1 || page_ == 3) &&
            (key == "\033[D" || key == "\033OD" || key == "\033[C" || key == "\033OC")) {
            const bool left = key == "\033[D" || key == "\033OD";
            selectedAgent_ = (selectedAgent_ + (left ? agents_ - 1 : 1)) % agents_;
            return "worker " + std::to_string(selectedAgent_);
        }
        if ((key == "h" || key == "H") && page_ == 1) { onlySelectedWorker_ = !onlySelectedWorker_; return ""; }
        if ((key == "f" || key == "F") && page_ == 2) {
            expandedMath_ = !expandedMath_;
            return "";
        }
        if ((key == "t" || key == "T") && page_ == 1) {
            expandedTargets_ = !expandedTargets_;
            scroll_[0] = 0;
            return "";
        }
        if ((key == "g" || key == "G") && page_ == 1) {
            chartMode_ = (chartMode_ + 1) % 3;
            lossChart_ = chartMode_ == 2;
            return "";
        }
        if ((key == "r" || key == "R") && page_ == 3) {
            learningDetails_ = !learningDetails_;
            scroll_[2] = 0;
            episodeFocus_ = true;
            return "";
        }
        if (page_ == 3 && learningDetails_) {
            if (key == "\t") {
                episodeFocus_ = !episodeFocus_;
                if (episodeFocus_) scroll_[2] = 0;
                return "";
            }
            if (episodeFocus_) {
                const int limit = std::max(0, batchSize_ - episodeRows_);
                bool handled = true;
                if (key == "\033[A" || key == "\033OA") episodeScroll_ -= 1;
                else if (key == "\033[B" || key == "\033OB") episodeScroll_ += 1;
                else if (key == "scroll_up") episodeScroll_ -= 3;
                else if (key == "scroll_down") episodeScroll_ += 3;
                else if (key == "\033[5~") episodeScroll_ -= episodeRows_;
                else if (key == "\033[6~") episodeScroll_ += episodeRows_;
                else if (key == "\033[H" || key == "\033OH" || key == "\033[1~") episodeScroll_ = 0;
                else if (key == "\033[F" || key == "\033OF" || key == "\033[4~") episodeScroll_ = limit;
                else handled = false;
                episodeScroll_ = std::clamp(episodeScroll_, 0, limit);
                if (handled) return "";
            }
        }
        if (key == "p" || key == "P" || key == " ") return "pause";
        if (key == "v" || key == "V") return "visual";
        if ((key == "n" || key == "N") && phase_ == "ready" && !paused_) {
            phase_ = "queued";
            return "next";
        }
        if (key == "\033[5~" || key == "scroll_up") scroll_[page_ - 1] = std::max(
                                                        0, scroll_[page_ - 1] - (key == "scroll_up"
                                                               ? 3
                                                               : std::max(1, viewport_ - 2)));
        else if (key == "\033[6~" || key == "scroll_down") scroll_[page_ - 1] = std::min(
                                                               100000, scroll_[page_ - 1] + (key == "scroll_down"
                                                                           ? 3
                                                                           : std::max(1, viewport_ - 2)));
        else if (page_ == 2 && !tensors_.empty() &&
                 (key == "\033[A" || key == "\033OA" || key == "\033[B" || key == "\033OB")) {
            if (key == "\033[A" || key == "\033OA") {
                selected_ = std::max(0, selected_ - 1);
                element_ = 0;
            } else if (key == "\033[B" || key == "\033OB") {
                selected_ = std::min(static_cast<int>(tensors_.size()) - 1, selected_ + 1);
                element_ = 0;
            }
            // Keep a selected tensor visible even when the model has many layers.
            const int row = 4 + selected_;
            if (row < scroll_[1]) scroll_[1] = row;
            else if (row >= scroll_[1] + viewport_) scroll_[1] = row - viewport_ + 1;
            return "inspect " + std::to_string(selected_) + " " + std::to_string(element_);
        } else if (page_ == 2 && !tensors_.empty()) {
            if (key == "h" || key == "\033[D" || key == "\033OD") element_ = std::max(0, element_ - 1);
            else if (key == "l" || key == "\033[C" || key == "\033OC") element_ = std::min(
                                                                           tensors_[selected_].count - 1, element_ + 1);
            else if (key == "[") element_ = std::max(0, element_ - 64);
            else if (key == "]") element_ = std::min(tensors_[selected_].count - 1, element_ + 64);
            else if (key == "\033[H" || key == "\033OH" || key == "\033[1~") element_ = 0;
            else if (key == "\033[F" || key == "\033OF" || key == "\033[4~") element_ = tensors_[selected_].count - 1;
            else return "";
            return "inspect " + std::to_string(selected_) + " " + std::to_string(element_);
        } else if (key == "\033[A" || key == "\033OA") scroll_[page_ - 1] = std::max(0, scroll_[page_ - 1] - 1);
        else if (key == "\033[B" || key == "\033OB") scroll_[page_ - 1] = std::min(100000, scroll_[page_ - 1] + 1);
        else if (key == "\033[H" || key == "\033OH") scroll_[page_ - 1] = 0;
        else if (key == "\033[F" || key == "\033OF") scroll_[page_ - 1] = scrollMax_;
        return "";
    }

    void NNTrainingScreen::wheel(int direction, int column, int row) {
        if (!deviceInfo_ && page_ == 3 && learningDetails_) {
            episodeFocus_ = column >= episodeLeft_ && column < episodeLeft_ + episodeWidth_ &&
                            row >= episodeTop_ && row < episodeTop_ + episodeHeight_;
        }
        key(direction < 0 ? "scroll_up" : "scroll_down");
    }

    std::vector<std::string> NNTrainingScreen::frame(int width, int rows, bool exited, bool failed) {
        width = std::max(1, width);
        rows = std::max(1, rows);
        const Palette p;
        Canvas screen(width, rows);
        const int margin = width >= 65 ? 2 : 1, w = std::max(1, std::min(156, width - margin * 2)), x = (width - w) / 2;
        const bool spacious = width >= 60 && rows >= 26;
        const int bodyTop = spacious ? 10 : 6;
        const bool failure = failed || !error_.empty();
        const std::string state = failure
                                      ? "FAILED"
                                      : exited
                                            ? (phase_ == "ready" ? "COMPLETE" : "STOPPED")
                                            : paused_
                                                  ? "PAUSED"
                                                  : phase_ == "ready"
                                                        ? "READY"
                                                        : phase_ == "evaluating"
                                                              ? "EVALUATING"
                                                              : phase_ == "collecting"
                                                                    ? "TRAINING"
                                                                    : phase_ == "optimizing"
                                                                          ? "UPDATING"
                                                                          : phase_ == "queued"
                                                                                ? "STARTING"
                                                                                : phase_ == "stopped"
                                                                                    ? "STOPPED"
                                                                                    : "STARTING";
        const auto statusColor = failure
                                     ? p.caps.error_color
                                     : exited
                                           ? (phase_ == "ready" ? p.green : p.gold)
                                           : paused_
                                                 ? p.gold
                                                 : phase_ == "ready"
                                                       ? p.green
                                                       : p.purple;
        const std::string status = glyph(paused_ ? "Ⅱ" : phase_ == "ready" ? "●" : "↻",
                                         paused_ ? "||" : phase_ == "ready" ? "*" : "o") + " " + state;
        screen.heading(x, w, "NEURAL TRAINING LAB", p, status, statusColor);
        if (!model_.empty()) {
            const auto mode = trainingMode_ == "resume"
                                  ? "Resumed session"
                                  : trainingMode_ == "restart"
                                        ? "Restarted session"
                                        : trainingMode_ == "fork"
                                              ? "Forked actor / fresh critic"
                                        : trainingMode_ == "new"
                                              ? "New model"
                                              : trainingMode_;
            const auto note = mode.empty() ? std::string{} : "  " + glyph("·", "/") + "  " + mode;
            const int room = w >= 108 ? w - 35 : w;
            const auto modelLabel = ellipsize(model_, std::max(1, room - textCells(note)));
            screen.put(x, spacious ? 3 : 2, modelLabel, p.bold, room);
            screen.put(x + textCells(modelLabel), spacious ? 3 : 2, note, p.dim, room - textCells(modelLabel));
        }
        if (spacious && w >= 108) {
            const std::string runtime = device_ + "  " + glyph("·", "/") + "  Seed " + std::to_string(seed_);
            screen.put(x + w - textCells(runtime), 3, runtime, p.dim, textCells(runtime));
        }
        if (spacious) {
            const auto run = w < 88
                                 ? "Update " + std::to_string(runBatch_) + "/" + std::to_string(runBatches_) +
                                   "  " + glyph("·", "/") + "  " + std::to_string(batchSize_) + " ep/update  " +
                                   glyph("·", "/") + "  " +
                                   (agents_ > 1 ? std::to_string(agents_) + " workers" : "Solo")
                                 : "Update " + std::to_string(runBatch_) + "/" + std::to_string(runBatches_) +
                                   "  " + glyph("·", "/") + "  " + std::to_string(batchSize_) + " episodes/update  " +
                                   glyph("·", "/") + "  " +
                                   (agents_ > 1 ? std::to_string(agents_) + (independentWorkers_ ? " independent workers" : "-agent arena / collisions on") : (independentWorkers_ ? "1 independent worker" : "Solo balance"));
            screen.put(x, 4, run, p.purple, w);
            const double fraction = runTimingSeen_ ? static_cast<double>(runCompleted_) / runBatches_ : 0.;
            const int barWidth = w >= 100 ? 32 : 12;
            const int filled = fraction <= 0 ? 0 : fraction >= 1 ? barWidth :
                               std::clamp(static_cast<int>(fraction * barWidth), 1, barWidth - 1);
            const std::string full = glyph("━", "=");
            const std::string empty = glyph("─", "-");
            std::string filledBar, emptyBar;
            for (int i = 0; i < filled; ++i) filledBar += full;
            for (int i = filled; i < barWidth; ++i) emptyBar += empty;
            screen.put(x, 5, filledBar, p.green, barWidth);
            screen.put(x + filled, 5, emptyBar, p.dim, barWidth - filled);
            const std::string progress = "  " + number(100. * fraction, 1) + "%  ETA " +
                (paused_ ? "paused" : runTimingSeen_ ? duration(runEta_) : "warming up") +
                (w >= 100 ? "  /  elapsed " + duration(runElapsed_) : "") +
                (w >= 128 && runTimingSeen_ ? "  /  episodes " + std::to_string(runEpisodes_) + "/" + std::to_string(batchSize_) : "");
            screen.put(x + barWidth, 5, progress, p.green, w - barWidth);
        }
        const std::array<std::string, 3> tabs{"[1]  Overview", "[2]  Parameters", "[3]  Learning"};
        int tabX = x;
        for (int tab = 0; tab < 3; ++tab) {
            screen.shortcuts(tabX, spacious ? 6 : 3, " " + tabs[tab] + " ", p, -1,
                             page_ == tab + 1 ? p.selected : p.dim, page_ == tab + 1 ? p.caps.shell_style : "");
            tabX += shortcut_width(tabs[tab]) + 4;
        }
        const auto batchSummary = "Batch " + std::to_string(batch_) + "  " + glyph("·", "/") + "  " +
                                  std::to_string(completedEpisodes_) + " episodes saved" + (w < 108
                                      ? "  " + glyph("·", "/") + "  " + device_ + " / seed " + std::to_string(seed_)
                                      : "");
        screen.put(x, spacious ? 8 : 4, batchSummary, p.dim, w);
        if (inputs_ > 0) {
            const auto divider = "  " + glyph("·", "/") + "  ";
            std::string contract = std::to_string(inputs_) + " inputs " + glyph("→", "->") + " " +
                                   std::to_string(outputs_) + " targets";
            if (activeMotors_ >= 0) contract += divider + std::to_string(activeMotors_) + "/" +
                                    std::to_string(totalMotors_) + " joint motors";
            const std::string motorNote = disabledMotors_.empty() ? "" : divider + disabledMotors_ + " off";
            const int contractWidth = textCells(contract) + textCells(motorNote);
            const bool inlineSummary = spacious && w >= 108 && textCells(batchSummary) + contractWidth + 4 <= w;
            const int contractX = inlineSummary ? x + textCells(batchSummary) + 4 : x;
            const int contractY = inlineSummary ? 8 : spacious ? 7 : 5;
            screen.put(contractX, contractY, contract, p.dim, w - (contractX - x));
            screen.put(contractX + textCells(contract), contractY, motorNote, p.gold,
                       w - (contractX - x) - textCells(contract));
        }
        if (failure) {
            // Give the actual exception room to wrap instead of clipping it in a footer.
            const int top = std::min(bodyTop, rows - 4);
            screen.put(x, top, "Training stopped", p.caps.error_color, w);
            std::istringstream words(error().empty()
                                         ? "The training worker exited unexpectedly. Check the command output for details."
                                         : error());
            std::string word, line;
            int y = top + 2;
            while (words >> word) {
                if (!line.empty() && textCells(line) + 1 + textCells(word) > w) {
                    if (y < rows - 3) screen.put(x, y++, line, p.text, w);
                    line.clear();
                }
                line += (line.empty() ? "" : " ") + word;
            }
            if (!line.empty() && y < rows - 3) screen.put(x, y, line, p.text, w);
            screen.shortcuts(x, rows - 2, "Q back   Esc home", p, w);
            screen.put(x, rows - 1,
                       checkpoint_.empty() ? "No checkpoint was reported saved" : "Last saved / " + checkpoint_, p.dim,
                       w);
            scrollMax_ = 0;
            return screen.lines();
        }
        if (width < 60 || rows < 18) {
            screen.put(
                x, 6, "Episode " + std::to_string(episode_) + "/" + std::to_string(batchSize_) + "  /  " +
                      integer(steps_) + " steps", p.text, w);
            screen.put(x, 8, updates_ ? "Loss " + number(loss_, 5) : "Collecting the first batch...", p.text, w);
            screen.put(x, std::min(10, rows - 3), "Enlarge terminal: 60 columns / 18 rows", p.dim, w);
            screen.shortcuts(x, rows - 2,
                             exited || failure
                                 ? "Q back   Esc home"
                                 : std::string(visual_ ? "V headless" : "V visual") + "  P pause  N next  Q back", p,
                             w);
            screen.put(x, rows - 1, checkpoint_.empty() ? "Checkpoint pending" : "Saved / " + checkpoint_, p.dim, w);
            scrollMax_ = 0;
            return screen.lines();
        }
        if (count_ == 0) {
            screen.put(x, 6, failure
                                 ? (error_.empty() ? "Training could not start" : error_)
                                 : exited
                                       ? "Training session ended"
                                       : "Loading training configuration...", failure ? p.caps.error_color : p.dim, w);
            screen.shortcuts(x, rows - 2, "Q back / Esc home", p, w);
            return screen.lines();
        }
        viewport_ = rows - bodyTop - 3;
        const bool wide = w >= 108;
        const int tensorHeight = std::max(12, static_cast<int>(tensors_.size()) + 6);
        const int mathTop = wide ? tensorHeight + 1 : tensorHeight + 14;
        const int episodeHeight = std::clamp(viewport_ / 2, 12, 18);
        episodeRows_ = episodeHeight - 7;
        episodeScroll_ = std::clamp(episodeScroll_, 0, std::max(0, batchSize_ - episodeRows_));
        const int formulaTop = wide ? std::max(episodeHeight, 14) + 1 : episodeHeight + 16;
        const int scalingColumns = w >= 145 ? 3 : w >= 100 ? 2 : 1;
        const int scalingRows = (static_cast<int>(actions_.size()) + scalingColumns - 1) / scalingColumns;
        const int formulaUpperHeight = wide
                                           ? std::max(12, static_cast<int>(layers_.size()) + 9)
                                           : 20 + static_cast<int>(layers_.size());
        const int formulaHeight = formulaUpperHeight + 5 + scalingRows;
        int contentHeight = 0;
        const int overviewColumns = wide ? 5 : 2;
        const int overviewCardW = (w - (overviewColumns - 1) * 2) / overviewColumns;
        const int episodeWidth = wide ? 3 * (overviewCardW + 2) - 2 : w;
        std::vector<const JointVectorControl *> targetJoints;
        for (const auto &joint: joint_vector_controls()) {
            const bool controlled = std::any_of(joint.slots.begin(), joint.slots.end(), [&](int slot) {
                return slot >= 0 && std::any_of(actions_.begin(), actions_.end(), [&](const auto &action) {
                    return action.key == sim::actionSpecs[slot].key;
                });
            });
            if (controlled) targetJoints.push_back(&joint);
        }
        const int targetColumns = episodeWidth - 4 >= 82 ? 2 : 1;
        const int targetRows = (static_cast<int>(targetJoints.size()) + targetColumns - 1) / targetColumns;
        const int episodePanelHeight = (expandedTargets_ ? std::max(7, (wide ? 8 : 7) + targetRows) : 7) +
                                       (agents_ > 1 ? std::min(agents_,4) + 2 : 0);
        const int overviewLiveY = ((5 + overviewColumns - 1) / overviewColumns) * 6 + (wide ? 1 : 0);
        const int chartGap = wide && overviewLiveY + episodePanelHeight + 12 > viewport_ ? 0 : 1;
        if (deviceInfo_) contentHeight = 29;
        else if (page_ == 1) contentHeight = std::max(
                            viewport_, overviewLiveY + episodePanelHeight + chartGap + (wide ? 15 : 23));
        else if (page_ == 2) contentHeight = mathTop + (expandedMath_ ? 20 : 9);
        else if (!learningDetails_) contentHeight = std::max(
                 viewport_, agents_ > 1 ? 38 : 34);
        else contentHeight = formulaTop + formulaHeight;
        Canvas body(w, contentHeight);
        if (deviceInfo_) {
            body.box(0, 0, w, contentHeight, "DEVICE / GPU ACTIVITY", p);
            const std::array<std::pair<const char *, const char *>, 12> fields{{
                {"rollout_inference", "Episode inference"}, {"backend", "Learning backend"}, {"actor", "Actor tensors"}, {"critic", "Critic tensors"},
                {"phase", "Current phase"}, {"physics", "Physics"}, {"neural", "Neural work"},
                {"updates", "Completed work"}, {"utilization", "GPU utilization"},
                {"allocated", "Tensor memory"}, {"reserved", "Driver / cache"}, {"budget", "Memory budget"}
            }};
            int y = 2;
            for (const auto &[name, label]: fields) {
                body.put(2, y++, label, p.purple, w - 4);
                const auto found = deviceMetrics_.find(name);
                body.put(4, y++, found == deviceMetrics_.end() ? "Waiting for measurement..." : found->second,
                         p.text, w - 6);
            }
        } else if (page_ == 1) {
            const int columns = wide ? 5 : 2, cardW = (w - (columns - 1) * 2) / columns;
            const double best = scores_.empty() ? 0 : *std::max_element(scores_.begin(), scores_.end());
            const double mean = means_.empty() ? 0 : means_.back();
            const std::array<std::string, 5> titles{
                "BATCH MEAN", "BEST EPISODE", actorCritic_ ? "EVAL BALANCED" : "EVAL SURVIVAL", "OPTIMIZER", "POLICY LOSS"
            };
            const std::array<std::string, 5> values{
                means_.empty() ? glyph("—", "--") : number(mean, 1),
                scores_.empty() ? glyph("—", "--") : number(best, 0),
                actorCritic_ && deterministicBalance_.valid ? number(deterministicBalance_.seconds, 3) + " s" :
                evaluationSeen_ ? integer(evaluationSteps_) + " steps" : glyph("—", "--"),
                std::to_string(updates_) + " updates", updates_ ? number(loss_, 5) : glyph("—", "--")
            };
            const std::string evaluationNote = actorCritic_ && deterministicBalance_.valid
                                                   ? "Det / " + integer(deterministicBalance_.balanced) + " steps"
                                                   : phase_ == "evaluating"
                                                   ? "Testing policy"
                                                   : evaluationSeen_
                                                         ? (evaluationUpdate_
                                                                ? "Update " + std::to_string(evaluationUpdate_)
                                                                : "Initial policy") + (previousEvaluationSteps_ >= 0
                                                               ? " / " + number(
                                                                     evaluationSteps_ - previousEvaluationSteps_, 0,
                                                                     true)
                                                               : "")
                                                         : "Awaiting update";
            const std::array<std::string, 5> notes{
                "training / steps", std::to_string(scores_.size()) + " explored", evaluationNote,
                "Adam / " + number(lr_, 3), "per update"
            };
            for (int i = 0; i < 5; ++i) {
                const int cx = (i % columns) * (cardW + 2), cy = (i / columns) * 6;
                body.box(cx, cy, cardW, 5, titles[i], p);
                body.put(cx + 2, cy + 2, values[i],
                         i == 0 ? p.caps.strong + p.green : i == 2 || i == 4 ? p.caps.strong + p.purple : p.bold,
                         cardW - 4);
                body.put(cx + 2, cy + 3, notes[i], p.dim, cardW - 4);
            }
            const int liveY = ((5 + columns - 1) / columns) * 6 + (wide ? 1 : 0);
            const int liveW = wide ? 3 * (cardW + 2) - 2 : w;
            const int innerX = wide ? 2 : 0, innerY = liveY + (wide ? 2 : 0);
            if (wide) body.box(0, liveY, liveW, episodePanelHeight, agents_ > 1 ? "TRAINING / SELECTED WORKER" : "TRAINING / LAST EPISODE", p);
            const auto &selectedAgent = arenaAgents_[selectedAgent_];
            const int shownSteps = agents_ > 1 ? selectedAgent.steps : steps_;
            const int shownEpisode = agents_ > 1 ? selectedAgent.episode : episode_;
            const int barW = std::clamp(liveW - 48, 6, 24);
            const double fraction = phase_ == "ready"
                                        ? 1.0
                                        : std::clamp(
                                            (std::max(0, shownEpisode - 1) + static_cast<double>(shownSteps) / std::max(
                                                 1, episodeLimit_)) / std::max(1, batchSize_), 0.0, 1.0);
            const std::string live = phase_ == "evaluating"
                                         ? "Evaluating policy"
                                         : phase_ == "ready"
                                               ? "Batch complete"
                                               : paused_
                                                     ? "Episode paused"
                                                     : "Episode " + std::to_string(shownEpisode) + " / " + std::to_string(
                                                           batchSize_);
            body.put(innerX, innerY, glyph("▸", ">") + " " + live, p.bold, 22);
            for (int i = 0; i < barW; ++i) body.put(innerX + 23 + i, innerY,
                                                    glyph(i < static_cast<int>(fraction * barW) ? "━" : "─",
                                                          i < static_cast<int>(fraction * barW) ? "=" : "-"),
                                                    i < static_cast<int>(fraction * barW) ? p.green : p.dim);
            body.put(innerX + 25 + barW, innerY, integer(shownSteps) + " / " + integer(episodeLimit_), p.text,
                     liveW - innerX - 27 - barW);
            body.put(innerX, innerY + 1, agents_ > 1
                     ? (independentWorkers_ ? "Worker " : "AURA ") + std::to_string(selectedAgent_ + 1) + " / " + selectedAgent.state +
                       " / balanced " + number(selectedAgent.balance.seconds, 2) + " s / Q " + number(selectedAgent.quality, 3)
                     : "Height " + number(height_, 2) + " m   Upright " + number(upright_, 3) + "   " +
                       (outcome_ == "fallen" ? "Fallen" : outcome_ == "time_limit" ? "Time limit" : ""), p.dim,
                     liveW - innerX - 2);
            if (agents_ > 1) body.shortcuts(innerX, innerY + 2, "Left/Right worker / H chart filter", p, liveW - innerX - 2);
            else body.put(innerX, innerY + 2, std::to_string(actions_.size()) + " motor targets / radians", p.dim,
                          liveW - innerX - 2);
            const std::string targetShortcut = expandedTargets_ ? "T collapse targets" : "T inspect joint targets";
            const int shortcutWidth = shortcut_width(targetShortcut);
            const int shortcutX = wide ? liveW - shortcutWidth - 3 : innerX + 1;
            const int shortcutY = wide ? liveY : innerY + 3;
            body.put(shortcutX - 1, shortcutY, std::string(shortcutWidth + 2, ' '), p.dim);
            body.shortcuts(shortcutX, shortcutY, targetShortcut, p, shortcutWidth);
            if (expandedTargets_) {
                const int targetWidth = (liveW - innerX - 2) / targetColumns;
                const int axisWidth = (targetWidth - 17) / 3;
                const int nameWidth = targetWidth - axisWidth * 3 - 1;
                const int tableY = innerY + (wide ? 3 : 4);
                for (int column = 0; column < targetColumns; ++column) {
                    const int cx = innerX + column * targetWidth;
                    body.put(cx, tableY, "Joint", p.dim, nameWidth);
                    for (int axis = 0; axis < 3; ++axis)
                        body.put(cx + nameWidth + axis * axisWidth, tableY, std::array{"Bend", "Turn", "Tilt"}[axis],
                                 p.purple, axisWidth - 1);
                }
                for (int i = 0; i < static_cast<int>(targetJoints.size()); ++i) {
                    const auto &joint = *targetJoints[i];
                    const int cx = innerX + (i / targetRows) * targetWidth, cy = tableY + 1 + i % targetRows;
                    body.put(cx, cy, joint.name, p.text, nameWidth - 1);
                    for (int axis = 0; axis < 3; ++axis) {
                        const int slot = joint.slots[axis];
                        const auto action = std::find_if(actions_.begin(), actions_.end(), [&](const auto &item) {
                            return slot >= 0 && item.key == sim::actionSpecs[slot].key;
                        });
                        const bool missing = action == actions_.end();
                        const bool off = !missing && action->motorKnown && !action->enabled;
                        const auto actionIndex = std::distance(actions_.begin(), action);
                        const bool known = !missing && (agents_ == 1 || selectedAgent.targets.size() == actions_.size());
                        const double target = known ? (agents_ > 1 ? selectedAgent.targets[actionIndex] : action->value) : 0;
                        const bool changed = known && std::abs(target) >= 0.0005;
                        const std::string value = slot < 0
                                                      ? "locked"
                                                      : !known
                                                            ? glyph("—", "--")
                                                            : off
                                                                  ? "off"
                                                                  : number(target, 3, true);
                        const auto color = slot < 0
                                               ? p.caps.syntax_command
                                               : off
                                                     ? p.gold
                                                     : changed
                                                           ? p.green + p.caps.strong
                                                           : p.dim;
                        body.put(cx + nameWidth + axis * axisWidth, cy, value, color, axisWidth - 1);
                    }
                }
                body.put(innerX, tableY + targetRows + 1, (agents_ > 1 ? (independentWorkers_ ? "Worker " : "AURA ") + std::to_string(selectedAgent_ + 1) + " sampled targets / radians" : "Sampled targets / radians") + " / green = non-neutral", p.dim,
                         liveW - innerX - 2);
            }
            if (agents_ > 1) {
                const int arenaY = liveY + episodePanelHeight - std::min(agents_,4) - 2;
                body.put(innerX, arenaY, (independentWorkers_ ? "INDEPENDENT WORKERS / active " + integer(std::count_if(arenaAgents_.begin(),arenaAgents_.end(),[](const auto &a){return !a.done && a.state!="waiting";})) + "/" + integer(agents_) + " / isolated contacts" : "SHARED ARENA / contact pairs " + integer(arenaContacts_) + " / " + number(arenaSpacing_,0) + " m spacing"), p.purple, liveW - 4);
                for (int i = (selectedAgent_/4)*4; i < std::min(agents_,(selectedAgent_/4+1)*4); ++i) {
                    const auto &agent = arenaAgents_[i];
                    body.put(innerX, arenaY + 1 + i%4, (independentWorkers_ ? "Worker " : "AURA ") + std::to_string(i + 1) + " / " + agent.state +
                             (agent.state == "waiting" ? "" :
                             " / " + integer(agent.steps) + " steps / Q " + number(agent.quality, 3) +
                             " / balanced " + number(agent.balance.seconds, 2) + " s / return " + number(agent.reward, 2)), agent.state == "BALANCED" ? p.green :
                             agent.state == "FALLEN" ? p.dim : p.gold, liveW - 4);
                }
            }
            if (wide) {
                const int bestX = 3 * (cardW + 2), bestW = w - bestX;
                body.box(bestX, liveY, bestW, 7, "BEST BALANCE POLICY", p);
                body.put(bestX + 2, liveY + 2,
                         bestBalance_.valid ? integer(bestBalance_.balanced) + " balanced steps" :
                         bestEvaluationSteps_ >= 0 ? integer(bestEvaluationSteps_) + " survival steps" : glyph("—", "--"),
                         p.caps.strong + p.green, bestW - 4);
                body.put(bestX + 2, liveY + 3,
                         bestEvaluationSteps_ >= 0 ? bestCheckpoint_ : "Available after evaluation", p.text, bestW - 4);
                if (bestBalance_.valid && deterministicBalance_.valid)
                    body.put(bestX + 2, liveY + 4, "Current " + integer(deterministicBalance_.balanced) +
                             " / " + number(deterministicBalance_.balanced - bestBalance_.balanced, 0, true) + " from best",
                             deterministicBalance_.balanced < bestBalance_.balanced ? p.gold : p.green, bestW - 4);
                else if (bestEvaluationSteps_ >= 0 && evaluationSeen_)
                    body.put(bestX + 2, liveY + 4,
                             "Current " + integer(evaluationSteps_) + "  /  " + number(
                                 evaluationSteps_ - bestEvaluationSteps_, 0, true) + " from best",
                             evaluationSteps_ < bestEvaluationSteps_ ? p.gold : p.green, bestW - 4);
                body.put(bestX + 2, liveY + 5, "Rank: balanced steps, then reward", p.dim, bestW - 4);
            } else if (bestEvaluationSteps_ >= 0)
                body.put(0, liveY + episodePanelHeight - 1,
                         (bestBalance_.valid ? "Best balance " + integer(bestBalance_.balanced) + " steps / " +
                                              number(bestBalance_.seconds, 2) + " s"
                                             : "Best checkpoint / survival " + integer(bestEvaluationSteps_) + " steps") +
                         "  " + glyph("·", "/") + "  " +
                         bestCheckpoint_, p.green, w);
            const int chartY = liveY + episodePanelHeight + chartGap;
            const auto rightChart = [&](int cx, int cy, int cw, int ch) {
                chart(body, cx, cy, cw, ch, chartMode_ == 0 ? "EVALUATION BALANCED TIME" : chartMode_ == 1 ? "EVALUATION SURVIVAL" : "POLICY LOSS",
                      chartMode_ == 0 ? evaluationBalanced_ : chartMode_ == 1 ? evaluations_ : losses_, {}, p.purple, p,
                      chartMode_ == 0 ? ChartKind::evaluationBalanced : chartMode_ == 1 ? ChartKind::evaluation : ChartKind::loss,
                      batchSize_, 0, chartMode_ == 2 ? lossUpdates_ : evaluationUpdates_);
            };
            if (wide) {
                const int rightX = 3 * (cardW + 2);
                if (independentWorkers_) worker_chart(body, 0, chartY, liveW, contentHeight - chartY, chartMode_ == 1 ? "TRAINING SURVIVAL" : "TRAINING BALANCED TIME", chartMode_ == 1 ? workerSteps_ : workerSeconds_, selectedAgent_, onlySelectedWorker_, p, chartMode_ == 1 ? ChartKind::training : ChartKind::balancedTime);
                else chart(body, 0, chartY, liveW, contentHeight - chartY, chartMode_ == 1 ? "TRAINING SURVIVAL" : "TRAINING BALANCED TIME",
                    chartMode_ == 1 ? scores_ : balancedDurations_, chartMode_ == 1 ? means_ : std::vector<double>{}, p.green, p,
                    chartMode_ == 1 ? ChartKind::training : ChartKind::balancedTime, batchSize_, chartMode_ == 1 ? episodeOffset_ : 0,
                    chartMode_ == 1 ? std::vector<int>{} : balancedIndices_);
                // Each column flows independently: expanding targets only moves the
                // training chart. Evaluation remains directly below its best card.
                const int evaluationY = liveY + 8;
                rightChart(rightX, evaluationY, w - rightX, std::max(15, viewport_ - evaluationY));
            } else {
                if (independentWorkers_) worker_chart(body, 0, chartY, w, 11, chartMode_ == 1 ? "TRAINING SURVIVAL" : "TRAINING BALANCED TIME", chartMode_ == 1 ? workerSteps_ : workerSeconds_, selectedAgent_, onlySelectedWorker_, p, chartMode_ == 1 ? ChartKind::training : ChartKind::balancedTime);
                else chart(body, 0, chartY, w, 11, chartMode_ == 1 ? "TRAINING SURVIVAL" : "TRAINING BALANCED TIME",
                    chartMode_ == 1 ? scores_ : balancedDurations_, chartMode_ == 1 ? means_ : std::vector<double>{}, p.green, p,
                    chartMode_ == 1 ? ChartKind::training : ChartKind::balancedTime, batchSize_, chartMode_ == 1 ? episodeOffset_ : 0,
                    chartMode_ == 1 ? std::vector<int>{} : balancedIndices_);
                rightChart(0, chartY + 12, w, 11);
            }
        } else if (page_ == 2) {
            const int tableW = wide ? (w - 2) / 2 : w, inspectorX = wide ? tableW + 2 : 0, inspectorY =
                    wide ? 0 : tensorHeight + 1, inspectorW = wide ? w - tableW - 2 : w;
            body.box(0, 0, tableW, tensorHeight, "NETWORK PARAMETERS", p);
            std::string architecture;
            for (const auto &layer: layers_) {
                if (architecture.empty()) architecture = std::to_string(layer.inputs);
                architecture += " " + glyph("→", "->") + " " + std::to_string(layer.outputs);
            }
            body.put(2, 1, architecture + " / " + integer(count_) + " values", p.dim, tableW - 4);
            const bool tableDetail = tableW >= 56;
            const int shapeX = tableW >= 68 ? 25 : 22, normX = tableW - (tableDetail ? 27 : 23), gradX =
                    tableW - (tableDetail ? 19 : 12);
            body.put(3, 3, "Layer / tensor", p.dim, 20);
            if (tableDetail) body.put(shapeX, 3, "Shape", p.dim, 10);
            body.put(normX, 3, "L2 norm", p.dim, 10);
            body.put(gradX, 3, "Grad L2", p.dim, 7);
            if (tableDetail) body.put(tableW - 11, 3, glyph("Max |Δ|", "Max delta"), p.dim, 9);
            for (int i = 0; i < static_cast<int>(tensors_.size()); ++i) {
                const auto &tensor = tensors_[i];
                const auto style = i == selected_ ? p.selected : p.text;
                body.put(1, 4 + i, std::string(tableW - 2, ' '), style);
                body.put(2, 4 + i, i == selected_ ? glyph("▸", ">") : " ", i == selected_ ? p.green : p.dim);
                body.put(4, 4 + i, (tensor.label.empty() ? tensorLabel(tensor.name) : tensor.label), style,
                         tableDetail && tableW < 68 ? 17 : 20);
                if (tableDetail) body.put(shapeX, 4 + i, shapeLabel(tensor.shape), style, 10);
                body.put(normX, 4 + i, number(tensor.norm, 3), style, 10);
                body.put(gradX, 4 + i, updates_ ? number(tensor.gradient, 3) : glyph("—", "--"), style, 10);
                if (tableDetail) body.put(tableW - 11, 4 + i, updates_ ? number(tensor.change, 5) : glyph("—", "--"),
                                          style, 9);
            }
            body.put(2, tensorHeight - 2, "L2 = magnitude / gradient = loss sensitivity", p.dim, tableW - 4);
            body.box(inspectorX, inspectorY, inspectorW, 12, "PARAMETER INSPECTOR", p);
            if (!tensors_.empty()) {
                const auto &tensor = tensors_[selected_];
                body.put(inspectorX + 2, inspectorY + 1, tensor.name, p.purple, inspectorW - 4);
                body.put(inspectorX + 2, inspectorY + 2,
                         "Element " + integer(element_ + 1) + " / " + integer(tensor.count) + "  " + glyph("·", "/") +
                         "  shape " + shapeLabel(tensor.shape), p.dim, inspectorW - 4);
                const bool valid = selected_ == inspectedTensor_ && element_ == inspectedElement_ && inspectedUpdate_ ==
                                   updates_;
                if (valid) {
                    const auto [before,after,gradient,moment,variance] = scalar_;
                    body.put(inspectorX + 2, inspectorY + 4,
                             "[" + coordinates_ + "]   " + number(before, 6) + "  " + glyph("→", "->") + "  " +
                             number(after, 6), p.bold, inspectorW - 4);
                    body.put(inspectorX + 2, inspectorY + 5,
                             glyph("Δ", "Change") + " " + number(after - before, 6, true), p.green, inspectorW - 4);
                    body.put(inspectorX + 2, inspectorY + 7,
                             glyph("∂L/∂θ", "dL/dtheta") + "  " + (updates_ ? number(gradient, 6) : glyph("—", "--")),
                             p.text, inspectorW - 4);
                    body.put(inspectorX + 2, inspectorY + 8,
                             "Range " + number(tensor.minimum, 4) + " to " + number(tensor.maximum, 4), p.dim,
                             inspectorW - 4);
                    body.put(inspectorX + 2, inspectorY + 10,
                             updates_
                                 ? "Snapshot from Adam update " + std::to_string(inspectedUpdate_)
                                 : "Initial value / no update yet", p.dim, inspectorW - 4);
                } else body.put(inspectorX + 2, inspectorY + 5, "Loading selected value...", p.dim, inspectorW - 4);
            }
            const int mathY = mathTop;
            body.box(0, mathY, w, expandedMath_ ? 20 : 9, "ADAM / UPDATE RULE", p);
            // Clear the border behind the shortcut, including one space on each side.
            const std::string mathShortcut = expandedMath_ ? "F collapse math" : "F expand math";
            const int mathShortcutWidth = shortcut_width(mathShortcut);
            body.put(w - 28, mathY, std::string(mathShortcutWidth + 2, ' '), p.dim);
            body.shortcuts(w - 27, mathY, mathShortcut, p, mathShortcutWidth);
            const auto [before,after,gradient,moment,variance] = scalar_;
            const bool valid = inspectedUpdate_ > 0 && inspectedUpdate_ == updates_ && selected_ == inspectedTensor_ &&
                               element_ == inspectedElement_;
            const double mhat = valid ? moment / (1 - std::pow(beta1_, inspectedUpdate_)) : 0;
            const double vhat = valid ? variance / (1 - std::pow(beta2_, inspectedUpdate_)) : 0;
            if (p.caps.utf8) {
                body.put(2, mathY + 2, "θₜ = θₜ₋₁ − η ·", p.bold);
                body.fraction(20, mathY + 1, "mᶜₜ", "√(vᶜₜ) + ε", p.purple);
                if (w >= 90) {
                    body.put(38, mathY + 2, "previous value − adaptive step", p.dim, w - 40);
                    body.put(38, mathY + 3, "c = bias corrected", p.dim, w - 40);
                }
            } else body.put(2, mathY + 2, "theta_new = theta_old - rate * m_hat / (sqrt(v_hat) + eps)", p.bold, w - 4);
            body.put(2, mathY + 5,
                     glyph("η", "Rate") + " = " + number(lr_, 3) + "   " + glyph("β₁", "beta1") + " = " +
                     number(beta1_, 3) + "   " + glyph("β₂", "beta2") + " = " + number(beta2_, 3) + "   " +
                     glyph("ε", "eps") + " = " + number(epsilon_), p.text, w - 4);
            body.put(2, mathY + 6,
                     valid
                         ? "Calculated " + glyph("Δθ", "change") + " " +
                           number(-lr_ * mhat / (std::sqrt(vhat) + epsilon_), 7, true) + "  /  stored " +
                           number(after - before, 7, true)
                         : (updates_
                                ? "Loading the selected calculation..."
                                : "The calculation appears after the first update."), p.green, w - 4);
            if (expandedMath_) {
                body.rule(2, mathY + 8, w - 4, p.dim);
                body.put(2, mathY + 9,
                         glyph("mₜ = β₁mₜ₋₁ + (1 − β₁)gₜ", "m = beta1 * previous_m + (1-beta1) * gradient"), p.text,
                         w - 4);
                body.put(2, mathY + 10,
                         glyph("vₜ = β₂vₜ₋₁ + (1 − β₂)gₜ²", "v = beta2 * previous_v + (1-beta2) * gradient^2"), p.text,
                         w - 4);
                if (p.caps.utf8) {
                    body.put(2, mathY + 13, "mᶜₜ =", p.bold);
                    body.fraction(9, mathY + 12, "mₜ", "1 − β₁ᵗ", p.purple);
                    body.put(28, mathY + 13, "vᶜₜ =", p.bold);
                    body.fraction(35, mathY + 12, "vₜ", "1 − β₂ᵗ", p.purple);
                } else body.put(2, mathY + 13, "m_hat = m / (1-beta1^t); v_hat = v / (1-beta2^t)", p.text, w - 4);
                body.put(2, mathY + 16,
                         valid
                             ? "t = " + std::to_string(inspectedUpdate_) + "   m = " + number(moment, 6) + "   v = " +
                               number(variance)
                             : "m: gradient momentum   v: squared-gradient momentum", p.dim, w - 4);
                body.put(2, mathY + 17,
                         valid
                             ? glyph("mᶜ", "m_hat") + " = " + number(mhat, 6) + "   " + glyph("vᶜ", "v_hat") + " = " +
                               number(vhat, 7)
                             : "Bias correction accounts for moments starting at zero.", p.dim, w - 4);
                body.put(2, mathY + 18,
                         glyph("ᶜ: bias corrected   gₜ = ∂L/∂θₜ₋₁   η: rate   ε: stabilizer",
                               "g = dL/dtheta; rate: learning rate; eps: numerical stabilizer"), p.dim, w - 4);
            }
        } else if (!learningDetails_ && agents_ > 1) {
            int completed = 0, recoveries = 0, falls = 0;
            double returns = 0, balanced = 0, quality = 0, low = 0, high = 0;
            for (const auto &agent : arenaAgents_) if (agent.completed) {
                if (!completed) { low = agent.returnMin; high = agent.returnMax; }
                low = std::min(low, agent.returnMin); high = std::max(high, agent.returnMax);
                completed += agent.completed; returns += agent.returnSum; balanced += agent.balancedSum;
                quality += agent.qualitySum; recoveries += agent.recoverySum; falls += agent.falls;
            }
            const int cardW = (w - 4) / 3;
            const std::array<std::string, 3> titles{"BATCH MEAN RETURN", "MEAN BALANCED TIME", "BATCH OUTCOMES"};
            const std::array<std::string, 3> values{
                completed ? number(returns / completed, 2) : glyph("—", "--"),
                completed ? number(balanced / completed, 2) + " s" : glyph("—", "--"),
                completed ? number(100. * falls / completed, 0) + "% falls" : glyph("—", "--")};
            const std::array<std::string, 3> notes{
                completed ? "Range " + number(low, 2) + " to " + number(high, 2) : "Completed episodes only",
                "Mean quality " + (completed ? number(quality / completed, 3) : "pending"),
                std::to_string(recoveries) + " recoveries / " + std::to_string(completed) + " episodes"};
            for (int i = 0; i < 3; ++i) {
                const int cx = i * (cardW + 2), cw = i == 2 ? w - cx : cardW;
                body.box(cx, 0, cw, 5, titles[i], p);
                body.put(cx + 2, 2, values[i], i == 1 ? p.purple + p.caps.strong : p.green + p.caps.strong, cw - 4);
                body.put(cx + 2, 3, notes[i], p.dim, cw - 4);
            }
            body.box(0, 6, w, 10, "AGENT COMPARISON / CURRENT BATCH", p);
            const int col = std::max(1, (w - 4) / 7);
            const std::array<std::string, 7> headers{"Agent", "Episodes", "Mean return", "Balanced s", "Quality", "Recoveries", "Falls"};
            for (int i = 0; i < 7; ++i) body.put(2 + i * col, 8, headers[i], p.purple, col - 1);
            for (int i = (selectedAgent_/4)*4; i < std::min(agents_,(selectedAgent_/4+1)*4); ++i) {
                const auto &a = arenaAgents_[i];
                const std::array<std::string, 7> cells{(independentWorkers_ ? "Worker " : "AURA ") + std::to_string(i + 1), std::to_string(a.completed),
                    a.completed ? number(a.returnSum / a.completed, 2) : "pending",
                    a.completed ? number(a.balancedSum / a.completed, 2) : "pending",
                    a.completed ? number(a.qualitySum / a.completed, 3) : "pending",
                    std::to_string(a.recoverySum), std::to_string(a.falls)};
                for (int j = 0; j < 7; ++j) body.put(2 + j * col, 9 + i%4, cells[j],
                    i == selectedAgent_ ? p.purple + p.caps.strong : j >= 2 ? p.green : p.text, col - 1);
            }
            const auto &a = arenaAgents_[selectedAgent_];
            body.put(2, 13, (independentWorkers_ ? "Selected worker " : "Selected AURA ") + std::to_string(selectedAgent_ + 1) + " / episode " + std::to_string(a.episode) +
                     " / " + a.state + " / return " + number(a.reward, 2) + " / balanced " + number(a.balance.seconds, 2) + " s",
                     p.text, w - 4);
            body.shortcuts(2, 14, "Left/Right worker / H chart filter", p, w - 4);
            const int chartH = std::max(15, contentHeight - 23);
            if (wide) {
                const int half = (w - 2) / 2;
                chart(body, 0, 17, half, chartH, "EPISODE RETURN / ALL AGENTS", rewardReturns_, {}, p.green, p,
                      ChartKind::rewardReturn, batchSize_, 0, rewardIndices_);
                chart(body, half + 2, 17, w - half - 2, chartH, "BALANCED TIME / ALL AGENTS", balancedDurations_, {}, p.purple, p,
                      ChartKind::balancedTime, batchSize_, 0, balancedIndices_);
            } else chart(body, 0, 17, w, chartH, "BALANCED TIME / ALL AGENTS", balancedDurations_, {}, p.purple, p,
                         ChartKind::balancedTime, batchSize_, 0, balancedIndices_);
            body.put(2, 17 + chartH + 1, "Charts: last 120 completed episodes across updates. Table: current batch only.", p.dim, w - 4);
            body.shortcuts(2, 17 + chartH + 2, "R learning details", p, w - 4);
            body.put(2, 17 + chartH + 3, "Quality 0–1 / FALLEN reward 0 / critic MSE " +
                     (criticLossSeen_ ? number(criticLoss_, 5) : "pending"), p.dim, w - 4);
        } else if (!learningDetails_) {
            const int cardW = (w - 4) / 3;
            const bool current = rewardSeen_ && rewardBatch_ == batch_;
            const int batchEpisodes = completedRewardBatch_ == batch_ ? rewardBatchEpisodes_ : 0;
            const std::array<std::string, 3> titles{"EPISODE RETURN", "REWARD / STEP", "BATCH MEAN RETURN"};
            const std::array<std::string, 3> values{
                current ? number(episodeReward_, 2) : glyph("—", "--"),
                current ? number(episodeReward_ / rewardSteps_, 3) : glyph("—", "--"),
                batchEpisodes ? number(rewardBatchReturn_ / batchEpisodes, 2) : glyph("—", "--")
            };
            const std::array<std::string, 3> notes{
                current
                    ? "Episode " + std::to_string(rewardEpisode_) + " / " + integer(rewardSteps_) + " steps"
                    : "Waiting for rewards",
                current ? "Last step " + number(stepReward_, 3) : "Return / steps",
                std::to_string(batchEpisodes) + " completed episodes"
            };
            for (int i = 0; i < 3; ++i) {
                const int cx = i * (cardW + 2), cw = i == 2 ? w - cx : cardW;
                body.box(cx, 0, cw, 5, titles[i], p);
                body.put(cx + 2, 2, values[i], i == 1 ? p.caps.strong + p.purple : p.caps.strong + p.green, cw - 4);
                body.put(cx + 2, 3, notes[i], p.dim, cw - 4);
            }
            const int chartH = std::max(15, contentHeight - 19);
            if (wide) {
                const int left = (w - 2) * 3 / 5;
                chart(body, 0, 6, left, chartH, "EPISODE REWARD", rewardReturns_, {}, p.green, p,
                      ChartKind::rewardReturn, batchSize_, 0, rewardIndices_);
                chart(body, left + 2, 6, w - left - 2, chartH, "REWARD QUALITY", rewardRates_, {}, p.purple, p,
                      ChartKind::rewardRate, batchSize_, 0, rewardIndices_);
            } else chart(body, 0, 6, w, chartH, "EPISODE REWARD", rewardReturns_, {}, p.green, p,
                         ChartKind::rewardReturn, batchSize_, 0, rewardIndices_);
            const int noteY = contentHeight - 12;
            body.box(0, noteY, w, 12, "BALANCE SYSTEM / ACTOR-CRITIC", p);
            body.put(2, noteY + 1, "Quality 0–1 per transition / confirmed FALLEN earns zero.", p.text, w - 4);
            body.put(2, noteY + 2, "Terminal step: 0. Return is the sum of step rewards.", p.dim, w - 4);
            body.put(2, noteY + 3, "Paired critic / MSE " + (criticLossSeen_ ? number(criticLoss_, 5) : "pending") +
                     " / target G divided by 100", p.dim, w - 4);
            const std::array<std::pair<std::string, const BalanceReadout *>, 3> measures{{
                {"Training", &trainingBalance_}, {"Deterministic", &deterministicBalance_},
                {"Stochastic", &stochasticBalance_}}};
            for (int i = 0; i < 3; ++i) {
                const auto &[label, value] = measures[i];
                body.put(2, noteY + 4 + i * 2, label + " / " + (value->valid ? value->duration() : "pending"), p.green, w - 4);
                if (value->valid) body.put(2, noteY + 5 + i * 2, value->outcome(), p.purple, w - 4);
            }
            body.shortcuts(2, noteY + 10, "R learning details", p, w - 4);
        } else {
            const int tableW = wide ? (w - 2) * 3 / 5 : w, notesX = wide ? tableW + 2 : 0, notesY =
                    wide ? 0 : episodeHeight + 1, notesW = wide ? w - tableW - 2 : w;
            body.box(0, 0, tableW, episodeHeight, "EPISODE DIAGNOSTICS", p);
            body.put(2, 1, objectiveUpdate_
                               ? "Collected before update " + std::to_string(objectiveUpdate_)
                               : "Results appear after " + std::to_string(batchSize_) + " episodes", p.dim, tableW - 4);
            const int stepX = 8, returnX = 16, advX = 27, logX = 40;
            body.put(2, 3, "Ep.", p.dim);
            body.put(stepX, 3, "Steps", p.dim);
            body.put(returnX, 3, "Return", p.dim);
            body.put(advX, 3, "Advantage", p.dim);
            body.put(logX, 3, "Sum log p", p.dim, tableW - logX - 2);
            for (int visible = 0; visible < episodeRows_ && episodeScroll_ + visible < batchSize_; ++visible) {
                const int i = episodeScroll_ + visible;
                const int row = 4 + visible;
                const auto &objective = objectives_[i];
                body.put(4, row, std::to_string(i + 1), p.text);
                if (!objective.valid) {
                    body.put(stepX, row, glyph("—", "--"), p.dim);
                    continue;
                }
                std::ostringstream steps, advantage, log;
                steps << std::setw(5) << objective.steps;
                advantage << std::showpos << std::fixed << std::setprecision(5) << std::setw(9) << objective.advantage;
                log << std::fixed << std::setprecision(3) << std::setw(10) << objective.logProbability;
                body.put(stepX, row, steps.str(), p.text);
                body.put(returnX, row, objective.hasReturn ? number(objective.totalReturn, 2) : glyph("—", "--"),
                         p.purple, 10);
                body.put(advX, row, advantage.str(), objective.advantage >= 0 ? p.green : p.gold);
                body.put(logX, row, log.str(), p.text);
            }
            body.put(2, episodeHeight - 3, "Episodes " + std::to_string(episodeScroll_ + 1) + glyph("–", "-") +
                     std::to_string(std::min(batchSize_, episodeScroll_ + episodeRows_)) + " of " + std::to_string(batchSize_) +
                     (episodeFocus_ ? " / focused" : " / Tab to focus"), episodeFocus_ ? p.purple : p.dim, tableW - 4);
            const int limit = std::max(0, batchSize_ - episodeRows_);
            if (limit) {
                const int thumb = std::max(1, episodeRows_ * episodeRows_ / std::max(1, batchSize_));
                const int top = (episodeRows_ - thumb) * episodeScroll_ / limit;
                for (int row = 0; row < episodeRows_; ++row)
                    body.put(tableW - 2, 4 + row, glyph(row >= top && row < top + thumb ? "┃" : "│", "|"),
                             row >= top && row < top + thumb ? p.purple : p.dim);
            }
            episodeLeft_ = x + 1; episodeWidth_ = tableW;
            body.put(2, episodeHeight - 2, "Episode advantage is diagnostic; loss uses step advantages.", p.dim,
                     tableW - 4);
            body.box(notesX, notesY, notesW, 14, "LEARNING CYCLE", p);
            const std::array<std::string, 4> labels{
                "Collect " + std::to_string(batchSize_) + " episodes", "Predict values with critic", "Differentiate actor loss",
                "Update actor and critic separately"
            };
            const std::array<std::string, 4> icons{glyph("○", "o"), glyph("≠", "!="), glyph("∇", "d"), glyph("↻", "~")};
            for (int i = 0; i < 4; ++i) {
                body.put(notesX + 2, notesY + 2 + i * 3, icons[i], p.purple);
                body.put(notesX + 5, notesY + 2 + i * 3, labels[i], p.text, notesW - 7);
                if (i < 3) {
                    body.put(notesX + 2, notesY + 3 + i * 3, glyph("│", "|"), p.dim);
                    body.put(notesX + 2, notesY + 4 + i * 3, glyph("↓", "v"), p.dim);
                }
            }
            const int formulaY = formulaTop;
            body.box(0, formulaY, w, formulaHeight, "POLICY GRADIENT / REWARD-TO-GO", p);
            const int mathW = wide ? (w - 2) / 2 : w;
            const int forwardX = wide ? mathW + 2 : 2;
            const int forwardW = wide ? w - forwardX - 2 : w - 4;
            if (wide) for (int row = 1; row < formulaUpperHeight; ++row) body.put(
                mathW, formulaY + row, glyph("│", "|"), p.dim);
            if (p.caps.utf8) {
                body.put(2, formulaY + 2, "Aᵢₜ =", p.bold);
                body.fraction(10, formulaY + 1, "Gᵢₜ − 100 Vφ(sᵢₜ)", integer(episodeLimit_), p.green);
            } else body.put(2, formulaY + 2, "A_it = (G_it - 100 V(s_it)) / " + std::to_string(episodeLimit_), p.bold,
                            mathW - 4);
            body.put(2, formulaY + 4,
                     glyph("Gᵢₜ = rᵢₜ + γ Gᵢ,ₜ₊₁   γ = ", "G_it = r_it + gamma * G_i,t+1   gamma = ") +
                     number(discount_, 3), p.purple, mathW - 4);
            if (p.caps.utf8) {
                body.put(2, formulaY + 6, "L = −(1/" + std::to_string(batchSize_) + ") ∑ᵢ ∑ₜ Aᵢₜ ℓᵢₜ", p.bold,
                         mathW - 4);
                body.put(2, formulaY + 7, "ℓᵢₜ = ∑ₖ log πθ(uᵢₜₖ | oᵢₜ)", p.purple, mathW - 4);
            } else {
                body.put(2, formulaY + 6, "L = -sum(A_it * ell_it) / " + std::to_string(batchSize_), p.bold, mathW - 4);
                body.put(2, formulaY + 7, "ell_it = sum over outputs: log p(raw sample | obs)", p.purple, mathW - 4);
            }
            body.put(2, formulaY + 8, "i: episode   t: step   k: output (" + std::to_string(actions_.size()) + ")",
                     p.dim, mathW - 4);
            body.put(2, formulaY + 9,
                     "V: detached critic prediction before fitting this batch.",
                     p.dim, mathW - 4);
            body.put(2, formulaY + 10, "Critic MSE target: G / 100. Separate Adam optimizers.", p.dim, mathW - 4);
            if (!wide) body.rule(2, formulaY + 11, w - 4, p.dim);
            const int forwardY = wide ? formulaY + 2 : formulaY + 12;
            body.put(forwardX, forwardY, "FORWARD PASS", p.purple + p.caps.strong, forwardW);
            for (int i = 0; i < static_cast<int>(layers_.size()); ++i) {
                const auto &layer = layers_[i];
                const auto output = i + 1 == static_cast<int>(layers_.size())
                                        ? glyph("μ", "mu")
                                        : "h" + subscript(i + 1);
                const auto previous = i == 0 ? "o" : "h" + subscript(i);
                std::string activation = layer.activation;
                if (activation == "Tanh") activation = "tanh";
                if (activation == "ReLU") activation = "relu";
                if (activation == "Sigmoid") activation = "sigmoid";
                const auto affine = "W" + subscript(i + 1) + previous + " + b" + subscript(i + 1);
                body.put(forwardX, forwardY + 1 + i,
                         output + " = " + (activation == "Identity" ? affine : activation + "(" + affine + ")") + "   "
                         + std::to_string(layer.inputs) + " " + glyph("→", "->") + " " + std::to_string(layer.outputs),
                         p.text, forwardW);
            }
            const int sampleY = forwardY + 2 + static_cast<int>(layers_.size());
            body.put(forwardX, sampleY, "EXPLORATION", p.purple + p.caps.strong, forwardW);
            body.put(forwardX, sampleY + 1,
                     glyph("u ∼ N(μ, σ²)   σ = ", "u ~ Normal(mu, sigma)   sigma = ") + number(sigma_, 3), p.green,
                     forwardW);
            body.put(forwardX, sampleY + 2,
                     "a = clip(u, " + number(sampleLow_, 1) + ", " + number(sampleHigh_, 1) + ")", p.purple, forwardW);
            body.put(forwardX, sampleY + 3, "The loss uses raw samples u", p.dim, forwardW);
            body.put(forwardX, sampleY + 4, "The body receives scaled actions a", p.dim, forwardW);
            const int scalingY = formulaY + formulaUpperHeight;
            body.rule(2, scalingY, w - 4, p.dim);
            body.put(2, scalingY + 1, "ACTION SCALING / RADIANS", p.purple + p.caps.strong, w - 4);
            const int scalingWidth = (w - 4) / scalingColumns;
            for (int i = 0; i < static_cast<int>(actions_.size()); ++i) {
                const auto &action = actions_[i];
                std::string label = action.label;
                for (const auto &joint: joint_vector_controls())
                    for (int axis = 0; axis < 3; ++axis)
                        if (joint.slots[axis] >= 0 && action.key == sim::actionSpecs[joint.slots[axis]].key)
                            label = joint.name + " / " + std::array{"Bend", "Turn", "Tilt"}[axis];
                const int cx = 2 + (i / scalingRows) * scalingWidth, cy = scalingY + 2 + i % scalingRows;
                const int labelWidth = scalingWidth - 23;
                body.put(cx, cy, label, p.text, labelWidth - 1);
                const bool off = action.motorKnown && !action.enabled;
                body.put(cx + labelWidth, cy,
                         off ? "motor off" : number(action.scale, 3) + (action.nonnegative ? " max(0, a)" : " x a"),
                         off ? p.gold : p.green, 22);
            }
            body.put(2, scalingY + 3 + scalingRows,
                     "Best: balanced duration, then quality return. Survival remains a diagnostic.", p.dim, w - 4);
        }
        scrollMax_ = std::max(0, contentHeight - viewport_);
        scroll_[page_ - 1] = std::clamp(scroll_[page_ - 1], 0, scrollMax_);
        if (!deviceInfo_ && page_ == 3 && learningDetails_) {
            episodeTop_ = bodyTop + 1;
            episodeHeight_ = std::max(0, std::min(viewport_, episodeHeight - scroll_[2]));
        }
        screen.copy(body, x, bodyTop, scroll_[page_ - 1], viewport_);
        if (scrollMax_ > 0) {
            const int track = viewport_, thumb = std::max(1, viewport_ * viewport_ / contentHeight);
            const int top = (track - thumb) * scroll_[page_ - 1] / std::max(1, scrollMax_);
            for (int row = 0; row < track; ++row) screen.put(x + w, bodyTop + row,
                                                             glyph(row >= top && row < top + thumb ? "┃" : "│",
                                                                   row >= top && row < top + thumb ? "#" : "|"),
                                                             row >= top && row < top + thumb ? p.purple : p.dim);
        }
        screen.rule(x, rows - 3, w, p.dim);
        const std::string navigation = deviceInfo_ ? "D close device info   " + glyph("↑↓ scroll", "Up/Down scroll") :
                                      "D device info   " + (page_ == 2
                                           ? glyph("↑↓ tensor  ←→ value", "Up/Down tensor  Left/Right value") + (
                                                 w >= 100 ? "  [ / ] jump  F math" : "  F math")
                                           : page_ == 3
                                                 ? std::string(
                                                       learningDetails_ ? (episodeFocus_ ? "R reward viewer   Tab page   " + glyph("↑↓ episodes", "Up/Down episodes") : "R reward viewer   Tab episodes") : "R learning details") + (
                                                       scrollMax_ > 0 && (!learningDetails_ || !episodeFocus_)
                                                           ? "   " + glyph("↑↓ scroll", "Up/Down scroll")
                                                           : "")
                                                 : page_ == 1
                                                       ? std::string(chartMode_ == 0 ? "G survival chart" : chartMode_ == 1 ? "G loss chart" : "G balance chart")
                                                         + "   T joint targets"
                                                       : glyph("↑↓ scroll  1 Overview  2 Parameters",
                                                               "Up/Down scroll  1 Overview  2 Parameters"));
        const std::string controls = exited || failure
                                         ? "Q back   Esc home"
                                         : (visual_ ? "V headless  " : "V visual  ") + std::string(
                                               paused_
                                                   ? "P resume  Q back   Esc home"
                                                   : phase_ == "ready"
                                                         ? (runBatches_ == 1 ? "N next batch  Q back   Esc home" :
                                                            "N next " + std::to_string(runBatches_) + " batches  Q back   Esc home")
                                                         : "P pause  Q back   Esc home");
        const int controlsWidth = shortcut_width(controls);
        screen.shortcuts(x, rows - 2, navigation, p, std::max(0, w - controlsWidth - 2));
        screen.shortcuts(x + std::max(0, w - controlsWidth), rows - 2, controls, p, std::min(w, controlsWidth));
        const std::string mode = visual_ ? "Visual training" : "Headless training";
        const std::string bottom = failure
                                       ? (error_.empty()
                                              ? "Training stopped unexpectedly"
                                              : "Training could not continue / " + error_)
                                       : exited
                                             ? "Training session ended"
                                             : !viewerError_.empty()
                                                   ? "Viewer unavailable / " + viewerError_
                                                   : phase_ == "ready"
                                                         ? glyph("●", "*") + " Ready  " + glyph("·", "/") + "  " + mode
                                                           + "  " + glyph("·", "/") + "  " + (
                                                               checkpoint_.empty()
                                                                   ? "Checkpoint pending"
                                                                   : "Saved / " + checkpoint_)
                                                         : paused_
                                                               ? glyph("Ⅱ", "||") + " Paused / " + mode
                                                               : mode + "  " + glyph("·", "/") + "  " + (updates_
                                                                     ? "Latest gradient norm " + number(
                                                                           gradient_, 3)
                                                                     : "Collecting experience before the first update");
        const std::string scrollHint = "PgUp/PgDn / wheel scroll";
        const int scrollHintWidth = shortcut_width(scrollHint);
        screen.put(x, rows - 1, failure || warning_.empty() ? bottom : "Warning / " + warning_,
                   failure ? p.caps.error_color : !warning_.empty() ? p.gold : !viewerError_.empty() ? p.gold : p.dim,
                   w - (scrollMax_ > 0 ? scrollHintWidth + 1 : 0));
        if (scrollMax_ > 0) screen.shortcuts(x + w - scrollHintWidth, rows - 1, scrollHint, p, scrollHintWidth);
        return screen.lines();
    }
}
