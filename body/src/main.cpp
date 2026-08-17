#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include "app/Application.hpp"

int main(int argc, char **argv) {
    using aura::app::Application;

    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <brain-working-directory> [--replanning-scenario]"
                  << std::endl;
        return 1;
    }

    if (argc == 3 && std::string(argv[2]) != "--replanning-scenario") {
        std::cerr << "Unknown option: " << argv[2] << std::endl;
        return 1;
    }

    std::unique_ptr<aura::scenario::Scenario> scenario;

    if (argc >= 3 && std::string(argv[2]) == "--replanning-scenario") {
        scenario = std::make_unique<aura::scenario::BlockFirstMoveTo>();
    } else {
        scenario = std::make_unique<aura::scenario::Scenario>();
    }

    Application application{argv[1], std::move(scenario)};
    return application.run();
}
