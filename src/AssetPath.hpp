#pragma once
#include <string>

namespace aura::app {
    // Paths inside the app's asset directory, independent of the working directory.
    std::string assetPath(const std::string &relativePath);
    // Path to the development action file, independent of the working directory.
    std::string viewerActionPath();
    // Path to the live viewer observation file, independent of the working directory.
    std::string viewerObservationPath();
}
