#include "bridge/BrainResponseParser.hpp"

#include "bridge/ActionParser.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace aura::bridge {
    BrainResponse parseBrainResponse(std::string_view text) {
        const auto json =
                nlohmann::json::parse(text);

        BrainResponse response{};

        if (json.value("type", std::string{}) == "preview_request") {
            response.type = BrainResponseType::PreviewRequest;
            std::set<int> candidateIds;

            for (const auto &candidate: json.at("candidates")) {
                if (candidate.at("action").get<std::string>() != "move_to") {
                    throw std::invalid_argument(
                        "Navigation previews support only move_to candidates"
                    );
                }

                const int id = candidate.at("id").get<int>();

                if (id <= 0 || !candidateIds.insert(id).second) {
                    throw std::invalid_argument(
                        "Navigation preview candidate IDs must be unique and positive"
                    );
                }

                const auto &target = candidate.at("target");
                response.previewCandidates.push_back({
                    id,
                    {
                        target.at(0).get<int>(),
                        target.at(1).get<int>()
                    }
                });
            }

            if (response.previewCandidates.empty()) {
                throw std::invalid_argument(
                    "Navigation preview requests need at least one candidate"
                );
            }

            return response;
        }

        if (json.value("type", std::string{}) == "counterfactual_request") {
            response.type = BrainResponseType::CounterfactualRequest;
            std::set<std::string> choices;

            for (const auto &candidate: json.at("candidates")) {
                const auto choice = candidate.at("choice").get<std::string>();

                if (choice.empty() || !choices.insert(choice).second) {
                    throw std::invalid_argument(
                        "Counterfactual choices must be unique and non-empty"
                    );
                }

                response.counterfactualCandidates.push_back({
                    choice,
                    parseAction(candidate.at("decision").dump())
                });
            }

            if (response.counterfactualCandidates.size() != 2) {
                throw std::invalid_argument(
                    "Counterfactual requests require exactly two candidates"
                );
            }

            return response;
        }

        // Action parsing remains authoritative; debug fields are optional diagnostics.
        response.action = parseAction(text);

        if (json.contains("debug")) {
            const auto &debug = json.at("debug");

            // Missing debug members intentionally retain their empty default values.
            response.debug.goal = debug.value("goal", std::string{});

            if (debug.contains("failures")) {
                const auto &failures = debug.at("failures");
                response.debug.planFailures = failures.value("plan_failures", 0);
                response.debug.replans = failures.value("replans", 0);
                response.debug.failedTargets = failures.value("failed_targets", 0);
                response.debug.bodyActionFailures = failures.value("body_action_failures", 0);
            }

            if (debug.contains("known_cells")) {
                for (const auto &position: debug.at("known_cells")) {
                    response.debug.knownCells.push_back({
                        position.at(0).get<int>(),
                        position.at(1).get<int>()
                    });
                }
            }

            if (debug.contains("goal_scores")) {
                for (auto it = debug.at("goal_scores").begin(); it != debug.at("goal_scores").end(); ++it) {
                    response.debug.goalScores[it.key()] = it.value().get<double>();
                }
            }

            if (debug.contains("visited_cells")) {
                for (const auto &position: debug.at("visited_cells")) {
                    response.debug.visitedCells.push_back({
                        position.at(0).get<int>(),
                        position.at(1).get<int>()
                    });
                }
            }

            if (debug.contains("plan") && !debug.at("plan").is_null()) {
                const auto &plan = debug.at("plan");
                response.debug.planGoal = plan.value("goal", std::string{});
                response.debug.planCurrentStep = plan.value("current_step", 0);
                response.debug.planStepCount = plan.value("step_count", 0);
                response.debug.planFailed = plan.value("failed", false);

                if (plan.contains("step") && !plan.at("step").is_null()) {
                    const auto &step = plan.at("step");
                    response.debug.planStepType = step.value("type", std::string{});

                    if (step.contains("target") && !step.at("target").is_null()) {
                        const auto &target = step.at("target");
                        response.debug.planStepTarget = {
                            target.at(0).get<int>(),
                            target.at(1).get<int>()
                        };
                        response.debug.hasPlanStepTarget = true;
                    }
                }
            }
        }

        return response;
    }
}
