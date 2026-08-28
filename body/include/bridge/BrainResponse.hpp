#pragma once
#include <string>
#include <vector>

#include "Action.hpp"
#include "BrainDebugState.hpp"

namespace aura::bridge {
    enum class BrainResponseType {
        Action,
        PreviewRequest,
        CounterfactualRequest
    };

    struct PreviewCandidate {
        int id;
        world::Position target;
    };

    struct CounterfactualCandidate {
        std::string choice;
        Action action;
    };

    /// Complete message returned by the brain after parsing action and debug payloads.
    struct BrainResponse {
        /// Distinguishes executable actions from read-only navigation queries.
        BrainResponseType type = BrainResponseType::Action;
        /// Command the body should execute next.
        Action action{};
        /// Candidate targets requested during a navigation preview round trip.
        std::vector<PreviewCandidate> previewCandidates;
        /// Labeled actions to simulate without changing the real timeline.
        std::vector<CounterfactualCandidate> counterfactualCandidates;
        /// Optional planning state used by developer-facing renderers.
        BrainDebugState debug;
    };
}
