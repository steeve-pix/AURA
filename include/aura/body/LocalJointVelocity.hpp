#pragma once
#include "aura/body/BodyPart3D.hpp"
#include "aura/body/Joint3D.hpp"

namespace aura::body {
    struct LocalJointVelocityAudit {
        double predictedWork = 0.0;
        double measuredLinearWork = 0.0;
        double measuredAngularWork = 0.0;
        double quadraticEnergy = 0.0;
        float projectedSpeedBefore = 0.0f;
        float projectedSpeedAfter = 0.0f;
    };
    // Experimental scalar anchor projection. Equal/opposite impulses affect only
    // these two bodies. Uses world-space inverse inertia obtained from local
    // principal moments. No geometry, descendant propagation, contacts or limits.
    LocalJointVelocityAudit correctLocalJointVelocity(
        BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);
}
