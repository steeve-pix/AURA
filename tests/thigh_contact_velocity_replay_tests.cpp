#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include "HipCapsuleRotationReplay.hpp"
#include "aura/body/ThighSelfCollision.hpp"
#include "aura/physics/Impulse.hpp"
#include "aura/physics/Inertia.hpp"
#include "aura/physics/MechanicalState.hpp"

using namespace aura;

namespace {
    struct Contact {
        math::Vec3 leftPoint, rightPoint, normal;
        float normalSpeed{}, inverseMass{}, penetration{};
    };

    Contact contact(const body::AuraBody3D &b) {
        const auto l = physics::bodyPartCollisionCapsule(b.leftThigh), r = physics::bodyPartCollisionCapsule(
            b.rightThigh);
        const auto closest = physics::closestPointsBetweenSegments(l.pointA, l.pointB, r.pointA, r.pointB);
        const auto delta = closest.pointB - closest.pointA;
        if (delta.lengthSquared() < 1e-12f)throw std::runtime_error("Undefined capsule collision normal");
        Contact c;
        c.leftPoint = closest.pointA;
        c.rightPoint = closest.pointB;
        c.normal = delta.normalized();
        c.penetration = physics::capsulePenetration(l, r);
        c.normalSpeed = (physics::velocityAtWorldPoint(b.rightThigh.body, c.rightPoint) -
                         physics::velocityAtWorldPoint(b.leftThigh.body, c.leftPoint)).dot(c.normal);
        const auto rotational = [&](const auto &rb, const auto &point) {
            const auto offset = point - rb.position;
            return physics::applyInverseInertiaWorld(rb, offset.cross(c.normal)).cross(offset).dot(c.normal);
        };
        c.inverseMass = 1.0f / b.leftThigh.body.mass + 1.0f / b.rightThigh.body.mass +
                        rotational(b.leftThigh.body, c.leftPoint) + rotational(b.rightThigh.body, c.rightPoint);
        return c;
    }

    struct State {
        double kinetic{}, px{}, py{}, pz{};
        Contact collision;
        test::HipRotationMetrics leg;
    };

    State state(body::AuraBody3D b, const body::AuraSkeleton3D &s) {
        State m;
        m.collision = contact(b);
        m.leg = test::hipRotationMetrics(b, s);
        auto parts = s.collectComponent(b, b.torso, s.head);
        parts.push_back(&b.head);
        for (const auto *p: parts) {
            const auto mechanical = physics::mechanicalState(p->body);
            m.kinetic += mechanical.linearKinetic + mechanical.rotationalKinetic;
            m.px += double(p->body.mass) * p->body.velocity.x;
            m.py += double(p->body.mass) * p->body.velocity.y;
            m.pz += double(p->body.mass) * p->body.velocity.z;
        }
        return m;
    }

    // Copied-state experiment only. No geometry, restitution, friction,
    // descendant response propagation or live solver integration.
    float impulse(body::AuraBody3D &b, const Contact &c) {
        if (c.penetration <= 0 || c.normalSpeed >= 0)return 0;
        const float magnitude = -c.normalSpeed / c.inverseMass;
        physics::applyImpulseAtPoint(b.leftThigh.body, -c.normal * magnitude, c.leftPoint);
        physics::applyImpulseAtPoint(b.rightThigh.body, c.normal * magnitude, c.rightPoint);
        return magnitude;
    }
}

