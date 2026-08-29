#include "bridge/Counterfactual.hpp"

#include <nlohmann/json.hpp>

#include "world/CellTypeUtils.hpp"

namespace aura::bridge {
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
                {"position_after", {
                    result.positionAfter.x,
                    result.positionAfter.y
                }},
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

            json["results"].push_back(resultJson);
        }

        return json.dump();
    }
}
