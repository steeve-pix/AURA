#include "bridge/Counterfactual.hpp"

#include <nlohmann/json.hpp>

#include "bridge/ObservationBuilder.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "world/CellTypeUtils.hpp"

namespace aura::bridge {
    simulation::PhysicalSimulationBranch &CounterfactualBranchStore::branchFor(const std::string &choice,
                                                                               const world::World &realWorld,
                                                                               const agent::Agent &realAgent) {
        auto [entry, inserted] = branches_.try_emplace(
            choice,
            simulation::createPhysicalSimulationBranch(
                realWorld,
                realAgent
            ));

        (void) inserted;
        return entry->second;
    }

    std::string serializedCounterfactualResponse(
        const std::vector<CounterfactualEvaluation> &evaluations
    ) {
        nlohmann::json json = {
            {"type", "counterfactual_response"},
            {"results", nlohmann::json::array()}
        };

        for (const auto &evaluation: evaluations) {
            const auto &result = evaluation.result;
            nlohmann::json resultJson = {
                {"choice", evaluation.choice},
                {"succeeded", result.succeeded},
                {"result", result.result},
                {
                    "position_after", {
                        result.positionAfter.x,
                        result.positionAfter.y
                    }
                },
                {"energy_after", result.energyAfter},
                {"path_length_before", nullptr},
                {"path_length_after", nullptr},
                {"outcome", nullptr}
            };

            if (result.pathLengthBefore.has_value()) {
                resultJson["path_length_before"] =
                        result.pathLengthBefore.value();
            }

            if (result.pathLengthAfter.has_value()) {
                resultJson["path_length_after"] =
                        result.pathLengthAfter.value();
            }

            if (result.outcome.has_value()) {
                resultJson["outcome"] = std::string(
                    world::toString(result.outcome.value())
                );
            }

            if (evaluation.observationAfter.has_value()) {
                resultJson["observation_after"] = nlohmann::json::parse(
                    serializedObservation(evaluation.observationAfter.value())
                );
            }

            json["results"].push_back(resultJson);
        }

        return json.dump();
    }

    CounterfactualEvaluation evaluateBranchAction(const std::string &choice,
                                                  simulation::PhysicalSimulationBranch &branch, const Action &action,
                                                  const sensors::RangeSensor &rangeSensor,
                                                  const std::string &worldId, world::CellType investigationOutcome) {
        const auto result =
                simulation::simulateBranchAction(branch, action, investigationOutcome);

        std::optional<world::Position> target;

        if (action.type == ActionType::MoveTo || action.type == ActionType::Investigate) {
            target = action.target;
        }

        const LastAction lastAction{
            .type = action.type,
            .target = target,
            .succeeded = result.succeeded,
            .result = result.result,
            .pathLengthBefore = result.pathLengthBefore,
            .pathLengthAfter = result.pathLengthAfter,
            .reachableBefore = action.type == ActionType::MoveTo
                                   ? std::optional{result.result != "unreachable"}
                                   : std::nullopt
        };

        const auto observationAfter =
                buildObservation(branch.world, branch.agent, rangeSensor, lastAction, worldId);

        return {
            .choice = choice,
            .result = result,
            .observationAfter = observationAfter
        };
    }
}
