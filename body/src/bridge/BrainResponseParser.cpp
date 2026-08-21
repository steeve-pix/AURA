#include "bridge/BrainResponseParser.hpp"

#include "bridge/ActionParser.hpp"
#include <nlohmann/json.hpp>

namespace aura::bridge
{
    BrainResponse parseBrainResponse(std::string_view text)
    {
        const auto json =
            nlohmann::json::parse(text);

        BrainResponse response{};

        // Action parsing remains authoritative; debug fields are optional diagnostics.
        response.action = parseAction(text);

        if (json.contains("debug"))
        {
            const auto& debug = json.at("debug");

            // Missing debug members intentionally retain their empty default values.
            response.debug.goal = debug.value("goal", std::string{});

            if (debug.contains("failures"))
            {
                const auto& failures = debug.at("failures");
                response.debug.planFailures = failures.value("plan_failures", 0);
                response.debug.replans = failures.value("replans", 0);
                response.debug.failedTargets = failures.value("failed_targets", 0);
                response.debug.bodyActionFailures = failures.value("body_action_failures", 0);
            }

            if (debug.contains("known_cells"))
            {
                for (const auto& position : debug.at("known_cells"))
                {
                    response.debug.knownCells.push_back({
                        position.at(0).get<int>(),
                        position.at(1).get<int>()
                    });
                }
            }

            if (debug.contains("goal_scores"))
            {
                for (auto it = debug.at("goal_scores").begin(); it != debug.at("goal_scores").end(); ++it)
                {
                    response.debug.goalScores[it.key()] = it.value().get<double>();
                }
            }

            if (debug.contains("visited_cells"))
            {
                for (const auto& position : debug.at("visited_cells"))
                {
                    response.debug.visitedCells.push_back({
                        position.at(0).get<int>(),
                        position.at(1).get<int>()
                    });
                }
            }

            if (debug.contains("plan") && !debug.at("plan").is_null())
            {
                const auto& plan = debug.at("plan");
                response.debug.planGoal = plan.value("goal", std::string{});
                response.debug.planCurrentStep = plan.value("current_step", 0);
                response.debug.planStepCount = plan.value("step_count", 0);
                response.debug.planFailed = plan.value("failed", false);

                if (plan.contains("step") && !plan.at("step").is_null())
                {
                    const auto& step = plan.at("step");
                    response.debug.planStepType = step.value("type", std::string{});

                    if (step.contains("target") && !step.at("target").is_null())
                    {
                        const auto& target = step.at("target");
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
