#pragma once

#include <string>
#include <vector>

#include "bridge/BrainResponse.hpp"
#include "simulation/CounterfactualSimulation.hpp"

namespace aura::bridge {
    struct CounterfactualEvaluation {
        std::string choice;
        simulation::CounterfactualResult result;
    };

    /// Serializes isolated action outcomes for the Python experiment controller.
    [[nodiscard]] std::string serializedCounterfactualResponse(
        const std::vector<CounterfactualEvaluation> &evaluations
    );
}
