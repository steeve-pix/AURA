#include "aura/sim/AuraSimulation.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace aura;

namespace {
    void check(bool ok, const char *message) {
        if (!ok) throw std::runtime_error(message);
    }

    bool near(double a, double b) { return std::abs(a - b) < 2e-6; }

    const sim::BodyPartObservation &part(const sim::AuraObservation &obs, const std::string &name) {
        return *std::find_if(obs.bodyPartStates.begin(), obs.bodyPartStates.end(),
                             [&](const auto &entry) { return entry.name == name; });
    }

    void sameState(sim::AuraSimulation &a, sim::AuraSimulation &b) {
        for (std::size_t i = 0; i < a.getParts().size(); ++i) {
            const auto &x = a.getParts()[i]->body, &y = b.getParts()[i]->body;
            check((x.position - y.position).lengthSquared() == 0 &&
                  (x.velocity - y.velocity).lengthSquared() == 0 &&
                  (x.angularVelocity - y.angularVelocity).lengthSquared() == 0 &&
                  (x.force - y.force).lengthSquared() == 0 && (x.torque - y.torque).lengthSquared() == 0 &&
                  x.orientation.w == y.orientation.w && x.orientation.x == y.orientation.x &&
                  x.orientation.y == y.orientation.y && x.orientation.z == y.orientation.z,
                  "observing must not change physical state");
        }
    }
}

int main() {
    try {
        sim::AuraSimulation simulation;
        auto initial = simulation.observation();
        check(initial.bodyPartStates.size() == 16 && initial.jointStates.size() == 15, "body/joint counts");
        check(near(part(initial, "pelvis").position.y, 4.77) &&
              near(part(initial, "torso").position.y, 6.6) &&
              near(part(initial, "head").position.y, 7.84), "reset body center heights");
        check(near(part(initial, "left_foot").floorClearance, .6), "reset foot clearance");
        for (const auto &p: initial.bodyPartStates) {
            check(near(p.up.y, 1) && near(p.up.x, 0) && near(p.up.z, 0), "identity up vector");
            check(p.geometricFloorContactPoints.empty(), "reset starts entirely above floor");
            check(std::isfinite(p.position.y) && std::isfinite(p.floorClearance) &&
                  std::isfinite(p.angularVelocity.lengthSquared()), "finite diagnostics");
        }

        // Synthetic poses only: no solver step on these detached fixtures.
        const float halfPi = std::numbers::pi_v<float> * .5f;
        for (const math::Vec3 axis: {math::Vec3{1, 0, 0}, math::Vec3{0, 1, 0}, math::Vec3{0, 0, 1}}) {
            for (float angle: {0.0f, .35f, halfPi, std::numbers::pi_v<float>}) {
                simulation.body().torso.body.orientation = math::Quaternion::fromAxisAngle(axis, angle);
                simulation.body().head.body.orientation = math::Quaternion::fromAxisAngle(axis, -angle);
                const auto obs = simulation.observation();
                const float expected = axis.y == 1 ? 1 : std::cos(angle);
                check(near(part(obs, "torso").up.y, expected) && near(part(obs, "head").up.y, expected),
                      "uprightness uses independent local up in world coordinates");
                check(near(part(obs, "pelvis").up.y, 1), "torso/head measurements are independent of pelvis");
            }
        }
        simulation.reset();
        simulation.body().torso.body.angularVelocity = {1, 2, 3};
        simulation.body().head.body.angularVelocity = {-3, 2, -1};
        auto moving = simulation.observation();
        check(part(moving, "torso").angularVelocity.x == 1 && part(moving, "head").angularVelocity.x == -3,
              "world angular velocities come from each rigid body");

        // Every floor shape: above the band, just inside it, and penetrating.
        for (auto *p: simulation.getParts()) {
            for (float clearance: {.02f, .005f, -.02f}) {
                p->body.position.y += clearance - physics::lowestPoint(p->body, p->size, p->floorShape).y;
                const auto obs = simulation.observation();
                const auto &reported = part(obs, p->name);
                check(near(reported.floorClearance, clearance), "signed surface floor clearance");
                check(reported.geometricFloorContactPoints.empty() == (clearance > .01f), "floor proximity band");
                if (p == &simulation.body().torso) {
                    check(reported.floorShape.type == physics::CollisionShapeType::Sphere, "torso uses floor sphere");
                    check(reported.geometricFloorContactPoints.size() == (clearance > .01f ? 0u : 1u),
                          "sphere contact count");
                }
                if (p == &simulation.body().leftFoot)
                    check(reported.geometricFloorContactPoints.size() == obs.leftFootContactPoints.size(),
                          "foot diagnostics reuse existing support contacts");
            }
        }
        simulation.reset();
        const auto obs = simulation.observation();
        for (std::size_t i = 0; i < obs.jointStates.size(); ++i) {
            const auto &joint = simulation.getJoints()[i];
            const auto expected = joint.partA->body.position +
                                  joint.partA->body.orientation.rotate(joint.constraint.localAnchorA);
            check((obs.jointStates[i].worldAnchorA - expected).lengthSquared() == 0, "joint anchor world transform");
        }
        sim::AuraSimulation observed, unobserved;
        for (int i = 0; i < 120; ++i) {
            (void) observed.observation();
            sameState(observed, unobserved);
            observed.step();
            unobserved.step();
            sameState(observed, unobserved);
        }
        std::cout << "Balance diagnostics: posture, floor shapes, anchors and read-only stepping passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
