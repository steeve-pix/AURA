#include "aura/sim/BalanceClassifier.hpp"
#include "aura/sim/AuraSimulation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace aura::sim;

namespace {
    constexpr double dt = 1. / 120;
    int checks = 0;

    void check(bool ok, const char *message) {
        ++checks;
        if (!ok) throw std::runtime_error(message);
    }

    double up(double degrees) { return std::cos(degrees * std::numbers::pi / 180); }

    BalanceMeasurements upright() {
        BalanceMeasurements m;
        m.leftFootContact = m.rightFootContact = m.supportPolygonValid = true;
        m.supportMargin = .2;
        return m;
    }

    void ticks(BalanceClassifier &c, const BalanceMeasurements &m, int count) {
        for (int i = 0; i < count; ++i) c.update(m, dt);
    }

    void settle(BalanceClassifier &c) {
        ticks(c, upright(), 18);
        check(c.assessment().state == BalanceState::Balanced, "supported settling reaches BALANCED");
    }
}

int main() {
    try {
        BalanceClassifier c;
        check(c.assessment().state == BalanceState::Unbalanced, "initial airborne state");
        BalanceMeasurements airborne;
        ticks(c, airborne, 120);
        check(c.assessment().state == BalanceState::Unbalanced, "airborne alone is not fallen");
        c.reset();
        ticks(c, upright(), 17);
        check(c.assessment().state == BalanceState::Unbalanced, "entry persistence");
        ticks(c, upright(), 1);
        check(c.assessment().state == BalanceState::Balanced, "entry at 18 ticks");
        check(c.assessment().stateDuration == 0, "duration starts at observed transition");
        ticks(c, upright(), 1);
        check(std::abs(c.assessment().stateDuration - dt) < 1e-12, "simulation time duration");
        auto mild = upright();
        mild.pelvisUp = up(8);
        mild.torsoUp = up(12);
        mild.comVelocity = {.1f, -.1f, .1f};
        ticks(c, mild, 60);
        check(c.assessment().state == BalanceState::Balanced, "mild controlled tilt");

        // Signed forward/backward and lateral tilt have the same up-dot-vertical.
        for (auto axis: {
                 aura::math::Vec3{1, 0, 0}, aura::math::Vec3{-1, 0, 0}, aura::math::Vec3{0, 0, 1},
                 aura::math::Vec3{0, 0, -1}
             }) {
            c.reset();
            settle(c);
            auto severe = upright();
            severe.pelvisUp = severe.torsoUp = aura::math::Quaternion::fromAxisAngle(axis, 50 * std::numbers::pi / 180).
                              rotate({0, 1, 0}).y;
            ticks(c, severe, 6);
            check(c.assessment().state == BalanceState::Unbalanced, "severe directional tilt exits balanced");
            ticks(c, severe, 120);
            check(c.assessment().state != BalanceState::Fallen, "tilt alone cannot establish fall");
            check(c.assessment().dangerousMotion, "danger posture diagnostic");
        }
        c.reset();
        settle(c);
        auto rotating = upright();
        rotating.pelvisAngularSpeed = 3;
        ticks(c, rotating, 6);
        check(c.assessment().state == BalanceState::Unbalanced, "rapid rotation while upright");
        auto oneFoot = upright();
        oneFoot.leftFootContact = false;
        c.reset();
        settle(c);
        ticks(c, oneFoot, 60);
        check(c.assessment().state == BalanceState::Balanced, "one-foot support can stay balanced");
        auto noFeet = upright();
        noFeet.leftFootContact = noFeet.rightFootContact = false;
        noFeet.supportPolygonValid = false;
        ticks(c, noFeet, 5);
        check(c.assessment().state == BalanceState::Balanced, "brief foot loss has grace");
        ticks(c, upright(), 1);
        check(c.assessment().outsideDuration == 0, "support recovery clears exit timer");
        ticks(c, noFeet, 6);
        check(c.assessment().state == BalanceState::Unbalanced, "sustained support loss exits balanced");
        ticks(c, noFeet, 120);
        check(c.assessment().state != BalanceState::Fallen, "airborne duration alone is not fall");
        ticks(c, upright(), 17);
        check(c.assessment().state == BalanceState::Unbalanced, "recovery requires settling");
        ticks(c, upright(), 1);
        check(c.assessment().state == BalanceState::Balanced, "recovery transition");

        auto invalidSupport = upright();
        invalidSupport.supportPolygonValid = false;
        ticks(c, invalidSupport, 1);
        ticks(c, upright(), 1);
        check(c.assessment().state == BalanceState::Balanced, "single polygon collapse is tolerated");
        auto outside = upright();
        outside.supportMargin = -.2;
        ticks(c, outside, 1);
        ticks(c, upright(), 1);
        check(c.assessment().state == BalanceState::Balanced, "one negative margin is tolerated");
        ticks(c, outside, 60);
        check(c.assessment().state == BalanceState::Unbalanced, "persistent poor support warns");
        check(c.assessment().collapseDuration == 0, "negative support margin never proves a fall");
        auto hysteresis = upright();
        hysteresis.pelvisUp = up(17);
        hysteresis.supportMargin = -.01;
        ticks(c, hysteresis, 60);
        check(c.assessment().state == BalanceState::Unbalanced, "retention envelope cannot re-enter");
        ticks(c, upright(), 18);
        ticks(c, hysteresis, 60);
        check(c.assessment().state == BalanceState::Balanced, "wider retention envelope prevents flicker");

        auto collapse = upright();
        collapse.pelvisHeightRatio = .3;
        collapse.torsoHeightRatio = .2;
        collapse.comHeightRatio = .25;
        collapse.torsoUp = up(75);
        c.reset();
        ticks(c, collapse, 23);
        check(c.assessment().state == BalanceState::Unbalanced, "collapse persistence before latch");
        ticks(c, collapse, 1);
        check(c.assessment().state == BalanceState::Fallen, "sustained low torso collapse");
        ticks(c, upright(), 120);
        check(c.assessment().state == BalanceState::Fallen, "fallen latches despite pose rise");
        c.reset();
        check(c.assessment().state == BalanceState::Unbalanced && c.assessment().collapseDuration == 0 &&
              c.assessment().stateDuration == 0, "reset clears state and timers");
        auto contact = upright();
        contact.torsoUp = up(70);
        contact.torsoHeightRatio = .3;
        contact.pelvisHeightRatio = .4;
        contact.comHeightRatio = .5;
        for (bool head: {false, true}) {
            c.reset();
            contact.headContact = head;
            contact.torsoContact = !head;
            ticks(c, contact, 4);
            ticks(c, upright(), 1);
            check(c.assessment().state != BalanceState::Fallen && c.assessment().collapseDuration == 0,
                  "brief head/torso contact artifact cannot latch fall");
            ticks(c, contact, 24);
            check(c.assessment().state == BalanceState::Fallen, "sustained tilted low contact");
        }
        c.reset();
        auto headOnly = upright();
        headOnly.headContact = true;
        ticks(c, headOnly, 120);
        check(c.assessment().state != BalanceState::Fallen, "head contact alone is insufficient");
        // Rising height is neither sufficient nor necessary evidence of recovery.
        c.reset();
        auto correction = upright();
        correction.torsoUp = up(70);
        correction.comVelocity.y = -5;
        correction.pelvisHeightRatio = correction.torsoHeightRatio = correction.comHeightRatio = 1.2;
        ticks(c, correction, 120);
        check(c.assessment().state == BalanceState::Unbalanced, "upward correction is not recovery");
        auto invalid = upright();
        invalid.torsoUp = std::numeric_limits<double>::quiet_NaN();
        ticks(c, invalid, 120);
        check(c.assessment().state != BalanceState::Fallen, "invalid numeric state is not a fall");
        BalanceConfig fast;
        fast.enterSeconds = 2 * dt;
        BalanceClassifier configured(fast);
        ticks(configured, upright(), 2);
        check(configured.assessment().state == BalanceState::Balanced, "configurable persistence");
        bool rejected = false;
        fast.enter.pelvisTiltDegrees = 90;
        try { BalanceClassifier bad(fast); } catch (const std::invalid_argument &) { rejected = true; }
        check(rejected, "invalid hysteresis configuration rejected");

        AuraSimulation sim;
        const auto initial = sim.observation().balance;
        for (int i = 0; i < 10; ++i)
            check(sim.observation().balance.stateDuration == initial.stateDuration,
                  "observations never advance timers");
        sim.step();
        auto once = sim.observation().balance;
        check(std::abs(once.stateDuration - dt) < 1e-12, "one classifier update per complete physics tick");
        for (int i = 0; i < 10; ++i)
            check(sim.observation().balance.stateDuration == once.stateDuration,
                  "repeated reads are stable after stepping");
        sim.reset();
        check(sim.observation().balance.stateDuration == 0, "simulation reset clears classifier");
        BalanceConfig alternative;
        alternative.enterSeconds = .01;
        AuraSimulation normal, other(alternative);
        for (int step = 0; step < 240; ++step) {
            normal.step();
            other.step();
            for (std::size_t i = 0; i < 16; ++i) {
                const auto &a = normal.getParts()[i]->body, &b = other.getParts()[i]->body;
                check((a.position - b.position).lengthSquared() == 0 && (a.velocity - b.velocity).lengthSquared() == 0
                      &&
                      (a.angularVelocity - b.angularVelocity).lengthSquared() == 0 &&
                      a.orientation.w == b.orientation.w && a.orientation.x == b.orientation.x &&
                      a.orientation.y == b.orientation.y && a.orientation.z == b.orientation.z,
                      "classification configuration cannot affect physical state");
            }
        }
        std::cout << "Balance classifier synthetic transitions and tick integration passed (" << checks << " checks)\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
