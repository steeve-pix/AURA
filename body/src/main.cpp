#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "app/Application.hpp"

namespace {
    constexpr std::uint32_t DEFAULT_MAZE_SEED = 1337;
    constexpr std::size_t CHALLENGE_MOVE_TO_INTERVAL = 8;

    void printUsage(const char *program) {
        std::cerr
                << "Usage: " << program
                << " <brain-working-directory>"
                << " [--seed N] [--max-steps N] [--challenge-scenario]"
                << std::endl;
    }

    std::uint32_t parseSeed(const std::string &value) {
        std::size_t parsedCharacters = 0;
        const auto parsed = std::stoull(value, &parsedCharacters);

        if (
            parsedCharacters != value.size()
            || parsed > std::numeric_limits<std::uint32_t>::max()
        ) {
            throw std::invalid_argument{"Invalid maze seed."};
        }

        return static_cast<std::uint32_t>(parsed);
    }

    int parseMaxSteps(const std::string &value) {
        std::size_t parsedCharacters = 0;
        const auto parsed = std::stoll(value, &parsedCharacters);

        if (
            parsedCharacters != value.size()
            || parsed <= 0
            || parsed > std::numeric_limits<int>::max()
        ) {
            throw std::invalid_argument{"Maximum steps must be positive."};
        }

        return static_cast<int>(parsed);
    }
}

int main(int argc, char **argv) {
    using aura::app::Application;

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::uint32_t mazeSeed = DEFAULT_MAZE_SEED;
    std::optional<int> maxSteps;
    bool challengeScenario = false;

    try {
        for (int index = 2; index < argc; ++index) {
            const std::string option = argv[index];

            if (
                option == "--challenge-scenario"
                || option == "--replanning-scenario"
            ) {
                challengeScenario = true;
                continue;
            }

            if (option == "--seed" && index + 1 < argc) {
                mazeSeed = parseSeed(argv[++index]);
                continue;
            }

            if (option == "--max-steps" && index + 1 < argc) {
                maxSteps = parseMaxSteps(argv[++index]);
                continue;
            }

            std::cerr << "Unknown or incomplete option: " << option << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    std::unique_ptr<aura::scenario::Scenario> scenario;

    if (challengeScenario) {
        scenario = std::make_unique<aura::scenario::PeriodicMoveToBlock>(
            CHALLENGE_MOVE_TO_INTERVAL
        );
    } else {
        scenario = std::make_unique<aura::scenario::Scenario>();
    }

    Application application{
        argv[1],
        std::move(scenario),
        mazeSeed,
        maxSteps
    };
    return application.run();
}
