#include "aura/sim/AuraSimulation.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace aura::sim;

namespace {
    void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }

    void samePhysics(AuraSimulation& a, AuraSimulation& b) {
        for (std::size_t i=0;i<a.getParts().size();++i) {
            const auto& x=a.getParts()[i]->body;
            const auto& y=b.getParts()[i]->body;
            for (const auto& pair : {std::pair{x.position,y.position},std::pair{x.velocity,y.velocity},
                    std::pair{x.angularVelocity,y.angularVelocity},std::pair{x.acceleration,y.acceleration},
                    std::pair{x.angularAcceleration,y.angularAcceleration},std::pair{x.force,y.force},
                    std::pair{x.torque,y.torque}})
                check(pair.first.x==pair.second.x && pair.first.y==pair.second.y && pair.first.z==pair.second.z,
                      "termination modes preserve physical state");
            check(x.orientation.w==y.orientation.w && x.orientation.x==y.orientation.x &&
                  x.orientation.y==y.orientation.y && x.orientation.z==y.orientation.z,
                  "termination modes preserve orientation");
        }
    }
}

int main() {
    try {
        for (const auto mode: {TerminationMode::Legacy, TerminationMode::BalanceV2})
            for (bool standing: {false, true})
                for (const auto state: {BalanceState::Balanced, BalanceState::Unbalanced, BalanceState::Fallen})
                    for (std::size_t step: {1199, 1200, 1201}) {
                        const auto s = evaluateTermination(mode, standing, state, step, 1200);
                        const bool terminated = mode == TerminationMode::Legacy
                                                    ? !standing
                                                    : state == BalanceState::Fallen;
                        check(s.terminated == terminated && s.truncated == (step >= 1200), "termination truth table");
                        check(s.done() == (s.terminated || s.truncated), "done consistency");
                        check(s.reason == (terminated ? "fallen" : step >= 1200 ? "time_limit" : "none"),
                              "reason precedence");
                    }
        AuraSimulation legacy({}, TerminationMode::Legacy), v2;
        check(v2.terminationMode() == TerminationMode::BalanceV2, "whole-body termination is default");
        int legacyEnd = -1, v2End = -1;
        bool recoveryWindow=false, fallenLatched=false;
        for (int step = 0; step <= 1200; ++step) {
            samePhysics(legacy,v2);
            const auto a = legacy.terminationStatus(), b = v2.terminationStatus();
            const auto assessment=v2.observation().balance;
            check(a.terminated==!legacy.isStanding(),"legacy uses original standing predicate");
            check(b.terminated==(assessment.state==BalanceState::Fallen),"V2 terminates exactly on FALLEN");
            if (assessment.state==BalanceState::Unbalanced)
                check(!b.terminated,"UNBALANCED permits V2 continuation");
            if (fallenLatched) check(b.terminated,"FALLEN remains latched through continued raw stepping");
            fallenLatched |= b.terminated;
            recoveryWindow |= a.terminated && !b.terminated && assessment.state==BalanceState::Unbalanced;
            if (legacyEnd < 0 && a.terminated) legacyEnd = step;
            if (v2End < 0 && b.terminated) v2End = step;
            check(legacy.steps()==static_cast<std::size_t>(step) && v2.steps()==legacy.steps(),"one step per raw tick");
            check(a.truncated==(step>=1200) && b.truncated==(step>=1200),"actual time cap is independent of mode");
            check(a.truncated == legacy.isTruncated() && a.done() == legacy.isDone(), "legacy API consistency");
            check(b.truncated == v2.isTruncated() && b.done() == v2.isDone(), "V2 API consistency");
            for (int read=0;read<3;++read) {
                const auto after=v2.observation().balance;
                check(after.state==assessment.state && after.stateDuration==assessment.stateDuration &&
                      after.enteringDuration==assessment.enteringDuration && after.outsideDuration==assessment.outsideDuration &&
                      after.collapseDuration==assessment.collapseDuration,"repeated observations cannot advance classification");
            }
            if (step == 1200) {
                check(b.terminated && b.truncated && b.reason == "fallen", "actual cap with latched fall");
                break;
            }
            legacy.step();
            v2.step();
        }
        check(legacyEnd>=0 && v2End>=0,"neutral replay exercises both terminal conditions");
        check(recoveryWindow,"V2 preserves an observed post-legacy recovery opportunity");
        const auto before = v2.observation().balance;
        for (int i = 0; i < 10; ++i) {
            check(v2.terminationStatus().terminated, "read cannot unlatch fall");
            check(v2.observation().balance.stateDuration == before.stateDuration, "end queries cannot advance timers");
        }
        v2.reset();
        check(v2.terminationMode() == TerminationMode::BalanceV2, "reset retains chosen mode");
        check(!v2.isDone() && !v2.terminationStatus().terminated && !v2.isTruncated() &&
              v2.steps()==0 && v2.time()==0., "reset clears ends, cap and clock");
        check(v2.observation().balance.state == BalanceState::Unbalanced, "reset clears FALLEN latch");
        legacy.reset();
        check(!legacy.isDone() && legacy.steps()==0 && legacy.time()==0. && legacy.terminationMode()==TerminationMode::Legacy,
              "legacy reset clears terminal state and time");
        samePhysics(legacy,v2);
        std::cout << "Portable termination behavior passed; observed neutral endpoints legacy="<<legacyEnd
                  <<" V2="<<v2End<<" (not historical assertions)\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
