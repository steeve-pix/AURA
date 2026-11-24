#include "screens/NNTraining.hpp"
#include "ui/ModelActivity.hpp"
#include "ui/TrainingCharts.hpp"
#include "../../include/aura/sim/ActionSpec.hpp"

#include <cassert>
#include <algorithm>
#include <clocale>
#include <cmath>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace {
    std::string plain(const std::string &line) {
        static const std::regex escapes("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])");
        return std::regex_replace(line, escapes, "");
    }

    int cells(const std::string &line) {
        const auto text = plain(line);
        std::mbstate_t state{};
        int result = 0;
        for (std::size_t at = 0; at < text.size();) {
            wchar_t decoded = 0;
            const auto length = std::mbrtowc(&decoded, text.data() + at, text.size() - at, &state);
            assert(length != static_cast<std::size_t>(-1) && length != static_cast<std::size_t>(-2));
            assert(length > 0);
            // Some IDE terminals advance for combining accents despite wcwidth=0.
            // Dashboard notation uses standalone symbols to keep borders aligned.
            assert(decoded < 0x0300 || decoded > 0x036f);
            result += std::max(0, wcwidth(decoded));
            at += length;
        }
        return result;
    }

    std::string text(aura::cli::NNTrainingScreen &screen, int width = 120, int rows = 60) {
        std::string result;
        for (const auto &line: screen.frame(width, rows, false)) result += plain(line) + '\n';
        return result;
    }

    void fixture(aura::cli::NNTrainingScreen &screen) {
        // Fixed presentation fixture, not a performance claim or a training result.
        screen.receive("AURA_NN\tstart\t8\t1200\t42\t19424");
        screen.receive("AURA_NN\toptimizer\t0.001\t0.9\t0.999\t1e-8");
        screen.receive("AURA_NN\tdevice\tcpu");
        screen.receive("AURA_NN\tinterface\t205\t32\t2\t5");
        screen.receive("AURA_NN\tmotors\t14\t15\tHead");
        screen.receive("AURA_NN\tevaluation\t0\t377");
        screen.receive("AURA_NN\tbest\t377\taura_balance_best.pt");
        screen.receive("AURA_NN\tlayer\t205\t64\tTanh");
        screen.receive("AURA_NN\tlayer\t64\t64\tTanh");
        screen.receive("AURA_NN\tlayer\t64\t32\tTanh");
        screen.receive("AURA_NN\tsampling\t0.25\t-1\t1");
        screen.receive("AURA_NN\tcredit\t0.995");
        for (std::size_t i = 0; i < aura::sim::actionSpecs.size(); ++i) {
            const auto &spec = aura::sim::actionSpecs[i];
            if (std::string(spec.key) == "head_target") continue;
            const double scale = i < 6 ? (i == 2 || i == 3 ? 0.3 : 0.2) : spec.maximum * 0.2;
            screen.receive("AURA_NN\taction\t" + std::string(spec.key) + "\t" + std::to_string(scale) +
                           "\t" + (spec.minimum >= 0 ? "1" : "0") + "\t" + spec.label);
        }
        const char *names[]{
            "network.0.weight", "network.0.bias", "network.2.weight", "network.2.bias", "network.4.weight",
            "network.4.bias"
        };
        const char *shapes[]{"64x205", "64", "64x64", "64", "32x64", "32"};
        const int counts[]{13120, 64, 4096, 64, 2048, 32};
        const char *labels[]{
            "Hidden 1 / weights", "Hidden 1 / biases", "Hidden 2 / weights", "Hidden 2 / biases", "Output / weights",
            "Output / biases"
        };
        for (int i = 0; i < 6; ++i) {
            screen.receive(
                "AURA_NN\ttensor\t" + std::to_string(i) + "\t" + names[i] + "\t" + shapes[i] + "\t" +
                std::to_string(counts[i]) + "\t" + labels[i]);
            screen.receive(
                "AURA_NN\tstats\t" + std::to_string(i) + "\t-0.0999234\t0.100171\t4.61812\t0.810305\t0.00100353");
        }
        const int scores[]{
            234, 271, 267, 240, 248, 282, 226, 257, 270, 250, 240, 260, 235, 250, 230, 245, 263, 258, 256, 253, 221,
            248, 245, 258
        };
        for (int batch = 1; batch <= 3; ++batch) {
            screen.receive("AURA_NN\tphase\tcollecting");
            for (int i = 0; i < 8; ++i) {
                const int steps = scores[(batch - 1) * 8 + i];
                screen.receive("AURA_NN\tbalance\ttraining\t20\t0.167\t230\t20\t0\t1\t0.4\t100\tfallen");
                screen.receive(
                    "AURA_NN\treward\t" + std::to_string(batch) + "\t" + std::to_string(i + 1) + "\t" +
                    std::to_string(steps) + "\t0\t" + std::to_string(steps * .95) + "\t1");
                screen.receive(
                    "AURA_NN\tepisode\t" + std::to_string(batch) + "\t" + std::to_string(i + 1) + "\t" +
                    std::to_string(steps) + "\tfallen");
            }
            screen.receive(
                "AURA_NN\tupdate\t" + std::to_string(batch) + "\t" + (batch == 1
                                                                          ? "-0.0376061"
                                                                          : batch == 2
                                                                                ? "-0.00341794"
                                                                                : "-0.0136858") +
                "\t250.25\t263\t1.39539\t0.00100353");
        }
        screen.receive(
            "AURA_NN\tprogress\t3\t8\t258\t3.03236\t0.495659\t0.0434403\t-0.112093\t0.138943\t0.0507737\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0");
        for (int i = 0; i < 8; ++i) screen.receive(
            "AURA_NN\tobjective\t" + std::to_string(i + 1) + "\t" + std::to_string(scores[16 + i]) +
            "\t0.0121429\t-21.3747");
        screen.receive("AURA_NN\tinspect\t0\t0\t0,0\t3\t0.0738121\t0.0741759\t-0.00692974\t-0.00115667\t4.12443e-7");
        screen.receive("AURA_NN\tphase\tready");
        assert(text(screen, 45, 12).find("Enlarge terminal") != std::string::npos);
    }
}

