#pragma once

#include "aura/body/AuraBody3D.hpp"
#include "aura/body/Joint3D.hpp"

namespace aura::body {
    // Experimental torso -> neck correction: neck and head form the child subtree.
    void correctNeckAngleAroundPivot(AuraBody3D &body, const Joint3D &neckJoint);
    void correctNeckPositionAsSubtree(AuraBody3D &body, const Joint3D &neckJoint);

    // Experimental leaf correction: rotate only the head, preserving its current world anchor.
    void correctHeadAngleAroundPivot(AuraBody3D &body, const Joint3D &headJoint);

    // Close the head connection by translating only the leaf; the neck stays fixed.
    void correctHeadPositionAsLeaf(AuraBody3D &body, const Joint3D &headJoint);

    // Experimental, specific to torso (A) -> pelvis (B). Only positions are corrected.
    void correctWaistPositionWithFloorContact(AuraBody3D &body, const Joint3D &waist,
                                              bool eitherFootIsTouchingFloor);
}
