#include "aura/body/JointConstraint.hpp"

#include <cmath>

#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Impulse.hpp"

namespace aura::body {
    void correctJointAngleWithComponent(BodyPart3D &parent, BodyPart3D &child,
        const Joint3D &joint, std::span<BodyPart3D *const> childComponent) {
        const float error = jointAngleError(parent, child, joint);
        if (std::abs(error) < 0.000001f) return;
        const auto axis = parent.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();
        const auto pivot = localToWorldPoint(child, joint.localAnchorB);
        rotateSubtreeAroundWorldPoint(childComponent, pivot, math::Quaternion::fromAxisAngle(axis, -error));
    }

    void correctJointPosition(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint) {
        const auto worldA =
                localToWorldPoint(partA, joint.localAnchorA);

        const auto worldB =
                localToWorldPoint(partB, joint.localAnchorB);

        const auto error =
                worldB - worldA;

        const float inverseMassA =
                1.0f / partA.body.mass;

        const float inverseMassB =
                1.0f / partB.body.mass;

        const float totalInverseMass =
                inverseMassA + inverseMassB;

        partA.body.position += error * (inverseMassA / totalInverseMass);
        partB.body.position -= error * (inverseMassB / totalInverseMass);
    }

    void correctJointPositionWithFloorContact(BodyPart3D &partA, BodyPart3D &partB,
                                             const Joint3D &joint, bool footIsTouchingFloor) {
        const auto error = localToWorldPoint(partB, joint.localAnchorB) -
                           localToWorldPoint(partA, joint.localAnchorA);
        const float inverseMassA = 1.0f / partA.body.mass;
        const float inverseMassB = 1.0f / partB.body.mass;
        const float totalInverseMass = inverseMassA + inverseMassB;
        auto deltaA = error * (inverseMassA / totalInverseMass);
        auto deltaB = error * (-inverseMassB / totalInverseMass);
        if (footIsTouchingFloor && deltaB.y < 0.0f) {
            // The floor supplies the missing vertical freedom: only A closes this gap.
            deltaA.y = error.y;
            deltaB.y = 0.0f;
        }
        partA.body.position += deltaA;
        partB.body.position += deltaB;
    }

    void correctJointVelocity(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint) {
        const auto worldA =
                localToWorldPoint(partA, joint.localAnchorA);

        const auto worldB =
                localToWorldPoint(partB, joint.localAnchorB);

        const auto velocityA =
                physics::velocityAtWorldPoint(partA.body, worldA);

        const auto velocityB =
                physics::velocityAtWorldPoint(partB.body, worldB);

        const auto relativeVelocity =
                velocityB - velocityA;

        if (relativeVelocity.lengthSquared() < 0.000001f) {
            return;
        }

        const auto direction =
                relativeVelocity.normalized();

        const auto rA =
                worldA - partA.body.position;
        const auto rB =
                worldB - partB.body.position;

        const auto rACrossN =
                rA.cross(direction);
        const auto rBCrossN =
                rB.cross(direction);

        const math::Vec3 invInertiaRA{
            rACrossN.x / partA.body.momentOfInertia.x,
            rACrossN.y / partA.body.momentOfInertia.y,
            rACrossN.z / partA.body.momentOfInertia.z
        };
        const math::Vec3 invInertiaRB{
            rBCrossN.x / partB.body.momentOfInertia.x,
            rBCrossN.y / partB.body.momentOfInertia.y,
            rBCrossN.z / partB.body.momentOfInertia.z
        };

        const float rotationalA =
                invInertiaRA.cross(rA).dot(direction);
        const float rotationalB =
                invInertiaRB.cross(rB).dot(direction);

        const float effectiveInverseMass =
                (1.0f / partA.body.mass) + (1.0f / partB.body.mass) + rotationalA + rotationalB;

        const float relativeSpeed =
                relativeVelocity.dot(direction);

        const float impulseMagnitude =
                -relativeSpeed / effectiveInverseMass;

        const auto impulse =
                direction * impulseMagnitude;

        physics::applyImpulseAtPoint(partA.body, -impulse, worldA);
        physics::applyImpulseAtPoint(partB.body, impulse, worldB);
    }

    void solveJointVelocityConstraints(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint) {
        for (int i = 0; i < jointVelocityIterations; ++i) {
            correctJointVelocity(partA, partB, joint);
            // Anchor impulses can change angular velocity, so enforce limit velocity last.
            correctJointAngularVelocity(partA, partB, joint);
        }
    }

    void solveJoint(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint) {
        correctJointAngle(partA, partB, joint);
        correctJointPosition(partA, partB, joint);
        solveJointVelocityConstraints(partA, partB, joint);
    }

    void correctJointAngle(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint) {
        const float error =
                jointAngleError(partA, partB, joint);

        if (std::abs(error) < 0.000001f) {
            return;
        }

        const auto axis =
                joint.hingeAxis.normalized();

        const float inverseInertiaA =
                1.0f / partA.body.momentOfInertia.x;

        const float inverseInertiaB =
                1.0f / partB.body.momentOfInertia.x;

        const float totalInverseInertia =
                inverseInertiaA + inverseInertiaB;

        const float correctionA =
                error * (inverseInertiaA / totalInverseInertia);
        const float correctionB =
                -error * (inverseInertiaB / totalInverseInertia);

        const auto rotationA =
                math::Quaternion::fromAxisAngle(axis, correctionA);

        const auto rotationB =
                math::Quaternion::fromAxisAngle(axis, correctionB);

        partA.body.orientation = (partA.body.orientation * rotationA).normalized();
        partB.body.orientation = (partB.body.orientation * rotationB).normalized();
    }

    void correctJointAngularVelocity(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint) {
        const auto axis =
                partA.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();

        const auto relativeAngularVelocity =
                partB.body.angularVelocity -
                partA.body.angularVelocity;

        const float hingeAngularVelocity =
                relativeAngularVelocity.dot(axis);

        const float angle =
                relativeJointAngle(partA, partB, joint);

        constexpr float angleTolerance = 1e-5f;
        const bool pushingPastMax =
                angle >= joint.maxAngle - angleTolerance && hingeAngularVelocity > 0.0f;

        const bool pushingPastMin =
                angle <= joint.minAngle + angleTolerance && hingeAngularVelocity < 0.0f;

        if (!pushingPastMax && !pushingPastMin) {
            return;
        }

        const float inverseInertiaA =
                1.0f / partA.body.momentOfInertia.x;

        const float inverseInertiaB =
                1.0f / partB.body.momentOfInertia.x;

        const float totalInverseInertia =
                inverseInertiaA + inverseInertiaB;

        const float correctionA =
                hingeAngularVelocity * (inverseInertiaA / totalInverseInertia);

        const float correctionB =
                hingeAngularVelocity * (inverseInertiaB / totalInverseInertia);

        partA.body.angularVelocity += axis * correctionA;
        partB.body.angularVelocity -= axis * correctionB;
    }

    void applyJointMotor(BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint) {
        const auto axis =
                partA.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();

        const float torqueMagnitude =
                jointMotorTorque(partA, partB, joint);

        const auto torque =
                axis * torqueMagnitude;

        partA.body.torque -= torque;
        partB.body.torque += torque;
    }
}