int main(int argc, char **argv) {
    if (argc != 2)return 2;
    auto input = body::createAuraBody3D();
    const auto skeleton = body::createAuraSkeleton3D(input);
    auto parts = skeleton.collectComponent(input, input.torso, skeleton.head);
    parts.push_back(&input.head);
    std::ifstream file(argv[1]);
    std::string line;
    std::set<std::string> loaded;
    double time = 0;
    float recordedPen = 0, recordedSpeed = 0;
    while (std::getline(file, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::string tag, name;
        row >> tag;
        if (tag == "thighContactSnapshot") {
            row >> time >> recordedPen >> recordedSpeed;
            if (!row)return 2;
            continue;
        }
        if (tag != "thighContactInput")return 2;
        row >> name;
        const auto it = std::find_if(parts.begin(), parts.end(), [&](auto *p) { return p->name == name; });
        if (it == parts.end() || !loaded.insert(name).second)return 2;
        auto &p = **it;
        auto &b = p.body;
        row >> p.size.x >> p.size.y >> p.size.z >> b.mass >> b.position.x >> b.position.y >> b.position.z
                >> b.orientation.w >> b.orientation.x >> b.orientation.y >> b.orientation.z
                >> b.velocity.x >> b.velocity.y >> b.velocity.z >> b.angularVelocity.x >> b.angularVelocity.y >> b.
                angularVelocity.z
                >> b.momentOfInertia.x >> b.momentOfInertia.y >> b.momentOfInertia.z;
        if (!row)return 2;
    }
    if (loaded.size() != 16)return 2;
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "FAIL " << message << '\n';
            passed = false;
        }
    };
    const auto before = state(input, skeleton);
    check(before.collision.penetration == recordedPen && before.collision.normalSpeed == recordedSpeed,
          "saved contact measurements reproduce exactly");
    check(before.collision.normalSpeed < 0, "worst overlapping thighs are closing");
    auto projected = input;
    body::ThighSwingCollisionAudit poseAudit;
    const auto pose = body::correctThighOverlapWithSwing(projected, skeleton, 0.01f, 0.001f, &poseAudit, false, true);
    const auto afterPose = state(projected, skeleton);
    auto projectedParts = skeleton.collectComponent(projected, projected.torso, skeleton.head);
    projectedParts.push_back(&projected.head);
    for (size_t i = 0; i < parts.size(); ++i)
        check((projectedParts[i]->body.velocity - parts[i]->body.velocity).lengthSquared() == 0 &&
              (projectedParts[i]->body.angularVelocity - parts[i]->body.angularVelocity).lengthSquared() == 0,
              "pose correction preserves velocities exactly");
    check(afterPose.collision.penetration <= before.collision.penetration,
          "legal pose attempt does not worsen thigh overlap");
    check(afterPose.collision.normalSpeed < 0 && afterPose.collision.inverseMass > 0,
          "projected contact permits a closing-speed impulse");
    auto resolved = projected;
    const auto currentContact = afterPose.collision;
    auto reference = projected;
    const float referenceMagnitude = impulse(reference, currentContact);
    const auto response = body::resolveThighNormalVelocity(resolved);
    const float magnitude = response.impulseMagnitude;
    check(response.applied && magnitude == referenceMagnitude, "production impulse matches audited copied response");
    check((resolved.leftThigh.body.velocity - reference.leftThigh.body.velocity).lengthSquared() == 0 &&
          (resolved.rightThigh.body.velocity - reference.rightThigh.body.velocity).lengthSquared() == 0 &&
          (resolved.leftThigh.body.angularVelocity - reference.leftThigh.body.angularVelocity).lengthSquared() == 0 &&
          (resolved.rightThigh.body.angularVelocity - reference.rightThigh.body.angularVelocity).lengthSquared() == 0,
          "production endpoint response equals reference exactly");
    const auto afterImpulse = state(resolved, skeleton);
    const double predicted = currentContact.normalSpeed * magnitude + 0.5 * currentContact.inverseMass *
                             double(magnitude) * magnitude;
    const double measured = afterImpulse.kinetic - afterPose.kinetic;
    std::cout << std::scientific << std::setprecision(9)
            << "thighContactReplay,time=" << time << ",poseAccepted=" << pose.corrected << ",poseFloorBlocks=" <<
            poseAudit.blockedByFloor
            << ",poseSwingBlocks=" << poseAudit.blockedBySwingLimit << ",impulse=" << magnitude <<
            ",inverseEffectiveMass=" << currentContact.inverseMass
            << ",predictedWork=" << predicted << ",measuredDeltaKE=" << measured << ",workResidual=" << measured -
            predicted << '\n';
    const auto &c = currentContact;
    std::cout << "contactGeometry,leftX,leftY,leftZ,rightX,rightY,rightZ,normalX,normalY,normalZ\ncontactGeometry," << c
            .leftPoint.x << ',' << c.leftPoint.y << ',' << c.leftPoint.z
            << ',' << c.rightPoint.x << ',' << c.rightPoint.y << ',' << c.rightPoint.z << ',' << c.normal.x << ',' << c.
            normal.y << ',' << c.normal.z << '\n';
    std::cout << "measurement,before,afterPose,afterImpulse\n";
    const auto row = [&](const char *name, double a, double b, double c) {
        std::cout << name << ',' << a << ',' << b << ',' << c << '\n';
    };
    row("penetration", before.collision.penetration, afterPose.collision.penetration,
        afterImpulse.collision.penetration);
    row("normalSpeed", before.collision.normalSpeed, afterPose.collision.normalSpeed,
        afterImpulse.collision.normalSpeed);
    row("totalKE", before.kinetic, afterPose.kinetic, afterImpulse.kinetic);
    row("wholePx", before.px, afterPose.px, afterImpulse.px);
    row("wholePz", before.pz, afterPose.pz, afterImpulse.pz);
    const std::array names{"leftHipGap", "leftKneeGap", "leftAnkleGap", "rightHipGap", "rightKneeGap", "rightAnkleGap"};
    for (size_t i = 0; i < 6; ++i)row(names[i], before.leg.gaps[i], afterPose.leg.gaps[i], afterImpulse.leg.gaps[i]);
    row("leftFootY", before.leg.feetY[0], afterPose.leg.feetY[0], afterImpulse.leg.feetY[0]);
    row("rightFootY", before.leg.feetY[1], afterPose.leg.feetY[1], afterImpulse.leg.feetY[1]);
    check(std::abs(afterImpulse.collision.normalSpeed) < 1e-5f, "inelastic local impulse removes closing normal speed");
    check(std::abs(
              afterImpulse.collision.normalSpeed - (
                  currentContact.normalSpeed + currentContact.inverseMass * magnitude)) < 1e-5f,
          "effective-mass prediction agrees with actual contact-speed response");
    check(measured <= 1e-5 && std::abs(measured - predicted) < 1e-4, "impulse removes energy and work audit closes");
    check(std::abs(afterImpulse.px - afterPose.px) < 1e-5 &&
          std::abs(afterImpulse.py - afterPose.py) < 1e-5 && std::abs(afterImpulse.pz - afterPose.pz) < 1e-5,
          "whole-body linear momentum is conserved");
    auto finalParts = skeleton.collectComponent(resolved, resolved.torso, skeleton.head);
    finalParts.push_back(&resolved.head);
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto &a = projectedParts[i]->body;
        const auto &b = finalParts[i]->body;
        check((a.position - b.position).lengthSquared() == 0 && a.orientation.w == b.orientation.w && a.orientation.x ==
              b.orientation.x &&
              a.orientation.y == b.orientation.y && a.orientation.z == b.orientation.z,
              "collision impulse changes no pose");
        if (projectedParts[i] != &projected.leftThigh && projectedParts[i] != &projected.rightThigh)
            check((a.velocity - b.velocity).lengthSquared() == 0 && (a.angularVelocity - b.angularVelocity).
                  lengthSquared() == 0,
                  "no velocity response is copied to the other fourteen bodies");
    }
    check(afterImpulse.leg.gaps == afterPose.leg.gaps && afterImpulse.leg.feetY == afterPose.leg.feetY,
          "all leg gaps and foot geometry are exactly unchanged by the impulse");
    auto separating = projected;
    // Unilateral response: outward contact velocity must receive no impulse.
    separating.rightThigh.body.velocity += currentContact.normal * (-2.0f * currentContact.normalSpeed);
    const auto outward = contact(separating);
    const auto outwardBefore = state(separating, skeleton);
    check(outward.normalSpeed > 0 && !body::resolveThighNormalVelocity(separating).applied &&
          state(separating, skeleton).kinetic == outwardBefore.kinetic,
          "already-separating thighs receive no collision impulse");
    auto belowTolerance = projected;
    check(!body::resolveThighNormalVelocity(belowTolerance, 1.0f).applied &&
          state(belowTolerance, skeleton).kinetic == afterPose.kinetic, "overlap below tolerance receives no impulse");
    auto coincident = projected;
    coincident.rightThigh.body.position = coincident.leftThigh.body.position;
    coincident.rightThigh.body.orientation = coincident.leftThigh.body.orientation;
    const auto degenerate = body::resolveThighNormalVelocity(coincident);
    check(degenerate.degenerateNormal && !degenerate.applied,
          "undefined normal is reported without an arbitrary impulse");
    std::cout << "replayPassed=" << passed << '\n';
    return passed ? 0 : 1;
}
