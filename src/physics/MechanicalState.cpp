#include "aura/physics/MechanicalState.hpp"

namespace aura::physics {
    MechanicalState mechanicalState(const RigidBody3D &body, const math::Vec3 &worldReference,
                                   const math::Vec3 &gravity) {
        const auto localOmega = body.orientation.conjugate().rotate(body.angularVelocity);
        const auto square = [](float v) { return static_cast<double>(v) * v; };
        const auto localSpin = math::Vec3{body.momentOfInertia.x * localOmega.x,
                                         body.momentOfInertia.y * localOmega.y,
                                         body.momentOfInertia.z * localOmega.z};
        MechanicalState state;
        state.linearKinetic = 0.5 * body.mass *
            (square(body.velocity.x) + square(body.velocity.y) + square(body.velocity.z));
        state.rotationalKinetic = 0.5 *
            (body.momentOfInertia.x * square(localOmega.x) +
             body.momentOfInertia.y * square(localOmega.y) +
             body.momentOfInertia.z * square(localOmega.z));
        state.potential = -static_cast<double>(body.mass) * body.position.dot(gravity);
        state.linearMomentum = body.velocity * body.mass;
        state.angularMomentum = body.orientation.rotate(localSpin) +
            (body.position - worldReference).cross(state.linearMomentum);
        return state;
    }
}
