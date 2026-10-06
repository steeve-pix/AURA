#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/ComponentMass.hpp"

int main() {
    using namespace aura::body;
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    BodyPart3D a, b;
    a.body.mass = 2.0f; b.body.mass = 3.0f;
    a.body.position = {1, 0, 0}; b.body.position = {6, 5, -5};
    const std::vector<BodyPart3D *> parts{&a, &b};
    check(componentMass(parts) == 5.0f, "sum component masses");
    check((componentCenterOfMass(parts) - aura::math::Vec3{4, 3, -3}).length() < 1e-6f,
          "mass-weighted center for unequal masses");
    const auto center = componentCenterOfMass(parts);
    const aura::math::Vec3 delta{2, -3, 7};
    a.body.position += delta; b.body.position += delta;
    check((componentCenterOfMass(parts) - center - delta).length() < 1e-6f, "center translates with component");
    const std::vector<BodyPart3D *> empty;
    check(componentMass(empty) == 0.0f, "empty mass is zero");
    bool rejected = false;
    try { componentCenterOfMass(empty); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "empty center is undefined");
    b.body.mass = 0.0f;
    rejected = false;
    try { componentMass(parts); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "reject zero dynamic mass before dividing");
    auto body = createAuraBody3D();
    const auto skeleton = createAuraSkeleton3D(body);
    const auto knee = skeleton.collectComponent(body, body.rightShin, skeleton.rightKnee);
    const auto hip = skeleton.collectComponent(body, body.rightThigh, skeleton.rightHip);
    check(componentMass(knee) == 2.0f && componentMass(hip) == 3.0f, "graph knee and hip masses include descendants");
    check((componentCenterOfMass(knee) - (body.rightShin.body.position + body.rightFoot.body.position) * 0.5f).length() < 1e-6f,
          "graph component center uses both shin and foot");
    a.body.mass = 2; b.body.mass = 3;
    a.body.position = {-3, 0, 0}; b.body.position = {2, 0, 0};
    a.body.momentOfInertia = {2, 4, 6}; b.body.momentOfInertia = {3, 5, 7};
    check(std::abs(componentMomentOfInertiaAboutAxis(parts, {}, {0, 1, 0}) - 39.0f) < 1e-5f,
          "intrinsic inertia plus mass times squared perpendicular distance");
    check(std::abs(componentMomentOfInertiaAboutAxis(parts, {}, {2, 0, 0}) - 5.0f) < 1e-5f,
          "normalize axis and exclude displacement parallel to axis");
    a.body.orientation = aura::math::Quaternion::fromAxisAngle({0, 0, 1}, 1.57079632679f);
    const std::vector<BodyPart3D *> single{&a};
    check(std::abs(componentMomentOfInertiaAboutAxis(single, a.body.position, {1, 0, 0}) - 4.0f) < 1e-5f,
          "world axis transformed into local principal inertia frame");
    const auto inertiaBefore = componentMomentOfInertiaAboutAxis(parts, {}, {0, 1, 0});
    a.body.position += delta; b.body.position += delta;
    check(std::abs(componentMomentOfInertiaAboutAxis(parts, delta, {0, 1, 0}) - inertiaBefore) < 1e-5f,
          "translation of component and pivot preserves inertia");
    rejected = false;
    try { componentMomentOfInertiaAboutAxis(parts, {}, {}); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "reject zero rotation axis");
    // A paid-for rotation of a stationary component has energy L^2/(2 I),
    // including both intrinsic spin and the COM velocities of its members.
    const auto com = componentCenterOfMass(parts);
    const aura::math::Vec3 axis{0, 1, 0};
    const float aggregateI = componentMomentOfInertiaAboutAxis(parts, com, axis);
    constexpr float angularImpulse = 3.0f;
    const auto deltaOmega = axis * (angularImpulse / aggregateI);
    double rotationalEnergy = 0.0;
    aura::math::Vec3 momentum{};
    for (const auto *part : parts) {
        const auto v = deltaOmega.cross(part->body.position - com);
        const auto w = part->body.orientation.conjugate().rotate(deltaOmega);
        const auto &I = part->body.momentOfInertia;
        rotationalEnergy += 0.5 * part->body.mass * v.lengthSquared() +
                            0.5 * (I.x * w.x * w.x + I.y * w.y * w.y + I.z * w.z * w.z);
        momentum += v * part->body.mass;
    }
    check(std::abs(rotationalEnergy - angularImpulse * angularImpulse / (2.0 * aggregateI)) < 1e-5,
          "aggregate inertia pays for intrinsic spin and induced member translation");
    check(momentum.length() < 1e-5f, "rotation around mass center adds no linear momentum");
    a.body.mass = 2; b.body.mass = 3;
    a.body.velocity = {1, 2, 3}; b.body.velocity = {6, -3, -2};
    check((componentCenterOfMassVelocity(parts) - aura::math::Vec3{4, -1, 0}).length() < 1e-6f,
          "mass-weighted component velocity");
    a.body.mass = 1; b.body.mass = 1;
    a.body.position = {-1, 0, 0}; b.body.position = {1, 0, 0};
    a.body.velocity = {0, -2, 0}; b.body.velocity = {0, 2, 0};
    a.body.angularVelocity = {}; b.body.angularVelocity = {};
    check((componentAngularMomentum(parts) - aura::math::Vec3{0, 0, 4}).length() < 1e-5f,
          "orbital angular momentum about component COM");
    a.body.angularVelocity = {2, 0, 0}; // Rotated body's world X uses local Y inertia = 4.
    check((componentAngularMomentum(parts) - aura::math::Vec3{8, 0, 4}).length() < 1e-5f,
          "intrinsic angular momentum transformed from local to world frame");
    const auto initialL = componentAngularMomentum(parts);
    a.body.velocity += delta; b.body.velocity += delta;
    check((componentAngularMomentum(parts) - initialL).length() < 1e-5f,
          "common translation velocity leaves COM angular momentum unchanged");
    // Articulated initial motion: work of a common rotation is L dot deltaOmega,
    // regardless of whether individual bodies have matching angular velocities.
    const auto testCenter = componentCenterOfMass(parts);
    const auto testV = componentCenterOfMassVelocity(parts);
    const auto testAxis = aura::math::Vec3{0, 0, 1};
    const float testI = componentMomentOfInertiaAboutAxis(parts, testCenter, testAxis);
    const float omegaMode = componentAngularMomentum(parts).dot(testAxis) / testI;
    const aura::math::Vec3 anchor{1, 1, 0};
    const aura::math::Vec3 impulse{2, -2, 0}; // Angular impulse along Z.
    const auto dw = testAxis * ((anchor - testCenter).cross(impulse).dot(testAxis) / testI);
    const auto dv = impulse * (1.0f / componentMass(parts));
    double actualWork = 0;
    for (const auto *part : parts) {
        const auto localW = part->body.orientation.conjugate().rotate(part->body.angularVelocity);
        const auto localDw = part->body.orientation.conjugate().rotate(dw);
        const auto &I = part->body.momentOfInertia;
        actualWork += part->body.mass * part->body.velocity.dot(dv + dw.cross(part->body.position - testCenter)) +
                      I.x * localW.x * localDw.x + I.y * localW.y * localDw.y + I.z * localW.z * localDw.z;
    }
    const auto modeAnchorV = testV + (testAxis * omegaMode).cross(anchor - testCenter);
    check(std::abs(actualWork - modeAnchorV.dot(impulse)) < 1e-5,
          "component anchor mode predicts work on articulated velocities");
    return passed ? 0 : 1;
}
