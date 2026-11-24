#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#if __has_include("CliBuildStamp.hpp")
#include "CliBuildStamp.hpp"
#else
#define AURA_CLI_SOURCE_FINGERPRINT "dev"
#define AURA_CLI_BUILD_ID "dev"
#endif

namespace aura::cli {
    inline std::optional<std::string> cli_source_fingerprint(const std::filesystem::path &root) {
        try {
            std::vector<std::filesystem::path> files;
            for (const auto &directory: {root / "cli", root / "python"}) {
                if (!std::filesystem::exists(directory)) continue;
                for (auto it = std::filesystem::recursive_directory_iterator(directory);
                     it != std::filesystem::recursive_directory_iterator(); ++it) {
                    const auto &entry = *it;
                    const auto name = entry.path().filename().string();
                    if (entry.is_directory() && (name.starts_with('.') || name == "__pycache__" ||
                        name == "build" || name.starts_with("build-") || name.starts_with("cmake-build-"))) {
                        it.disable_recursion_pending();
                        continue;
                    }
                    if (name.starts_with('.')) continue;
                    if (!entry.is_regular_file()) continue;
                    const auto ext = entry.path().extension().string();
                    if ((directory.filename() == "python" && ext == ".py") || (directory.filename() == "cli" &&
                                                                               (ext == ".cpp" || ext == ".hpp" || ext ==
                                                                                   ".mm" || ext == ".py" || ext ==
                                                                                   ".sh")))
                        files.push_back(entry.path());
                }
            }
            for (const auto &path: {root / "cli/CMakeLists.txt", root / "include/aura/sim/ActionSpec.hpp"})
                if (std::filesystem::is_regular_file(path)) files.push_back(path);
            if (!std::filesystem::is_regular_file(root / "cli/CMakeLists.txt")) return std::nullopt;
            std::sort(files.begin(), files.end(), [&](const auto &a, const auto &b) {
                return a.lexically_relative(root).generic_string() < b.lexically_relative(root).generic_string();
            });
            std::uint64_t hash = 14695981039346656037ull;
            const auto add = [&](unsigned char byte) { hash = (hash ^ byte) * 1099511628211ull; };
            for (const auto &path: files) {
                for (unsigned char byte: path.lexically_relative(root).generic_string()) add(byte);
                add(0);
                std::ifstream input(path, std::ios::binary);
                if (!input) return std::nullopt;
                char buffer[8192];
                while (input) {
                    input.read(buffer, sizeof buffer);
                    for (std::streamsize i = 0; i < input.gcount(); ++i) add(static_cast<unsigned char>(buffer[i]));
                }
                if (!input.eof()) return std::nullopt;
                add(0);
            }
            std::ostringstream text;
            text << std::hex << std::setfill('0') << std::setw(16) << hash;
            return text.str();
        } catch (const std::filesystem::filesystem_error &) { return std::nullopt; }
    }

    struct CLIUpdateStatus {
        bool known = false, available = false;
    };

    inline CLIUpdateStatus cli_update_status(bool force = false) {
        static CLIUpdateStatus cached;
        static auto checked = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (force || now - checked > std::chrono::seconds(2)) {
#ifdef AURA_PROJECT_DIR
            const auto current = cli_source_fingerprint(std::filesystem::path(AURA_PROJECT_DIR).lexically_normal());
            cached = {current.has_value(), current && *current != AURA_CLI_SOURCE_FINGERPRINT};
#else
            cached = {};
#endif
            checked = now;
        }
        return cached;
    }
}
