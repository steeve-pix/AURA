#pragma once

#include "aura/body/AuraBody3D.hpp"
#include "aura/body/Joint3D.hpp"
#include "aura/body/AuraSkeleton3D.hpp"

namespace aura::body {
    // Head, neck, both shoulders and both elbows/knees derive their angle
    // components from the skeleton graph. Elbow/knee position and velocity
    // corrections use the same components and retain descendant motion/floor policies.
    // Experimental branch corrections; hips and knees use the current Y=0 floor.
    // Floor-blocked translation moves the complementary group; hip/knee rotation
    // that penetrates the floor lifts the whole body without splitting anchors.
    enum class MajorBodyBranch3D { RightShoulder, LeftHip, RightHip, RightKnee, RightElbow, LeftKnee, LeftElbow };
    void correctBranchAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch);
    void correctBranchPositionAsSubtree(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch);
    // Legacy anchor propagation retained for baseline diagnostics. The application
    // uses local two-body anchor and angular-limit impulses.
    void correctBranchAnchorVelocityAsSubtree(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch);
    void correctBranchAngularVelocityAsSubtree(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch);
    void solveBranchSubtreeVelocityConstraints(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch);

    // Elbow/knee geometry rotates the graph-derived component without changing
    // linear or angular velocities. Local impulses subsequently repair anchor motion.
    void correctLimbAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton,
                                    const Joint3D &joint, const Joint3D &descendantJoint,
                                    MajorBodyBranch3D branch);

    // Pose-only knee projection; includes the existing flat-floor geometric lift.
    void correctRightKneeAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &knee, const Joint3D &ankle);

    // Pose-only projection of the forearm-hand component.
    void correctRightElbowAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &elbow, const Joint3D &wrist);

    // Y=0 experiment: keep the ordinary shin floor impulses, but propagate its
    // upward positional correction to the foot. Foot velocities are untouched.
    void resolveRightShinFloorWithFootTranslation(AuraBody3D &body, float dt);

    // Left shoulder only: upper arm, forearm and hand are one child subtree.
    void correctLeftShoulderAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &shoulder);
    void correctLeftShoulderPositionAsSubtree(AuraBody3D &body, const Joint3D &shoulder);
    void correctLeftShoulderAnchorVelocityAsSubtree(AuraBody3D &body, const Joint3D &shoulder);
    void correctLeftShoulderAngularVelocityAsSubtree(AuraBody3D &body, const Joint3D &shoulder);
    void solveLeftShoulderSubtreeVelocityConstraints(AuraBody3D &body, const Joint3D &shoulder);

    // Experimental torso -> neck correction: neck and head form the child subtree.
    void correctNeckAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &neckJoint);
    void correctNeckPositionAsSubtree(AuraBody3D &body, const Joint3D &neckJoint);
    void correctNeckAnchorVelocityAsSubtree(AuraBody3D &body, const Joint3D &neckJoint);
    void correctNeckAngularVelocityAsSubtree(AuraBody3D &body, const Joint3D &neckJoint);
    void solveNeckSubtreeVelocityConstraints(AuraBody3D &body, const Joint3D &neckJoint);

    // Experimental leaf correction: rotate only the head, preserving its current world anchor.
    void correctHeadAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &headJoint);

    // Close the head connection by translating only the leaf; the neck stays fixed.
    void correctHeadPositionAsLeaf(AuraBody3D &body, const Joint3D &headJoint);

    // Experimental, specific to torso (A) -> pelvis (B). Only positions are corrected.
    void correctWaistPositionWithFloorContact(AuraBody3D &body, const Joint3D &waist,
                                              bool eitherFootIsTouchingFloor);
}