int main(int argc, char **argv) {
    std::setlocale(LC_CTYPE, "");
    // One high episode must not push the ordinary samples to the bottom.
    aura::cli::NNTrainingScreen centered;
    centered.receive("AURA_NN\tstart\t8\t1200\t42\t100");
    const int centeredScores[]{240, 260, 270, 280, 290, 300, 250, 402};
    double average = 0;
    for (int i = 0; i < 8; ++i) {
        average += centeredScores[i] / 8.0;
        centered.receive(
            "AURA_NN\tepisode\t1\t" + std::to_string(i + 1) + "\t" + std::to_string(centeredScores[i]) + "\tfallen");
    }
    bool inChart = false;
    std::vector<double> ticks;
    centered.key("g"); // Survival remains available as a secondary diagnostic.
    for (const auto &row: centered.frame(160, 40, false)) {
        const auto line = plain(row);
        if (line.find("TRAINING SURVIVAL") != std::string::npos) inChart = true;
        if (!inChart || (line.find("────") == std::string::npos && line.find("----") == std::string::npos)) continue;
        std::smatch label;
        if (ticks.size() < 3 && std::regex_search(line, label, std::regex("^ *(?:│|\\|) +(-?[0-9]+(?:\\.[0-9]+)?) ")))
            ticks.push_back(std::stod(label[1].str()));
    }
    assert(ticks.size() == 3);
    assert(ticks.front() > 402 && ticks.back() < 240);
    assert(std::abs((ticks.front() + ticks.back()) / 2 - average) < 20);
    aura::cli::NNTrainingScreen screen;
    fixture(screen);
    aura::cli::NNTrainingScreen rewardView;
    fixture(rewardView);
    rewardView.key("3");
    rewardView.receive("AURA_NN\tphase\tcollecting");
    rewardView.receive("AURA_NN\treward\t4\t1\t100\t0.9\t80\t0");
    auto liveReward = text(rewardView, 160, 40);
    assert(liveReward.find("80.00") != std::string::npos);
    assert(liveReward.find("0.800") != std::string::npos);
    assert(liveReward.find("Last step 0.900") != std::string::npos);
    rewardView.receive("AURA_NN\treward\t4\t1\t100\t0\t80\t1");
    const auto completedReward = text(rewardView, 160, 40);
    rewardView.receive("AURA_NN\treward\t4\t1\t100\t0\t80\t1");
    assert(text(rewardView, 160, 40) == completedReward); // No duplicate history or mean.
    rewardView.receive("AURA_NN\tepisode\t4\t1\t100\tfallen");
    rewardView.receive("AURA_NN\treward\t4\t2\t200\t0\t180\t1");
    const auto batchReward = text(rewardView, 160, 40);
    assert(batchReward.find("130.00") != std::string::npos);
    assert(batchReward.find("2 completed episodes") != std::string::npos);
    rewardView.receive("AURA_NN\treward\t4\t2\t200\t0\tnan\t1");
    rewardView.receive("AURA_NN\treward\t3\t8\t200\t0\t100\t1");
    rewardView.receive("AURA_NN\treward\t4\t2\t0\t0\t100\t1");
    assert(text(rewardView, 160, 40) == batchReward);
    rewardView.key("r");
    assert(text(rewardView, 160, 40).find("EPISODE DIAGNOSTICS") != std::string::npos);
    rewardView.key("r");
    assert(text(rewardView, 160, 40) == batchReward);
    aura::cli::NNTrainingScreen resumed;
    fixture(resumed);
    resumed.receive("AURA_NN\tmodel\tbalance_2.pt\tresume\t24\t3");
    assert(text(resumed).find("balance_2.pt") != std::string::npos);
    assert(text(resumed).find("24 episodes saved") != std::string::npos);
    assert(resumed.updates() == 3);
    const auto resumedOverview = text(resumed);
    assert(resumedOverview.find("TRAINING / LAST EPISODE") != std::string::npos);
    assert(resumedOverview.find("BEST BALANCE POLICY") != std::string::npos);
    resumed.key("g");
    resumed.key("g");
    assert(text(resumed).find("/ update 3") != std::string::npos);
    resumed.key("g");
    resumed.receive("AURA_NN\thistory_episode\t25\t258\t100\t0.167");
    assert(text(resumed).find("/ episode 25") != std::string::npos); // Saved episode indices are retained.
    assert(screen.updates() == 3);
    const auto overview = text(screen);
    assert(overview.find("14/15 joint motors") != std::string::npos);
    assert(overview.find("Initial policy") != std::string::npos);
    assert(overview.find("Head off") != std::string::npos);
    assert(overview.find("[T] inspect joint targets") != std::string::npos);
    const auto compact = text(screen, 160, 40);
    assert(compact.find("wheel scroll") != std::string::npos); // Minimum readable chart height requires a short page scroll.
    assert(compact.find("TRAINING BALANCED TIME") != std::string::npos);
    assert(compact.find("POLICY LOSS") != std::string::npos);
    assert(compact.find("EVALUATION BALANCED TIME") != std::string::npos);
    assert(compact.find("[G] survival chart") != std::string::npos);
    screen.key("g");
    assert(text(screen, 160, 40).find("EVALUATION SURVIVAL") != std::string::npos);
    assert(text(screen, 160, 40).find("[G] loss chart") != std::string::npos);
    screen.key("g");
    assert(text(screen, 160, 40).find("[G] balance chart") != std::string::npos);
    screen.key("g");
    const auto evaluationRow = [](const std::vector<std::string> &frame) {
        for (std::size_t row = 0; row < frame.size(); ++row)
            if (plain(frame[row]).find("EVALUATION BALANCED TIME") != std::string::npos) return static_cast<int>(row);
        return -1;
    };
    const int collapsedEvaluationRow = evaluationRow(screen.frame(160, 45, false));
    assert(collapsedEvaluationRow >= 0);
    assert(screen.key("t").empty());
    assert(evaluationRow(screen.frame(160, 45, false)) == collapsedEvaluationRow);
    assert(text(screen, 160, 45).find("wheel scroll") != std::string::npos); // Expanded targets retain the readable chart height.
    const auto expandedTargets = text(screen, 160, 60);
    assert(expandedTargets.find("Bend") != std::string::npos);
    assert(expandedTargets.find("Turn") != std::string::npos);
    assert(expandedTargets.find("Tilt") != std::string::npos);
    assert(expandedTargets.find("Left shoulder") != std::string::npos);
    assert(expandedTargets.find("locked") != std::string::npos);
    auto populatedTargets = screen;
    std::string targetProgress = "AURA_NN\tprogress\t3\t8\t258\t3.03\t0.496";
    for (int axis = 0; axis < 32; ++axis) targetProgress += "\t" + std::to_string(
                                              axis == 0 ? 0.125 : axis == 18 ? 0.250 : 0.0);
    populatedTargets.receive(targetProgress);
    const auto targetValues = text(populatedTargets, 160, 60);
    assert(targetValues.find("+0.125") != std::string::npos);
    assert(targetValues.find("+0.250") != std::string::npos);

    for (const auto &row: screen.frame(160, 60, false)) {
        const auto line = plain(row);
        const auto hint = line.find("[T] collapse targets");
        if (hint != std::string::npos) {
            assert(hint > 0 && line[hint - 1] == ' ');
            assert(line[hint + std::string("[T] collapse targets").size()] == ' ');
        }
    }
    for (int width: {60, 80, 108, 120, 160})
        for (int rows: {18, 32, 60})
            for (const auto &line: screen.frame(width, rows, false)) assert(cells(line) <= width);
    assert(text(screen, 160, 60).find("Right wrist") != std::string::npos);
    assert(text(screen, 160, 60).find("head_target") == std::string::npos);
    screen.key("t");
    assert(text(screen, 160, 40) == compact);
    assert(overview.find("282") != std::string::npos);
    assert(overview.find("EVAL SURVIVAL") != std::string::npos);
    screen.receive("AURA_NN\tphase\tevaluating");
    assert(text(screen).find("EVALUATING") != std::string::npos);
    screen.receive("AURA_NN\tevaluation\t3\t267");
    screen.receive("AURA_NN\tphase\tready");
    assert(text(screen).find("267 steps") != std::string::npos);
    screen.receive("AURA_NN\tevaluation\t3\t999"); // Duplicate results cannot replace a measurement.
    screen.receive("AURA_NN\tevaluation\t2\t999"); // Nor can stale updates.
    screen.receive("AURA_NN\tevaluation\t4\t999"); // Future updates have no matching trained policy.
    assert(text(screen).find("267 steps") != std::string::npos);
    auto evaluated = screen;
    evaluated.receive("AURA_NN\tupdate\t4\t-0.1\t250\t280\t1\t0.001");
    evaluated.receive("AURA_NN\tevaluation\t4\t1201"); // Respect the configured episode limit.
    assert(text(evaluated).find("267 steps") != std::string::npos);
    evaluated.receive("AURA_NN\tevaluation\t4\t259");
    assert(text(evaluated).find("259 steps") != std::string::npos);
    assert(text(evaluated).find("Update 4 / -8") != std::string::npos);
    evaluated.receive("AURA_NN\tbest\t310\tbalance_best.pt");
    assert(text(evaluated).find("BEST BALANCE POLICY") != std::string::npos);
    assert(text(evaluated).find("310 survival steps") != std::string::npos);
    assert(text(evaluated).find("balance_best.pt") != std::string::npos);
    screen.receive("AURA_NN\tupdate\t4\tnan\t250\t280\t1\t0.001");
    assert(screen.updates() == 3);
    screen.receive("AURA_NN\tstats\tbad");
    screen.key("2");
    assert(screen.key("v") == "visual");
    assert(screen.key("V") == "visual");
    screen.receive("AURA_NN\tviewer\t1");
    assert(text(screen, 120, 40).find("[V] headless") != std::string::npos);
    screen.receive("AURA_NN\tviewer\t0");
    assert(text(screen, 120, 40).find("[V] visual") != std::string::npos);
    screen.receive("AURA_NN\tviewer_error\tNo display available");
    assert(text(screen, 120, 40).find("No display available") != std::string::npos);
    screen.receive("AURA_NN\tviewer\t0");
    assert(screen.key("\033[B") == "inspect 1 0");
    assert(screen.key("\033[F") == "inspect 1 63");
    assert(text(screen).find("Loading selected value") != std::string::npos);
    screen.key("\033[A");
    assert(text(screen).find("0.074176") != std::string::npos);
    screen.receive("AURA_NN\tpaused\t1");
    assert(text(screen, 160, 40).find("[P] resume") != std::string::npos);
    assert(text(screen, 160, 40).find("[N] next") == std::string::npos);
    assert(screen.key("n").empty());
    assert(screen.key("p") == "pause");
    screen.receive("AURA_NN\tpaused\t0");
    assert(text(screen, 160, 40).find("[N] next") != std::string::npos);
    assert(screen.key("n") == "next");
    assert(screen.key("n").empty());
    screen.receive("AURA_NN\tphase\tcollecting");
    assert(text(screen).find("Batch 4") != std::string::npos);
    screen.key("3");
    const auto rewards = text(screen, 160, 40);
    assert(rewards.find("EPISODE RETURN") != std::string::npos);
    assert(rewards.find("REWARD / STEP") != std::string::npos);
    assert(rewards.find("EPISODE REWARD") != std::string::npos);
    assert(rewards.find("REWARD QUALITY") != std::string::npos);
    assert(rewards.find("BALANCE SYSTEM / ACTOR-CRITIC") != std::string::npos);
    assert(rewards.find("[R] learning details") != std::string::npos);
    assert(rewards.find("0.950") != std::string::npos);
    assert(rewards.find("FORWARD PASS") == std::string::npos);
    screen.key("r");
    screen.receive("AURA_NN\tobjective\t1\t249\t-0.01735\t-21.3747\t237.86");
    assert(text(screen).find("Collected before update 3") != std::string::npos);
    const auto learning = text(screen, 120, 60);
    assert(learning.find("head_target") == std::string::npos);
    assert(learning.find("FORWARD PASS") != std::string::npos);
    assert(learning.find("The loss uses raw samples u") != std::string::npos);
    assert(learning.find("body receives scaled actions a") != std::string::npos);
    assert(learning.find("Return") != std::string::npos);
    assert(learning.find("237.86") != std::string::npos);
    assert(learning.find("Predict values with critic") != std::string::npos);
    assert(learning.find("REWARD-TO-GO") != std::string::npos);
    assert(learning.find("0.995") != std::string::npos);
    assert(learning.find("Separate Adam optimizers") != std::string::npos);
    assert(learning.find("Reward is unused") == std::string::npos);
    const auto wideLearning = text(screen, 160, 60);
    assert(wideLearning.find("EXPLORATION") != std::string::npos);
    assert(wideLearning.find("ACTION SCALING / RADIANS") != std::string::npos);
    assert(wideLearning.find("Right ankle / Tilt") != std::string::npos);
    assert(wideLearning.find("wheel scroll") == std::string::npos);
    assert(wideLearning.find("0.300 max(0, a)") != std::string::npos);
    bool sideBySide = false;
    for (const auto &row: screen.frame(160, 60, false)) {
        const auto line = plain(row);
        if (line.find("Left hip / Bend") != std::string::npos && line.find("Right elbow / Bend") != std::string::npos)
            sideBySide = true;
    }
    assert(sideBySide);

    screen.receive("AURA_NN\tobjective\t1\t249\t-0.01735\t-21.3747\tinvalid");
    assert(text(screen, 120, 60).find("237.86") != std::string::npos);
    screen.receive("AURA_NN\tphase\tready");
    for (int page = 1; page <= 3; ++page)
        for (int width: {1, 20, 59, 60, 65, 80, 100, 112, 120, 160, 220})
            for (int rows: {1, 8, 17, 18, 20, 24, 32, 40, 60}) {
                screen.key(std::to_string(page));
                for (const bool expanded: {false, true}) {
                    const auto frame = screen.frame(width, rows, false);
                    assert(static_cast<int>(frame.size()) == rows);
                    for (const auto &line: frame) {
                        if (cells(line) > width)
                            std::cerr << "Overflow: page " << page << " " << width << "x" << rows << " cells=" <<
                                    cells(line) << " line=" << plain(line) << '\n';
                        assert(cells(line) <= width);
                    }
                    if (page == 3) screen.key("r");
                    else screen.key("f");
                }
            }
    screen.key("2");
    screen.frame(65, 20, false);
    screen.key("\033[6~");
    screen.key("\033[6~");
    screen.key("\033[6~");
    const auto scrolled = text(screen, 65, 20);
    assert(scrolled.find("ADAM") != std::string::npos || scrolled.find("Calculated") != std::string::npos);
    // Page scrolling leaves tensor selection and the selected scalar unchanged.
    aura::cli::NNTrainingScreen scrolling;
    fixture(scrolling);
    scrolling.key("2");
    scrolling.key("f");
    const auto beforeScroll = text(scrolling, 120, 24);
    for (const auto &key: {"\033[6~", "scroll_down"}) assert(scrolling.key(key).empty());
    // Bring the heading back into view after testing mouse scrolling past it.
    assert(scrolling.key("scroll_up").empty());
    const auto afterScroll = text(scrolling, 120, 24);
    assert(afterScroll != beforeScroll);
    assert(scrolling.key("l") == "inspect 0 1");
    const auto rowOf = [](const std::string &frame, const std::string &label) {
        const auto position = frame.find(label);
        assert(position != std::string::npos);
        return std::count(frame.begin(), frame.begin() + position, '\n');
    };
    assert(rowOf(text(scrolling, 120, 24), "ADAM / UPDATE RULE") == rowOf(afterScroll, "ADAM / UPDATE RULE"));
    for (const auto &key: {"\033[5~", "scroll_up"}) assert(scrolling.key(key).empty());
    assert(text(scrolling, 120, 24) != afterScroll);
    assert(scrolling.key("\033[B") == "inspect 1 0");
    // More episodes and a deeper/wider model must remain fully inspectable.
    aura::cli::NNTrainingScreen variant;
    variant.receive("AURA_NN\tstart\t16\t777\t9\t999");
    variant.receive("AURA_NN\tlayer\t17\t128\tTanh");
    variant.receive("AURA_NN\tlayer\t128\t96\tReLU");
    variant.receive("AURA_NN\tlayer\t96\t32\tTanh");
    variant.receive("AURA_NN\tlayer\t32\t4\tTanh");
    variant.receive("AURA_NN\tdevice\ttest-device");
    for (int i = 0; i < 8; ++i)
        variant.receive(
            "AURA_NN\ttensor\t" + std::to_string(i) + "\tnetwork." + std::to_string(i) + ".weight\t4x32\t128");
    variant.receive("AURA_NN\tupdate\t1\t-0.1\t11\t16\t1\t.001");
    for (int i = 1; i <= 16; ++i)
        variant.receive("AURA_NN\tobjective\t" + std::to_string(i) + "\t" + std::to_string(1000 + i) + "\t0.1\t-2");
    variant.key("2");
    const auto parameterText = text(variant, 160, 90);
    assert(parameterText.find("128") != std::string::npos);
    assert(parameterText.find("network.7 / weight") != std::string::npos);
    for (int i = 0; i < 7; ++i) variant.key("\033[B");
    assert(variant.key("l") == "inspect 7 1");
    variant.key("3");
    variant.key("r");
    text(variant, 160, 90);
    variant.key("\033[F"); // Last episode is reached through the inner list.
    const auto variantText = text(variant, 160, 90);
    assert(variantText.find("1016") != std::string::npos);
    assert(variantText.find("777") != std::string::npos);
    assert(variantText.find("Collect 16 episodes") != std::string::npos);
    assert(variantText.find("detached critic prediction") != std::string::npos);
    assert(variantText.find("relu") != std::string::npos);
    for (int width: {60, 80, 120, 160})
        for (int rows: {18, 32, 90})
            for (int page = 1; page <= 3; ++page) {
                variant.key(std::to_string(page));
                for (const auto &line: variant.frame(width, rows, false)) assert(cells(line) <= width);
            }
    if (argc == 3 && std::string(argv[1]) == "--preview") {
        aura::cli::NNTrainingScreen preview;
        preview.receive("AURA_NN\tmodel\tbalance.pt\tresume\t0\t0");
        preview.receive("AURA_NN\tobjective_contract\tbalance_quality\t3\tbalance_v2\t2\tactor_critic_mc");
        preview.receive("AURA_NN\tbalance\tdeterministic\t60\t0.500\t300\t60\t0\t1\t0.4\t100\tfallen");
        fixture(preview);
        preview.receive("AURA_NN\tbalance\tdeterministic\t32\t0.267\t235\t32\t0\t1\t0.3\t80\tfallen");
        preview.receive("AURA_NN\tbalance\tbest\t80\t0.667\t230\t80\t0\t1\t0.4\t110\tfallen");
        preview.receive("AURA_NN\tcheckpoint\tbalance.pt\t24");
        preview.receive("AURA_NN\tevaluation\t3\t267");
        preview.receive("AURA_NN\tbest\t310\tbalance_best.pt");
        preview.receive("AURA_NN\tobjective\t1\t249\t-0.01735\t-21.3747\t237.86");
        for (int page = 1; page <= 3; ++page) {
            preview.key(std::to_string(page));
            std::ofstream file(std::string(argv[2]) + "-" + std::to_string(page) + ".ansi");
            for (const auto &line: preview.frame(120, 40, false)) file << line << '\n';
        }
        preview.key("1");
        preview.key("t");
        std::ofstream targets(std::string(argv[2]) + "-targets.ansi");
        for (const auto &line: preview.frame(160, 45, false)) targets << line << '\n';
        preview.key("3");
        preview.key("r");
        std::ofstream learningPreview(std::string(argv[2]) + "-learning.ansi");
        for (const auto &line: preview.frame(160, 60, false)) learningPreview << line << '\n';
        preview.key("2");
        preview.key("f");
        std::ofstream file(std::string(argv[2]) + "-math.ansi");
        for (const auto &line: preview.frame(120, 40, false)) file << line << '\n';
    }
    aura::cli::NNTrainingScreen restored;
    restored.receive("AURA_NN\tmodel\thistory.pt\tresume\t64\t7");
    restored.receive("AURA_NN\tstart\t32\t1200\t42\t19424");
    restored.receive("AURA_NN\thistory_update\t3\t0.1\t290\t310\t0.300\t1");
    restored.receive("AURA_NN\thistory_update\t7\t0.2\t300\t320\t0.500\t1");
    restored.receive("AURA_NN\thistory_episode\t63\t300\t80\t0.250");
    restored.receive("AURA_NN\thistory_episode\t64\t320\t90\t0.500");
    restored.receive("AURA_NN\thistory_state\t0.2\t0.3\t1.2\t0.001");
    restored.receive("AURA_NN\thistory_end\t64\t2");
    assert(restored.updates() == 7);
    assert(text(restored).find("/ episode 64") != std::string::npos);
    assert(text(restored).find("0.500 s / update 7") != std::string::npos);
    const auto restoredBefore = text(restored);
    restored.receive("AURA_NN\tevaluation\t7\t320");
    assert(text(restored) == restoredBefore); // Initial resume evaluation does not duplicate saved history.
    restored.key("g"); restored.key("g");
    assert(text(restored).find("/ update 7") != std::string::npos);
    aura::cli::NNTrainingScreen ended;
    ended.receive("AURA_NN\tphase\tstopped");
    std::string stopped;
    for (const auto &line: ended.frame(120, 40, true)) stopped += plain(line);
    assert(stopped.find("STOPPED") != std::string::npos && stopped.find("EXITED") == std::string::npos);
    ended.receive("AURA_NN\tphase\tready");
    std::string completed;
    for (const auto &line: ended.frame(120, 40, true)) completed += plain(line);
    assert(completed.find("COMPLETE") != std::string::npos);
    std::string failed;
    for (const auto &line: ended.frame(120, 40, true, true)) failed += plain(line);
    assert(failed.find("FAILED") != std::string::npos && failed.find("[P] pause") == std::string::npos);
    aura::cli::NNTrainingScreen warned;
    warned.receive("AURA_NN\tstart\t8\t1200\t42\t100");
    warned.receive("/torch/functional_tensor.py:368: UserWarning: Failed to initialize NumPy: No module named 'numpy'");
    warned.receive("  cpu = _conversion_method_template(device=torch.device(\"cpu\"))");
    warned.receive("AURA_NN\tphase\tcollecting");
    assert(warned.error().empty());
    std::string warningFrame;
    for (const auto &line: warned.frame(120, 40, false)) warningFrame += plain(line);
    assert(warningFrame.find("FAILED") == std::string::npos);
    assert(warningFrame.find("training can continue.") != std::string::npos);
    warned.receive("AURA_NN\tdevice\tcpu");
    assert(text(warned, 120, 40).find("training can continue on cpu") != std::string::npos);
    warned.receive("AURA_NN\tdevice\tmps:0");
    const auto mpsWarning = text(warned, 120, 40);
    assert(mpsWarning.find("training can continue on mps:0") != std::string::npos);
    assert(mpsWarning.find("CPU training") == std::string::npos);
    assert(warningFrame.find("[P] pause") != std::string::npos);
    warned.receive("Traceback (most recent call last):");
    warned.receive("  File \"worker.py\", line 123");
    warned.receive(
        "ValueError: Checkpoint action schema is incompatible; choose a current model or create a new model.");
    warned.receive("  ignored source excerpt");
    std::string liveFrame;
    for (const auto &line: warned.frame(120, 40, false)) liveFrame += plain(line);
    assert(liveFrame.find("FAILED") == std::string::npos);
    std::string crashFrame;
    for (const auto &line: warned.frame(70, 30, true, true)) crashFrame += plain(line) + " ";
    assert(crashFrame.find("FAILED") != std::string::npos);
    assert(crashFrame.find("ValueError: Checkpoint action schema") != std::string::npos);
    assert(crashFrame.find("create a new model.") != std::string::npos);
    warned.receive("AURA_NN\terror\tPermissionError: Cannot save the checkpoint in models/");
    assert(warned.error() == "PermissionError: Cannot save the checkpoint in models/");
    std::string explicitFrame;
    for (const auto &line: warned.frame(120, 40, false)) explicitFrame += plain(line);
    assert(explicitFrame.find("FAILED") != std::string::npos);
    assert(explicitFrame.find("PermissionError: Cannot save") != std::string::npos);
    evaluated.receive("AURA_NN\tobjective_contract\tbalance_quality\t3\tbalance_v2\t2\tactor_critic_mc");
    evaluated.receive("AURA_NN\tbalance\tdeterministic\t200\t1.667\t60\t150\t1\t1\t0.8\t208\tfallen");
    evaluated.receive("AURA_NN\tbalance\tstochastic\t180\t1.5\t80\t130\t2\t1\t0.7\t182\tfallen");
    evaluated.receive("AURA_NN\tbalance\tbest\t200\t1.667\t60\t150\t1\t1\t0.8\t208\tfallen");
    evaluated.key("1");
    assert(text(evaluated, 160, 50).find("EVAL BALANCED") != std::string::npos);
    assert(text(evaluated, 160, 50).find("200 balanced steps") != std::string::npos);
    evaluated.key("3");
    assert(text(evaluated, 160, 50).find("Quality 0.800") != std::string::npos);
    assert(text(evaluated, 160, 50).find("Recoveries 2") != std::string::npos);
    assert(text(evaluated, 160, 50).find("MSE pending") != std::string::npos);
    evaluated.receive("AURA_NN\tcritic_update\t0.066894233");
    assert(text(evaluated, 160, 50).find("MSE 0.06689") != std::string::npos);
    evaluated.key("1");
    const auto compactBalance = text(evaluated, 100, 60);
    assert(compactBalance.find("Best balance 200 steps / 1.67 s") != std::string::npos);
    assert(compactBalance.find("Best evaluation 310") == std::string::npos);
    const std::vector<std::string> balanceEvent{
        "AURA_NN", "balance", "stochastic", "180", "1.5", "80", "130", "2", "1", "0.7", "182", "fallen"};
    const auto activity = aura::cli::balance_activity(balanceEvent);
    assert(activity.find("Stochastic balance: Balanced 180 / 1.50 s") != std::string::npos);
    assert(activity.find("Recoveries 2") != std::string::npos);
    assert(activity.find("Quality 0.700") != std::string::npos);
    assert(activity.find("Falls 100%") != std::string::npos);
    assert(aura::cli::balance_activity({"AURA_NN", "critic_update", "0.066894233"}).find("MSE 0.066894") != std::string::npos);
    assert(aura::cli::balance_activity({"AURA_NN", "critic_update", "nan"}).empty());
    assert(aura::cli::balance_activity({"AURA_NN", "critic_update", "0.2garbage"}).empty());
    auto invalidEvent = balanceEvent;
    invalidEvent[9] = "nan";
    assert(aura::cli::balance_activity(invalidEvent).empty());
    assert(aura::cli::balance_activity({"AURA_NN", "balance", "best"}).empty());
    assert(aura::cli::balance_activity({"AURA_NN", "objective_contract", "balance_quality", "3", "balance_v2", "2", "actor_critic_mc"}).find("paired actor–critic") != std::string::npos);
    evaluated.receive("AURA_NN\tschedule\t8\t10\t4");
    evaluated.receive("AURA_NN\trun_progress\t3\t10\t4");
    evaluated.receive("AURA_NN\tarena_agent\t1\t320\tBALANCED\t0.8\t75.2\t0\t2");
    evaluated.receive("AURA_NN\tarena_agent\t2\t300\tFALLEN\t0\t61.2\t1\t2");
    evaluated.key("1");
    const auto arenaFrame = text(evaluated, 180, 60);
    assert(arenaFrame.find("Update 3/10") != std::string::npos);
    assert(arenaFrame.find("4-agent arena / collisions on") != std::string::npos);
    const auto arenaLines = evaluated.frame(180, 60, false);
    assert(arenaLines[4].find("Update 3/10") != std::string::npos);
    assert(arenaLines[4].find("Overview") == std::string::npos);
    assert(arenaLines[6].find("Overview") != std::string::npos);
    assert(arenaFrame.find("AURA 1 / BALANCED") != std::string::npos);
    assert(arenaFrame.find("AURA 2 / FALLEN") != std::string::npos);
    assert(arenaFrame.find("contact pairs 2") != std::string::npos);
    evaluated.receive("AURA_NN\tphase\tcollecting");
    evaluated.receive("AURA_NN\tarena_spacing\t12");
    std::string targetsA, targetsB;
    for (std::size_t i = 0; i < aura::sim::actionSpecs.size() - 1; ++i) {
        targetsA += "\t0.123"; targetsB += "\t0.456";
    }
    evaluated.receive("AURA_NN\tarena_agent\t1\t320\tBALANCED\t0.8\t75.2\t0\t0");
    evaluated.receive("AURA_NN\tarena_agent\t2\t300\tFALLEN\t0\t61.2\t1\t0");
    evaluated.receive("AURA_NN\tagent_detail\t1\t1\t4\t0.9\t0.8\t120\t1\t199\t100\t2\t0\t0.8\t75.2\tnone" + targetsA);
    evaluated.receive("AURA_NN\tagent_detail\t2\t2\t1\t0.1\t0\t60\t0.5\t239\t60\t1\t1\t0.3\t61.2\tfallen" + targetsB);
    evaluated.key("t");
    auto firstAgent = text(evaluated, 180, 60);
    assert(firstAgent.find("AURA 1 sampled targets") != std::string::npos);
    assert(firstAgent.find("+0.123") != std::string::npos);
    assert(firstAgent.find("12 m spacing") != std::string::npos);
    evaluated.key("\033[C");
    auto secondAgent = text(evaluated, 180, 60);
    assert(secondAgent.find("AURA 2 sampled targets") != std::string::npos);
    assert(secondAgent.find("+0.456") != std::string::npos);
    const std::string resultA = "AURA_NN\tagent_result\t1\t1\t120\t1\t199\t100\t2\t0\t0.8\t75.2\tnone";
    evaluated.receive(resultA);
    evaluated.receive("AURA_NN\tagent_result\t2\t2\t60\t0.5\t239\t60\t1\t1\t0.3\t61.2\tfallen");
    evaluated.key("3");
    const auto comparison = text(evaluated, 180, 60);
    assert(comparison.find("AGENT COMPARISON / CURRENT BATCH") != std::string::npos);
    assert(comparison.find("68.20") != std::string::npos);
    assert(comparison.find("50% falls") != std::string::npos);
    assert(comparison.find("Selected AURA 2 / episode 2") != std::string::npos);
    assert(comparison.find("BALANCED TIME / ALL AGENTS") != std::string::npos);
    if (argc == 3 && std::string(argv[1]) == "--preview") {
        std::ofstream arena(std::string(argv[2]) + "-arena.ansi");
        for (const auto &line : evaluated.frame(160, 45, false)) arena << line << '\n';
        evaluated.key("1");
        std::ofstream joints(std::string(argv[2]) + "-arena-targets.ansi");
        for (const auto &line : evaluated.frame(160, 55, false)) joints << line << '\n';
        evaluated.key("3");
    }
    evaluated.receive(resultA);
    assert(text(evaluated, 180, 60) == comparison);
    evaluated.key("\033[D");
    assert(text(evaluated, 180, 60).find("Selected AURA 1 / episode 1") != std::string::npos);
    for (int width : {60, 100, 160, 180}) for (int height : {25, 40, 60})
        for (const auto &line : evaluated.frame(width, height, false)) assert(cells(line) <= width);
    aura::cli::NNTrainingScreen longEpisodes;
    fixture(longEpisodes);
    longEpisodes.receive("AURA_NN\tstart\t128\t1200\t42\t19424");
    for (int ep = 1; ep <= 128; ++ep)
        longEpisodes.receive("AURA_NN\tobjective\t" + std::to_string(ep) + "\t300\t0.1\t-12\t" + std::to_string(1000 + ep));
    longEpisodes.key("3"); longEpisodes.key("r");
    const auto firstEpisodes = text(longEpisodes, 160, 60);
    const int formulaRow = rowOf(firstEpisodes, "POLICY GRADIENT / REWARD-TO-GO");
    assert(firstEpisodes.find("1001.00") != std::string::npos);
    assert(firstEpisodes.find("1128.00") == std::string::npos);
    assert(firstEpisodes.find("of 128 / focused") != std::string::npos);
    longEpisodes.key("\033[F");
    const auto lastEpisodes = text(longEpisodes, 160, 60);
    assert(lastEpisodes.find("1128.00") != std::string::npos);
    assert(lastEpisodes.find("1001.00") == std::string::npos);
    assert(rowOf(lastEpisodes, "POLICY GRADIENT / REWARD-TO-GO") == formulaRow);
    longEpisodes.key("\033[H");
    assert(text(longEpisodes, 160, 60) == firstEpisodes);
    longEpisodes.key("\033[B");
    assert(text(longEpisodes, 160, 60).find("1001.00") == std::string::npos);
    longEpisodes.key("\033[A");
    assert(text(longEpisodes, 160, 60) == firstEpisodes);
    longEpisodes.key("\033[6~");
    const auto nextEpisodePage = text(longEpisodes, 160, 60);
    // Page size follows the episode pane's current viewport instead of a fixed
    // number of rows, so assert movement without pinning one terminal layout.
    assert(nextEpisodePage.find("1001.00") == std::string::npos);
    assert(nextEpisodePage.find("1128.00") == std::string::npos);
    longEpisodes.key("\033[5~");
    assert(text(longEpisodes, 160, 60) == firstEpisodes);
    longEpisodes.wheel(1, 10, 16);
    const auto wheelEpisodes = text(longEpisodes, 160, 60);
    assert(wheelEpisodes.find("1001.00") == std::string::npos);
    assert(rowOf(wheelEpisodes, "POLICY GRADIENT / REWARD-TO-GO") == formulaRow);
    longEpisodes.key("\033[H");
    longEpisodes.wheel(1, 150, 16); // Outside the episode pane: scroll the page.
    assert(rowOf(text(longEpisodes, 160, 40), "POLICY GRADIENT / REWARD-TO-GO") < formulaRow);
    longEpisodes.key("\t"); // Refocus the pane and bring it back into view.
    assert(rowOf(text(longEpisodes, 160, 60), "POLICY GRADIENT / REWARD-TO-GO") == formulaRow);
    for (int width : {60, 100, 160}) for (int height : {20, 40, 60})
        for (const auto &line : longEpisodes.frame(width, height, false)) assert(cells(line) <= width);
    aura::cli::NNTrainingScreen workers;
    fixture(workers);
    workers.receive("AURA_NN\tcollector\tindependent");
    workers.receive("AURA_NN\tschedule\t32\t3\t6");
    for(int worker=1;worker<=6;++worker)
        workers.receive("AURA_NN\tworker_history\t"+std::to_string(worker)+"\t1\t400\t0.5");
    auto workerFrame=text(workers,160,60);
    assert(workerFrame.find("6 independent workers")!=std::string::npos);
    assert(workerFrame.find("episodes per worker")!=std::string::npos);
    assert(workerFrame.find("Worker 1")!=std::string::npos);
    for(int i=0;i<4;++i)workers.key("\033[C");
    assert(text(workers,160,60).find("Worker 5")!=std::string::npos);
    for (int width : {60,100,160}) for (int height : {20,40,60})
        for(const auto &line:workers.frame(width,height,false)) assert(cells(line)<=width);
    {
        using namespace aura::cli::screen;
        std::vector<std::vector<double>> history(4,std::vector<double>(84,.5));
        history[0][0]=9000; // Outside the visible window; must not flatten today's chart.
        history[1].resize(82);
        Canvas preview(104,19);const Palette palette;
        worker_chart(preview,0,0,104,19,"BALANCE",history,1,false,palette);
        std::string rendered;
        for(const auto &line:preview.lines())rendered+=plain(line)+'\n';
        assert(rendered.find("9000")==std::string::npos);
        assert(rendered.find("25")!=std::string::npos && rendered.find("84")!=std::string::npos);
        assert(rendered.find("Worker 1")!=std::string::npos && rendered.find("Worker 4")!=std::string::npos);
        assert(rendered.find("aggregate mean")==std::string::npos);
        Canvas survival(104,19);
        worker_chart(survival,0,0,104,19,"SURVIVAL",{{407,426,400}},0,true,palette,ChartKind::training);
        rendered.clear();for(const auto &line:survival.lines())rendered+=plain(line)+'\n';
        assert(rendered.find("426.000")==std::string::npos);
        assert(rendered.find("Survival steps")!=std::string::npos);
        for(const auto &sample:std::vector<std::vector<double>>{{0.,0.},{.5},{std::numeric_limits<double>::quiet_NaN()}}) {
            Canvas small(60,11);
            worker_chart(small,0,0,60,11,"BALANCE",{sample},0,true,palette);
            for(const auto &line:small.lines())assert(cells(line)<=60);
        }
        for (int height=11;height<=40;++height) {
            Canvas duration(80,height);
            chart(duration,0,0,80,height,"BALANCE",{0.,1.09,.65},{},palette.purple,palette,ChartKind::evaluationBalanced,0);
            assert(plain(duration.lines()[height-5]).find("0.000")!=std::string::npos);
        }
        Canvas largeLoss(80,19);
        chart(largeLoss,0,0,80,19,"LOSS",{-1e8,-2e8},{},palette.purple,palette,ChartKind::loss,0);
        assert(plain(largeLoss.lines()[14]).find("e+08")!=std::string::npos);
    }
    aura::cli::NNTrainingScreen device;
    fixture(device);
    device.receive("AURA_NN\trun_progress\t19\t500\t4");
    device.receive("AURA_NN\trun_timing\t18\t500\t180\t4820\t7");
    assert(text(device, 160).find("3.6%") != std::string::npos);
    assert(text(device, 160).find("ETA 1h 20m") != std::string::npos);
    assert(text(device, 160).find("elapsed 3m 0s") != std::string::npos);
    assert(device.key("D") == "device_info 1");
    device.receive("AURA_NN\tdevice_metric\tbackend\tApple Metal / MPS");
    device.receive("AURA_NN\tdevice_metric\tutilization\t37% / system-wide, includes other apps");
    device.receive("AURA_NN\tdevice_metric\tallocated\t2.0 MiB / live PyTorch tensors");
    const auto gpuPanel = text(device, 160);
    assert(gpuPanel.find("DEVICE / GPU ACTIVITY") != std::string::npos);
    assert(gpuPanel.find("37% / system-wide") != std::string::npos);
    assert(gpuPanel.find("live PyTorch tensors") != std::string::npos);
    if (argc == 3 && std::string(argv[1]) == "--preview") {
        std::ofstream preview(std::string(argv[2]) + "-device.txt");
        for (const auto &line: device.frame(160, 40, false)) preview << plain(line) << '\n';
    }
    for (int width : {60, 100, 160}) for (int height : {20, 40, 60})
        for (const auto &line : device.frame(width, height, false)) assert(cells(line) <= width);
    assert(device.key("D") == "device_info 0");
    device.receive("AURA_NN\trun_timing\t500\t500\t5000\t0\t8");
    assert(text(device, 160).find("100.0%") != std::string::npos);
    device.receive("AURA_NN\trun_timing\t600\t500\t5000\t0\t8");
    assert(text(device, 160).find("100.0%") != std::string::npos);
    assert(device.key("d") == "device_info 1");
    assert(device.key("2") == "device_info 0");
    std::cout <<
            "Screen checks: Unicode cells, compact sizes, scroll, stale selection, batch state and malformed values passed.\n";
}
