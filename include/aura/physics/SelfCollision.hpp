#pragma once
#include "aura/body/AuraSkeleton3D.hpp"

namespace aura::physics {
    struct CollisionSphere {
        math::Vec3 center;
        float radius;
    };

    // Inscribed COM sphere prototype, not full box/capsule collision coverage.
    // Parts must have finite, positive dimensions and masses.
    CollisionSphere collisionSphere(const body::BodyPart3D &part);
    // a and b must belong to owner; identity is independent of mutable names.
    bool shouldSelfCollide(const body::BodyPart3D &a, const body::BodyPart3D &b,
                           const body::AuraBody3D &owner, const body::AuraSkeleton3D &skeleton);
    // Unfiltered pair projection: position only; coincident centers use world +X.
    void resolveSelfCollision(body::BodyPart3D &a, body::BodyPart3D &b);
    // One deterministic pair sweep. Joint repairs may reopen an overlap.
    void resolveBodySelfCollisions(body::AuraBody3D &body, const body::AuraSkeleton3D &skeleton);
    // Read-only candidate test, crossing pairs only. Existing overlap may remain,
    // but positive penetration must not increase by more than tolerance.
    // Component pointers must belong to owner; tolerance must be nonnegative.
    bool componentTranslationIsSelfCollisionSafe(
        const body::AuraBody3D &owner, const body::AuraSkeleton3D &skeleton,
        const std::vector<body::BodyPart3D *> &component,
        const math::Vec3 &delta, float tolerance);
    float maximumSelfCollisionPenetration(const body::AuraBody3D &body,
                                          const body::AuraSkeleton3D &skeleton);
}
