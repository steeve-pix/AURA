#include "bridge/ObservationSerializer.hpp"

#include <sstream>

#include "world/CellTypeUtils.hpp"

namespace aura::bridge {
    std::string serializedObservation(const Observation &observation) {
        std::ostringstream json;
        json << "{\n"
                << "  \"position\": [" << observation.position.x << ", "
                << observation.position.y << "],\n"
                << "  \"energy\": " << observation.energy << ",\n"
                << R"(  "north": ")" << world::toString(observation.surroundings.north) << "\",\n"
                << R"(  "east": ")" << world::toString(observation.surroundings.east) << "\",\n"
                << R"(  "south": ")" << world::toString(observation.surroundings.south) << "\",\n"
                << R"(  "west": ")" << world::toString(observation.surroundings.west) << "\"\n"
                << '}';

        return json.str();
    }
}
