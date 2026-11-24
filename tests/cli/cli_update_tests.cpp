#include "app/CLIUpdate.hpp"
#include "app/Commands.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main(int argc, char **argv) {
    using namespace aura::cli;
    if (argc == 2) {
        const auto hash = cli_source_fingerprint(argv[1]);
        if (!hash) return 1;
        std::cout << *hash;
        return 0;
    }
    char pattern[] = "/tmp/aura-cli-fingerprint-XXXXXX";
    const auto raw = mkdtemp(pattern);
    assert(raw);
    const std::filesystem::path root(raw);
    std::filesystem::create_directories(root / "cli/build");
    std::filesystem::create_directories(root / "cli/app");
    std::filesystem::create_directories(root / "models");
    std::ofstream(root / "cli/CMakeLists.txt") << "project(test)\n";
    std::ofstream(root / "cli/app/main.cpp") << "same-size-A";
    const auto first = cli_source_fingerprint(root);
    assert(first && first->size() == 16);
    std::ofstream(root / "cli/app/main.cpp") << "same-size-B";
    const auto changed = cli_source_fingerprint(root);
    assert(changed && changed != first);
    std::ofstream(root / "cli/build/generated.hpp") << "build output";
    std::ofstream(root / "models/checkpoint.pt") << "trained weights";
    assert(cli_source_fingerprint(root) == changed);
    std::ofstream(root / "cli/new.hpp") << "new source";
    assert(cli_source_fingerprint(root) != changed);
    std::filesystem::remove(root / "cli/new.hpp");
    assert(cli_source_fingerprint(root) == changed);
    std::filesystem::create_directories(root / "cli/screens");
    std::filesystem::create_directories(root / "python/experiments");
    std::filesystem::create_directories(root / "python/__pycache__");
    std::ofstream(root / "cli/screens/Nested.hpp") << "nested screen A";
    const auto nested = cli_source_fingerprint(root);
    assert(nested != changed);
    std::ofstream(root / "cli/screens/Nested.hpp") << "nested screen B";
    assert(cli_source_fingerprint(root) != nested);
    std::ofstream(root / "python/experiments/example.py") << "nested Python source";
    const auto python = cli_source_fingerprint(root);
    std::ofstream(root / "python/__pycache__/ignored.py") << "cache output";
    std::ofstream(root / "cli/build/generated.hpp") << "changed build output";
    assert(cli_source_fingerprint(root) == python);
    assert(!cli_source_fingerprint(root / "missing"));
    assert(find_command("/update") && find_command("update")->id == CommandId::update);
    std::filesystem::remove_all(root);
    std::cout << "CLI update fingerprint and command checks passed.\n";
}
