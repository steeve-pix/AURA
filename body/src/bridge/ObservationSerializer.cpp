#include "bridge/ObservationSerializer.hpp"

#include <sstream>

#include "world/CellTypeUtils.hpp"

namespace aura::bridge {
    std::string serializedObservation(const Observation &observation) {
        std::ostringstream json;
        // The compact single-line form matches the bridge's newline-delimited messages.
        json << "{"
                << "\"position\":["
                << observation.position.x << ","
                << observation.position.y << "],"
                << "\"energy\":" << observation.energy << ","
                << "\"north\":\""
                << aura::world::toString(observation.surroundings.north) << "\","
                << "\"east\":\""
                << aura::world::toString(observation.surroundings.east) << "\","
                << "\"south\":\""
                << aura::world::toString(observation.surroundings.south) << "\","
                << "\"west\":\""
                << aura::world::toString(observation.surroundings.west) << "\""
                << "}";

        return json.str();
    }
}
