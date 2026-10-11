#pragma once
#include <array>
#include <algorithm>
#include "aura/body/JointGeometry.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include "aura/body/ComponentPosition.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/SelfCollision.hpp"

// Test-only replay. The live position chooser and simulation ordering are untouched.
namespace joint_com_replay {
    inline auto parts(aura::body::AuraBody3D &b) {
        return std::array{
            &b.head, &b.neck, &b.torso, &b.pelvis,
            &b.leftUpperArm, &b.leftForearm, &b.leftHand, &b.rightUpperArm, &b.rightForearm, &b.rightHand,
            &b.leftThigh, &b.leftShin, &b.leftFoot, &b.rightThigh, &b.rightShin, &b.rightFoot
        };
    }

    // Accumulate in double so float pose quantization is visible in the residual.
    inline auto center(aura::body::AuraBody3D &b) {
        std::array<double, 3> result{};
        double mass = 0;
        for (auto *p: parts(b)) {
            const double m = p->body.mass;
            mass += m;
            result[0] += m * p->body.position.x;
            result[1] += m * p->body.position.y;
            result[2] += m * p->body.position.z;
        }
        for (auto &x: result)x /= mass;
        return result;
    }

    struct Capture {
        double horizontal = -1, time = 0;
        int iteration = 0;
        std::string sweep, joint;
        aura::body::AuraBody3D before, after;
        size_t a = 0, b = 0;
    };

    inline float thighPenetration(const aura::body::AuraBody3D &b) {
        return aura::physics::capsulePenetration(aura::physics::bodyPartCollisionCapsule(b.leftThigh),
                                                 aura::physics::bodyPartCollisionCapsule(b.rightThigh));
    }

