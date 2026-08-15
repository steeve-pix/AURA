#include "sensors/RangeSensor.hpp"

#include "navigation/Pathfinder.hpp"
#include "world/CellType.hpp"
#include "world/Position.hpp"

namespace aura::sensors {
    RangeSensor::RangeSensor(int radius) : radius_(radius) {
    }

    RangeObservation RangeSensor::observe(const world::World &world, const agent::Agent &agent) const {
        RangeObservation observation;

        const auto position = agent.position();

        // Radius describes a square footprint rather than Euclidean distance.
        for (int y = position.y - radius_; y <= position.y + radius_; ++y) {
            for (int x = position.x - radius_; x <= position.x + radius_; ++x) {
                const world::Position scannedPosition{x, y};

                if (!world.isInside(scannedPosition)) {
                    continue;
                }

                const auto type =
                        world.cellAt(scannedPosition);

                observation.cells.push_back({type, scannedPosition});

                if (type == world::CellType::Battery || type == world::CellType::Unknown) {
                    // Object metadata reflects physical reachability from the current frame,
                    // not whether the object merely falls inside sensor range.
                    const auto path =
                            navigation::findPath(world, agent.position(), scannedPosition);

                    const bool reachable =
                            scannedPosition == agent.position() || !path.empty();

                    const int pathLength =
                            reachable ? static_cast<int>(path.size()) : -1;
                    observation.objects.push_back({type, scannedPosition, reachable, pathLength});
                }
            }
        }

        return observation;
    }

    int RangeSensor::radius() const {
        return radius_;
    }
}
