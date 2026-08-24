#pragma once
#include <vector>

#include "Action.hpp"
#include "BrainDebugState.hpp"

namespace aura::bridge {
    enum class BrainResponseType {
        Action,
        PreviewRequest
    };

    struct PreviewCandidate {
        int id;
        world::Position target;
    };

    /// Complete message returned by the brain after parsing action and debug payloads.
    struct BrainResponse {
        /// Distinguishes executable actions from read-only navigation queries.
        BrainResponseType type = BrainResponseType::Action;
        /// Command the body should execute next.
        Action action{};
        /// Candidate targets requested during a navigation preview round trip.
        std::vector<PreviewCandidate> previewCandidates;
        /// Optional planning state used by developer-facing renderers.
        BrainDebugState debug;
    };
}
