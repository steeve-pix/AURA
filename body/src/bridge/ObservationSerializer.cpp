#include "bridge/ObservationSerializer.hpp"

#include <sstream>

#include "world/CellTypeUtils.hpp"

namespace aura::bridge {
    std::string serializedObservation(const Observation &observation) {
        std::ostringstream json;
        // The compact single-line form matches the bridge's newline-delimited messages.
        json << "{";

        json << "\"position\":["
                << observation.position.x << ","
                << observation.position.y << "],";

        json << "\"energy\":"
                << observation.energy << ",";

        json << "\"north\":\""
                << aura::world::toString(observation.surroundings.north)
                << "\",";

        json << "\"east\":\""
                << aura::world::toString(observation.surroundings.east)
                << "\",";

        json << "\"south\":\""
                << aura::world::toString(observation.surroundings.south)
                << "\",";

        json << "\"west\":\""
                << aura::world::toString(observation.surroundings.west)
                << "\",";

        json << "\"nearby_objects\":[";

        for (std::size_t i = 0; i < observation.nearby.objects.size(); ++i) {
            const auto &object =
                    observation.nearby.objects[i];

            json << "{"
                    << "\"type\":\""
                    << aura::world::toString(object.type)
                    << "\","
                    << "\"position\":["
                    << object.position.x << ","
                    << object.position.y
                    << "]"
                    << "}";

            if (i + 1 < observation.nearby.objects.size()) {
                json << ",";
            }
        }

        json << "]";

        json << "}";

        return json.str();
    }
}
