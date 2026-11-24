#pragma once
#include "BodyDiagnostics.hpp"
#include <functional>
#include <string_view>
#include "aura/body/BodySelfCollision.hpp"
#include "aura/physics/Collision.hpp"

namespace aura::app {
    inline constexpr double FIXED_DT = 1.0 / 120.0;
    inline constexpr int jointIterations = 16;

    // Optional read-only stage samples for attributing position/momentum changes.
    using SimulationTrace = std::function<void(std::string_view stage, std::string_view joint)>;

    // The single application configuration. Historical stepping stays in tests.
    // Call once per fixed tick; pose projections never imply velocity changes.
    void stepSimulation(body::AuraBody3D &auraBody, const body::AuraSkeleton3D &skeleton,
                        std::span<BodyJoint> joints, std::span<body::BodyPart3D * const> parts,
                        const std::vector<body::BodyCollisionPair> &bodyPairs,
                        std::span<const physics::RoomBody> roomBodies, const physics::RoomBounds &room,
                        BodyDiagnosticHistory &diagnosticHistory,
                        physics::FloorFrictionModel floorFriction = physics::FloorFrictionModel::Coulomb,
                        const SimulationTrace &trace = {});
}
