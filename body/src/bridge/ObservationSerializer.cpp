#include "bridge/ObservationSerializer.hpp"

#include "world/CellTypeUtils.hpp"

#include <nlohmann/json.hpp>

namespace aura::bridge {
    std::string serializedObservation(const Observation &observation) {
        nlohmann::json json;

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
        json["nearby_objects"] = nlohmann::json::array();

        for (const auto &object: observation.nearby.objects) {
            json["nearby_objects"].push_back({
                {"type", aura::world::toString(object.type)},
                {"position", {object.position.x, object.position.y}}
            });
        }

        return json.dump();
    }
}
