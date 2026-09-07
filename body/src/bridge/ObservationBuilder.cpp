#include "bridge/ObservationBuilder.hpp"

#include "sensors/LocalSensor.hpp"

namespace aura::bridge {
    Observation buildObservation(const world::World &world, const agent::Agent &agent,
                                 const sensors::RangeSensor &rangeSensor,
                                 const std::optional<LastAction> &lastAction,
                                 const std::string &worldId) {
        const auto local =
                sensors::LocalSensor::observe(world, agent);

        const auto nearby =
                rangeSensor.observe(world, agent);

        return {
            .position = agent.position(),
            .energy = agent.energy(),
            .surroundings = local,
            .nearby = nearby,
            .sensor_radius = rangeSensor.radius(),
            .lastAction = lastAction,
            .worldId = worldId
        };
    }
}
