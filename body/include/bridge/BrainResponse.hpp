#pragma once
#include "Action.hpp"
#include "BrainDebugState.hpp"

namespace aura::bridge {
    /// Complete message returned by the brain after parsing action and debug payloads.
    struct BrainResponse {
        /// Command the body should execute next.
        Action action;
        /// Optional planning state used by developer-facing renderers.
        BrainDebugState debug;
    };
}
