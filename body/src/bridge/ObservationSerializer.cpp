#include "bridge/ObservationSerializer.hpp"

#include "world/CellTypeUtils.hpp"

#include <nlohmann/json.hpp>

namespace aura::bridge {
    std::string serializedObservation(const Observation &observation) {
        nlohmann::json json;

        // Protocol names are centralized here so enum spelling never leaks into JSON.
        const auto actionTypeName =
                [](ActionType type) {
            switch (type) {
                case ActionType::Idle:
                    return "idle";
                case ActionType::Move:
                    return "move";
                case ActionType::MoveTo:
                    return "move_to";
                case ActionType::Investigate:
                    return "investigate";
            }

            return "unknown";
        };

        json["world_id"] = observation.worldId;

        json["position"] = {
            observation.position.x,
            observation.position.y
        };

        json["sensor_radius"] = observation.sensor_radius;

        json["energy"] = observation.energy;
        json["north"] = aura::world::toString(observation.surroundings.north);
        json["east"] = aura::world::toString(observation.surroundings.east);
        json["south"] = aura::world::toString(observation.surroundings.south);
        json["west"] = aura::world::toString(observation.surroundings.west);
        json["visible_cells"] = nlohmann::json::array();
        json["nearby_objects"] = nlohmann::json::array();

        if (observation.lastAction.has_value()) {
            const auto &lastAction = observation.lastAction.value();

            json["last_action"] = {
                {"type", actionTypeName(lastAction.type)},
                {"succeeded", lastAction.succeeded},
                {"result", lastAction.result}
            };

            if (lastAction.target.has_value()) {
                const auto &target = lastAction.target.value();
                json["last_action"]["target"] = {target.x, target.y};
            }

            if (lastAction.pathLengthBefore.has_value()) {
                json["last_action"]["path_length_before"] =
                        lastAction.pathLengthBefore.value();
            }

            if (lastAction.pathLengthAfter.has_value()) {
                json["last_action"]["path_length_after"] =
                        lastAction.pathLengthAfter.value();
            }

            if (lastAction.nextStepBefore.has_value()) {
                const auto &nextStep = lastAction.nextStepBefore.value();
                json["last_action"]["next_step_before"] = {
                    nextStep.x, nextStep.y
                };
            }

            if (lastAction.nextStepAfter.has_value()) {
                const auto &nextStep = lastAction.nextStepAfter.value();
                json["last_action"]["next_step_after"] = {
                    nextStep.x, nextStep.y
                };
            }

            if (lastAction.reachableBefore.has_value()) {
                json["last_action"]["reachable_before"] = lastAction.reachableBefore.value();
            }
        } else {
            // An explicit null distinguishes the first cycle from an omitted field.
            json["last_action"] = nullptr;
        }

        for (const auto &cell: observation.nearby.cells) {
            json["visible_cells"].push_back({
                {"type", aura::world::toString(cell.type)},
                {"position", {cell.position.x, cell.position.y}}
            });
        }

        for (const auto &object: observation.nearby.objects) {
            nlohmann::json objectJson = {
                {"type", aura::world::toString(object.type)},
                {"position", {object.position.x, object.position.y}},
                {"reachable", object.reachable},
                {"path_length", object.pathLength},
                {"next_step", nullptr}
            };

            if (object.nextStep.has_value()) {
                const auto &nextStep = object.nextStep.value();

                objectJson["next_step"] = {
                    nextStep.x, nextStep.y
                };
            }

            json["nearby_objects"].push_back(objectJson);
        }

        return json.dump();
    }
}
