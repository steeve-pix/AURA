#include "aura/body/LocalJointVelocity.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Inertia.hpp"
#include <cmath>
#include <algorithm>

namespace aura::body {
    LocalJointVelocityAudit correctLocalJointHingeAngularVelocity(
        BodyPart3D &a, BodyPart3D &b, const Joint3D &joint) {
        LocalJointVelocityAudit audit;
        if (joint.type != JointType::Hinge || !joint.enforceHingeAxis)return audit;
        const auto axis = a.body.orientation.normalized().rotate(joint.hingeAxis.normalized());
        const auto seed = std::abs(axis.x) < .8f ? math::Vec3{1, 0, 0} : math::Vec3{0, 1, 0};
        const auto u = axis.cross(seed).normalized(), v = axis.cross(u).normalized();
        const auto relative = b.body.angularVelocity - a.body.angularVelocity;
        const double speedU = relative.dot(u), speedV = relative.dot(v);
        audit.projectedSpeedBefore = std::hypot(speedU, speedV);
        audit.projectedSpeedAfter = audit.projectedSpeedBefore;
        if (audit.projectedSpeedBefore < 1e-7f)return audit;
        const auto response = [&](const math::Vec3 &torque) {
            return physics::applyInverseInertiaWorld(a.body, torque) +
                   physics::applyInverseInertiaWorld(b.body, torque);
        };
        const auto ru = response(u), rv = response(v);
        const double kuu = u.dot(ru), kuv = u.dot(rv), kvv = v.dot(rv);
        const double determinant = kuu * kvv - kuv * kuv;
        if (!(determinant > 0))return audit;
        const auto impulse = u * float((-kvv * speedU + kuv * speedV) / determinant) +
                             v * float((kuv * speedU - kuu * speedV) / determinant);
        audit.predictedWork = relative.dot(impulse);
        const auto apply = [&](physics::RigidBody3D &rb, const math::Vec3 &torque) {
            const auto delta = physics::applyInverseInertiaWorld(rb, torque);
            const auto inverse = rb.orientation.normalized().conjugate();
            const auto old = inverse.rotate(rb.angularVelocity), dw = inverse.rotate(delta);
            const auto &I = rb.momentOfInertia;
            audit.measuredAngularWork += I.x * old.x * dw.x + I.y * old.y * dw.y + I.z * old.z * dw.z;
            audit.quadraticEnergy += .5 * (I.x * dw.x * dw.x + I.y * dw.y * dw.y + I.z * dw.z * dw.z);
            rb.angularVelocity += delta;
        };
        apply(a.body, -impulse);
        apply(b.body, impulse);
        const auto after = b.body.angularVelocity - a.body.angularVelocity;
        audit.projectedSpeedAfter = std::hypot(after.dot(u), after.dot(v));
        return audit;
    }

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
                                  crossA.dot(inverseInertia(a.body, crossA)) + crossB.dot(
                                      inverseInertia(b.body, crossB));
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

