#pragma once
#include <array>

#include "../../../src/app/BodyDiagnostics.hpp"
#include "aura/app/BodyJoint.hpp"
#include "aura/body/AuraBody3D.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/BodySelfCollision.hpp"

namespace aura::sim {
    class AuraSimulation {
    public:
        AuraSimulation();

        body::AuraBody3D &body();

        body::AuraSkeleton3D &getSkeleton();

        std::array<app::BodyJoint, 15> &getJoints();

        std::array<body::BodyPart3D *, 16> &getParts();

        [[nodiscard]] const std::vector<body::BodyCollisionPair> &getBodyPairs() const;

        [[nodiscard]] const physics::RoomBounds &getRoom() const;

        std::array<physics::RoomBody, 16> &getRoomBodies();

        app::BodyDiagnosticHistory &getDiagnosticHistory();

        [[nodiscard]] double time() const;

        void step();

        void reset();

    private:
        body::AuraBody3D auraBody;
        body::AuraSkeleton3D skeleton;
        std::array<app::BodyJoint, 15> joints;
        std::array<body::BodyPart3D *, 16> parts;
        std::vector<body::BodyCollisionPair> bodyPairs;
        physics::RoomBounds room;
        std::array<physics::RoomBody, 16> roomBodies;
        app::BodyDiagnosticHistory diagnosticHistory;
        double simulatedTime = 0.0;
    };
}
