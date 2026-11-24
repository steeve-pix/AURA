#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointGeometry.hpp"

int main(int argc, char **argv) {
    using namespace aura;
    if (argc != 2)return 2;
    auto b = body::createAuraBody3D();
    const auto s = body::createAuraSkeleton3D(b);
    auto parts = s.collectComponent(b, b.torso, s.head);
    parts.push_back(&b.head);
    std::ifstream file(argv[1]);
    std::string line;
    int loaded = 0;
    while (std::getline(file, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::string tag, name;
        row >> tag >> name;
        auto it = std::find_if(parts.begin(), parts.end(), [&](auto *p) { return p->name == name; });
        if (tag != "kneeSwingInput" || it == parts.end())return 2;
        auto &p = **it;
        auto &r = p.body;
        row >> p.size.x >> p.size.y >> p.size.z >> r.mass >> r.position.x >> r.position.y >> r.position.z
                >> r.orientation.w >> r.orientation.x >> r.orientation.y >> r.orientation.z
                >> r.velocity.x >> r.velocity.y >> r.velocity.z >> r.angularVelocity.x >> r.angularVelocity.y >> r.
                angularVelocity.z
                >> r.momentOfInertia.x >> r.momentOfInertia.y >> r.momentOfInertia.z;
        if (!row)return 2;
        ++loaded;
    }
    if (loaded != 16)return 2;
    // Captured at 9.225 s, forward iteration 1, immediately before the
    // original float angle repair missed its lower bound. The later 9.233 s
    // completed-step failure has the same near-singular twist conditioning.
    body::correctLimbAngleAroundPivot(b, s, s.leftKnee, s.leftAnkle, body::MajorBodyBranch3D::LeftKnee);
    const float angle = body::relativeJointAngle(b.leftThigh, b.leftShin, s.leftKnee);
    if (std::abs(body::jointAngleError(b.leftThigh, b.leftShin, s.leftKnee)) > 1e-5f)return 1;
    const auto original = b;
    float worst = 0;
    for (int i = 0; i < 256; ++i) {
        auto trial = original;
        auto component = s.collectComponent(trial, trial.leftThigh, s.leftHip);
        const auto rotation = math::Quaternion::fromAxisAngle({std::sin(float(i)), std::cos(float(i) * 0.3f), 0.7f},
                                                              float(i) * 0.025f);
        body::rotateSubtreeAroundWorldPoint(component, body::localToWorldPoint(trial.pelvis, s.leftHip.localAnchorA),
                                            rotation);
        const float drift = std::abs(body::relativeJointAngle(trial.leftThigh, trial.leftShin, s.leftKnee) - angle);
        worst = std::max(worst, drift);
        if (drift > 1e-5f) {
            std::cerr << "common rotation twist drift=" << drift << '\n';
            return 1;
        }
        const auto gap = [](const auto &a, const auto &b, const auto &j) {
            return (body::localToWorldPoint(b, j.localAnchorB) - body::localToWorldPoint(a, j.localAnchorA)).length();
        };
        if (std::abs(gap(trial.leftThigh, trial.leftShin, s.leftKnee) - gap(
                         original.leftThigh, original.leftShin, s.leftKnee)) > 2e-6f ||
            std::abs(gap(trial.leftShin, trial.leftFoot, s.leftAnkle) - gap(
                         original.leftShin, original.leftFoot, s.leftAnkle)) > 2e-6f)
            return 1;
        for (auto *part: component) {
            auto old = std::find_if(parts.begin(), parts.end(), [&](auto *p) { return p->name == part->name; });
            if ((part->body.velocity - (*old)->body.velocity).lengthSquared() != 0 ||
                (part->body.angularVelocity - (*old)->body.angularVelocity).lengthSquared() != 0)
                return 1;
        }
    }
    std::cout << "maximum common-rotation knee drift=" << worst << '\n';
    return 0;
}
