#include "screens/ResearchScreen.hpp"
#include "app/Commands.hpp"
#include "ui/TrainingExitPrompt.hpp"
#include <cassert>
#include <clocale>
#include <fstream>
#include <regex>

static std::string plain(const std::vector<std::string> &frame) {
    std::string value;
    for (const auto &line: frame) value += std::regex_replace(line, std::regex("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])"), "") + '\n';
    return value;
}
int main(int argc, char **argv) {
    std::setlocale(LC_CTYPE, "");
    using namespace aura::cli;
    assert(find_command("/compare")->id == CommandId::compare);
    assert(find_command("/diagnoise")->id == CommandId::diagnose);
    assert(find_command("diagnose")->id == CommandId::diagnose);
    assert(find_command("/report")->id == CommandId::report);
    assert(!find_command("/eval") && !find_command("/evaluate"));
    if(argc==3 && std::string(argv[1])=="--preview") {
        std::ofstream file(std::string(argv[2])+"-exit.ansi");
        for(const auto &line: training_exit_frame(120,40)) file<<line<<'\n';
    }
    if(argc==4 && std::string(argv[1])=="--events") {
        ResearchScreen actual;
        std::ifstream input(argv[2]);
        for(std::string line;std::getline(input,line);) actual.receive(line);
        std::ofstream file(argv[3]);
        for(const auto &line: actual.frame(120,40,true)) file<<line<<'\n';
    }
    for (const std::string mode: {"compare", "diagnose", "report"}) {
        ResearchScreen screen;
        screen.receive("AURA_RESEARCH\tstart\t"+mode+"\tbalance.pt\t548");
        screen.receive("AURA_RESEARCH\trow\tSnapshot\t446e6b408360a246\tUpdate 548 / read-only");
        if(mode == "diagnose") {
            screen.receive("AURA_RESEARCH\trow\tActor\tFinite\t19,424 parameters / norm 7.7130");
            screen.receive("AURA_RESEARCH\trow\tCritic\tFinite\t17,409 parameters / norm 7.1858");
            screen.receive("AURA_RESEARCH\trow\tAdam steps\tActor [548] / critic [548]\tAligned");
            screen.receive("AURA_RESEARCH\trow\tEnvironment\tCompatible with current Balance System\t205 inputs / 32 actions");
            screen.receive("AURA_RESEARCH\trow\tHistory\t0 recorded / 548 saved updates\tEarlier metrics were not recorded");
            screen.receive("AURA_RESEARCH\trow\tControlled balance\t0.825 s / 99 steps\tLongest 99 steps / 0 recoveries");
            screen.receive("AURA_RESEARCH\trow\tQuality / return\t0.2593 / 101.00\tfallen / 390 survival steps");
        }
        if(mode == "report") {
            screen.receive("AURA_RESEARCH\trow\tSaved updates\t548\t17,536 episodes");
            screen.receive("AURA_RESEARCH\trow\tRecorded updates\t0\tMissing past measurements remain unavailable");
        }
        screen.receive("AURA_RESEARCH\tsection\tMATCHED EVALUATION");
        screen.receive("AURA_RESEARCH\trow\tProtocol\tCPU / deterministic + 3 exploration streams\tSame saved world, reward and exploration");
        screen.receive("AURA_RESEARCH\tsection\tBEST POLICY");
        screen.receive("AURA_RESEARCH\trow\tDeterministic\t0.825 s balanced / reward 101.21\t392 survival steps / fallen");
        screen.receive("AURA_RESEARCH\trow\tStochastic mean\t0.558 s balanced / reward 94.24\tFall rate 100% / 3 matched streams");
        screen.receive("AURA_RESEARCH\tprogress\t6\t12\tLatest / stream 190042");
        assert(plain(screen.frame(120,40,false)).find("50%") != std::string::npos);
        for(int i=0;i<15;++i) screen.receive("AURA_RESEARCH\trow\tStream "+std::to_string(i)+"\t0.558 s balanced\tPaired seed / finite rewards");
        screen.receive("AURA_RESEARCH\tsaved\t/Users/steeve.dimitry/Developer/AURA/reports/balance-comparison-20261015.md");
        screen.receive("AURA_RESEARCH\tcomparison\tSeed baseline\t0.675\t0.4\t80.2\t1");
        screen.receive("AURA_RESEARCH\tcomparison\tBest\t0.825\t0.558\t94.24\t1");
        screen.receive("AURA_RESEARCH\tcomparison\tLatest\t0.000\t0.000\t90.5\t1");
        screen.receive("AURA_RESEARCH\tcomplete");
        auto overview = plain(screen.frame(120,24,true));
        assert(overview.find("[1] Overview") != std::string::npos);
        assert(overview.find("[2] Details") != std::string::npos);
        assert(overview.find("Up/Down") == std::string::npos);
        assert(plain(screen.frame(80,24,true)).find("Up/Down") == std::string::npos);
        assert(overview.find("446e6b408360a246") == std::string::npos);
        if(mode == "compare") {
            assert(overview.find("0.825 s") != std::string::npos);
            assert(overview.find("0.558 s / 94.24") != std::string::npos);
            assert(overview.find("Latest") != std::string::npos);
        }
        for(const int width: {40,60,80,120,160}) for(const int rows: {10,24,40,60}) {
            const auto frame=screen.frame(width,rows,true);
            assert(static_cast<int>(frame.size())==rows);
            for(const auto &line: frame) {
                const auto value=std::regex_replace(line,std::regex("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])"),"");
                assert(aura::cli::screen::textCells(value)<=width);
            }
        }
        screen.key("2");
        screen.frame(120,40,true);
        screen.key("\033[F");
        assert(plain(screen.frame(120,40,true)).find("Report path") != std::string::npos);
        screen.key("\033[H");
        assert(plain(screen.frame(120,40,true)).find("Snapshot") != std::string::npos);
        screen.key("1");
        if(argc==3 && std::string(argv[1])=="--preview") {
            std::ofstream file(std::string(argv[2])+"-"+mode+".ansi");
            for(const auto &line: screen.frame(120,40,true)) file<<line<<'\n';
        }
        screen.receive("UserWarning: harmless warning");
        assert(screen.error().empty());
        screen.receive("AURA_RESEARCH\terror\tCheckpoint uses a different Balance System contract");
        screen.key("2"); screen.frame(120,40,true); screen.key("\033[F");
        assert(plain(screen.frame(120,40,true)).find("different Balance System contract")!=std::string::npos);
    }
}
