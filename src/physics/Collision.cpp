#include "aura/physics/Collision.hpp"
#include "aura/physics/Impulse.hpp"
#include "aura/physics/Inertia.hpp"

#include <algorithm>

#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/MechanicalState.hpp"

namespace aura::physics {
    bool intersectFloor(const RigidBody3D &body, float halfHeight, float floorY) {
        return body.position.y - halfHeight < floorY;
    }

    void resolveFloorCollision(RigidBody3D &body, const math::Vec3 &size, float floorY, float dt,
                               FloorCollisionAudit *audit) {
        constexpr int solverIterations = 4;
        if (audit) *audit = {};
        const auto kinetic = [&] {
            const auto state = mechanicalState(body);
            return state.linearKinetic + state.rotationalKinetic;
        };
        if (audit) {
            audit->energyBefore = audit->energyAfterDamping = audit->energyAfterNormal = kinetic();
            audit->linearSpeedBefore = audit->linearSpeedAfterDamping = audit->linearSpeedAfterNormal = body.velocity.length();
            audit->angularSpeedBefore = audit->angularSpeedAfterDamping = audit->angularSpeedAfterNormal = body.angularVelocity.length();
        }

        const auto lowest =
                lowestPoint(body, size);

        const float penetration =
                floorY - lowest.y;
        if (audit) audit->lowestBefore = lowest.y;

        if (penetration > 0.0f) {
            body.position.y += penetration;
        }

        constexpr float contactTolerance = 0.01f;

        const auto contacts =
                floorContactPoints(
                    body,
                    size,
                    floorY,
                    contactTolerance
                );
        if (audit) {
            audit->lowestAfterPosition = lowestPoint(body, size).y;
            audit->contacts = contacts;
            audit->normalImpulses.resize(contacts.size());
        }

        if (contacts.empty()) {
            return;
        }
        constexpr float frictionStrength = 6.0f;
        constexpr float angularDamping = 1.5f;


        const float damping =
                std::max(0.0f, 1 - frictionStrength * dt);

        const float aDamping =
                std::max(0.0f, 1.0f - angularDamping * dt);

        const auto oldVelocity = body.velocity;
        const auto oldOmega = body.angularVelocity;
        body.velocity.x *= damping;
        body.velocity.z *= damping;

        body.angularVelocity *= aDamping;
        if (audit) {
            audit->dampingDeltaVelocity = body.velocity - oldVelocity;
            audit->dampingDeltaOmega = body.angularVelocity - oldOmega;
            audit->energyAfterDamping = kinetic();
            audit->linearSpeedAfterDamping = body.velocity.length();
            audit->angularSpeedAfterDamping = body.angularVelocity.length();
        }
        const auto afterDampingVelocity = body.velocity;
        const auto afterDampingOmega = body.angularVelocity;

        const math::Vec3 normal{0.0f, 1.0f, 0.0f};

        for (int iteration = 0; iteration < solverIterations; ++iteration) {
            for (const auto &contactPoint: contacts) {
                const auto contactVelocity = velocityAtWorldPoint(body, contactPoint);
                const float normalVelocity = contactVelocity.dot(normal);
                if (normalVelocity >= 0.0f) {
                    continue;
                }

                const math::Vec3 r =
                        contactPoint - body.position;

                const math::Vec3 rCrossN =
                        r.cross(normal);

                const auto inverseInertiaTimesRCrossN = applyInverseInertiaWorld(body, rCrossN);

                const float rotationalTerm =
                        inverseInertiaTimesRCrossN.cross(r).dot(normal);

                const float effectiveInverseMass =
                        (1.0f / body.mass) + rotationalTerm;

                const float impulseMagnitude =
                        -(1.0f + body.restitution) * normalVelocity / effectiveInverseMass;
                const math::Vec3 impulse = normal * impulseMagnitude;

                FloorNormalImpulseAudit event;
                if (audit) {
                    event.iteration = iteration + 1;
                    event.contact = &contactPoint - contacts.data();
                    event.magnitude = impulseMagnitude;
                    event.normalVelocityBefore = normalVelocity;
                    event.energyBefore = kinetic();
                    event.before = body;
                }
                applyImpulseAtPoint(body, impulse, contactPoint);
                if (audit) audit->normalImpulses[&contactPoint - contacts.data()] += impulse;
                if (audit) {
                    event.normalVelocityAfter = velocityAtWorldPoint(body, contactPoint).dot(normal);
                    event.energyAfter = kinetic();
                    event.after = body;
                    audit->impulseEvents.push_back(event);
                }
            }
        }
        if (audit) {
            audit->normalDeltaVelocity = body.velocity - afterDampingVelocity;
            audit->normalDeltaOmega = body.angularVelocity - afterDampingOmega;
            audit->energyAfterNormal = kinetic();
            audit->linearSpeedAfterNormal = body.velocity.length();
            audit->angularSpeedAfterNormal = body.angularVelocity.length();
        }
    }
}
