#include "aura/body/AuraBodyConstraint.hpp"

#include "aura/body/JointGeometry.hpp"

#include <cmath>
#include <array>

namespace aura::body {
    void correctNeckAngleAroundPivot(AuraBody3D &body, const Joint3D &neckJoint) {
        const float error = jointAngleError(body.torso, body.neck, neckJoint);
        if (std::abs(error) < 0.000001f) return;
        const auto worldAxis = body.torso.body.orientation.rotate(neckJoint.hingeAxis.normalized()).normalized();
        const auto pivot = localToWorldPoint(body.neck, neckJoint.localAnchorB);
        const auto rotation = math::Quaternion::fromAxisAngle(worldAxis, -error);
        const std::array subtree{&body.neck, &body.head};
        rotateSubtreeAroundWorldPoint(subtree, pivot, rotation);
    }

    void correctNeckPositionAsSubtree(AuraBody3D &body, const Joint3D &neckJoint) {
        const auto error = localToWorldPoint(body.neck, neckJoint.localAnchorB) -
                           localToWorldPoint(body.torso, neckJoint.localAnchorA);
        const std::array subtree{&body.neck, &body.head};
        translateSubtree(subtree, -error);
    }

    void correctHeadAngleAroundPivot(AuraBody3D &body, const Joint3D &headJoint) {
        const float error = jointAngleError(body.neck, body.head, headJoint);
        if (std::abs(error) < 0.000001f) return;
        const auto worldAxis = body.neck.body.orientation.rotate(headJoint.hingeAxis.normalized()).normalized();
        // Use the head's anchor so an existing integration gap is not rotated or enlarged.
        // Position correction still closes that gap afterwards.
        const auto pivot = localToWorldPoint(body.head, headJoint.localAnchorB);
        const auto rotation = math::Quaternion::fromAxisAngle(worldAxis, -error);
        const std::array subtree{&body.head};
        rotateSubtreeAroundWorldPoint(subtree, pivot, rotation);
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
