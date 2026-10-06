#include <cmath>
#include <iostream>
#include "aura/body/LocalJointVelocity.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/MechanicalState.hpp"

int main() {
    using namespace aura;
    bool passed = true;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    for (int fixture = 0; fixture < 4; ++fixture) {
        body::BodyPart3D a, b;
        a.body.mass = 2; b.body.mass = 3;
        a.body.momentOfInertia = {0.4f, 0.7f, 1.3f};
        b.body.momentOfInertia = {0.8f, 1.1f, 0.5f};
        a.body.orientation = math::Quaternion::fromAxisAngle({0, 1, 0}, 0.6f);
        b.body.orientation = math::Quaternion::fromAxisAngle({1, 0, 0}, -0.7f);
        a.body.position = {0, 1, 0}; b.body.position = {0, -1, 0};
        a.body.velocity = {1, -2, 3}; b.body.velocity = {-4, 2, 1};
        a.body.angularVelocity = {1, 3, -2}; b.body.angularVelocity = {-2, 1, 4};
        body::Joint3D joint;
        if (fixture > 0) {
            joint.localAnchorA = {0.2f, -0.5f, 0.3f};
            joint.localAnchorB = {-0.1f, 0.5f, -0.2f};
        }
        if (fixture == 2) { a.body.velocity = b.body.velocity = {}; a.body.angularVelocity = b.body.angularVelocity = {}; }
        if (fixture == 3) {
            // Matching world anchors permit vector angular momentum conservation.
            const auto anchor = body::localToWorldPoint(a, joint.localAnchorA);
            b.body.position = anchor - b.body.orientation.rotate(joint.localAnchorB);
        }
        const auto oldA = a, oldB = b;
        const auto energy = [&] { return physics::mechanicalState(a.body, {}, {}).energy() + physics::mechanicalState(b.body, {}, {}).energy(); };
        const auto momentum = [&] { return a.body.velocity * a.body.mass + b.body.velocity * b.body.mass; };
        const auto angularMomentum = [&] { return physics::mechanicalState(a.body).angularMomentum + physics::mechanicalState(b.body).angularMomentum; };
        const double before = energy();
        const auto oldP = momentum(), oldL = angularMomentum();
        const auto audit = body::correctLocalJointVelocity(a, b, joint);
        const double work = audit.measuredLinearWork + audit.measuredAngularWork;
        check(std::abs(audit.predictedWork - work) < 1e-4, "work prediction matches actual two-body response");
        check(std::abs(energy() - before - work - audit.quadraticEnergy) < 1e-4, "work audit matches kinetic energy change");
        check(energy() <= before + 1e-4, "local projection dissipates energy");
        check(std::abs(audit.projectedSpeedAfter) < 1e-5f, "selected anchor direction repaired");
        check((momentum() - oldP).length() < 1e-5f, "equal opposite impulses preserve linear momentum");
        if (fixture == 3) check((angularMomentum() - oldL).length() < 1e-5f, "coincident anchors preserve angular momentum");
        check((a.body.position - oldA.body.position).length() == 0 && (b.body.position - oldB.body.position).length() == 0,
              "velocity solver leaves positions unchanged");
        const auto oldAnchorOffsetA = oldA.body.orientation.rotate(joint.localAnchorA);
        check((a.body.orientation.rotate(joint.localAnchorA) - oldAnchorOffsetA).length() == 0, "orientation unchanged");
    }
    return passed ? 0 : 1;
}
