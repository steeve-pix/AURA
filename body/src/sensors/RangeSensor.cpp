#include "sensors/RangeSensor.hpp"
#include "world/CellType.hpp"
#include "world/Position.hpp"

namespace aura::sensors {
    RangeSensor::RangeSensor(int radius) : radius_(radius) {
    }

    RangeObservation RangeSensor::observe(const world::World &world, const agent::Agent &agent) const {
        RangeObservation observation;

        const auto position = agent.position();

        for (int y = position.y - radius_; y <= position.y + radius_; ++y) {
            for (int x = position.x - radius_; x <= position.x + radius_; ++x) {
                const world::Position scannedPosition{x, y};

                if (!world.isInside(scannedPosition)) {
                    continue;
                }

                const auto type =
                        world.cellAt(scannedPosition);

                observation.cells.push_back({type, scannedPosition});

                if (type == world::CellType::Battery) {
                    observation.objects.push_back({type, scannedPosition});
                }
            }
        }

        return observation;
    }
}
