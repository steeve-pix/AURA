#pragma once
#include <array>
#include <vector>
#include "AuraSkeleton3D.hpp"
#include "aura/physics/CollisionShape.hpp"
#include "aura/physics/RoomCollision.hpp"

namespace aura::body {
    // Stable physical-part ordering, independent of names and body copies.
    std::array<BodyPart3D *, 16> collisionParts(AuraBody3D &body);

    std::array<const BodyPart3D *, 16> collisionParts(const AuraBody3D &body);

    void configureBodyCollisionJoints(AuraSkeleton3D &skeleton);

    // Missing hinge swing projection. Entire child component moves rigidly;
    // any necessary floor lift is common to the body and remains pose-only.
    bool projectBodyHingeAxis(AuraBody3D &body, const AuraSkeleton3D &skeleton,
                              BodyPart3D &parent, BodyPart3D &child, const Joint3D &joint);

    struct BodyCollisionPair {
        unsigned a, b, batch;
    };

    // Cumulative batches: 1 arm/core, 2 +leg/leg and arm/leg, 3 all eligible.
    std::vector<BodyCollisionPair> bodyCollisionPairs(AuraBody3D &body, const AuraSkeleton3D &skeleton, int batch);

    struct BodyCollisionMetrics {
        float maximumPenetration{};
        unsigned pairA{}, pairB{};
        size_t overlappingPairs{};
        double squaredError{};
    };

    BodyCollisionMetrics bodyCollisionMetrics(const AuraBody3D &body,
                                              const std::vector<BodyCollisionPair> &pairs, float tolerance = 0.001f);

    struct BodyCollisionStep {
        size_t acceptedProjections{}, blockedProjections{}, candidateEvaluations{}, impulses{};
        size_t environmentProjections{};
        float maximumEnvironmentTranslation{};
        float largestImpulse{};
        double maximumPositiveEnergy{}, maximumMomentumError{};
    };

    // Fixed-budget search along the contact's articulation path: coordinate
    // probes plus torque-directed shoulder/hip requests (up to 6*angularStep).
    // Uses existing hip swing reconstruction and graph components. Pose only.
    // Configured hinges probe their one permitted axis; SwingTwist hips use
    // their bounded swing coordinates. Legacy unconfigured shoulders retain
    // their historical free transverse motion for explicit comparisons.
    // Feasibility covers physical-box floor/room bounds and no-worse joint limits.
    // Distal hinge compensation searches at most 0.256 rad to recover clearance;
    // the complete candidate must still improve collision error and pass all checks.
    // Coordinate probes also sample smaller fractions of angularStep. If distal
    // recovery cannot retain the improvement, test the existing common room
    // geometry projection together with the candidate (poses only). The internal
    // target is half tolerance; the public collision acceptance is unchanged.
    // Every overlapping, closing contact receives a local normal impulse,
    // including shallow contacts inside the positional acceptance tolerance.
    BodyCollisionStep solveBodySelfCollisions(AuraBody3D &body, const AuraSkeleton3D &skeleton,
                                              const std::vector<BodyCollisionPair> &pairs,
                                              const physics::RoomBounds &room,
                                              int projectionBudget = 4, float angularStep = 0.01f,
                                              float tolerance = 0.001f);
}
