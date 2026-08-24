#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bridge/BrainResponse.hpp"
#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::bridge {
    struct NavigationPreview {
        int id;
        bool reachable;
        std::optional<int> pathLength;
        std::optional<world::Position> nextStep;
    };

    /// Computes candidate routes without changing the world or moving AURA.
    std::vector<NavigationPreview> previewNavigation(
        const world::World &world,
        world::Position start,
        const std::vector<PreviewCandidate> &candidates
    );

    /// Serializes one complete preview response for the Python brain.
    std::string serializedNavigationPreviewResponse(
        const std::vector<NavigationPreview> &previews
    );
}
