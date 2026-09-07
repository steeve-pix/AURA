#pragma once

#include <string>
#include <vector>

#include "Observation.hpp"
#include "simulation/CounterfactualSimulation.hpp"
#include <optional>
#include <unordered_map>

#include "sensors/RangeSensor.hpp"

namespace aura::bridge {
    struct CounterfactualEvaluation {
        std::string choice;
        simulation::CounterfactualResult result;
        std::optional<Observation> observationAfter = std::nullopt;
    };

    class CounterfactualBranchStore {
    public:
        simulation::PhysicalSimulationBranch &branchFor(const std::string &choice, const world::World &realWorld,
                                                        const agent::Agent &realAgent);

    private:
        std::unordered_map<std::string, simulation::PhysicalSimulationBranch> branches_;
    };

    /// Serializes isolated action outcomes for the Python experiment controller.
    [[nodiscard]] std::string serializedCounterfactualResponse(
        const std::vector<CounterfactualEvaluation> &evaluations
    );

    [[nodiscard]] CounterfactualEvaluation evaluateBranchAction(
        const std::string &choice,
        simulation::PhysicalSimulationBranch &branch,
        const Action &action, const sensors::RangeSensor &rangeSensor,
        const std::string &worldId, world::CellType investigationOutcome = world::CellType::Empty);
}
