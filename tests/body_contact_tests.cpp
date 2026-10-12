#include "aura/body/BodySelfCollision.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Inertia.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <set>

using namespace aura;

namespace {
    void check(bool value, const char *message) { if (!value)throw std::runtime_error(message); }

    physics::RigidBody3D rigid(math::Vec3 position, math::Vec3 size, float mass = 1) {
        physics::RigidBody3D result;
        result.position = position;
        result.mass = mass;
        result.momentOfInertia = physics::boxMomentOfInertia(mass, size);
        return result;
    }

    void detection() {
        using Type = physics::CollisionShapeType;
        const math::Vec3 sphereSize{1, 1, 1}, limbSize{.4f, 2, .4f}, boxSize{1, 1, 1};
        for (auto ta: {Type::Sphere, Type::Capsule, Type::Box})
            for (auto tb: {Type::Sphere, Type::Capsule, Type::Box}) {
                const auto sa = ta == Type::Capsule ? limbSize : sphereSize, sb = tb == Type::Capsule
                    ? limbSize
                    : boxSize;
                auto a = rigid({0, 2, 0}, sa), b = rigid({.3f, 2.1f, .1f}, sb);
                a.orientation = math::Quaternion::fromAxisAngle({1, 1, 0}, .37f);
                b.orientation = math::Quaternion::fromAxisAngle({0, 1, 1}, -.51f);
                const auto ab = physics::detectBodyContact(a, sa, {ta}, b, sb, {tb});
                const auto ba = physics::detectBodyContact(b, sb, {tb}, a, sa, {ta});
                check(ab.overlapping && ab.penetration > 0, "all mixed shapes detect overlap");
                check(std::abs(ab.normal.length() - 1) < 1e-5f, "contact normal unit length");
                check(std::abs(ab.penetration - ba.penetration) < 1e-5f && (ab.normal + ba.normal).length() < 1e-5f,
                      "swap symmetry");
                b.position = {10, 2, 0};
                check(!physics::detectBodyContact(a, sa, {ta}, b, sb, {tb}).overlapping, "separated mixed shapes");
            }
        auto box = rigid({0, 2, 0}, boxSize), sphere = rigid({.8f, 2, 0}, sphereSize);
        check(std::abs(
                  physics::detectBodyContact(sphere, sphereSize, {Type::Sphere}, box, boxSize, {Type::Box}).penetration
                  - .2f) < 1e-6f, "sphere/face depth");
        sphere.position = {0, 2, 0};
        const auto inside = physics::detectBodyContact(sphere, sphereSize, {Type::Sphere}, box, boxSize, {Type::Box});
        check(inside.overlapping && std::abs(inside.penetration - 1) < 1e-6f, "contained sphere escape depth");
        auto limb = rigid({.6f, 2, 0}, limbSize);
        check(
            std::abs(
                physics::detectBodyContact(limb, limbSize, {Type::Capsule}, box, boxSize, {Type::Box}).penetration -
                .1f) < 1e-6f, "segment/box closest distance");
        limb.position = {0, 2, 0};
        limb.orientation = math::Quaternion::fromAxisAngle({0, 0, 1}, 1.57079632679f);
        check(physics::detectBodyContact(limb, limbSize, {Type::Capsule}, box, boxSize, {Type::Box}).penetration > .69f,
              "crossing segment box containment");
        auto box2 = box;
        box2.position.x = .8f;
        check(std::abs(
                  physics::detectBodyContact(box, boxSize, {Type::Box}, box2, boxSize, {Type::Box}).penetration - .2f) <
              1e-6f, "box SAT depth");
    }

