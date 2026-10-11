#include "aura/body/JointGeometry.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <algorithm>

namespace aura::body {
    namespace {
        JointSwingTwist decompose(const math::Quaternion &relative, const math::Vec3 &axis) {
            const auto q = relative.normalized();
            const float projection = math::Vec3{
                static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z)
            }.dot(axis);
            const float lengthSquared = q.w * q.w + projection * projection;
            if (lengthSquared < 1e-12f) {
                // At a perpendicular 180-degree swing, twist is not identifiable.
                // Identity twist retains reconstruction without inventing an angle.
                return {q, math::Quaternion{}, false};
            }
            const auto twist = math::Quaternion{
                q.w, axis.x * projection,
                axis.y * projection, axis.z * projection
            }.normalized();
            return {(q * twist.conjugate()).normalized(), twist, true};
        }

        void validateSwingJoint(const Joint3D &joint) {
            if (joint.type != JointType::SwingTwist)
                throw std::invalid_argument("Swing preview requires a SwingTwist joint");
            const auto axis = joint.hingeAxis.normalized();
            if (!std::isfinite(axis.lengthSquared()) || (axis - math::Vec3{1, 0, 0}).lengthSquared() > 1e-12f ||
                !std::isfinite(joint.minSwingY) || !std::isfinite(joint.maxSwingY) ||
                !std::isfinite(joint.minSwingZ) || !std::isfinite(joint.maxSwingZ) ||
                joint.minSwingY > joint.maxSwingY || joint.minSwingZ > joint.maxSwingZ)
                throw std::invalid_argument("Swing projection requires an X twist axis and ordered finite Y/Z bounds");
        }

