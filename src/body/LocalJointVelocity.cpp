#include "aura/body/LocalJointVelocity.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"

namespace aura::body {
    LocalJointVelocityAudit correctLocalJointVelocity(
        BodyPart3D &a, BodyPart3D &b, const Joint3D &joint) {
        LocalJointVelocityAudit audit;
        const auto anchorA = localToWorldPoint(a, joint.localAnchorA);
        const auto anchorB = localToWorldPoint(b, joint.localAnchorB);
        const auto relative = physics::velocityAtWorldPoint(b.body, anchorB) -
                              physics::velocityAtWorldPoint(a.body, anchorA);
        if (relative.lengthSquared() < 1e-12f) return audit;
        const auto direction = relative.normalized();
        const auto rA = anchorA - a.body.position, rB = anchorB - b.body.position;
        const auto inverseInertia = [](const physics::RigidBody3D &body, const math::Vec3 &v) {
            const auto local = body.orientation.conjugate().rotate(v);
            const auto &I = body.momentOfInertia;
            return body.orientation.rotate({local.x / I.x, local.y / I.y, local.z / I.z});
        };
        const auto crossA = rA.cross(direction), crossB = rB.cross(direction);
        const float denominator = 1.0f / a.body.mass + 1.0f / b.body.mass +
            crossA.dot(inverseInertia(a.body, crossA)) + crossB.dot(inverseInertia(b.body, crossB));
        audit.projectedSpeedBefore = relative.dot(direction);
        const auto impulse = direction * (-audit.projectedSpeedBefore / denominator);
        audit.predictedWork = relative.dot(impulse);
        const auto apply = [&](physics::RigidBody3D &body, const math::Vec3 &p, const math::Vec3 &r) {
            const auto dv = p * (1.0f / body.mass);
            const auto dw = inverseInertia(body, r.cross(p));
            const auto oldLocalW = body.orientation.conjugate().rotate(body.angularVelocity);
            const auto localDw = body.orientation.conjugate().rotate(dw);
            const auto &I = body.momentOfInertia;
            audit.measuredLinearWork += body.mass * body.velocity.dot(dv);
            audit.measuredAngularWork += I.x * oldLocalW.x * localDw.x +
                I.y * oldLocalW.y * localDw.y + I.z * oldLocalW.z * localDw.z;
            audit.quadraticEnergy += 0.5 * body.mass * dv.lengthSquared() +
                0.5 * (I.x * localDw.x * localDw.x + I.y * localDw.y * localDw.y + I.z * localDw.z * localDw.z);
            body.velocity += dv;
            body.angularVelocity += dw;
        };
        apply(a.body, -impulse, rA);
        apply(b.body, impulse, rB);
        audit.projectedSpeedAfter = (physics::velocityAtWorldPoint(b.body, anchorB) -
            physics::velocityAtWorldPoint(a.body, anchorA)).dot(direction);
        return audit;
    }
}
