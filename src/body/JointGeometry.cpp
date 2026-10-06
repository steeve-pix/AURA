#include "aura/body/JointGeometry.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace aura::body {
    void translateSubtreeVelocity(std::span<BodyPart3D *const> parts, const math::Vec3 &deltaVelocity) {
        for (auto *part: parts) part->body.velocity += deltaVelocity;
    }

    void rotateSubtreeVelocityAroundWorldPoint(std::span<BodyPart3D *const> parts,
                                               const math::Vec3 &pivot, const math::Vec3 &deltaOmega) {
        for (auto *part: parts) {
            part->body.angularVelocity += deltaOmega;
            part->body.velocity += deltaOmega.cross(part->body.position - pivot);
        }
    }

    void rotateSubtreeAroundWorldPoint(std::span<BodyPart3D *const> parts,
                                       const math::Vec3 &pivot, const math::Quaternion &rotation) {
        for (auto *part: parts) rotateBodyAroundWorldPoint(*part, pivot, rotation);
    }

    void translateSubtree(std::span<BodyPart3D *const> parts, const math::Vec3 &delta) {
        for (auto *part: parts) part->body.position += delta;
    }

    void rotateBodyAroundWorldPoint(BodyPart3D &part, const math::Vec3 &pivot,
                                    const math::Quaternion &rotation) {
        const auto worldRotation = rotation.normalized();
        const auto offset = part.body.position - pivot;
        part.body.position = pivot + worldRotation.rotate(offset);
        // Orientations map local -> world, so a world rotation multiplies on the left.
        part.body.orientation = (worldRotation * part.body.orientation).normalized();
    }

    math::Vec3 localToWorldPoint(const BodyPart3D &part, const math::Vec3 &localPoint) {
        return part.body.position + part.body.orientation.rotate(localPoint);
    }

    float relativeJointAngle(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        if (joint.hingeAxis.lengthSquared() == 0.0f) {
            throw std::invalid_argument("Joint hinge axis must be nonzero");
        }
        const auto axis = joint.hingeAxis.normalized();
        const auto relative =
        (partA.body.orientation.normalized().conjugate() *
         partB.body.orientation.normalized()).normalized();

        // Project the quaternion's vector part onto the axis to isolate its twist.
        const math::Vec3 vectorPart{relative.x, relative.y, relative.z};
        const float twistSinHalfAngle = vectorPart.dot(axis);
        const float twistCosHalfAngle = relative.w;
        if (twistSinHalfAngle * twistSinHalfAngle +
            twistCosHalfAngle * twistCosHalfAngle < 1e-12f) {
            return 0.0f;
        }

        const float angle = 2.0f * std::atan2(twistSinHalfAngle, twistCosHalfAngle);
        // q and -q represent the same orientation; wrapping gives the same signed angle.
        return std::remainder(angle, 2.0f * std::numbers::pi_v<float>);
    }

    float jointAngleError(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        const float angle =
                relativeJointAngle(partA, partB, joint);

        if (angle < joint.minAngle) {
            return angle - joint.minAngle;
        }
        if (angle > joint.maxAngle) {
            return angle - joint.maxAngle;
        }

        return 0.0;
    }

    float jointMotorError(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        return joint.targetAngle - relativeJointAngle(partA, partB, joint);
    }

    float jointMotorTorque(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        const auto axis =
                partA.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();

        const float error =
                jointMotorError(partA, partB, joint);

        const auto relativeAngularVelocity =
                partB.body.angularVelocity -
                partA.body.angularVelocity;

        const float hingeAngularVelocity =
                relativeAngularVelocity.dot(axis);

        return error * joint.motorStiffness - hingeAngularVelocity * joint.motorDamping;
    }
}
