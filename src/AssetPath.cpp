#include "AssetPath.hpp"
#include <filesystem>
#include <stdexcept>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <array>
#include <climits>
#endif

namespace aura::app {
    std::string assetPath(const std::string &relativePath) {
        static const std::filesystem::path directory = []() -> std::filesystem::path {
#ifdef AURA_ASSET_DIR
            return AURA_ASSET_DIR;
#elif defined(__APPLE__)
            const auto bundle = CFBundleGetMainBundle();
            const auto url = bundle ? CFBundleCopyResourceURL(bundle, CFSTR("assets"), nullptr, nullptr) : nullptr;
            if (!url) throw std::runtime_error("AURA app bundle is missing its assets directory");
            std::array<UInt8, PATH_MAX> path{};
            const bool converted = CFURLGetFileSystemRepresentation(url, true, path.data(), path.size());
            CFRelease(url);
            if (!converted) throw std::runtime_error("Cannot resolve AURA bundle asset path");
            return reinterpret_cast<const char *>(path.data());
#endif
        }();
        return (directory / relativePath).string();
    }

    std::string viewerActionPath() {
        return (std::filesystem::path(AURA_PROJECT_DIR) / "viewer_action.json").string();
    }

    std::string viewerObservationPath() {
        return (std::filesystem::path(AURA_PROJECT_DIR) / "viewer_observation.json").string();
    }
}
