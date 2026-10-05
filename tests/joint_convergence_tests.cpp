#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointGeometry.hpp"

int main() {
    aura::body::BodyPart3D torso, thigh, shin, foot;
    torso.body.position = {0.0f, 5.0f, 0.0f};
    thigh.body.position = {0.4f, 3.4f, 0.2f};
    shin.body.position = {-0.3f, 1.2f, -0.1f};
    foot.body.position = {0.2f, 0.1f, 0.7f};

    aura::body::Joint3D hip, knee, ankle;
    hip.localAnchorA = {0.0f, -1.0f, 0.0f};
    hip.localAnchorB = {0.0f, 0.9f, 0.0f};
    knee.localAnchorA = {0.0f, -0.9f, 0.0f};
    knee.localAnchorB = {0.0f, 0.8f, 0.0f};
    knee.minAngle = 0.0f;
    knee.maxAngle = 2.2f;
    ankle.localAnchorA = {0.0f, -0.8f, 0.0f};
    ankle.localAnchorB = {0.0f, 0.2f, -0.4f};

    const auto gap = [](const auto &partA, const auto &partB, const auto &joint) {
        return (aura::body::localToWorldPoint(partB, joint.localAnchorB) -
                aura::body::localToWorldPoint(partA, joint.localAnchorA)).length();
    };
    const auto gaps = [&] {
        return std::array{gap(torso, thigh, hip), gap(thigh, shin, knee), gap(shin, foot, ankle)};
    };
    const auto initial = gaps();
    bool passed = std::all_of(initial.begin(), initial.end(), [](float value) { return value > 0.1f; });
    std::cout << "iterations hip_gap knee_gap ankle_gap\n"
              << "0 " << initial[0] << ' ' << initial[1] << ' ' << initial[2] << '\n';

    // No forces, contacts, motors, or integration: only the current joint sweep.
    float previousMax = *std::max_element(initial.begin(), initial.end());
    for (int iteration = 1; iteration <= 20; ++iteration) {
        aura::body::solveJoint(torso, thigh, hip);
        aura::body::solveJoint(thigh, shin, knee);
        aura::body::solveJoint(shin, foot, ankle);
        aura::body::solveJoint(thigh, shin, knee);
        aura::body::solveJoint(torso, thigh, hip);

        const auto current = gaps();
        const float maxGap = *std::max_element(current.begin(), current.end());
        passed = passed && std::all_of(current.begin(), current.end(), [](float value) {
            return std::isfinite(value);
        }) && maxGap <= previousMax + 1e-6f;
        previousMax = maxGap;
        if (iteration == 1 || iteration == 2 || iteration == 4 ||
            iteration == 8 || iteration == 16 || iteration == 20) {
            std::cout << iteration << ' ' << current[0] << ' ' << current[1] << ' ' << current[2] << '\n';
        }
    }
    passed = passed && previousMax < 1e-4f;
    if (!passed) std::cerr << "FAIL constraint-only anchor convergence\n";
    return passed ? 0 : 1;
}
