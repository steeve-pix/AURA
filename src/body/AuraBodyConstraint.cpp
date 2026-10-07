#include "aura/body/AuraBodyConstraint.hpp"

#include "aura/body/JointGeometry.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"

#include <algorithm>
#include <cmath>
#include <array>

namespace aura::body {
    void resolveRightShinFloorWithFootTranslation(AuraBody3D &body, float dt) {
        const auto before = body.rightShin.body.position;
        physics::resolveFloorCollision(body.rightShin.body, body.rightShin.size, 0.0f, dt);
        const auto delta = body.rightShin.body.position - before;
        if (delta.y <= 0.0f) return;
        body.rightFoot.body.position += delta;
        // Normally delta is upward and the foot starts above the floor. If the
        // foot was already penetrating, lift both parts equally to protect it.
        const float footY = physics::lowestPoint(body.rightFoot.body, body.rightFoot.size).y;
        if (footY < 0.0f) {
            const std::array subtree{&body.rightShin, &body.rightFoot};
            translateSubtree(subtree, {0.0f, -footY, 0.0f});
        }
    }

    namespace {
        struct BranchParts {
            BodyPart3D *parent;
            std::vector<BodyPart3D *> subtree;
            BodyPart3D *foot;

            [[nodiscard]] std::span<BodyPart3D *const> childParts() const {
                return subtree;
            }
        };

        BranchParts branchParts(AuraBody3D &body, const AuraSkeleton3D &skeleton,
                                MajorBodyBranch3D branch) {
            // The branch identifies the graph edge. Physics may use a temporary
            // joint copy (e.g. a test with different limits) without changing topology.
            if (branch == MajorBodyBranch3D::RightShoulder)
                return {&body.torso, skeleton.collectComponent(body, body.rightUpperArm, skeleton.rightShoulder), nullptr};
            if (branch == MajorBodyBranch3D::RightElbow)
                return {&body.rightUpperArm, skeleton.collectComponent(body, body.rightForearm, skeleton.rightElbow), nullptr};
            if (branch == MajorBodyBranch3D::LeftElbow)
                return {&body.leftUpperArm, skeleton.collectComponent(body, body.leftForearm, skeleton.leftElbow), nullptr};
            if (branch == MajorBodyBranch3D::RightKnee)
                return {&body.rightThigh, skeleton.collectComponent(body, body.rightShin, skeleton.rightKnee), &body.rightFoot};
            if (branch == MajorBodyBranch3D::LeftKnee)
                return {&body.leftThigh, skeleton.collectComponent(body, body.leftShin, skeleton.leftKnee), &body.leftFoot};
            if (branch == MajorBodyBranch3D::LeftHip)
                return {&body.pelvis, skeleton.collectComponent(body, body.leftThigh, skeleton.leftHip), &body.leftFoot};
            return {&body.pelvis, skeleton.collectComponent(body, body.rightThigh, skeleton.rightHip), &body.rightFoot};
        }

        auto allParts(AuraBody3D &body) {
            return std::array{&body.head, &body.neck, &body.torso, &body.pelvis,
                &body.leftUpperArm, &body.leftForearm, &body.leftHand,
                &body.rightUpperArm, &body.rightForearm, &body.rightHand,
                &body.leftThigh, &body.leftShin, &body.leftFoot,
                &body.rightThigh, &body.rightShin, &body.rightFoot};
        }

        void translateOutsideBranch(AuraBody3D &body, const BranchParts &branch, const math::Vec3 &delta) {
            for (auto *part: allParts(body)) {
                if (std::find(branch.subtree.begin(), branch.subtree.end(), part) == branch.subtree.end())
                    part->body.position += delta;
            }
        }

        void propagateRootVelocityChange(BodyPart3D &root, std::span<BodyPart3D *const> descendants,
                                         const math::Vec3 &pivot, const math::Vec3 &oldVelocity,
                                         const math::Vec3 &oldOmega) {
            const auto deltaOmega = root.body.angularVelocity - oldOmega;
            const auto deltaComVelocity = root.body.velocity - oldVelocity;
            const auto deltaPivotVelocity = deltaComVelocity - deltaOmega.cross(root.body.position - pivot);
            // Root was already changed by the ordinary corrector; update descendants only.
            translateSubtreeVelocity(descendants, deltaPivotVelocity);
            rotateSubtreeVelocityAroundWorldPoint(descendants, pivot, deltaOmega);
        }
    }

