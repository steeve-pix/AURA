#include "sensors/LocalSensor.hpp"

namespace aura::sensors {
    LocalObservation LocalSensor::observe(const world::World &world,
                                          const agent::Agent &agent) {
        const auto position = agent.position();

        // Cardinal order matches LocalObservation and the JSON protocol fields.
        // Boundary walls keep these adjacent lookups inside the world for a valid agent.
        LocalObservation observation{
            world.cellAt({position.x, position.y - 1}),
            world.cellAt({position.x + 1, position.y}),
            world.cellAt({position.x, position.y + 1}),
            world.cellAt({position.x - 1, position.y})
        };

        return observation;
    }
}