    void impulse() {
        auto a = rigid({0, 2, 0}, {.5f, 1.5f, .5f}, 2), b = rigid({.3f, 2, .1f}, {1, 1, 1}, 3);
        a.orientation = math::Quaternion::fromAxisAngle({1, 2, 3}, .8f);
        b.orientation = math::Quaternion::fromAxisAngle({2, 1, 0}, -.5f);
        auto c = physics::detectBodyContact(a, {.5f, 1.5f, .5f}, {physics::CollisionShapeType::Capsule},
                                            b, {1, 1, 1}, {physics::CollisionShapeType::Box});
        check(c.overlapping, "impulse input contact");
        a.velocity = c.normal * 2;
        b.velocity = -c.normal;
        const auto oldA = a, oldB = b;
        const auto result = physics::resolveBodyContactVelocity(a, b, c);
        check(result.applied && std::abs(result.normalSpeedAfter) < 2e-6f, "normal closing speed removed");
        check(result.energyChange <= 1e-5 && result.momentumChange.length() < 1e-6f, "impulse energy/linear momentum");
        const auto angularMomentum = [](const auto &body) {
            const auto q = body.orientation.normalized();
            const auto w = q.conjugate().rotate(body.angularVelocity);
            const auto &I = body.momentOfInertia;
            return body.position.cross(body.velocity * body.mass) + q.rotate({I.x * w.x, I.y * w.y, I.z * w.z});
        };
        const auto delta = angularMomentum(a) + angularMomentum(b) - angularMomentum(oldA) - angularMomentum(oldB);
        check(delta.length() < 2e-6f, "common-point impulse angular momentum");
        check((a.position - oldA.position).lengthSquared() == 0 && (b.position - oldB.position).lengthSquared() == 0,
              "impulse leaves pose unchanged");
        check(!physics::resolveBodyContactVelocity(a, b, c).applied || std::abs(result.normalSpeedAfter) < 2e-6f,
              "resting response bounded");
    }

    void shallowBodyContact() {
        auto b = body::createAuraBody3D();
        const auto s = body::createAuraSkeleton3D(b);
        b.leftHand.body.position = {-.90975f, 6, 0};
        b.leftHand.body.velocity = {2, 0, 0};
        const auto pairs = body::bodyCollisionPairs(b, s, 1);
        const auto depth = body::bodyCollisionMetrics(b, pairs).maximumPenetration;
        check(depth > 0 && depth < .001f, "shallow contact is inside positional acceptance slop");
        // Velocity-only: a valid shallow contact still needs a physical response.
        const auto result = body::solveBodySelfCollisions(b, s, pairs, {}, 0);
        check(result.impulses > 0 && result.maximumPositiveEnergy <= 1e-5,
              "shallow closing contact receives dissipative impulse");
        check(result.maximumMomentumError < 1e-6, "shallow contact preserves momentum");
    }

    void hierarchy() {
        auto b = body::createAuraBody3D();
        const auto s = body::createAuraSkeleton3D(b);
        const auto p1 = body::bodyCollisionPairs(b, s, 1), p2 = body::bodyCollisionPairs(b, s, 2), p3 =
                body::bodyCollisionPairs(b, s, 3);
        check(p1.size() == 10 && p2.size() == 57 && p3.size() == 105, "cumulative pair batches");
        const auto parts = body::collisionParts(b);
        for (const auto &p: p3)check(!s.areDirectlyConnected(b, *parts[p.a], *parts[p.b]), "adjacent pairs excluded");
        check(body::bodyCollisionMetrics(b, p3).maximumPenetration < 1e-6f, "neutral mixed shapes compatible");
        // Shoulder neighbors are filtered out of runtime collision pairs, but
        // their solid volumes still need clearance in the reference pose.
        for (const auto* arm : {&b.leftUpperArm, &b.rightUpperArm}) {
            const auto contact = physics::detectBodyContact(
                b.torso.body, b.torso.size, b.torso.collisionShape,
                arm->body, arm->size, arm->collisionShape);
            check(contact.penetration <= 1e-6f, "neutral upper arm must not intersect torso");
            const float clearance = std::abs(arm->body.position.x - b.torso.body.position.x)
                - 0.5f * (b.torso.size.x + arm->size.x);
            check(std::abs(clearance - 0.001f) < 1e-6f, "neutral shoulder has barely touching clearance");
        }
        for (const auto& edge : s.connections(b))
            check((body::localToWorldPoint(*edge.a, edge.joint->localAnchorA) -
                   body::localToWorldPoint(*edge.b, edge.joint->localAnchorB)).length() < 1e-6f,
                  "reference pose keeps every joint attached");
        // Deliberately fold the left arm into the core without detaching any anchors.
        const auto arm = s.collectComponent(b, b.leftUpperArm, s.leftShoulder);
        body::rotateSubtreeAroundWorldPoint(arm, body::localToWorldPoint(b.torso, s.leftShoulder.localAnchorA),
                                            math::Quaternion::fromAxisAngle({0, 0, 1}, .5f));
        const auto before = b;
        const auto initial = body::bodyCollisionMetrics(b, p1).squaredError;
        check(initial > 0, "arm/core fixture overlaps");
        for (int i = 0; i < 12; ++i)body::solveBodySelfCollisions(b, s, p1, {}, 4);
        check(body::bodyCollisionMetrics(b, p1).squaredError < initial,
              "articulated search improves arm/core collision");
        const auto oldParts = body::collisionParts(before);
        const auto nowParts = body::collisionParts(b);
        for (size_t i = 0; i < 16; ++i) {
            check((oldParts[i]->body.velocity - nowParts[i]->body.velocity).lengthSquared() == 0 &&
                  (oldParts[i]->body.angularVelocity - nowParts[i]->body.angularVelocity).lengthSquared() == 0,
                  "geometry no velocity edits");
            check(physics::lowestPoint(nowParts[i]->body, nowParts[i]->size).y >= -1e-6f, "geometry floor valid");
        }
        for (const auto &e: s.connections(b))
            check((body::localToWorldPoint(*e.a, e.joint->localAnchorA) -
                   body::localToWorldPoint(*e.b, e.joint->localAnchorB)).length() < 3e-6f, "all anchors preserved");
    }

