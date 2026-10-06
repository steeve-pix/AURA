#include <iostream>
#include <limits>
#include "../src/BodyDiagnostics.hpp"

int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    aura::body::BodyPart3D a, b;
    a.name = "a"; b.name = "b";
    a.body.mass = 2; b.body.mass = 3;
    a.body.position = {-3, 1, 0}; b.body.position = {2, 1, 0};
    a.body.velocity = {0, -3, 0}; b.body.velocity = {0, 2, 0};
    const std::array<aura::body::BodyPart3D *, 2> parts{&a, &b};
    const auto state = aura::app::bodyMechanics(parts);
    check(std::abs(state.mass - 5) < 1e-6, "total mass");
    check((state.center - aura::math::Vec3{0, 1, 0}).length() < 1e-6f, "mass-weighted center divides sum by mass");
    check(state.momentum.length() < 1e-6f, "opposite linear momenta cancel");
    check((state.angularMomentum - aura::math::Vec3{0, 0, 30}).length() < 1e-6f,
          "orbital angular momentum remains when total linear momentum is zero");
    check(std::abs(state.rmsSpeed - std::sqrt(6.0)) < 1e-6, "mass-weighted speed remains nonzero");
    check(std::abs(state.energy() - aura::app::bodyEnergy(parts)) < 1e-6, "stage and summary energy agree");
    const auto q = aura::math::Quaternion::fromAxisAngle({0, 1, 0}, 0.7f);
    const aura::math::Quaternion negative{-q.w, -q.x, -q.y, -q.z};
    check(std::abs(aura::app::rotationAngle(q) - 0.7f) < 1e-6f &&
          std::abs(aura::app::rotationAngle(negative) - 0.7f) < 1e-6f, "orientation angle ignores quaternion sign");
    aura::app::DiagnosticPeak peak;
    peak.observe(4, "a", 1); peak.observe(1, "b", 2);
    check(peak.value == 4 && peak.owner == "a" && peak.step == 1, "interval peak retains earlier transient");
    aura::body::Joint3D joint;
    joint.motorDamping = 0;
    joint.targetAngle = 1;
    b.body.angularVelocity = {2, 0, 0};
    std::array<aura::app::BodyJoint, 1> joints{{{"testJoint", &a, &b, joint, true}}};
    aura::app::BodyDiagnosticHistory history;
    history.initialEnergy = aura::app::bodyEnergy(parts);
    history.sampleMotorWork(joints, 0.1);
    check(std::abs(history.motorAdded - 2) < 1e-6 && history.motorRemoved == 0,
          "motor input uses equal/opposite torque power");
    b.body.angularVelocity = {-2, 0, 0};
    history.sampleMotorWork(joints, 0.1);
    check(std::abs(history.motorRemoved + 2) < 1e-6 && std::abs(history.totalMotorWork) < 1e-6,
          "motor braking is signed negative work");
    joints[0].motor = false;
    history.sampleMotorWork(joints, 0.1);
    check(std::abs(history.motorRemoved + 2) < 1e-6, "disabled motor contributes no work");
    std::ostringstream captured;
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    history.observe(parts, joints, 0.1);
    history.observe(parts, joints, 0.2);
    history.printSummary(parts, joints, 0.2, true, false);
    aura::app::printBodyDiagnostics(parts, joints, 0.2);
    std::cout.rdbuf(previous);
    const auto text = captured.str();
    check(text.find("finite=1 paused=1 detail=0") != std::string::npos, "binary state flags");
    const auto first = text.find("[aura/failure]");
    check(first != std::string::npos && text.find("[aura/failure]", first + 1) == std::string::npos,
          "first gap event emitted once per joint");
    check(history.gap.value == 0 && history.motorAdded == 0 && history.motorRemoved == 0 &&
          history.intervalStart == 0.2, "printing clears interval metrics");
    check(text.find("PART ROTATION") != std::string::npos, "detail exposes full rotation");
    check(aura::app::logNumber(0.00152) == "1.52e-03", "small threshold breaches keep useful precision");
    check(aura::app::logNumber(-0.0) == "0.000", "negative zero does not add visual noise");
    check(aura::app::logNumber(-1e200).size() <= 10 && aura::app::logNumber(1e-200).size() <= 10,
          "extreme numbers fit detail columns");
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) check(line.size() <= 90, "summary and detail rows stay within 90 columns");
    captured.str(""); captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto precision = std::cout.precision();
    const auto flags = std::cout.flags();
    aura::app::printStartup(joints, 1.0 / 120, 16);
    aura::app::printControl("restart", 0, false, false, true);
    aura::app::printLoggingHelp();
    std::cout.rdbuf(previous);
    check(std::cout.precision() == precision && std::cout.flags() == flags, "logging preserves terminal stream formatting");
    check(captured.str().find("dt=8.333ms | rate=120Hz") != std::string::npos, "startup retains timestep precision");
    check(captured.str().find("[aura/control]") != std::string::npos &&
          captured.str().find("follow=1") != std::string::npos, "control events use one format with binary flags");
    b.body.velocity.x = std::numeric_limits<float>::quiet_NaN();
    check(!aura::app::bodyMechanics(parts).finite, "invalid body makes summary invalid");
    return passed ? 0 : 1;
}
