#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include "SharedHipReplay.hpp"

int main(int argc, char** argv) {
    using namespace aura;
    if (argc != 2) return 2;
    auto input = body::createAuraBody3D();
    const auto skeleton = body::createAuraSkeleton3D(input);
    auto parts = skeleton.collectComponent(input, input.torso, skeleton.head);
    parts.push_back(&input.head);
    std::ifstream file(argv[1]);
    std::string line;
    std::set<std::string> loaded;
    while (std::getline(file, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::string tag, name;
        row >> tag >> name;
        if (tag != "sharedHipInput") continue;
        const auto it = std::find_if(parts.begin(), parts.end(), [&](const auto* p) { return p->name == name; });
        if (it == parts.end() || !loaded.insert(name).second) return 2;
        auto& p = **it;
        auto& b = p.body;
        row >> p.size.x >> p.size.y >> p.size.z >> b.mass >> b.position.x >> b.position.y >> b.position.z
            >> b.orientation.w >> b.orientation.x >> b.orientation.y >> b.orientation.z
            >> b.velocity.x >> b.velocity.y >> b.velocity.z
            >> b.angularVelocity.x >> b.angularVelocity.y >> b.angularVelocity.z
            >> b.momentOfInertia.x >> b.momentOfInertia.y >> b.momentOfInertia.z;
        if (!row) return 2;
    }
    if (loaded.size() != 16) return 2;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    std::ostringstream output;
    const auto replay = test::replaySharedHips(input, skeleton, output);
    check(replay.initial.penetration > 0.001f, "saved pose has meaningful thigh overlap");
    check(std::abs(replay.initial.angles[0] - skeleton.leftHip.maxAngle) < 1e-5f &&
          std::abs(replay.initial.angles[3] - skeleton.rightHip.maxAngle) < 1e-5f,
          "both hips start at their upper limits");
    for (const auto* trial : {&replay.leftOnly, &replay.rightOnly, &replay.symmetric}) {
        check(trial->improves, "separating hip rotations reduce overlap");
        check(!trial->limitsSafe && !trial->feasible, "separation violates hip limits");
        check(trial->gapsSafe, "component rotation preserves all six leg anchors");
    }
    check(replay.symmetric.metrics.penetration < replay.leftOnly.metrics.penetration &&
          replay.symmetric.metrics.penetration < replay.rightOnly.metrics.penetration,
          "both hips contribute to separation, even though the request is infeasible");
    check(replay.velocitiesUnchanged, "every copied trial leaves velocities untouched");
    check(replay.feasibleTrials > 0 && replay.improvingFeasibleTrials == 0,
          "sampled limit-safe and floor-safe rotations cannot improve this saturated pose");
    check(replay.bestUnconstrained.metrics.penetration == 0 && !replay.bestUnconstrained.feasible,
          "ignoring feasibility hides the limit and floor conflict");
    std::cout << output.str();
    return passed ? 0 : 1;
}
