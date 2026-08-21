#include "sensors/LocalSensor.hpp"

namespace aura::sensors
{
    LocalObservation LocalSensor::observe(const world::World& world,
                                          const agent::Agent& agent)
    {
        const auto position = agent.position();

        // Field order is part of the observation protocol. Generated worlds keep valid
        // agents behind boundary walls, so each adjacent lookup remains in bounds.
        LocalObservation observation{
            world.cellAt({position.x, position.y - 1}),
            world.cellAt({position.x + 1, position.y}),
            world.cellAt({position.x, position.y + 1}),
            world.cellAt({position.x - 1, position.y})
        };

        return observation;
    }
}
