#include "aura/physics/Motion.hpp"

namespace aura::physics {
    void integrateLinearMotion(RigidBody3D &body, float dt) {
        body.velocity += body.acceleration * dt;
        body.position += body.velocity * dt;
    }

    void integrateAngularMotion(RigidBody3D &body, float dt) {
        body.angularVelocity += body.angularAcceleration * dt;

        const math::Quaternion omega{
            0.0f,
            body.angularVelocity.x, body.angularVelocity.y, body.angularVelocity.z,
        };

        const math::Quaternion qDot =
                omega * body.orientation;

        body.orientation = math::Quaternion{
            body.orientation.w + 0.5f * qDot.w * dt,
            body.orientation.x + 0.5f * qDot.x * dt,
            body.orientation.y + 0.5f * qDot.y * dt,
            body.orientation.z + 0.5f * qDot.z * dt,
        }.normalized();
    }

    void updateAngularAcceleration(RigidBody3D &body) {
        body.angularAcceleration = body.torque * (1.0f / body.momentOfInertia);
    }

    void clearTorque(RigidBody3D &body) {
        body.torque = {};
    }
}
