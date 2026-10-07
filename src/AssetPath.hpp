#pragma once
#include <string>

namespace aura::app {
    // Paths inside the app's asset directory, independent of the working directory.
    std::string assetPath(const std::string &relativePath);
}
