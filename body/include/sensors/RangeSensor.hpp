#pragma once

#include "agent/Agent.hpp"
#include "sensors/RangeObservation.hpp"
#include "world/World.hpp"

namespace aura::sensors {
    /// Scans a square area centered on AURA and evaluates routes to visible objects.
    class RangeSensor {
    public:
        /// Creates a sensor whose square extends `radius` cells in every direction.
        explicit RangeSensor(int radius);

        /// Captures all in-bounds cells and actionable objects inside the sensor square.
        [[nodiscard]] RangeObservation observe(const world::World &world, const agent::Agent &agent) const;

        /// Returns the configured sensor radius in grid cells.
        [[nodiscard]] int radius() const;

    private:
        int radius_;
    };
}
