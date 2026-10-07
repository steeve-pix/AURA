#include <algorithm>
#include <cmath>
#include <iostream>

#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"

using namespace aura;
using namespace aura::body;
namespace {
    auto gap(const BodyPart3D &a, const BodyPart3D &b, const Joint3D &joint) {
        return localToWorldPoint(b, joint.localAnchorB) - localToWorldPoint(a, joint.localAnchorA);
    }
    auto relativeVelocity(const BodyPart3D &a, const BodyPart3D &b, const Joint3D &joint) {
        return physics::velocityAtWorldPoint(b.body, localToWorldPoint(b, joint.localAnchorB)) -
               physics::velocityAtWorldPoint(a.body, localToWorldPoint(a, joint.localAnchorA));
    }
    void assemble(BodyPart3D &a, BodyPart3D &b, const Joint3D &joint, float angle) {
        b.body.orientation = a.body.orientation * math::Quaternion::fromAxisAngle({1, 0, 0}, angle);
        b.body.position = localToWorldPoint(a, joint.localAnchorA) - b.body.orientation.rotate(joint.localAnchorB);
    }
    bool sameState(const BodyPart3D &a, const BodyPart3D &b) {
        constexpr float tolerance = 2e-5f;
        return (a.body.position - b.body.position).length() < tolerance &&
            (a.body.velocity - b.body.velocity).length() < tolerance &&
            (a.body.angularVelocity - b.body.angularVelocity).length() < tolerance &&
            std::abs(a.body.orientation.w - b.body.orientation.w) < tolerance &&
            std::abs(a.body.orientation.x - b.body.orientation.x) < tolerance &&
            std::abs(a.body.orientation.y - b.body.orientation.y) < tolerance &&
            std::abs(a.body.orientation.z - b.body.orientation.z) < tolerance;
    }
}
int main() {
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    constexpr float tolerance = 2e-5f;
    for (bool knee : {false, true}) for (bool tilted : {false, true}) for (int mode : {-1, 0, 1}) {
        auto body = createAuraBody3D();
        const auto skeleton = createAuraSkeleton3D(body);
        auto &parent = knee ? body.leftThigh : body.leftUpperArm;
        auto &root = knee ? body.leftShin : body.leftForearm;
        auto &leaf = knee ? body.leftFoot : body.leftHand;
        const auto &joint = knee ? skeleton.leftKnee : skeleton.leftElbow;
        const auto &descendant = knee ? skeleton.leftAnkle : skeleton.leftWrist;
        const auto branch = knee ? MajorBodyBranch3D::LeftKnee : MajorBodyBranch3D::LeftElbow;
        const float angle = mode < 0 ? joint.minAngle - 0.3f : mode > 0 ? joint.maxAngle + 0.3f : 0.6f;
        parent.body.position.y += 10.0f;
        if (tilted) parent.body.orientation = math::Quaternion::fromAxisAngle({1, 2, 3}, 0.7f);
        assemble(parent, root, joint, angle);
        root.body.position += math::Vec3{0.003f, -0.002f, 0.001f};
        assemble(root, leaf, descendant, -0.2f);
        root.body.velocity = {0.1f, 0.2f, 0.3f};
        leaf.body.velocity = {-0.2f, 0.4f, 0.1f};
        root.body.angularVelocity = {0.2f, 0.3f, 0.4f};
        leaf.body.angularVelocity = {0.25f, 0.28f, 0.41f};
        // Copy the identical fixture to the proven right branch for equivalence.
        auto reference = body;
        auto &rightParent = knee ? reference.rightThigh : reference.rightUpperArm;
        auto &rightRoot = knee ? reference.rightShin : reference.rightForearm;
        auto &rightLeaf = knee ? reference.rightFoot : reference.rightHand;
        rightParent = parent; rightRoot = root; rightLeaf = leaf;
        const auto &rightJoint = knee ? skeleton.rightKnee : skeleton.rightElbow;
        const auto &rightDescendant = knee ? skeleton.rightAnkle : skeleton.rightWrist;
        const auto rightBranch = knee ? MajorBodyBranch3D::RightKnee : MajorBodyBranch3D::RightElbow;
        const auto initialGap = gap(root, leaf, descendant).length();
        const auto initialAngle = relativeJointAngle(root, leaf, descendant);
        const auto oldRoot = root.body; const auto oldLeaf = leaf.body;
        const auto initialOmega = leaf.body.angularVelocity - root.body.angularVelocity;
        const auto solveRight = [&] {
            if (knee) correctRightKneeAngleAroundPivot(reference, skeleton, rightJoint, rightDescendant);
            else correctRightElbowAngleAroundPivot(reference, skeleton, rightJoint, rightDescendant);
            correctBranchPositionAsSubtree(reference, skeleton, rightJoint, rightBranch);
            solveBranchSubtreeVelocityConstraints(reference, skeleton, rightJoint, rightBranch);
        };
        correctLimbAngleAroundPivot(body, skeleton, joint, descendant, branch);
        check(std::abs(relativeJointAngle(parent, root, joint) - std::clamp(angle, joint.minAngle, joint.maxAngle)) < tolerance,
              "left limb enforces both angular limits with tilted parent");
        check((root.body.velocity-oldRoot.velocity).lengthSquared()==0 &&
              (leaf.body.velocity-oldLeaf.velocity).lengthSquared()==0 &&
              (root.body.angularVelocity-oldRoot.angularVelocity).lengthSquared()==0 &&
              (leaf.body.angularVelocity-oldLeaf.angularVelocity).lengthSquared()==0,
              "left limb projection leaves linear/angular velocities exactly unchanged");
        const auto initialV = relativeVelocity(root, leaf, descendant);
        correctBranchPositionAsSubtree(body, skeleton, joint, branch);
        solveBranchSubtreeVelocityConstraints(body, skeleton, joint, branch);
        check(gap(parent, root, joint).length() < tolerance &&
              std::abs(gap(root, leaf, descendant).length() - initialGap) < tolerance,
              "left limb closes parent joint without changing descendant gap");
        check(std::abs(relativeJointAngle(root, leaf, descendant) - initialAngle) < tolerance,
              "left limb preserves descendant relative angle");
        check((relativeVelocity(root, leaf, descendant) - initialV).length() < tolerance &&
              (leaf.body.angularVelocity - root.body.angularVelocity - initialOmega).length() < tolerance,
              "left limb preserves descendant relative linear and angular motion");
        if (knee) check(physics::lowestPoint(leaf.body, leaf.size).y >= -tolerance,
                        "airborne left knee repair remains above floor");
        solveRight();
        check(sameState(parent, rightParent) && sameState(root, rightRoot) && sameState(leaf, rightLeaf),
              "left geometry and velocity solve matches proven right solve");
    }
    for (float errorY : {-0.02f, 0.02f}) {
        auto body = createAuraBody3D();
        const auto skeleton = createAuraSkeleton3D(body);
        body.leftThigh.body.position.y -= errorY;
        const auto beforeGap = gap(body.leftShin, body.leftFoot, skeleton.leftAnkle);
        const auto beforeAngle = relativeJointAngle(body.leftShin, body.leftFoot, skeleton.leftAnkle);
        correctBranchPositionAsSubtree(body, skeleton, skeleton.leftKnee, MajorBodyBranch3D::LeftKnee);
        check(gap(body.leftThigh, body.leftShin, skeleton.leftKnee).length() < tolerance &&
              (gap(body.leftShin, body.leftFoot, skeleton.leftAnkle) - beforeGap).length() < tolerance &&
              std::abs(relativeJointAngle(body.leftShin, body.leftFoot, skeleton.leftAnkle) - beforeAngle) < tolerance,
              "contact-aware left knee translation preserves ankle and repairs knee");
        check(physics::lowestPoint(body.leftFoot.body, body.leftFoot.size).y >= -tolerance,
              "left knee translation never pushes planted foot through floor");
    }
    auto body = createAuraBody3D();
    const auto skeleton = createAuraSkeleton3D(body);
    assemble(body.leftThigh, body.leftShin, skeleton.leftKnee, -0.5f);
    assemble(body.leftShin, body.leftFoot, skeleton.leftAnkle, 0.0f);
    const float lowest = physics::lowestPoint(body.leftFoot.body, body.leftFoot.size).y;
    body.leftThigh.body.position.y -= lowest;
    body.leftShin.body.position.y -= lowest;
    body.leftFoot.body.position.y -= lowest;
    const auto hipGap = gap(body.pelvis, body.leftThigh, skeleton.leftHip);
    const auto footVelocity = body.leftFoot.body.velocity;
    const auto shinVelocity = body.leftShin.body.velocity;
    correctLimbAngleAroundPivot(body, skeleton, skeleton.leftKnee, skeleton.leftAnkle, MajorBodyBranch3D::LeftKnee);
    check(physics::lowestPoint(body.leftFoot.body, body.leftFoot.size).y >= -tolerance &&
          gap(body.leftShin, body.leftFoot, skeleton.leftAnkle).length() < tolerance &&
          gap(body.leftThigh, body.leftShin, skeleton.leftKnee).length() < tolerance &&
          (gap(body.pelvis, body.leftThigh, skeleton.leftHip) - hipGap).length() < tolerance &&
          (body.leftFoot.body.velocity-footVelocity).lengthSquared()==0 &&
          (body.leftShin.body.velocity-shinVelocity).lengthSquared()==0,
          "left knee floor projection preserves attachments and body velocities");
    return passed ? 0 : 1;
}