    void correctBranchAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch) {
        const auto parts = branchParts(body, skeleton, branch);
        auto &root = *parts.subtree.front();
        const float error = jointAngleError(*parts.parent, root, joint);
        if (std::abs(error) < 0.000001f) return;
        if (branch == MajorBodyBranch3D::RightShoulder || branch == MajorBodyBranch3D::RightElbow ||
            branch == MajorBodyBranch3D::RightKnee || branch == MajorBodyBranch3D::LeftElbow ||
            branch == MajorBodyBranch3D::LeftKnee) {
            correctJointAngleWithComponent(*parts.parent, root, joint, parts.childParts());
        } else {
            // Hip angular corrections are deliberately outside this migration.
            const auto axis = parts.parent->body.orientation.rotate(joint.hingeAxis.normalized()).normalized();
            const auto pivot = localToWorldPoint(root, joint.localAnchorB);
            rotateSubtreeAroundWorldPoint(parts.childParts(), pivot, math::Quaternion::fromAxisAngle(axis, -error));
        }
        if (parts.foot != nullptr) {
            const float lowestY = physics::lowestPoint(parts.foot->body, parts.foot->size).y;
            if (lowestY < 0.0f) {
                // Hip or knee rotation can lower a foot even with a fixed hip pivot. Lift the whole
                // connected body so contact repair does not split any neighboring anchors.
                const auto wholeBody = allParts(body);
                translateSubtree(wholeBody, {0.0f, -lowestY, 0.0f});
            }
        }
    }

    void correctBranchPositionAsSubtree(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch) {
        const auto parts = branchParts(body, skeleton, branch);
        const auto error = localToWorldPoint(*parts.subtree.front(), joint.localAnchorB) -
                           localToWorldPoint(*parts.parent, joint.localAnchorA);
        auto delta = -error;
        if (parts.foot != nullptr && delta.y < 0.0f) {
            const float lowestY = physics::lowestPoint(parts.foot->body, parts.foot->size).y;
            const bool touching = !physics::floorContactPoints(parts.foot->body, parts.foot->size, 0.0f, 0.01f).empty();
            const float allowedY = touching ? 0.0f : std::max(delta.y, -lowestY);
            const float blockedY = allowedY - delta.y;
            delta.y = allowedY;
            // Translate the other connected branches together, rather than moving pelvis alone.
            if (blockedY > 0.0f) translateOutsideBranch(body, parts, {0.0f, blockedY, 0.0f});
        }
        translateSubtree(parts.childParts(), delta);
    }

    void correctLimbAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton,
                                    const Joint3D &joint, const Joint3D &descendantJoint,
                                    MajorBodyBranch3D branch) {
        // Pose projection does not imply a physical impulse. Anchor and limit
        // velocity errors are repaired later by the local two-body solvers.
        (void)descendantJoint; // Retained for source compatibility with limb wrappers.
        correctBranchAngleAroundPivot(body, skeleton, joint, branch);
    }

    void correctRightKneeAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &knee, const Joint3D &ankle) {
        correctLimbAngleAroundPivot(body, skeleton, knee, ankle, MajorBodyBranch3D::RightKnee);
    }

    void correctRightElbowAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &elbow, const Joint3D &wrist) {
        correctLimbAngleAroundPivot(body, skeleton, elbow, wrist, MajorBodyBranch3D::RightElbow);
    }

    void correctBranchAnchorVelocityAsSubtree(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch) {
        const auto parts = branchParts(body, skeleton, branch);
        auto &root = *parts.subtree.front();
        const auto oldVelocity = root.body.velocity;
        const auto oldOmega = root.body.angularVelocity;
        correctJointVelocity(*parts.parent, root, joint);
        propagateRootVelocityChange(root, parts.childParts().subspan(1),
                                    localToWorldPoint(root, joint.localAnchorB), oldVelocity, oldOmega);
    }

    void correctBranchAngularVelocityAsSubtree(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch) {
        const auto parts = branchParts(body, skeleton, branch);
        auto &root = *parts.subtree.front();
        const auto oldVelocity = root.body.velocity;
        const auto oldOmega = root.body.angularVelocity;
        correctJointAngularVelocity(*parts.parent, root, joint);
        propagateRootVelocityChange(root, parts.childParts().subspan(1),
                                    localToWorldPoint(root, joint.localAnchorB), oldVelocity, oldOmega);
    }

    void solveBranchSubtreeVelocityConstraints(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch) {
        for (int i = 0; i < jointVelocityIterations; ++i) {
            correctBranchAnchorVelocityAsSubtree(body, skeleton, joint, branch);
            correctBranchAngularVelocityAsSubtree(body, skeleton, joint, branch);
        }
    }

    void correctLeftShoulderAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &shoulder) {
        const auto component = skeleton.collectComponent(body, body.leftUpperArm, shoulder);
        correctJointAngleWithComponent(body.torso, body.leftUpperArm, shoulder, component);
    }

    void correctLeftShoulderPositionAsSubtree(AuraBody3D &body, const Joint3D &shoulder) {
        const auto error = localToWorldPoint(body.leftUpperArm, shoulder.localAnchorB) -
                           localToWorldPoint(body.torso, shoulder.localAnchorA);
        const std::array subtree{&body.leftUpperArm, &body.leftForearm, &body.leftHand};
        translateSubtree(subtree, -error);
    }

    void correctLeftShoulderAnchorVelocityAsSubtree(AuraBody3D &body, const Joint3D &shoulder) {
        const auto oldVelocity = body.leftUpperArm.body.velocity;
        const auto oldOmega = body.leftUpperArm.body.angularVelocity;
        correctJointVelocity(body.torso, body.leftUpperArm, shoulder);
        const std::array descendants{&body.leftForearm, &body.leftHand};
        propagateRootVelocityChange(body.leftUpperArm, descendants,
                                    localToWorldPoint(body.leftUpperArm, shoulder.localAnchorB), oldVelocity, oldOmega);
    }

    void correctLeftShoulderAngularVelocityAsSubtree(AuraBody3D &body, const Joint3D &shoulder) {
        const auto oldVelocity = body.leftUpperArm.body.velocity;
        const auto oldOmega = body.leftUpperArm.body.angularVelocity;
        correctJointAngularVelocity(body.torso, body.leftUpperArm, shoulder);
        const std::array descendants{&body.leftForearm, &body.leftHand};
        propagateRootVelocityChange(body.leftUpperArm, descendants,
                                    localToWorldPoint(body.leftUpperArm, shoulder.localAnchorB), oldVelocity, oldOmega);
    }

    void solveLeftShoulderSubtreeVelocityConstraints(AuraBody3D &body, const Joint3D &shoulder) {
        for (int i = 0; i < jointVelocityIterations; ++i) {
            correctLeftShoulderAnchorVelocityAsSubtree(body, shoulder);
            correctLeftShoulderAngularVelocityAsSubtree(body, shoulder);
        }
    }

    void correctNeckAnchorVelocityAsSubtree(AuraBody3D &body, const Joint3D &neckJoint) {
        const auto oldVelocity = body.neck.body.velocity;
        const auto oldOmega = body.neck.body.angularVelocity;
        correctJointVelocity(body.torso, body.neck, neckJoint);
        const std::array descendants{&body.head};
        propagateRootVelocityChange(body.neck, descendants,
                                    localToWorldPoint(body.neck, neckJoint.localAnchorB), oldVelocity, oldOmega);
    }

    void correctNeckAngularVelocityAsSubtree(AuraBody3D &body, const Joint3D &neckJoint) {
        const auto oldVelocity = body.neck.body.velocity;
        const auto oldOmega = body.neck.body.angularVelocity;
        correctJointAngularVelocity(body.torso, body.neck, neckJoint);
        const std::array descendants{&body.head};
        propagateRootVelocityChange(body.neck, descendants,
                                    localToWorldPoint(body.neck, neckJoint.localAnchorB), oldVelocity, oldOmega);
    }

    void solveNeckSubtreeVelocityConstraints(AuraBody3D &body, const Joint3D &neckJoint) {
        for (int i = 0; i < jointVelocityIterations; ++i) {
            correctNeckAnchorVelocityAsSubtree(body, neckJoint);
            correctNeckAngularVelocityAsSubtree(body, neckJoint);
        }
    }

    void correctNeckAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &neckJoint) {
        const auto component = skeleton.collectComponent(body, body.neck, neckJoint);
        correctJointAngleWithComponent(body.torso, body.neck, neckJoint, component);
    }

    void correctNeckPositionAsSubtree(AuraBody3D &body, const Joint3D &neckJoint) {
        const auto error = localToWorldPoint(body.neck, neckJoint.localAnchorB) -
                           localToWorldPoint(body.torso, neckJoint.localAnchorA);
        const std::array subtree{&body.neck, &body.head};
        translateSubtree(subtree, -error);
    }

    void correctHeadAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &headJoint) {
        const auto component = skeleton.collectComponent(body, body.head, headJoint);
        correctJointAngleWithComponent(body.neck, body.head, headJoint, component);
    }

    void correctHeadPositionAsLeaf(AuraBody3D &body, const Joint3D &headJoint) {
        const auto error = localToWorldPoint(body.head, headJoint.localAnchorB) -
                           localToWorldPoint(body.neck, headJoint.localAnchorA);
        const std::array subtree{&body.head};
        translateSubtree(subtree, -error);
    }

    void correctWaistPositionWithFloorContact(AuraBody3D &body, const Joint3D &waist,
                                              bool eitherFootIsTouchingFloor) {
        const auto error = localToWorldPoint(body.pelvis, waist.localAnchorB) -
                           localToWorldPoint(body.torso, waist.localAnchorA);
        // Keep the original pairwise mass weights for this experiment, not group masses.
        const float inverseMassA = 1.0f / body.torso.body.mass;
        const float inverseMassB = 1.0f / body.pelvis.body.mass;
        const float totalInverseMass = inverseMassA + inverseMassB;
        auto deltaUpper = error * (inverseMassA / totalInverseMass);
        auto deltaLower = error * (-inverseMassB / totalInverseMass);
        if (eitherFootIsTouchingFloor && deltaLower.y < 0.0f) {
            deltaUpper.y = error.y;
            deltaLower.y = 0.0f;
        }
        translateUpperBody(body, deltaUpper);
        translateLowerBody(body, deltaLower);
    }
}
