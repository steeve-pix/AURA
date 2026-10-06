#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"

using namespace aura;
using namespace aura::body;
namespace {
    using PartMember = BodyPart3D AuraBody3D::*;
    using JointMember = Joint3D AuraSkeleton3D::*;
    struct Fixture {
        const char *name;
        PartMember parent;
        PartMember child;
        JointMember joint;
        std::vector<PartMember> legacyComponent;
    };
    auto allParts(AuraBody3D &b) {
        return std::array{&b.head, &b.neck, &b.torso, &b.pelvis,
            &b.leftUpperArm, &b.leftForearm, &b.leftHand,
            &b.rightUpperArm, &b.rightForearm, &b.rightHand,
            &b.leftThigh, &b.leftShin, &b.leftFoot,
            &b.rightThigh, &b.rightShin, &b.rightFoot};
    }
    auto relativeVelocity(const BodyPart3D &a, const BodyPart3D &b, const Joint3D &joint) {
        return physics::velocityAtWorldPoint(b.body, localToWorldPoint(b, joint.localAnchorB)) -
               physics::velocityAtWorldPoint(a.body, localToWorldPoint(a, joint.localAnchorA));
    }
    // Reference to the pre-refactor geometry and policies, intentionally retaining
    // explicit lists. This regression guards against a wrong graph cut or wrapper.
    void legacyCorrection(AuraBody3D &b, const AuraSkeleton3D &s, const Fixture &f) {
        auto &parent = b.*f.parent;
        auto &child = b.*f.child;
        const auto &joint = s.*f.joint;
        const bool elbow = f.joint == &AuraSkeleton3D::rightElbow;
        const bool knee = f.joint == &AuraSkeleton3D::rightKnee;
        const auto &descJoint = elbow ? s.rightWrist : s.rightAnkle;
        auto &leaf = elbow ? b.rightHand : b.rightFoot;
        const auto beforeVelocity = relativeVelocity(child, leaf, descJoint);
        const float error = jointAngleError(parent, child, joint);
        if (std::abs(error) >= 0.000001f) {
            const auto axis = parent.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();
            const auto pivot = localToWorldPoint(child, joint.localAnchorB);
            const auto rotation = math::Quaternion::fromAxisAngle(axis, -error);
            for (auto member : f.legacyComponent) rotateBodyAroundWorldPoint(b.*member, pivot, rotation);
            if (knee) {
                const float y = physics::lowestPoint(b.rightFoot.body, b.rightFoot.size).y;
                if (y < 0.0f) for (auto *part : allParts(b)) part->body.position.y -= y;
            }
        }
        if (elbow || knee) leaf.body.velocity += beforeVelocity - relativeVelocity(child, leaf, descJoint);
    }
    void migratedCorrection(AuraBody3D &b, const AuraSkeleton3D &s, const Fixture &f) {
        const auto &joint = s.*f.joint;
        if (f.joint == &AuraSkeleton3D::head) correctHeadAngleAroundPivot(b, s, joint);
        else if (f.joint == &AuraSkeleton3D::neck) correctNeckAngleAroundPivot(b, s, joint);
        else if (f.joint == &AuraSkeleton3D::leftShoulder) correctLeftShoulderAngleAroundPivot(b, s, joint);
        else if (f.joint == &AuraSkeleton3D::rightShoulder)
            correctBranchAngleAroundPivot(b, s, joint, MajorBodyBranch3D::RightShoulder);
        else if (f.joint == &AuraSkeleton3D::rightElbow) correctRightElbowAngleAroundPivot(b, s, joint, s.rightWrist);
        else correctRightKneeAngleAroundPivot(b, s, joint, s.rightAnkle);
    }
}

