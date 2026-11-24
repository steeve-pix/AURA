#pragma once
#include "aura/body/AuraSkeleton3D.hpp"

namespace aura::test {
    // Explicit profile for archived physics replays. Their stored poses/expectations
    // predate three-coordinate motors; they must not silently use today's robot.
    inline void restorePreRobotArticulation(body::AuraSkeleton3D &s) {
        for (auto *j: {
                 &s.waist, &s.neck, &s.head, &s.leftShoulder, &s.rightShoulder,
                 &s.leftWrist, &s.rightWrist, &s.leftAnkle, &s.rightAnkle
             }) {
            j->type = body::JointType::Hinge;
            j->motorControlsSwing = false;
            j->enforceHingeAxis = false;
            j->targetSwingY = j->targetSwingZ = 0;
        }
        for (auto *j: {&s.leftShoulder, &s.rightShoulder}) {
            j->minAngle = -1.5f;
            j->maxAngle = 1.5f;
        }
        for (auto *j: {&s.leftHip, &s.rightHip}) {
            j->type = body::JointType::SwingTwist;
            j->motorControlsSwing = false;
            j->minAngle = -.8f;
            j->maxAngle = .8f;
            j->minSwingY = j->minSwingZ = -.4f;
            j->maxSwingY = j->maxSwingZ = .4f;
            j->targetSwingY = j->targetSwingZ = 0;
        }
    }
}
