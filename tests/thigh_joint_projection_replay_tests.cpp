#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>
#include "HipCapsuleRotationReplay.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/ComponentPosition.hpp"
#include "aura/body/ThighSelfCollision.hpp"

using namespace aura;

namespace {
    auto parts(body::AuraBody3D &b, const body::AuraSkeleton3D &s) {
        auto p = s.collectComponent(b, b.torso, s.head);
        p.push_back(&b.head);
        return p;
    }

    double objective(const body::AuraBody3D &b, const body::AuraSkeleton3D &s) {
        const auto m = test::hipRotationMetrics(b, s);
        // Both errors are lengths. This experiment minimizes their unweighted
        // squared sum; it is not a runtime constraint priority policy.
        return double(m.gaps[0]) * m.gaps[0] + double(m.penetration) * m.penetration;
    }

    bool safe(body::AuraBody3D &candidate, body::AuraBody3D &baseline, const body::AuraSkeleton3D &s) {
        auto c = parts(candidate, s), b = parts(baseline, s);
        for (size_t i = 0; i < c.size(); ++i)
            if (!body::floorMoveIsNoWorse(physics::lowestPoint(b[i]->body, b[i]->size).y,
                                          physics::lowestPoint(c[i]->body, c[i]->size).y))
                return false;
        const auto cm = test::hipRotationMetrics(candidate, s), bm = test::hipRotationMetrics(baseline, s);
        const std::array<const body::Joint3D *, 6> joints{
            &s.leftHip, &s.leftKnee, &s.leftAnkle, &s.rightHip, &s.rightKnee, &s.rightAnkle
        };
        for (size_t i = 0; i < 6; ++i) {
            const auto error = [&](float a) {
                return std::max({0.0f, joints[i]->minAngle - a, a - joints[i]->maxAngle});
            };
            if (error(cm.angles[i]) > error(bm.angles[i]) + 1e-6f)return false;
            if (i != 0 && cm.gaps[i] > bm.gaps[i] + 1e-6f)return false;
        }
        // This is a mid-sweep state: right-hip twist still awaits its own
        // correction. Preserve, rather than hide, that pre-existing violation.
        const auto l = body::relativeJointSwingAngles(candidate.pelvis, candidate.leftThigh, s.leftHip);
        const auto r = body::relativeJointSwingAngles(candidate.pelvis, candidate.rightThigh, s.rightHip);
        const auto within = [](const auto &a, const auto &j) {
            return a.y >= j.minSwingY - 1e-6f && a.y <= j.maxSwingY + 1e-6f &&
                   a.z >= j.minSwingZ - 1e-6f && a.z <= j.maxSwingZ + 1e-6f;
        };
        return within(l, s.leftHip) && within(r, s.rightHip);
    }

    void sample(const char *name, const body::AuraBody3D &b, const body::AuraSkeleton3D &s) {
        const auto m = test::hipRotationMetrics(b, s);
        const auto l = body::relativeJointSwingAngles(b.pelvis, b.leftThigh, s.leftHip);
        const auto r = body::relativeJointSwingAngles(b.pelvis, b.rightThigh, s.rightHip);
        std::cout << "jointProjection," << name << ',' << m.penetration << ',' << m.gaps[0] << ',' << m.angles[0] << ','
                << l.y << ',' << l.z
                << ',' << m.angles[3] << ',' << r.y << ',' << r.z << ',' << m.feetY[0] << ',' << m.feetY[1] << ',' <<
                objective(b, s);
        for (const auto gap: m.gaps)std::cout << ',' << gap;
        std::cout << '\n';
    }
}