        void validateSwingComponent(const BodyPart3D &parent, const BodyPart3D &child,
                                    const Joint3D &joint, std::span<BodyPart3D *const> component) {
            validateSwingJoint(joint);
            bool containsChild = false;
            for (const auto *part: component) {
                if (part == &parent) throw std::invalid_argument("Swing component must exclude parent");
                containsChild |= part == &child;
            }
            if (!containsChild) throw std::invalid_argument("Swing component must contain child");
        }
    }

    JointSwingTwist relativeJointSwingTwist(const BodyPart3D &partA,
                                            const BodyPart3D &partB, const Joint3D &joint) {
        if (joint.hingeAxis.lengthSquared() == 0) throw std::invalid_argument("Joint hinge axis must be nonzero");
        return decompose(partA.body.orientation.normalized().conjugate() * partB.body.orientation.normalized(),
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
        const math::Vec3 v{static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z)};
        const float length = v.length();
        if (length < 1e-8f) return v * 2.0f;
        return v * (2.0f * std::atan2(length, q.w) / length);
    }

    float relativeJointSwingZ(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        return relativeJointSwingAngles(partA, partB, joint).z;
    }

    bool correctHingeAxisWithComponent(const BodyPart3D &parent, BodyPart3D &child,
                                       const Joint3D &joint, std::span<BodyPart3D *const> component) {
        if (joint.type != JointType::Hinge || !joint.enforceHingeAxis)return false;
        const auto relative = relativeJointSwingTwist(parent, child, joint);
        if (jointSwingRotationVector(relative.swing).lengthSquared() < 1e-14f)return false;
        const auto target = (parent.body.orientation.normalized() * relative.twist).normalized();
        const auto rotation = (target * child.body.orientation.normalized().conjugate()).normalized();
        rotateSubtreeAroundWorldPoint(component, localToWorldPoint(child, joint.localAnchorB), rotation);
        return true;
    }

    SwingAngles relativeJointSwingAngles(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        const auto vector = jointSwingRotationVector(relativeJointSwing(partA, partB, joint));
        return {vector.y, vector.z};
    }

    float relativeJointSwingY(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        return relativeJointSwingAngles(partA, partB, joint).y;
    }

    bool correctJointSwingWithComponent(const BodyPart3D &partA, BodyPart3D &partB,
                                        const Joint3D &joint, std::span<BodyPart3D *const> component) {
        if (joint.type != JointType::SwingTwist) return false;
        validateSwingComponent(partA, partB, joint, component);
        const auto original = relativeJointSwingTwist(partA, partB, joint);
        if (!original.twistDefined) return false;
        const auto vector = jointSwingRotationVector(original.swing);
        const math::Vec3 clamped{
            0, std::clamp(vector.y, joint.minSwingY, joint.maxSwingY),
            std::clamp(vector.z, joint.minSwingZ, joint.maxSwingZ)
        };
        if (clamped.y == vector.y && clamped.z == vector.z) return false;
        const float angle = clamped.length();
        const auto swing = angle == 0 ? math::Quaternion{} : math::Quaternion::fromAxisAngle(clamped, angle);
        const auto target = (partA.body.orientation.normalized() * swing * original.twist).normalized();
        const auto rotation = (target * partB.body.orientation.normalized().conjugate()).normalized();
        rotateSubtreeAroundWorldPoint(component, localToWorldPoint(partA, joint.localAnchorA), rotation);
        return true;
    }

    JointSwingProbe previewJointSwing(const BodyPart3D &partA, const BodyPart3D &partB,
                                      const Joint3D &joint, const math::Vec3 &localProbeAxis, float probeAngle) {
        validateSwingJoint(joint);
        const auto axis = joint.hingeAxis.normalized();
        if (localProbeAxis.lengthSquared() == 0 || !std::isfinite(localProbeAxis.lengthSquared()) || !
            std::isfinite(probeAngle))
            throw std::invalid_argument("Swing probe requires a finite angle and nonzero finite axis");
        const auto parent = partA.body.orientation.normalized();
        const auto oldWorld = partB.body.orientation.normalized();
        const auto relative = (parent.conjugate() * oldWorld).normalized();
        const auto original = decompose(relative, axis);
        const auto candidate = decompose(math::Quaternion::fromAxisAngle(localProbeAxis, probeAngle) * relative, axis);
        if (!original.twistDefined || !candidate.twistDefined) return {};
        const auto swingVector = jointSwingRotationVector(candidate.swing);
        unsigned bounds = 0;
        if (swingVector.y < joint.minSwingY) bounds |= SwingYMin;
        if (swingVector.y > joint.maxSwingY) bounds |= SwingYMax;
        if (swingVector.z < joint.minSwingZ) bounds |= SwingZMin;
        if (swingVector.z > joint.maxSwingZ) bounds |= SwingZMax;
        const auto target = (parent * candidate.swing * original.twist).normalized();
        const auto worldRotation = (target * oldWorld.conjugate()).normalized();
        return {worldRotation, {swingVector.y, swingVector.z}, bounds, true};
    }

    bool applyJointSwingWithComponent(const BodyPart3D &partA, BodyPart3D &partB,
                                      const Joint3D &joint, std::span<BodyPart3D *const> component,
                                      const math::Vec3 &localProbeAxis, float probeAngle) {
        if (joint.type != JointType::SwingTwist) return false;
        validateSwingComponent(partA, partB, joint, component);
        const auto probe = previewJointSwing(partA, partB, joint, localProbeAxis, probeAngle);
        if (!probe.twistDefined || probe.blockedBounds) return false;
        const auto &worldRotation = probe.worldRotation;
        rotateSubtreeAroundWorldPoint(component, localToWorldPoint(partA, joint.localAnchorA), worldRotation);
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
        // Quaternion scale cancels in atan2. Avoid float normalization/products
        // here: near a perpendicular 180-degree swing both twist terms are small.
        const auto &a = partA.body.orientation;
        const auto &b = partB.body.orientation;
        const double w = double(a.w) * b.w + double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
        const double x = double(a.w) * b.x - double(a.x) * b.w - double(a.y) * b.z + double(a.z) * b.y;
        const double y = double(a.w) * b.y + double(a.x) * b.z - double(a.y) * b.w - double(a.z) * b.x;
        const double z = double(a.w) * b.z - double(a.x) * b.y + double(a.y) * b.x - double(a.z) * b.w;
        const auto axis = joint.hingeAxis.normalized();
        const double sine = x * axis.x + y * axis.y + z * axis.z;
        const double normA = double(a.w) * a.w + double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z;
        const double normB = double(b.w) * b.w + double(b.x) * b.x + double(b.y) * b.y + double(b.z) * b.z;
        if (sine * sine + w * w < 1e-12 * normA * normB) return 0.0f;
        return static_cast<float>(std::remainder(2.0 * std::atan2(sine, w), 2.0 * std::numbers::pi));
    }

    float relativeJointAngle(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint) {
        return relativeJointTwistAngle(partA, partB, joint);
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
