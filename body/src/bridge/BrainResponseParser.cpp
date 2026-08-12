#include "bridge/BrainResponseParser.hpp"

#include "bridge/ActionParser.hpp"
#include <nlohmann/json.hpp>

namespace aura::bridge {
    BrainResponse parseBrainResponse(std::string_view text) {
        const auto json =
                nlohmann::json::parse(text);

        BrainResponse response{};

        response.action = parseAction(text);

        if (json.contains("debug")) {
            const auto &debug = json.at("debug");

            response.debug.goal = debug.value("goal", std::string{});

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
        }

        return response;
    }
}
