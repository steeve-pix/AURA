#include <iostream>
#include <ostream>

#include "app/Application.hpp"

int main(int argc, char **argv) {
    using aura::app::Application;

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <application-name>" << std::endl;
        return 1;
    }

    Application application{argv[1]};
    return application.run();
}
