#pragma once
#include "BodyPart3D.hpp"
#include "Joint3D.hpp"

namespace aura::body {
    void correctJointPosition(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);

    // Experimental: part B is the foot; active floor contact blocks downward translation.
    void correctJointPositionWithFloorContact(BodyPart3D &partA, BodyPart3D &partB,
                                             const Joint3D &joint, bool footIsTouchingFloor);

    void correctJointVelocity(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);

    inline constexpr int jointVelocityIterations = 4;
    // Couple anchor velocity and angular-limit velocity without repeating position correction.
    void solveJointVelocityConstraints(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);

    void solveJoint(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);

    void correctJointAngle(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);

    void correctJointAngularVelocity(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);

    void applyJointMotor(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);
}
