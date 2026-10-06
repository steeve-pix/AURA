#include <array>
#include <cmath>
#include <iostream>
#include <vector>
#include "aura/body/ComponentVelocity.hpp"
#include "aura/body/ComponentMass.hpp"
#include "aura/physics/MechanicalState.hpp"

int main() {
    using namespace aura;
    bool passed = true;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { passed = false; std::cerr << "FAIL " << message << '\n'; }
    };
    for (int count : {1, 2, 3}) {
        body::BodyPart3D parent;
        std::array<body::BodyPart3D, 3> children;
        parent.body.mass = 2;
        parent.body.position = {0, 3, 0};
        parent.body.velocity = {-1, 2, 3};
        parent.body.angularVelocity = {2, -1, 3};
        parent.body.momentOfInertia = {1, 2, 3};
        parent.body.orientation = math::Quaternion::fromAxisAngle({0, 1, 0}, 0.4f);
        std::vector<body::BodyPart3D *> parts;
        for (int i = 0; i < count; ++i) {
            auto &b = children[i].body;
            b.mass = i + 1;
            b.position = {0.2f * i, 2.0f - i, -0.3f * i};
            b.velocity = {3.0f + i, -2.0f + i, 1.0f - i};
            b.angularVelocity = {-2.0f + i, 3.0f - i, 1.0f + i};
            b.momentOfInertia = {0.4f, 0.7f, 1.1f};
            b.orientation = math::Quaternion::fromAxisAngle({1, 0, 0}, 0.2f * (i + 1));
            parts.push_back(&children[i]);
        }
        const auto oldChildren = children;
        const auto oldParent = parent;
        const auto energy = [&] {
            double sum = physics::mechanicalState(parent.body).energy();
            for (auto *part : parts) sum += physics::mechanicalState(part->body).energy();
            return sum;
        };
        const auto momentum = [&] {
            auto sum = parent.body.velocity * parent.body.mass;
            for (auto *part : parts) sum += part->body.velocity * part->body.mass;
            return sum;
        };
        const double before = energy();
        const auto pBefore = momentum();
        body::Joint3D joint;
        joint.localAnchorA = {0, -0.5f, 0};
        joint.localAnchorB = {0, 0.5f, 0};
        const auto audit = body::correctJointComponentModeVelocity(parent, children[0], joint, parts);
        const double measured = audit.linearWork + audit.angularWork;
        check(std::abs(audit.predictedWork - measured) < 1e-4, "predicted and measured work agree");
        check(std::abs(energy() - before - measured - audit.quadraticEnergy) < 1e-4,
              "work plus quadratic response matches actual energy change");
        check(energy() <= before + 1e-4, "accounted impulse does not increase kinetic energy");
        check(std::abs(audit.modeSpeedAfter) < 1e-4f, "selected mode reaches zero");
        check((momentum() - pBefore).length() < 1e-4f, "equal opposite response conserves linear momentum");
        check((parent.body.position - oldParent.body.position).length() == 0, "parent geometry unchanged");
        for (int i = 0; i < count; ++i) {
            check((children[i].body.position - oldChildren[i].body.position).length() == 0, "child geometry unchanged");
            const auto deltaW = children[i].body.angularVelocity - oldChildren[i].body.angularVelocity;
            const auto rootDeltaW = children[0].body.angularVelocity - oldChildren[0].body.angularVelocity;
            check((deltaW - rootDeltaW).length() < 1e-5f, "common angular response preserves relative angular motion");
            const auto deltaV = children[i].body.velocity - oldChildren[i].body.velocity;
            const auto rootDeltaV = children[0].body.velocity - oldChildren[0].body.velocity;
            check((deltaV - rootDeltaV - rootDeltaW.cross(children[i].body.position - children[0].body.position)).length() < 1e-5f,
                  "common rigid response preserves internal velocity residuals");
        }
    }
    // Pure translation endpoint: zero lever arm needs no angular axis/inertia.
    body::BodyPart3D a, b;
    a.body.velocity = {1, 0, 0}; b.body.velocity = {-1, 0, 0};
    a.body.momentOfInertia = b.body.momentOfInertia = {1, 1, 1};
    const std::vector<body::BodyPart3D *> single{&b};
    const auto translation = body::correctJointComponentModeVelocity(a, b, {}, single);
    check(std::abs(translation.modeSpeedAfter) < 1e-5f, "zero angular impulse handles translation");
    check(a.body.angularVelocity.length() == 0 && b.body.angularVelocity.length() == 0, "translation creates no spin");
    return passed ? 0 : 1;
}
