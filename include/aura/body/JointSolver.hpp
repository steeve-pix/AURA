#pragma once
#include "AuraSkeleton3D.hpp"

namespace aura::body {
    // Current humanoid solver, in dependency order: hinge axis, angular limits,
    // component attachment, then four local anchor/hinge/limit impulse passes.
    // Geometry is pose-only. Velocity impulses affect endpoint bodies only.
    // The application selects mass-balanced anchors; historical callers retain
    // the prior child/parent projection by default. The caller handles room
    // feasibility and the net horizontal COM shift from angular projection.
    void solveBodyJoint(AuraBody3D &body, const AuraSkeleton3D &skeleton,
                        BodyPart3D &parent, BodyPart3D &child, const Joint3D &joint, bool massBalancedAnchor = false);
}