int main(int argc, char **argv) {
    if (argc != 2)return 2;
    auto input = body::createAuraBody3D();
    const auto skeleton = body::createAuraSkeleton3D(input);
    auto inputParts = parts(input, skeleton);
    std::ifstream file(argv[1]);
    std::string line;
    std::set<std::string> loaded;
    bool metadata = false;
    while (std::getline(file, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::string tag, name;
        row >> tag;
        if (tag == "thighJointGeometryLargest") {
            std::string op;
            float increase;
            row >> name >> op >> increase;
            if (!row || name != "leftHip" || op != "correctBranchAngleAroundPivot")return 2;
            metadata = true;
            continue;
        }
        if (tag != "thighJointGeometryInput")return 2;
        row >> name;
        const auto it = std::find_if(inputParts.begin(), inputParts.end(), [&](auto *p) { return p->name == name; });
        if (it == inputParts.end() || !loaded.insert(name).second)return 2;
        auto &p = **it;
        auto &b = p.body;
        row >> p.size.x >> p.size.y >> p.size.z >> b.mass >> b.position.x >> b.position.y >> b.position.z
                >> b.orientation.w >> b.orientation.x >> b.orientation.y >> b.orientation.z
                >> b.velocity.x >> b.velocity.y >> b.velocity.z >> b.angularVelocity.x >> b.angularVelocity.y >> b.
                angularVelocity.z
                >> b.momentOfInertia.x >> b.momentOfInertia.y >> b.momentOfInertia.z;
        if (!row)return 2;
    }
    if (!metadata || loaded.size() != 16)return 2;
    bool passed = true;
    const auto check = [&](bool value, const char *label) {
        if (!value) {
            passed = false;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    auto current = input;
    body::correctBranchAngleAroundPivot(current, skeleton, skeleton.leftHip, body::MajorBodyBranch3D::LeftHip, true);
    auto serial = current;
    body::ThighSwingCollisionAudit audit;
    const auto serialStep = body::correctThighOverlapWithSwing(serial, skeleton, 0.01f, 0.001f, &audit, false, true);
    // Coupled copied search: retain the required Y clamp, then jointly choose
    // a fraction of the pending attachment repair and small legal Y/Z swings.
    // Each leg's swing request is capped at 0.01 rad. No limit is relaxed.
    auto fullAttachment = current;
    const auto repair = body::correctJointPositionWithComponents(fullAttachment, skeleton, fullAttachment.pelvis,
                                                                 fullAttachment.leftThigh, skeleton.leftHip);
    auto best = current;
    double bestCost = objective(best, skeleton);
    float bestAlpha = 0;
    math::Vec3 bestLeft{}, bestRight{};
    int feasible = 0, noWorse = 0;
    std::vector<math::Vec3> requests;
    for (float y: {-0.01f, -0.005f, 0.0f, 0.005f, 0.01f})
        for (float z: {-0.01f, -0.005f, 0.0f, 0.005f, 0.01f}) {
            math::Vec3 request{0, y, z};
            if (request.length() <= 0.010000001f)requests.push_back(request);
        }
    const auto swing = [&](auto &b, bool left, const math::Vec3 &request) {
        if (request.lengthSquared() == 0)return true;
        auto &thigh = left ? b.leftThigh : b.rightThigh;
        const auto &hip = left ? skeleton.leftHip : skeleton.rightHip;
        const auto component = skeleton.collectComponent(b, thigh, hip);
        return body::applyJointSwingWithComponent(b.pelvis, thigh, hip, component, request.normalized(),
                                                  request.length());
    };
    const float initialPen = test::hipRotationMetrics(input, skeleton).penetration;
    for (int step = 0; step <= 40; ++step)
        for (const auto &dl: requests)
            for (const auto &dr: requests) {
                const float alpha = step / 40.0f;
                auto candidate = current;
                if (repair.choice != body::ComponentPositionChoice::MultipleContacts) {
                    auto &root = repair.choice == body::ComponentPositionChoice::Child
                                     ? candidate.leftThigh
                                     : candidate.pelvis;
                    body::translateSubtree(skeleton.collectComponent(candidate, root, skeleton.leftHip),
                                           repair.translation * alpha);
                }
                if (!swing(candidate, true, dl) || !swing(candidate, false, dr) || !safe(candidate, input, skeleton))
                    continue;
                ++feasible;
                const auto m = test::hipRotationMetrics(candidate, skeleton);
                noWorse += m.penetration <= initialPen + 1e-6f;
                const double cost = objective(candidate, skeleton);
                if (cost < bestCost) {
                    bestCost = cost;
                    best = candidate;
                    bestAlpha = alpha;
                    bestLeft = dl;
                    bestRight = dr;
                }
            }
    std::cout << std::scientific << std::setprecision(9)
            <<
            "jointProjection,variant,penetration,leftHipGap,leftTwist,leftSwingY,leftSwingZ,rightTwist,rightSwingY,rightSwingZ,leftFootY,rightFootY,objective,leftHipGap,leftKneeGap,leftAnkleGap,rightHipGap,rightKneeGap,rightAnkleGap\n";
    sample("before", input, skeleton);
    sample("currentAngle", current, skeleton);
    sample("currentThenCollision", serial, skeleton);
    sample("coupledSmallSearch", best, skeleton);
    std::cout << "jointProjectionSearch,feasible=" << feasible << ",noWorseThanPreOperation=" << noWorse << ",alpha=" <<
            bestAlpha
            << ",leftRequestY=" << bestLeft.y << ",leftRequestZ=" << bestLeft.z << ",rightRequestY=" << bestRight.y <<
            ",rightRequestZ=" << bestRight.z
            << ",serialAccepted=" << serialStep.corrected << ",serialSwingBlocks=" << audit.blockedBySwingLimit
            << ",serialFloorBlocks=" << audit.blockedByFloor << ",serialTwistAnchorBlocks=" << audit.
            blockedByTwistOrAnchor << '\n';
    const auto before = test::hipRotationMetrics(input, skeleton), after = test::hipRotationMetrics(current, skeleton);
    check(std::abs(before.penetration - 0.1319572628f) < 1e-7f && std::abs(after.penetration - 0.2213125229f) < 1e-7f,
          "saved largest angular projection reproduces the live penetration jump");
    check(std::abs(body::relativeJointSwingY(current.pelvis, current.leftThigh, skeleton.leftHip) - 0.4f) < 1e-6f,
          "current correction enforces left swing Y");
    check(feasible > 0 && safe(best, input, skeleton) && bestCost <= objective(current, skeleton),
          "coupled trial respects constraints and improves its stated objective");
    for (auto *variant: {&current, &serial, &best}) {
        const auto output = parts(*variant, skeleton);
        for (size_t i = 0; i < output.size(); ++i)
            check((output[i]->body.velocity - inputParts[i]->body.velocity).lengthSquared() == 0 &&
                  (output[i]->body.angularVelocity - inputParts[i]->body.angularVelocity).lengthSquared() == 0,
                  "copied geometry variants preserve all velocities exactly");
    }
    const auto bestMetrics = test::hipRotationMetrics(best, skeleton);
    for (size_t i = 1; i < 6; ++i)check(std::abs(bestMetrics.gaps[i] - before.gaps[i]) < 1e-6f,
                                        "non-target leg gaps are preserved");
    for (size_t i: {size_t(1), size_t(2), size_t(4), size_t(5)})
        check(std::abs(bestMetrics.angles[i] - before.angles[i]) < 1e-6f,
              "descendant knee/ankle angles remain unchanged");
    check(std::abs(bestMetrics.angles[0] - before.angles[0]) < 1e-6f &&
          std::abs(bestMetrics.angles[3] - before.angles[3]) < 1e-6f,
          "coupled swings preserve both hip twists including the pending right limit violation");
    std::cout << "jointProjectionPassed=" << passed << '\n';
    return passed ? 0 : 1;
}