    LocalJointVelocityAudit correctLocalJointAngularLimitVelocity(
        BodyPart3D &a, BodyPart3D &b, const Joint3D &joint) {
        LocalJointVelocityAudit audit;
        if (joint.motorControlsSwing) {
            const auto gradients = jointCoordinateGradients(a, b, joint);
            const auto swing = relativeJointSwingAngles(a, b, joint);
            const std::array<float, 3> angles{relativeJointAngle(a, b, joint), swing.y, swing.z};
            const std::array<float, 3> minimum{joint.minAngle, joint.minSwingY, joint.minSwingZ};
            const std::array<float, 3> maximum{joint.maxAngle, joint.maxSwingY, joint.maxSwingZ};
            const auto relative = b.body.angularVelocity - a.body.angularVelocity;
            struct Bound {
                math::Vec3 normal;
                bool equality;
            };
            std::array<Bound, 3> bounds{};
            int count = 0;
            for (int i = 0; i < 3; ++i) {
                if (maximum[i] == minimum[i]) bounds[count++] = {gradients[i], true};
                else if (angles[i] >= maximum[i] - 1e-5f) bounds[count++] = {gradients[i], false};
                else if (angles[i] <= minimum[i] + 1e-5f) bounds[count++] = {-gradients[i], false};
            }
            if (count == 0) return audit;
            const auto response = [&](const math::Vec3 &impulse) {
                return physics::applyInverseInertiaWorld(a.body, impulse) + physics::applyInverseInertiaWorld(
                           b.body, impulse);
            };
            // At most three constraints: enumerate the unilateral active set,
            // always including locked coordinates. Solve the coupled inertia
            // system together so stopping pitch/roll cannot reintroduce ankle yaw.
            math::Vec3 impulse{};
            bool found = false;
            for (int mask = 0; mask < (1 << count) && !found; ++mask) {
                std::array < int, 3 > indices{};
                int n = 0;
                bool admissible = true;
                for (int i = 0; i < count; ++i) {
                    if (bounds[i].equality && !(mask & (1 << i))) admissible = false;
                    if (mask & (1 << i)) indices[n++] = i;
                }
                if (!admissible) continue;
                double matrix[3][4]{};
                for (int row = 0; row < n; ++row) {
                    const auto normal = bounds[indices[row]].normal;
                    for (int col = 0; col < n; ++col) matrix[row][col] = normal.dot(
                                                          response(bounds[indices[col]].normal));
                    matrix[row][n] = -normal.dot(relative);
                }
                for (int col = 0; col < n && admissible; ++col) {
                    int pivot = col;
                    for (int row = col + 1; row < n; ++row)
                        if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) pivot = row;
                    if (std::abs(matrix[pivot][col]) < 1e-12) {
                        admissible = false;
                        break;
                    }
                    for (int k = col; k <= n; ++k) std::swap(matrix[col][k], matrix[pivot][k]);
                    const double diagonal = matrix[col][col];
                    for (int k = col; k <= n; ++k) matrix[col][k] /= diagonal;
                    for (int row = 0; row < n; ++row)
                        if (row != col) {
                            const double factor = matrix[row][col];
                            for (int k = col; k <= n; ++k) matrix[row][k] -= factor * matrix[col][k];
                        }
                }
                if (!admissible) continue;
                math::Vec3 candidate{};
                for (int row = 0; row < n; ++row) {
                    if (!bounds[indices[row]].equality && matrix[row][n] > 1e-7) admissible = false;
                    candidate += bounds[indices[row]].normal * static_cast<float>(matrix[row][n]);
                }
                const auto after = relative + response(candidate);
                for (int i = 0; i < count; ++i) {
                    const float rate = after.dot(bounds[i].normal);
                    if (bounds[i].equality ? std::abs(rate) > 1e-5f : rate > 1e-5f) admissible = false;
                }
                if (admissible) {
                    impulse = candidate;
                    found = true;
                }
            }
            if (!found) return audit;
            audit.predictedWork = relative.dot(impulse);
            const auto apply = [&](physics::RigidBody3D &rb, const math::Vec3 &p) {
                const auto delta = physics::applyInverseInertiaWorld(rb, p);
                const auto inverse = rb.orientation.normalized().conjugate();
                const auto old = inverse.rotate(rb.angularVelocity), dw = inverse.rotate(delta);
                const auto &I = rb.momentOfInertia;
                audit.measuredAngularWork += I.x * old.x * dw.x + I.y * old.y * dw.y + I.z * old.z * dw.z;
                audit.quadraticEnergy += .5 * (I.x * dw.x * dw.x + I.y * dw.y * dw.y + I.z * dw.z * dw.z);
                rb.angularVelocity += delta;
            };
            apply(a.body, -impulse);
            apply(b.body, impulse);
            const auto after = b.body.angularVelocity - a.body.angularVelocity;
            for (int i = 0; i < count; ++i) {
                const float beforeRate = relative.dot(bounds[i].normal), afterRate = after.dot(bounds[i].normal);
                audit.projectedSpeedBefore += bounds[i].equality ? std::abs(beforeRate) : std::max(0.f, beforeRate);
                audit.projectedSpeedAfter += bounds[i].equality ? std::abs(afterRate) : std::max(0.f, afterRate);
            }
            return audit;
        }
        const auto axis = a.body.orientation.rotate(joint.hingeAxis.normalized()).normalized();
        const float omega = (b.body.angularVelocity - a.body.angularVelocity).dot(axis);
        audit.projectedSpeedBefore = audit.projectedSpeedAfter = omega;
        const float angle = relativeJointAngle(a, b, joint);
        constexpr float angleTolerance = 1e-5f; // Same activation tolerance as legacy limit solver.
        if (!((angle >= joint.maxAngle - angleTolerance && omega > 0.0f) ||
              (angle <= joint.minAngle + angleTolerance && omega < 0.0f)))
            return audit;
        const auto inverseInertia = [](const physics::RigidBody3D &body, const math::Vec3 &v) {
            const auto local = body.orientation.conjugate().rotate(v);
            const auto &I = body.momentOfInertia;
            return body.orientation.rotate({local.x / I.x, local.y / I.y, local.z / I.z});
        };
        const float denominator = axis.dot(inverseInertia(a.body, axis) + inverseInertia(b.body, axis));
        const auto angularImpulse = axis * (-omega / denominator);
        audit.predictedWork = (b.body.angularVelocity - a.body.angularVelocity).dot(angularImpulse);
        const auto apply = [&](physics::RigidBody3D &body, const math::Vec3 &impulse) {
            const auto delta = inverseInertia(body, impulse);
            const auto localOld = body.orientation.conjugate().rotate(body.angularVelocity);
            const auto localDelta = body.orientation.conjugate().rotate(delta);
            const auto &I = body.momentOfInertia;
            audit.measuredAngularWork += I.x * localOld.x * localDelta.x +
                    I.y * localOld.y * localDelta.y + I.z * localOld.z * localDelta.z;
            audit.quadraticEnergy += 0.5 * (I.x * localDelta.x * localDelta.x +
                                            I.y * localDelta.y * localDelta.y + I.z * localDelta.z * localDelta.z);
            body.angularVelocity += delta;
        };
        apply(a.body, -angularImpulse);
        apply(b.body, angularImpulse);
        audit.projectedSpeedAfter = (b.body.angularVelocity - a.body.angularVelocity).dot(axis);
        return audit;
    }
}
