#pragma once
#include "BodyDiagnostics.hpp"
#include "aura/body/BodySelfCollision.hpp"

namespace aura::app {
    inline constexpr double FIXED_DT = 1.0 / 120.0;
    inline constexpr int jointIterations = 16;

    // The single application configuration. Historical stepping stays in tests.
    // Call once per fixed tick; pose projections never imply velocity changes.
    void stepSimulation(body::AuraBody3D& auraBody, const body::AuraSkeleton3D& skeleton,
        std::span<BodyJoint> joints, std::span<body::BodyPart3D* const> parts,
        const std::vector<body::BodyCollisionPair>& bodyPairs,
        std::span<const physics::RoomBody> roomBodies, const physics::RoomBounds& room,
        BodyDiagnosticHistory& diagnosticHistory);
}
