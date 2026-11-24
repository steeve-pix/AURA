#include "aura/sim/AuraSimulation.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/LocalJointVelocity.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
using namespace aura;

namespace {
    bool passed = true;

    void check(bool valid, const char *message) {
        if (!valid) {
            passed = false;
            std::cerr << "FAIL " << message << '\n';
        }
    }

    math::Quaternion pose(float x, float y, float z) {
        const math::Vec3 v{0, y, z};
        return math::Quaternion::fromAxisAngle(v, v.length()) * math::Quaternion::fromAxisAngle({1, 0, 0}, x);
    }

    float gap(const body::BodyPart3D &a, const body::BodyPart3D &b, const body::Joint3D &j) {
        return (body::localToWorldPoint(a, j.localAnchorA) - body::localToWorldPoint(b, j.localAnchorB)).length();
    }

    float energy(const body::BodyPart3D &p) {
        const auto w = p.body.orientation.conjugate().rotate(p.body.angularVelocity), I = p.body.momentOfInertia;
        return .5f * (I.x * w.x * w.x + I.y * w.y * w.y + I.z * w.z * w.z);
    }
}

int main() {
    // Test each multi-axis joint independently, with a tilted complete body so
    // local/world mistakes cannot be hidden by the initial identity orientation.
    for (int jointIndex = 0; jointIndex < 15; ++jointIndex)
        for (bool tilted: {false, true}) {
            sim::AuraSimulation sim;
            auto &entry = sim.getJoints()[jointIndex];
            auto &j = entry.constraint;
            if (!j.motorControlsSwing) continue;
            if (tilted) body::rotateSubtreeAroundWorldPoint(sim.getParts(), {},
                                                            math::Quaternion::fromAxisAngle({1, 2, 3}, .7f));
            body::translateSubtree(sim.getParts(), {0, 20, 0});
            const auto component = sim.getSkeleton().collectComponent(sim.body(), *entry.partB, j);
            const std::array<float, 3> lo{j.minAngle, j.minSwingY, j.minSwingZ}, hi{
                j.maxAngle, j.maxSwingY, j.maxSwingZ
            };
            std::array < float, 3 > angles{};
            for (int k = 0; k < 3; ++k) angles[k] = lo[k] + .6f * (hi[k] - lo[k]);
            const auto set = [&](const std::array<float, 3> &a) {
                const auto target = entry.partA->body.orientation * pose(a[0], a[1], a[2]);
                body::rotateSubtreeAroundWorldPoint(component, body::localToWorldPoint(*entry.partB, j.localAnchorB),
                                                    (target * entry.partB->body.orientation.conjugate()).normalized());
            };
            const auto measured = [&] {
                const auto v = body::relativeJointSwingAngles(*entry.partA, *entry.partB, j);
                return std::array < float, 3 >
                {
                    body::relativeJointAngle(*entry.partA, *entry.partB, j), v.y, v.z
                };
            };
            set(angles);
            const auto gradients = body::jointCoordinateGradients(*entry.partA, *entry.partB, j);
            const std::array<math::Vec3, 3> basis{math::Vec3{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (const auto direction: basis) {
                const auto original = entry.partB->body.orientation;
                std::array < std::array < float, 3 >, 2 > values{};
                constexpr float epsilon = .0002f;
                for (int sign = 0; sign < 2; ++sign) {
                    entry.partB->body.orientation = math::Quaternion::fromAxisAngle(
                                                        direction, sign ? epsilon : -epsilon) * original;
                    values[sign] = measured();
                }
                entry.partB->body.orientation = original;
                for (int k = 0; k < 3; ++k)
                    check(std::abs((values[1][k] - values[0][k]) / (2 * epsilon) - gradients[k].dot(direction)) < .003f,
                          "coordinate gradient matches measured world rotation");
            }
            for (int coordinate = 0; coordinate < 3; ++coordinate)
                for (bool maximum: {false, true}) {
                    auto requested = angles;
                    requested[coordinate] = (maximum ? hi[coordinate] : lo[coordinate]) + (maximum ? .07f : -.07f);
                    set(requested);
                    std::array < float, 15 > before{};
                    for (int i = 0; i < 15; ++i) {
                        const auto &c = sim.getJoints()[i];
                        before[i] = gap(*c.partA, *c.partB, c.constraint);
                    }
                    const auto parent = entry.partA->body;
                    body::correctJointAngleWithComponent(*entry.partA, *entry.partB, j, component);
                    body::correctJointSwingWithComponent(*entry.partA, *entry.partB, j, component);
                    const auto actual = measured();
                    for (int k = 0; k < 3; ++k)
                        check(std::abs(actual[k] - (k == coordinate ? (maximum ? hi[k] : lo[k]) : angles[k])) < 2e-5f,
                              "compound limit correction retains other joint coordinates");
                    for (int i = 0; i < 15; ++i) {
                        const auto &c = sim.getJoints()[i];
                        check(std::abs(gap(*c.partA, *c.partB, c.constraint) - before[i]) < 5e-6f,
                              "joint correction retains every descendant attachment");
                    }
                    check((entry.partA->body.position - parent.position).length() == 0,
                          "limit projection leaves parent fixed");
                    const auto gradient = body::jointCoordinateGradients(*entry.partA, *entry.partB, j)[coordinate];
                    entry.partA->body.angularVelocity = {};
                    entry.partB->body.angularVelocity = gradient * (maximum ? 1.f : -1.f);
                    const float beforeEnergy = energy(*entry.partA) + energy(*entry.partB);
                    body::correctLocalJointAngularLimitVelocity(*entry.partA, *entry.partB, j);
                    check(std::abs(
                              (entry.partB->body.angularVelocity - entry.partA->body.angularVelocity).dot(gradient)) <
                          2e-5f,
                          "outward limit velocity is removed, including locked ankle yaw");
                    check(energy(*entry.partA) + energy(*entry.partB) <= beforeEnergy + 1e-5f,
                          "limit impulses do not add angular energy");
                }
            // Simultaneous stops are a coupled problem, not three independent
            // scalar impulses. Check every corner, including mixed inward motion.
            for (int mask = 0; mask < 8; ++mask) {
                std::array < float, 3 > corner{};
                for (int k = 0; k < 3; ++k) corner[k] = (mask & (1 << k)) ? hi[k] : lo[k];
                set(corner);
                entry.partA->body.angularVelocity = {.2f, .4f, -.3f};
                entry.partB->body.angularVelocity = {1.3f, -.9f, .7f};
                const auto momentum = [](const body::BodyPart3D &p) {
                    const auto q = p.body.orientation.normalized();
                    const auto w = q.conjugate().rotate(p.body.angularVelocity), I = p.body.momentOfInertia;
                    return q.rotate({I.x * w.x, I.y * w.y, I.z * w.z});
                };
                const auto beforeMomentum = momentum(*entry.partA) + momentum(*entry.partB);
                const float beforeEnergy = energy(*entry.partA) + energy(*entry.partB);
                body::correctLocalJointAngularLimitVelocity(*entry.partA, *entry.partB, j);
                const auto rates = body::jointCoordinateGradients(*entry.partA, *entry.partB, j);
                const auto omega = entry.partB->body.angularVelocity - entry.partA->body.angularVelocity;
                for (int k = 0; k < 3; ++k) {
                    const float rate = rates[k].dot(omega);
                    check(lo[k] == hi[k] ? std::abs(rate) < 2e-5f : (mask & (1 << k)) ? rate < 2e-5f : rate > -2e-5f,
                          "all simultaneous stops allow only inward coordinate velocity");
                }
                check(energy(*entry.partA) + energy(*entry.partB) <= beforeEnergy + 1e-5f,
                      "coupled stops dissipate angular energy");
                check((momentum(*entry.partA) + momentum(*entry.partB) - beforeMomentum).length() < 1e-5f,
                      "coupled angular impulses conserve endpoint angular momentum");
            }
            std::cout << entry.name << ": compound coordinates, directional limits, attachments, impulses checked\n";
        }
    for (int request = 0; request < 8; ++request) {
        sim::AuraSimulation sim;
        float maxGap = 0, maxLimit = 0, maxOverlap = 0, maxFloor = 0;
        for (int step = 0; step < 1200; ++step) {
            const float ramp = step < 120 ? step / 120.f : step < 360 ? 1.f : step < 480 ? (480 - step) / 120.f : 0;
            sim::AuraAction a;
            if (request == 0) {
                a.leftHipTarget = -.7f * ramp;
                a.rightHipTarget = -.4f * ramp;
            }
            if (request == 1) {
                a.leftHipTarget = -.6f * ramp;
                a.leftHipSwingZTarget = .3f * ramp;
                a.rightHipSwingZTarget = .3f * ramp;
            }
            if (request == 2) {
                a.leftHipSwingYTarget = -.5f * ramp;
                a.rightHipSwingYTarget = .5f * ramp;
            }
            if (request == 3) {
                a.waistSwingYTarget = .7f * ramp;
                a.waistSwingZTarget = -.2f * ramp;
            }
            if (request == 4) {
                a.neckSwingYTarget = -.6f * ramp;
                a.neckSwingZTarget = .2f * ramp;
            }
            if (request == 5) {
                a.leftWristSwingYTarget = .7f * ramp;
                a.rightWristSwingYTarget = -.7f * ramp;
                a.leftWristSwingZTarget = .2f * ramp;
            }
            if (request == 6) {
                a.leftAnkleSwingZTarget = .2f * ramp;
                a.rightAnkleSwingZTarget = -.2f * ramp;
            }
            if (request == 7)
                for (const auto &spec: sim::actionSpecs)
                    if (std::string_view(spec.jointName) != "head")
                        a.*(spec.target) = static_cast<float>(spec.minimum < 0 ? spec.minimum * .1 : spec.maximum * .1)
                                           * ramp;
            sim.step(a);
            check(app::bodyMechanics(sim.getParts()).finite, "full-body articulated motion stays finite");
            for (const auto &c: sim.getJoints()) {
                maxGap = std::max(maxGap, gap(*c.partA, *c.partB, c.constraint));
                maxLimit = std::max(maxLimit, std::abs(body::jointAngleError(*c.partA, *c.partB, c.constraint)));
                if (c.constraint.type == body::JointType::SwingTwist) {
                    const auto v = body::relativeJointSwingAngles(*c.partA, *c.partB, c.constraint);
                    maxLimit = std::max({
                        maxLimit, c.constraint.minSwingY - v.y, v.y - c.constraint.maxSwingY,
                        c.constraint.minSwingZ - v.z, v.z - c.constraint.maxSwingZ
                    });
                }
            }
            maxOverlap = std::max(
                maxOverlap, body::bodyCollisionMetrics(sim.body(), sim.getBodyPairs()).maximumPenetration);
            for (const auto *p: sim.getParts()) maxFloor = std::max(
                                                    maxFloor, -physics::lowestPoint(p->body, p->size, p->floorShape).y);
        }
        std::cout << "articulationMotion request=" << request << " duration=10s maxGap=" << maxGap << " maxLimit=" <<
                maxLimit
                << " maxOverlap=" << maxOverlap << " maxFloor=" << maxFloor << '\n';
        check(maxGap <= .001f && maxLimit <= 1e-5f && maxOverlap <= .001f && maxFloor <= .001f,
              "full-body articulation respects attachment limit collision floor bounds");
    }
    return passed ? 0 : 1;
}
