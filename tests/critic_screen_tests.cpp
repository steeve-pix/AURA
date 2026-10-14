#include "CriticScreen.hpp"
#include "ScreenCanvas.hpp"
#include <cassert>
#include <clocale>
#include <regex>

std::string plain(const std::string& line) {
    static const std::regex escapes("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])");
    return std::regex_replace(line,escapes,"");
}
std::string text(aura::cli::CriticScreen& screen,int width=160,int rows=40,bool exited=false) {
    std::string result;
    for (const auto& line:screen.frame(width,rows,exited)) result+=plain(line)+'\n';
    return result;
}
int main() {
    std::setlocale(LC_ALL,"");
    aura::cli::CriticScreen screen;
    assert(text(screen).find("CRITIC TRAINING LAB")!=std::string::npos);
    screen.receive("AURA_CRITIC\tstart\tFrozen test actor.pt\t205\t14\t32\t16\t20\t43\t.001\t.995\t100");
    screen.receive("AURA_CRITIC\tprogress\ttraining\t8\t32\t334");
    assert(text(screen).find("Training 8 / 32")!=std::string::npos);
    assert(text(screen).find("FROZEN")!=std::string::npos);
    const auto timed=[&](double seconds) {
        std::string result;
        for (const auto& line:screen.frame(160,40,false,seconds)) result+=plain(line)+'\n';
        return result;
    };
    const auto firstFrame=timed(0), nextFrame=timed(.2);
    assert(firstFrame!=nextFrame);
    assert(firstFrame.find("\\ Waiting")!=std::string::npos);
    assert(nextFrame.find("| Waiting")!=std::string::npos);
    assert(nextFrame.find("Collecting training episodes 8 / 32")!=std::string::npos);
    assert(nextFrame.find("Curves appear after both datasets are collected")!=std::string::npos);
    const auto slow=timed(35);
    assert(slow.find("35s elapsed")!=std::string::npos);
    assert(slow.find("No recent progress")!=std::string::npos);
    screen.receive("AURA_CRITIC\tprogress\ttraining\t9\t32\t334");
    assert(timed(36).find("No recent progress")==std::string::npos);
    screen.receive("AURA_CRITIC\tphase\tvalidation");
    screen.receive("AURA_CRITIC\tprogress\tvalidation\t16\t16\t342");
    assert(text(screen).find("VALIDATION EPISODES")!=std::string::npos);
    screen.receive("AURA_CRITIC\tbaseline\t.22066\t.98852\t11036\t5509");
    screen.receive("AURA_CRITIC\tphase\toptimizing");
    screen.receive("AURA_CRITIC\tmetric\t0\t1.32536\t1.32133");
    screen.receive("AURA_CRITIC\tmetric\t1\t1.11388\t1.11002");
    screen.receive("AURA_CRITIC\tmetric\t5\t.52369\t.52304");
    screen.receive("AURA_CRITIC\tmetric\t10\t.30765\t.31234");
    screen.receive("AURA_CRITIC\tmetric\t20\t.11807\t.11947");
    const auto finalMetrics=text(screen);
    assert(finalMetrics.find("0.11947")!=std::string::npos);
    assert(finalMetrics.find("45.9% lower")!=std::string::npos);
    assert(finalMetrics.find("Waiting")==std::string::npos);
    assert(finalMetrics.find("No recent progress")==std::string::npos);
    assert(finalMetrics.find("TRAINING ERROR")!=std::string::npos);
    assert(finalMetrics.find("VALIDATION ERROR")!=std::string::npos);
    screen.receive("AURA_CRITIC\tmetric\t20\t9\t9");
    screen.receive("AURA_CRITIC\tmetric\t19\t1\t1");
    screen.receive("AURA_CRITIC\tmetric\t21\t1\t1");
    screen.receive("AURA_CRITIC\tmetric\t20\tnan\t1");
    assert(text(screen)==finalMetrics);
    screen.receive("AURA_CRITIC\tsaved\t/tmp/test/report.json\t/tmp/test/critic.pt");
    screen.receive("AURA_CRITIC\tresult\t0\t1\t.02095");
    assert(text(screen,160,40,true).find("COMPLETE")!=std::string::npos);
    assert(text(screen).find("Actor change 0.00000000")!=std::string::npos);
    for (int width:{1,30,59,60,80,108,120,160,220}) for (int rows:{1,12,19,20,24,40,60}) {
        auto frame=screen.frame(width,rows,false);
        assert(frame.size()==static_cast<std::size_t>(rows));
        for (const auto& line:frame) assert(aura::cli::screen::textCells(plain(line))<=width);
        screen.key("\033[6~"); screen.key("scroll_down");
        assert(screen.frame(width,rows,false).size()==static_cast<std::size_t>(rows));
        screen.key("\033[H");
    }
    aura::cli::CriticScreen stopped;
    stopped.receive("AURA_CRITIC\tstopped");
    assert(text(stopped,160,40,true).find("STOPPED")!=std::string::npos);
    assert(text(stopped,160,40,true).find("FAILED")==std::string::npos);
    assert(text(stopped,160,40,true).find("Stopped before error measurements")!=std::string::npos);
    assert(text(stopped,160,40,true).find("Waiting")==std::string::npos);
    aura::cli::CriticScreen failed;
    failed.receive("AURA_CRITIC\terror\tCannot load actor checkpoint");
    assert(text(failed).find("Cannot load actor checkpoint")!=std::string::npos);
    assert(text(failed).find("FAILED")!=std::string::npos);
}
