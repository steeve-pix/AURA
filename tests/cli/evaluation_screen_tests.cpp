#include "screens/EvaluationScreen.hpp"
#include "app/Commands.hpp"
#include <cassert>
#include <clocale>
#include <fstream>

std::string text(aura::cli::EvaluationScreen &screen, int width = 120, int height = 24) {
    std::string result;
    for (const auto &line: screen.frame(width, height, false)) result += line + "\n";
    return result;
}

int main(int argc, char **argv) {
    std::setlocale(LC_ALL, "");
    assert(aura::cli::find_command("eval") == nullptr);
    assert(aura::cli::find_command("/evaluate") == nullptr);
    aura::cli::EvaluationScreen screen;
    screen.receive("AURA_EVAL\tstart\tbalance.pt\t88\t11\t1200");
    screen.receive("AURA_EVAL\tinterface\t205\t15\t2\t2");
    screen.receive("AURA_EVAL\tmotors\t14\t15\tHead");
    assert(text(screen).find("14/15") != std::string::npos);
    assert(text(screen).find("Head off") != std::string::npos);
    screen.receive("AURA_EVAL\tprogress\t200");
    assert(text(screen).find("EVALUATING") != std::string::npos);
    screen.receive("AURA_EVAL\tresult\t259\t267\tfallen\t2.158");
    assert(text(screen).find("COMPLETE") != std::string::npos);
    assert(text(screen).find("259 steps") != std::string::npos);
    assert(text(screen).find("8 steps") != std::string::npos);
    for (int width: {1, 30, 59, 60, 80, 120, 180})
        for (int height: {1, 8, 18, 19, 24, 40})
            assert(screen.frame(width, height, true).size() == static_cast<std::size_t>(height));
    if (argc == 2) {
        std::ofstream out(argv[1]);
        for (const auto &line: screen.frame(120, 24, true)) out << line << '\n';
    }
    screen.receive("AURA_EVAL\tresult\t260\t-\ttime_limit\t10");
    assert(text(screen).find("First run") != std::string::npos);
    screen.receive("AURA_EVAL\tbalance\tdeterministic\t200\t1.667\t60\t150\t1\t1\t0.8\t208\tfallen");
    screen.receive("AURA_EVAL\tbalance\tstochastic\t180\t1.5\t80\t130\t2\t1\t0.7\t182\tfallen");
    assert(text(screen, 160, 30).find("BALANCED") != std::string::npos);
    assert(text(screen, 160, 30).find("Quality 0.800") != std::string::npos);
    assert(text(screen, 160, 30).find("Recoveries 2") != std::string::npos);
    screen.receive("AURA_EVAL\tworld\t4");
    assert(text(screen, 160, 30).find("means per agent; counts rounded") != std::string::npos);
    screen.receive("AURA_EVAL\tbalance\tdeterministic\t200\t1.667\t60\t150\t1\t1\tnan\t208\tfallen");
    assert(text(screen, 160, 30).find("Quality 0.800") != std::string::npos);
    screen.receive("AURA_EVAL\terror\tIncompatible checkpoint");
    assert(text(screen).find("Incompatible checkpoint") != std::string::npos);
    assert(text(screen, 50, 16).find("Incompatible checkpoint") != std::string::npos);
}