    void foldedNeck() {
        auto b = body::createAuraBody3D();
        auto s = body::createAuraSkeleton3D(b);
        body::configureBodyCollisionJoints(s);
        const auto branch = s.collectComponent(b, b.neck, s.neck);
        body::rotateSubtreeAroundWorldPoint(branch, body::localToWorldPoint(b.torso, s.neck.localAnchorA),
                                            math::Quaternion::fromAxisAngle({0, 0, 1}, 2.0f));
        const auto pairs = body::bodyCollisionPairs(b, s, 3);
        check(body::bodyCollisionMetrics(b, pairs).maximumPenetration > .5f, "folded neck reproduces deep overlap");
        const auto headRelative = (b.neck.body.orientation.conjugate() * b.head.body.orientation).normalized();
        b.head.body.velocity = {1, 2, 3};
        b.head.body.angularVelocity = {-2, 1, 4};
        check(body::projectBodyHingeAxis(b, s, b.torso, b.neck, s.neck), "folded hinge alignment is repaired");
        const auto afterRelative = (b.neck.body.orientation.conjugate() * b.head.body.orientation).normalized();
        check(body::jointSwingRotationVector(afterRelative * headRelative.conjugate()).length() < 1e-6f,
              "neck hinge projection preserves descendant head relative orientation");
        check((b.head.body.velocity - math::Vec3{1, 2, 3}).lengthSquared() == 0 &&
              (b.head.body.angularVelocity - math::Vec3{-2, 1, 4}).lengthSquared() == 0,
              "hinge projection changes pose only");
        b.head.body.velocity = {};
        b.head.body.angularVelocity = {};
        for (int i = 0; i < 32; ++i)body::solveBodySelfCollisions(b, s, pairs, {}, 4);
        const auto residual = body::bodyCollisionMetrics(b, pairs).maximumPenetration;
        std::cout << "Folded-neck residual=" << residual << '\n';
        check(residual <= .001f, "collision solver recovers off-axis folded neck");
    }

