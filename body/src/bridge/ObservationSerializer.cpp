#include "bridge/ObservationSerializer.hpp"

#include "world/CellTypeUtils.hpp"

#include <nlohmann/json.hpp>

namespace aura::bridge {
    std::string serializedObservation(const Observation &observation) {
        nlohmann::json json;

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
                {"succeeded", lastAction.succeeded}
            };

            if (lastAction.target.has_value()) {
                const auto &target = lastAction.target.value();
                json["last_action"]["target"] = {target.x, target.y};
            }
        } else {
            json["last_action"] = nullptr;
        }

        for (const auto &cell: observation.nearby.cells) {
            json["visible_cells"].push_back({
                {"type", aura::world::toString(cell.type)},
                {"position", {cell.position.x, cell.position.y}}
            });
        }

        for (const auto &object: observation.nearby.objects) {
            json["nearby_objects"].push_back({
                {"type", aura::world::toString(object.type)},
                {"position", {object.position.x, object.position.y}},
                {"reachable", object.reachable},
                {"path_length", object.pathLength}
            });
        }

        return json.dump();
    }
}
