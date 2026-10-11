#pragma once
#include "AuraSkeleton3D.hpp"

namespace aura::body {
    // Current humanoid solver, in dependency order: hinge axis, angular limits,
    // component attachment, then four local anchor/hinge/limit impulse passes.
    // Geometry is pose-only. Velocity impulses affect endpoint bodies only.
    void solveBodyJoint(AuraBody3D &body, const AuraSkeleton3D &skeleton,
                        BodyPart3D &parent, BodyPart3D &child, const Joint3D &joint);
}
