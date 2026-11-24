#pragma once
#include "BalanceClassifier.hpp"
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace aura::sim {
    enum class TerminationMode { Legacy, BalanceV2 };

    inline std::string_view terminationModeName(TerminationMode mode) {
        switch (mode) {
            case TerminationMode::Legacy: return "legacy";
            case TerminationMode::BalanceV2: return "balance_v2";
        }
        throw std::invalid_argument("Unknown termination mode");
    }

    struct TerminationStatus {
        bool terminated = false, truncated = false;
        std::string_view reason = "none";
        bool done() const { return terminated || truncated; }
    };

    // No new fall criteria. The cap is independent: both flags may be true;
    // physical/predicate failure takes reason precedence over a coincident cap.
    inline TerminationStatus evaluateTermination(TerminationMode mode, bool legacyStanding,
                                                 BalanceState balance, std::size_t steps, std::size_t limit) {
        (void) terminationModeName(mode);
        const bool terminated = mode == TerminationMode::Legacy ? !legacyStanding : balance == BalanceState::Fallen;
        const bool truncated = steps >= limit;
        return {terminated, truncated, terminated ? "fallen" : truncated ? "time_limit" : "none"};
    }
}