int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    const std::array<Fixture, 6> fixtures{{
        {"head", &AuraBody3D::neck, &AuraBody3D::head, &AuraSkeleton3D::head, {&AuraBody3D::head}},
        {"neck", &AuraBody3D::torso, &AuraBody3D::neck, &AuraSkeleton3D::neck, {&AuraBody3D::neck, &AuraBody3D::head}},
        {"left shoulder", &AuraBody3D::torso, &AuraBody3D::leftUpperArm, &AuraSkeleton3D::leftShoulder,
            {&AuraBody3D::leftUpperArm, &AuraBody3D::leftForearm, &AuraBody3D::leftHand}},
        {"right shoulder", &AuraBody3D::torso, &AuraBody3D::rightUpperArm, &AuraSkeleton3D::rightShoulder,
            {&AuraBody3D::rightUpperArm, &AuraBody3D::rightForearm, &AuraBody3D::rightHand}},
        {"right elbow", &AuraBody3D::rightUpperArm, &AuraBody3D::rightForearm, &AuraSkeleton3D::rightElbow,
            {&AuraBody3D::rightForearm, &AuraBody3D::rightHand}},
        {"right knee", &AuraBody3D::rightThigh, &AuraBody3D::rightShin, &AuraSkeleton3D::rightKnee,
            {&AuraBody3D::rightShin, &AuraBody3D::rightFoot}}
    }};
    auto body = createAuraBody3D();
    const auto skeleton = createAuraSkeleton3D(body);
    const std::array allJoints{&AuraSkeleton3D::waist, &AuraSkeleton3D::neck, &AuraSkeleton3D::head,
        &AuraSkeleton3D::leftShoulder, &AuraSkeleton3D::leftElbow, &AuraSkeleton3D::leftWrist,
        &AuraSkeleton3D::rightShoulder, &AuraSkeleton3D::rightElbow, &AuraSkeleton3D::rightWrist,
        &AuraSkeleton3D::leftHip, &AuraSkeleton3D::leftKnee, &AuraSkeleton3D::leftAnkle,
        &AuraSkeleton3D::rightHip, &AuraSkeleton3D::rightKnee, &AuraSkeleton3D::rightAnkle};
    for (auto member : allJoints) {
        const auto first = skeleton.collectComponent(body, body.head, skeleton.*member);
        const auto parts = allParts(body);
        const auto otherStart = std::find_if(parts.begin(), parts.end(), [&](auto *part) {
            return std::find(first.begin(), first.end(), part) == first.end();
        });
        check(otherStart != parts.end(), "every skeleton joint cuts the tree into two components");
        if (otherStart == parts.end()) continue;
        const auto second = skeleton.collectComponent(body, **otherStart, skeleton.*member);
        check(first.size() + second.size() == 16, "every cut covers the complete body");
        for (auto *part : first)
            check(std::find(second.begin(), second.end(), part) == second.end(), "every cut has disjoint sides");
    }
    for (const auto &f : fixtures) {
        const auto component = skeleton.collectComponent(body, body.*f.child, skeleton.*f.joint);
        check(component.size() == f.legacyComponent.size(), "child component has expected size");
        for (auto member : f.legacyComponent)
            check(std::count(component.begin(), component.end(), &(body.*member)) == 1, "component includes each descendant once");
        const auto other = skeleton.collectComponent(body, body.*f.parent, skeleton.*f.joint);
        check(other.size() + component.size() == 16, "cut components cover all 16 parts");
        for (auto *part : component)
            check(std::find(other.begin(), other.end(), part) == other.end(), "cut components are disjoint");
    }
    auto copiedBody = body;
    const auto copiedSkeleton = skeleton;
    const auto copiedArm = copiedSkeleton.collectComponent(copiedBody, copiedBody.rightUpperArm, copiedSkeleton.rightShoulder);
    check(copiedArm.size() == 3 && copiedArm.front() == &copiedBody.rightUpperArm &&
          std::find(copiedArm.begin(), copiedArm.end(), &body.rightHand) == copiedArm.end(), "copies never retain original body pointers");
    bool rejected = false;
    try { skeleton.collectComponent(body, body.head, copiedSkeleton.head); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "reject foreign cut joint");
    rejected = false;
    try { skeleton.collectComponent(body, copiedBody.head, skeleton.head); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "reject foreign starting body");

    // Both limits, an in-range no-op, tilted parents, existing anchor gaps,
    // nonzero velocities, and low fixtures exercising the knee floor lift.
    for (const auto &f : fixtures) for (bool low : {false, true}) for (bool tilted : {false, true})
        for (int mode : {-1, 0, 1}) {
            auto initial = createAuraBody3D();
            const auto s = createAuraSkeleton3D(initial);
            auto &parent = initial.*f.parent;
            auto &child = initial.*f.child;
            const auto &joint = s.*f.joint;
            for (auto *part : allParts(initial)) {
                part->body.position.y += low ? 0.0f : 10.0f;
                part->body.velocity = {0.1f, -0.3f, 0.2f};
                part->body.angularVelocity = {0.4f, 0.2f, -0.3f};
            }
            if (tilted) parent.body.orientation = math::Quaternion::fromAxisAngle({1, 2, 3}, 0.7f);
            const float angle = mode < 0 ? joint.minAngle - 0.2f :
                                mode > 0 ? joint.maxAngle + 0.2f : (joint.minAngle + joint.maxAngle) * 0.5f;
            child.body.orientation = parent.body.orientation * math::Quaternion::fromAxisAngle({1, 0, 0}, angle);
            child.body.position = localToWorldPoint(parent, joint.localAnchorA) -
                child.body.orientation.rotate(joint.localAnchorB) + math::Vec3{0.002f, -0.003f, 0.001f};
            child.body.velocity = {-0.2f, 0.6f, 0.7f};
            child.body.angularVelocity = {-0.1f, 0.5f, 0.8f};
            auto reference = initial;
            auto migrated = initial;
            legacyCorrection(reference, s, f);
            migratedCorrection(migrated, s, f);
            const auto oldParts = allParts(reference);
            const auto newParts = allParts(migrated);
            for (std::size_t i = 0; i < oldParts.size(); ++i) {
                const auto &a = oldParts[i]->body;
                const auto &b = newParts[i]->body;
                check((a.position - b.position).length() < 1e-6f &&
                      (a.velocity - b.velocity).length() < 1e-6f &&
                      (a.angularVelocity - b.angularVelocity).length() < 1e-6f &&
                      std::abs(a.orientation.w - b.orientation.w) < 1e-6f &&
                      std::abs(a.orientation.x - b.orientation.x) < 1e-6f &&
                      std::abs(a.orientation.y - b.orientation.y) < 1e-6f &&
                      std::abs(a.orientation.z - b.orientation.z) < 1e-6f,
                      f.name);
            }
        }
    return passed ? 0 : 1;
}
