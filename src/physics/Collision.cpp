#include "aura/physics/Collision.hpp"
#include "aura/physics/Impulse.hpp"
#include "aura/physics/Inertia.hpp"

#include <algorithm>
#include <cmath>

#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/MechanicalState.hpp"

namespace aura::physics {
    namespace {
        // Accumulated sequential impulses: solve support, then both tangential
        // axes, with four sweeps for each constraint. Later contacts may retract excess
        // support instead of producing a false bounce on an inelastic floor.
        void solveSupportedContacts(RigidBody3D &body, const std::vector<math::Vec3> &contacts,
                                    FloorCollisionAudit *audit) {
            const auto beforeVelocity = body.velocity, beforeOmega = body.angularVelocity;
            std::vector<float> normal(contacts.size()), target(contacts.size());
            std::vector<math::Vec3> tangent(contacts.size());
            for (std::size_t i = 0; i < contacts.size(); ++i)
                target[i] = -body.restitution * std::min(0.0f, velocityAtWorldPoint(body, contacts[i]).y);
            const auto kinetic = [&] {
                const auto s = mechanicalState(body);
                return s.linearKinetic + s.rotationalKinetic;
            };
            double normalWork = 0;
            math::Vec3 normalDV{}, normalDW{}, frictionDV{}, frictionDW{};
            for (int sweep = 0; sweep < 4; ++sweep) {
                for (std::size_t i = 0; i < contacts.size(); ++i) {
                    const auto &point = contacts[i];
                    const auto r = point - body.position;
                    const auto lever = r.cross({0, 1, 0});
                    const float k = 1.0f / body.mass + lever.dot(applyInverseInertiaWorld(body, lever));
                    const float velocity = velocityAtWorldPoint(body, point).y;
                    const float next = std::max(0.0f, normal[i] + (target[i] - velocity) / k);
                    const float delta = next - normal[i];
                    normal[i] = next;
                    if (delta == 0) continue;
                    FloorNormalImpulseAudit event;
                    if (audit) {
                        event.iteration = sweep + 1; event.contact = i; event.magnitude = delta;
                        event.normalVelocityBefore = velocity; event.energyBefore = kinetic(); event.before = body;
                    }
                    const auto oldV = body.velocity, oldW = body.angularVelocity;
                    applyImpulseAtPoint(body, {0, delta, 0}, point);
                    normalDV += body.velocity - oldV; normalDW += body.angularVelocity - oldW;
                    if (audit) {
                        audit->normalImpulses[i].y += delta;
                        event.normalVelocityAfter = velocityAtWorldPoint(body, point).y;
                        event.energyAfter = kinetic(); event.after = body;
                        normalWork += event.energyAfter - event.energyBefore;
                        audit->impulseEvents.push_back(event);
                    }
                }
            }
            for (int sweep = 0; sweep < 4; ++sweep) {
                for (std::size_t i = 0; i < contacts.size(); ++i) {
                    if (normal[i] <= 0) continue;
                    const auto &point = contacts[i];
                    const auto r = point - body.position;
                    const auto slip = velocityAtWorldPoint(body, point);
                    const auto rx = r.cross({1, 0, 0}), rz = r.cross({0, 0, 1});
                    const auto ix = applyInverseInertiaWorld(body, rx), iz = applyInverseInertiaWorld(body, rz);
                    const double xx = 1.0 / body.mass + rx.dot(ix), xz = rx.dot(iz);
                    const double zz = 1.0 / body.mass + rz.dot(iz), determinant = xx * zz - xz * xz;
                    if (!(determinant > 0) || !std::isfinite(determinant)) continue;
                    auto requested = tangent[i] + math::Vec3{
                        float((-zz * slip.x + xz * slip.z) / determinant), 0,
                        float((xz * slip.x - xx * slip.z) / determinant)};
                    const float magnitude = requested.length();
                    if (magnitude > groundStaticFriction * normal[i])
                        requested = magnitude > 0 ? requested * (groundDynamicFriction * normal[i] / magnitude)
                                                  : math::Vec3{};
                    const auto delta = requested - tangent[i];
                    tangent[i] = requested;
                    if (delta.lengthSquared() == 0) continue;
                    const auto oldV = body.velocity, oldW = body.angularVelocity;
                    applyImpulseAtPoint(body, delta, point);
                    frictionDV += body.velocity - oldV; frictionDW += body.angularVelocity - oldW;
                    if (audit) audit->frictionImpulses[i] += delta;
                }
            }
            if (audit) {
                audit->normalDeltaVelocity = normalDV; audit->normalDeltaOmega = normalDW;
                audit->frictionDeltaVelocity = frictionDV; audit->frictionDeltaOmega = frictionDW;
                // Normal support precedes friction; accumulate actual impulse work.
                audit->energyAfterNormal = audit->energyAfterDamping + normalWork;
                audit->energyAfterFriction = kinetic();
                audit->linearSpeedAfterNormal = (beforeVelocity + normalDV).length();
                audit->angularSpeedAfterNormal = (beforeOmega + normalDW).length();
            }
        }
    }

