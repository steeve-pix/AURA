#include "bridge/NavigationPreview.hpp"

#include "navigation/Pathfinder.hpp"

#include <nlohmann/json.hpp>

namespace aura::bridge {
    std::vector<NavigationPreview> previewNavigation(
        const world::World &world,
        world::Position start,
        const std::vector<PreviewCandidate> &candidates
    ) {
        std::vector<NavigationPreview> previews;
        previews.reserve(candidates.size());

        for (const auto &candidate: candidates) {
            const auto path = navigation::findPath(
                world,
                start,
                candidate.target
            );
            const bool reachable =
                    start == candidate.target || !path.empty();

            previews.push_back({
                candidate.id,
                reachable,
                reachable
                    ? std::optional<int>{static_cast<int>(path.size())}
                    : std::nullopt,
                path.empty()
                    ? std::nullopt
                    : std::optional<world::Position>{path.front()}
            });
        }

        return previews;
    }

    std::string serializedNavigationPreviewResponse(
        const std::vector<NavigationPreview> &previews
    ) {
        nlohmann::json json = {
            {"type", "preview_response"},
            {"previews", nlohmann::json::array()}
        };

        for (const auto &preview: previews) {
            nlohmann::json previewJson = {
                {"id", preview.id},
                {"reachable", preview.reachable},
                {
                    "path_length", preview.pathLength.has_value()
                                       ? nlohmann::json(preview.pathLength.value())
                                       : nlohmann::json{nullptr}
                },
                {"next_step", nullptr}
            };

            if (preview.nextStep.has_value()) {
                const auto &nextStep = preview.nextStep.value();
                previewJson["next_step"] = {nextStep.x, nextStep.y};
            }

            json["previews"].push_back(previewJson);
        }

        return json.dump();
    }
}
