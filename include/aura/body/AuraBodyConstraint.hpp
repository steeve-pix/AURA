#pragma once

#include "aura/body/AuraBody3D.hpp"
#include "aura/body/Joint3D.hpp"
#include "aura/body/AuraSkeleton3D.hpp"

namespace aura::body {
    // Head, neck, both shoulders, right elbow and right knee derive their angle
    // components from the skeleton graph. Wrapper-specific velocity compensation
    // and knee floor lifting retain the pre-refactor behavior.
    // Experimental branch corrections; hips and right knee use the current Y=0 floor.
    // Floor-blocked translation moves the complementary group; hip rotation
    // that penetrates the floor lifts the whole body without splitting anchors.
    enum class MajorBodyBranch3D { RightShoulder, LeftHip, RightHip, RightKnee, RightElbow };
    void correctBranchAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &joint, MajorBodyBranch3D branch);
    void correctBranchPositionAsSubtree(AuraBody3D &body, const Joint3D &joint, MajorBodyBranch3D branch);
    void correctBranchAnchorVelocityAsSubtree(AuraBody3D &body, const Joint3D &joint, MajorBodyBranch3D branch);
    void correctBranchAngularVelocityAsSubtree(AuraBody3D &body, const Joint3D &joint, MajorBodyBranch3D branch);
    void solveBranchSubtreeVelocityConstraints(AuraBody3D &body, const Joint3D &joint, MajorBodyBranch3D branch);

    // Geometry projection changes anchor offsets in moving bodies. Preserve the
    // ankle's world relative anchor velocity as well as its relative angular velocity.
    void correctRightKneeAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &knee, const Joint3D &ankle);

    // Preserve wrist motion while projecting only the forearm-hand subtree.
    void correctRightElbowAngleAroundPivot(AuraBody3D &body, const AuraSkeleton3D &skeleton, const Joint3D &elbow, const Joint3D &wrist);

    // Y=0 experiment: keep the ordinary shin floor impulses, but propagate its
    // upward positional correction to the foot. Foot velocities are untouched.
    void resolveRightShinFloorWithFootTranslation(AuraBody3D &body, float dt);

    // Right-shin experiment: additionally propagate the shin's actual floor
    // velocity change as a rigid velocity field about its COM.
    void resolveRightShinFloorWithFootMotion(AuraBody3D &body, float dt);

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
