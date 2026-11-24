#pragma once
#include "ActionSpec.hpp"
#include "aura/body/JointGeometry.hpp"

namespace aura::sim {
    // Read the actual coordinate commanded by a control, not always hinge twist.
    inline float actionAngle(const ActionSpec &spec, const body::BodyPart3D &a,
                             const body::BodyPart3D &b, const body::Joint3D &joint) {
        if (spec.coordinate == JointCoordinate::SwingY) return body::relativeJointSwingY(a, b, joint);
        if (spec.coordinate == JointCoordinate::SwingZ) return body::relativeJointSwingZ(a, b, joint);
        return body::relativeJointAngle(a, b, joint);
    }
}