    void replay(const char *path) {
        auto b = body::createAuraBody3D();
        auto s = body::createAuraSkeleton3D(b);
        // These snapshots predate the neutral shoulder-clearance change. Their
        // poses retain the original torso-side shoulder mounts at X = +/-0.9.
        s.leftShoulder.localAnchorA.x = -0.9f;
        s.rightShoulder.localAnchorA.x = 0.9f;
        body::configureBodyCollisionJoints(s);
        const auto parts = body::collisionParts(b);
        std::set<std::string> loaded;
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line)) {
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream row(line);
            std::string tag, name;
            row >> tag >> name;
            if (tag != "bodyContactInput")continue;
            const auto it = std::find_if(parts.begin(), parts.end(), [&](auto *p) { return p->name == name; });
            check(it != parts.end() && loaded.insert(name).second, "fixture part identity");
            auto &p = **it;
            auto &rb = p.body;
            row >> p.size.x >> p.size.y >> p.size.z >> rb.mass >> rb.position.x >> rb.position.y >> rb.position.z
                    >> rb.orientation.w >> rb.orientation.x >> rb.orientation.y >> rb.orientation.z
                    >> rb.velocity.x >> rb.velocity.y >> rb.velocity.z
                    >> rb.angularVelocity.x >> rb.angularVelocity.y >> rb.angularVelocity.z
                    >> rb.momentOfInertia.x >> rb.momentOfInertia.y >> rb.momentOfInertia.z;
            check(bool(row), "fixture numeric fields");
            rb.velocity = {};
            rb.angularVelocity = {};
        }
        check(loaded.size() == 16, "complete body fixture");
        const auto pairs = body::bodyCollisionPairs(b, s, 3);
        const auto initial = body::bodyCollisionMetrics(b, pairs);
        std::cout << "Replay initial penetration=" << initial.maximumPenetration << '\n';
        for (unsigned index: {1u, 2u})
            for (float angle: {-.01f, .01f}) {
                auto copy = b;
                const auto edge = s.connections(copy)[index];
                const auto component = s.collectComponent(copy, *edge.b, *edge.joint);
                body::rotateSubtreeAroundWorldPoint(
                    component, body::localToWorldPoint(*edge.b, edge.joint->localAnchorB),
                    math::Quaternion::fromAxisAngle(edge.a->body.orientation.rotate(edge.joint->hingeAxis), angle));
                float low = 100;
                for (const auto *p: body::collisionParts(copy))low = std::min(
                                                                   low, physics::lowestPoint(p->body, p->size).y);
                std::cout << "head probe joint=" << edge.b->name << " angle=" << angle
                        << " penetration=" << body::bodyCollisionMetrics(copy, pairs).maximumPenetration << " minY=" <<
                        low << '\n';
            }
        for (const auto &edge: s.connections(b)) {
            const auto sw = body::relativeJointSwingAngles(*edge.a, *edge.b, *edge.joint);
            std::cout << edge.a->name << "/" << edge.b->name << " twist=" << body::relativeJointAngle(
                        *edge.a, *edge.b, *edge.joint)
                    << " swing=" << sw.y << ',' << sw.z << '\n';
        }
        for (int n = 0; n < 64; ++n) {
            const auto result = body::solveBodySelfCollisions(b, s, pairs, {}, 4);
            if (n == 0 || n == 3 || n == 15 || n == 63)
                std::cout << "replay iteration=" << n + 1 << " penetration=" << body::bodyCollisionMetrics(b, pairs).
                        maximumPenetration
                        << " accepted=" << result.acceptedProjections << " blocked=" << result.blockedProjections <<
                        '\n';
        }
        check(body::bodyCollisionMetrics(b, pairs).maximumPenetration <= .001f, "saved full-body collision resolves");
        for (const auto *p: body::collisionParts(b)) {
            check(p->body.velocity.lengthSquared() == 0 && p->body.angularVelocity.lengthSquared() == 0,
                  "replay projection leaves velocities exactly unchanged");
            check(physics::lowestPoint(p->body, p->size).y >= -1e-6f, "replay stays floor feasible");
        }
        for (const auto &edge: s.connections(b)) {
            check((body::localToWorldPoint(*edge.a, edge.joint->localAnchorA) -
                   body::localToWorldPoint(*edge.b, edge.joint->localAnchorB)).length() < 5e-6f,
                  "replay preserves anchors");
            if (edge.joint->enforceHingeAxis)
                check(body::jointSwingRotationVector(body::relativeJointSwing(*edge.a, *edge.b, *edge.joint)).length() <
                      1e-6f, "replay hinges retain axis alignment");
            const float twist = body::relativeJointAngle(*edge.a, *edge.b, *edge.joint);
            check(twist >= edge.joint->minAngle - 1e-5f && twist <= edge.joint->maxAngle + 1e-5f,
                  "replay twist limits respected");
            if (edge.joint->type == body::JointType::SwingTwist) {
                const auto sw = body::relativeJointSwingAngles(*edge.a, *edge.b, *edge.joint);
                check(sw.y >= edge.joint->minSwingY - 1e-5f && sw.y <= edge.joint->maxSwingY + 1e-5f &&
                      sw.z >= edge.joint->minSwingZ - 1e-5f && sw.z <= edge.joint->maxSwingZ + 1e-5f,
                      "replay hip swing limits respected");
            }
        }
    }
}

int main(int argc, char **argv) {
    try {
        if (argc == 2)replay(argv[1]);
        else {
            detection();
            impulse();
            shallowBodyContact();
            hierarchy();
            foldedNeck();
        }
        std::cout << "Mixed body contact tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