    bool intersectFloor(const RigidBody3D &body, float halfHeight, float floorY) {
        return body.position.y - halfHeight < floorY;
    }

    void resolveFloorCollision(RigidBody3D &body, const math::Vec3 &size, float floorY, float dt,
                               FloorCollisionAudit *audit, FloorFrictionModel friction, bool applyAngularDamping) {
        resolveFloorCollision(body, size, CollisionShape{}, floorY, dt, audit, friction, applyAngularDamping);
    }

    void resolveFloorCollision(RigidBody3D &body, const math::Vec3 &size, CollisionShape shape,
                               float floorY, float dt, FloorCollisionAudit *audit, FloorFrictionModel friction, bool applyAngularDamping) {
        constexpr int solverIterations = 4;
        if (audit) *audit = {};
        const auto kinetic = [&] {
            const auto state = mechanicalState(body);
            return state.linearKinetic + state.rotationalKinetic;
        };
        if (audit) {
            audit->energyBefore = audit->energyAfterDamping = audit->energyAfterNormal =
                    audit->energyAfterFriction = kinetic();
            audit->linearSpeedBefore = audit->linearSpeedAfterDamping =
                                       audit->linearSpeedAfterNormal = body.velocity.length();
            audit->angularSpeedBefore = audit->angularSpeedAfterDamping =
                                        audit->angularSpeedAfterNormal = body.angularVelocity.length();
        }

        const auto lowest =
                lowestPoint(body, size, shape);

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
                    shape,
                    floorY,
                    contactTolerance
                );
        if (audit) {
            audit->lowestAfterPosition = lowestPoint(body, size, shape).y;
            audit->contacts = contacts;
            audit->normalImpulses.resize(contacts.size());
            audit->frictionImpulses.resize(contacts.size());
        }

        if (contacts.empty()) {
            return;
        }
        constexpr float angularDamping = 1.5f;
        const float aDamping =
                applyAngularDamping ? std::max(0.0f, 1.0f - angularDamping * dt) : 1.0f;

        const auto oldVelocity = body.velocity;
        const auto oldOmega = body.angularVelocity;
        if (friction == FloorFrictionModel::LegacyVelocityDamping) {
            constexpr float legacyFrictionStrength = 6.0f;
            const float damping = std::max(0.0f, 1.0f - legacyFrictionStrength * dt);
            body.velocity.x *= damping;
            body.velocity.z *= damping;
        }

        body.angularVelocity *= aDamping;
        if (audit) {
            audit->dampingDeltaVelocity = body.velocity - oldVelocity;
            audit->dampingDeltaOmega = body.angularVelocity - oldOmega;
            audit->energyAfterDamping = kinetic();
            audit->linearSpeedAfterDamping = body.velocity.length();
            audit->angularSpeedAfterDamping = body.angularVelocity.length();
        }
        if (friction == FloorFrictionModel::Coulomb) {
            solveSupportedContacts(body, contacts, audit);
            return;
        }
        const auto afterDampingVelocity = body.velocity;
        const auto afterDampingOmega = body.angularVelocity;

        const math::Vec3 normal{0.0f, 1.0f, 0.0f};
        std::vector<float> accumulatedNormal(contacts.size(), 0.0f);

        for (int iteration = 0; iteration < solverIterations; ++iteration) {
            for (std::size_t index = 0; index < contacts.size(); ++index) {
                const auto &contactPoint = contacts[index];
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
                accumulatedNormal[index] += impulseMagnitude;
                if (audit) audit->normalImpulses[index] += impulse;
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

        if (audit) audit->energyAfterFriction = audit->energyAfterNormal;
    }
}
