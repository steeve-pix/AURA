#include "aura/body/JointGeometry.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace aura::body {
    namespace {
        JointSwingTwist decompose(const math::Quaternion &relative, const math::Vec3 &axis) {
            const auto q = relative.normalized();
            const float projection = math::Vec3{q.x, q.y, q.z}.dot(axis);
            const float lengthSquared = q.w*q.w + projection*projection;
            if (lengthSquared < 1e-12f) {
                // At a perpendicular 180-degree swing, twist is not identifiable.
                // Identity twist retains reconstruction without inventing an angle.
                return {q, math::Quaternion{}, false};
            }
            const auto twist = math::Quaternion{q.w, axis.x*projection,
                                               axis.y*projection, axis.z*projection}.normalized();
            return {(q*twist.conjugate()).normalized(), twist, true};
        }
    }

    JointSwingTwist relativeJointSwingTwist(const BodyPart3D &partA,
                                           const BodyPart3D &partB, const Joint3D &joint) {
        if (joint.hingeAxis.lengthSquared() == 0) throw std::invalid_argument("Joint hinge axis must be nonzero");
        return decompose(partA.body.orientation.normalized().conjugate()*partB.body.orientation.normalized(),
                         joint.hingeAxis.normalized());
    }

    math::Quaternion relativeJointSwing(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        return relativeJointSwingTwist(partA, partB, joint).swing;
    }

    math::Vec3 jointSwingRotationVector(const math::Quaternion &swing) {
        auto q = swing.normalized();
        // Canonicalize q/-q, including the 180-degree tie.
        if (q.w < 0 || (q.w == 0 && (q.x < 0 || (q.x == 0 && (q.y < 0 || (q.y == 0 && q.z < 0))))))
            q = {-q.w, -q.x, -q.y, -q.z};
        const math::Vec3 v{q.x,q.y,q.z};
        const float length = v.length();
        if (length < 1e-8f) return v*2.0f;
        return v*(2.0f*std::atan2(length,q.w)/length);
    }

    float relativeJointSwingZ(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        return jointSwingRotationVector(relativeJointSwing(partA,partB,joint)).z;
    }

    bool applyJointSwingWithComponent(const BodyPart3D &partA, BodyPart3D &partB,
                                      const Joint3D &joint, std::span<BodyPart3D *const> component,
                                      const math::Vec3 &localProbeAxis, float probeAngle) {
        if (joint.type != JointType::SwingTwist) return false;
        const auto axis = joint.hingeAxis.normalized();
        if ((axis-math::Vec3{1,0,0}).lengthSquared()>1e-12f ||
            localProbeAxis.lengthSquared()==0 || !std::isfinite(localProbeAxis.lengthSquared()) ||
            !std::isfinite(probeAngle) || !std::isfinite(joint.minSwingZ) || !std::isfinite(joint.maxSwingZ) ||
            joint.minSwingZ>joint.maxSwingZ)
            throw std::invalid_argument("Swing probe requires an X twist axis, nonzero probe axis and ordered Z bounds");
        bool containsChild=false;
        for (const auto* part:component) {
            if (part == &partA) throw std::invalid_argument("Swing component must exclude parent");
            containsChild |= part == &partB;
        }
        if (!containsChild) throw std::invalid_argument("Swing component must contain child");
        const auto parent = partA.body.orientation.normalized();
        const auto oldWorld = partB.body.orientation.normalized();
        const auto relative = (parent.conjugate()*oldWorld).normalized();
        const auto original = decompose(relative,axis);
        const auto candidate = decompose(math::Quaternion::fromAxisAngle(localProbeAxis,probeAngle)*relative,axis);
        if (!original.twistDefined || !candidate.twistDefined) return false;
        const float swingZ = jointSwingRotationVector(candidate.swing).z;
        if (swingZ<joint.minSwingZ || swingZ>joint.maxSwingZ) return false;
        const auto target = (parent*candidate.swing*original.twist).normalized();
        const auto worldRotation = (target*oldWorld.conjugate()).normalized();
        rotateSubtreeAroundWorldPoint(component,localToWorldPoint(partA,joint.localAnchorA),worldRotation);
        return true;
    }
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

    float relativeJointTwistAngle(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
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

    float relativeJointAngle(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        return relativeJointTwistAngle(partA,partB,joint);
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