    inline bool report(Capture &c, const aura::body::AuraSkeleton3D &skeleton, const aura::body::Joint3D &joint) {
        auto balanced = c.before, current = c.before;
        auto bp = parts(balanced), ip = parts(c.before), ap = parts(c.after);
        auto ca = skeleton.collectComponent(balanced, *bp[c.a], joint);
        auto cb = skeleton.collectComponent(balanced, *bp[c.b], joint);
        double ma = 0, mb = 0;
        for (auto *p: ca)ma += p->body.mass;
        for (auto *p: cb)mb += p->body.mass;
        const auto error = aura::body::localToWorldPoint(*bp[c.b], joint.localAnchorB) -
                           aura::body::localToWorldPoint(*bp[c.a], joint.localAnchorA);
        // Close the relative anchor error while cancelling mass-weighted translation.
        const auto da = error * static_cast<float>(mb / (ma + mb));
        const auto db = -error * static_cast<float>(ma / (ma + mb));
        for (auto *p: ca)p->body.position += da;
        for (auto *p: cb)p->body.position += db;
        auto cp = parts(current);
        const auto result =
                aura::body::correctJointPositionWithComponents(current, skeleton, *cp[c.a], *cp[c.b], joint);
        const auto start = center(c.before);
        // Feasibility is evidence, not a pass requirement: a balanced repair may be
        // incompatible with external contact even when its COM algebra is correct.
        bool exactReplay = true, unchanged = true, floorSafe = true, sphereSafe = true;
        double maxSphereIncrease = 0;
        std::string worstPair;
        for (size_t i = 0; i < bp.size(); ++i) {
            const auto &x = ip[i]->body;
            const auto &y = bp[i]->body;
            const auto sameVec = [](auto x, auto y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
            unchanged &= sameVec(x.velocity, y.velocity) && sameVec(x.angularVelocity, y.angularVelocity) &&
                    x.orientation.w == y.orientation.w && x.orientation.x == y.orientation.x &&
                    x.orientation.y == y.orientation.y && x.orientation.z == y.orientation.z;
            exactReplay &= sameVec(cp[i]->body.position, ap[i]->body.position);
            const float before = aura::physics::lowestPoint(ip[i]->body, ip[i]->size).y, after =
                    aura::physics::lowestPoint(bp[i]->body, bp[i]->size).y;
            if (after < std::min(0.0f, before) - 1e-6f)floorSafe = false;
            std::cout << "jointCOMFloor," << ip[i]->name << ',' << before << ',' << after << ',' << (
                after >= std::min(0.0f, before) - 1e-6f) << '\n';
            for (size_t j = i + 1; j < bp.size(); ++j) {
                if (!aura::physics::shouldSelfCollide(*ip[i], *ip[j], c.before, skeleton))continue;
                const auto penetration = [](auto *a, auto *b) {
                    return std::max(0.0f, aura::physics::collisionSphere(*a).radius +
                                          aura::physics::collisionSphere(*b).radius - (
                                              a->body.position - b->body.position).length());
                };
                const double increase = penetration(bp[i], bp[j]) - penetration(ip[i], ip[j]);
                if (increase > maxSphereIncrease) {
                    maxSphereIncrease = increase;
                    worstPair = ip[i]->name + std::string("/") + ip[j]->name;
                }
                sphereSafe &= increase <= .001;
            }
        }
        std::cout << std::scientific << std::setprecision(12) << "jointCOMWorst,time=" << c.time << ",iteration=" << c.
                iteration
                << ",sweep=" << c.sweep << ",joint=" << c.joint << ",error=" << error.x << ',' << error.y << ',' <<
                error.z
                << ",massA=" << ma << ",massB=" << mb << ",choice=" << (
                    result.choice == aura::body::ComponentPositionChoice::Child
                        ? "childB"
                        : result.choice == aura::body::ComponentPositionChoice::Parent
                              ? "parentA"
                              : "blocked") << ",exactLiveReplay=" << exactReplay << '\n';
        for (const auto &[name,component]: std::array{std::pair{"A", ca}, std::pair{"B", cb}}) {
            std::cout << "jointCOMComponent," << name;
            for (const auto *p: component)std::cout << ',' << p->name;
            std::cout << '\n';
        }
        double maxInternalDistanceDrift = 0;
        for (size_t i = 0; i < bp.size(); ++i)
            for (size_t j = i + 1; j < bp.size(); ++j) {
                const bool ai = std::find(ca.begin(), ca.end(), bp[i]) != ca.end(), aj =
                        std::find(ca.begin(), ca.end(), bp[j]) != ca.end();
                if (ai == aj)
                    maxInternalDistanceDrift = std::max(maxInternalDistanceDrift, std::abs(static_cast<double>(
                                                            (bp[i]->body.position - bp[j]->body.position).length() - (
                                                                ip[i]->body.position - ip[j]->body.position).
                                                            length())));
            }
        struct Edge {
            size_t a, b;
            const aura::body::Joint3D *joint;
        };
        const std::array<Edge, 15> edges{
            {
                {2, 3, &skeleton.waist}, {2, 1, &skeleton.neck}, {1, 0, &skeleton.head},
                {2, 4, &skeleton.leftShoulder}, {4, 5, &skeleton.leftElbow}, {5, 6, &skeleton.leftWrist},
                {2, 7, &skeleton.rightShoulder}, {7, 8, &skeleton.rightElbow}, {8, 9, &skeleton.rightWrist},
                {3, 10, &skeleton.leftHip}, {10, 11, &skeleton.leftKnee}, {11, 12, &skeleton.leftAnkle},
                {3, 13, &skeleton.rightHip}, {13, 14, &skeleton.rightKnee}, {14, 15, &skeleton.rightAnkle}
            }
        };
        double maxOtherAnchorGapDrift = 0;
        for (const auto &edge: edges)
            if (edge.joint != &joint) {
                const auto gap = [&](auto ps) {
                    return (aura::body::localToWorldPoint(*ps[edge.b], edge.joint->localAnchorB) -
                            aura::body::localToWorldPoint(*ps[edge.a], edge.joint->localAnchorA)).length();
                };
                maxOtherAnchorGapDrift = std::max(maxOtherAnchorGapDrift,
                                                  std::abs(static_cast<double>(gap(bp) - gap(ip))));
            }
        std::cout << "jointCOMTranslation,A," << da.x << ',' << da.y << ',' << da.z << "\njointCOMTranslation,B," << db.
                x << ',' << db.y << ',' << db.z << '\n';
        double balancedDrift = 0;
        float balancedGap = 0;
        for (auto [label,pose]: std::array{
                 std::pair{"before", &c.before}, std::pair{"current", &c.after}, std::pair{"balanced", &balanced}
             }) {
            auto ps = parts(*pose);
            const auto com = center(*pose);
            const float gap = (aura::body::localToWorldPoint(*ps[c.b], joint.localAnchorB) -
                               aura::body::localToWorldPoint(*ps[c.a], joint.localAnchorA)).length();
            std::cout << "jointCOMCandidate," << label << ",gap=" << gap << ",deltaCOM=" << com[0] - start[0] << ',' <<
                    com[1] - start[1] << ',' << com[2] - start[2]
                    << ",thighPenetration=" << thighPenetration(*pose) << '\n';
            if (pose == &balanced) {
                balancedGap = gap;
                for (size_t k = 0; k < 3; ++k)balancedDrift = std::max(balancedDrift, std::abs(com[k] - start[k]));
            }
        }
        std::cout << "jointCOMFeasibility,floorSafe=" << floorSafe << ",activeThighNoWorse=" << (
                    thighPenetration(balanced) <= thighPenetration(c.before) + .001f)
                << ",experimentalSphereNoWorse=" << sphereSafe << ",maximumSphereIncrease=" << maxSphereIncrease <<
                ",spherePair=" << worstPair
                << ",velocitiesAndOrientationsUnchanged=" << unchanged << ",maxOtherAnchorGapDrift=" <<
                maxOtherAnchorGapDrift << ",maxInternalDistanceDrift=" << maxInternalDistanceDrift << '\n';
        for (auto *p: ip) {
            const auto &b = p->body;
            std::cout << "jointCOMInput," << p->name << ',' << p->size.x << ',' << p->size.y << ',' << p->size.z << ','
                    << b.mass
                    << ',' << b.position.x << ',' << b.position.y << ',' << b.position.z
                    << ',' << b.orientation.w << ',' << b.orientation.x << ',' << b.orientation.y << ',' << b.
                    orientation.z
                    << ',' << b.velocity.x << ',' << b.velocity.y << ',' << b.velocity.z
                    << ',' << b.angularVelocity.x << ',' << b.angularVelocity.y << ',' << b.angularVelocity.z
                    << ',' << b.momentOfInertia.x << ',' << b.momentOfInertia.y << ',' << b.momentOfInertia.z << '\n';
        }
        return exactReplay && unchanged && balancedGap < 1e-5 && balancedDrift < 1e-6 && maxInternalDistanceDrift < 2e-6
               && maxOtherAnchorGapDrift < 2e-6;
    }
}
