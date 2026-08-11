#pragma once

#include "agent/Agent.hpp"
#include "sensors/RangeObservation.hpp"
#include "world/World.hpp"

namespace aura::sensors {
    class RangeSensor {
    public:
        explicit RangeSensor(int radius);

        [[nodiscard]] RangeObservation observe(const world::World &world, const agent::Agent &agent) const;

        [[nodiscard]] int radius() const;
    private:
        int radius_;
    };
}