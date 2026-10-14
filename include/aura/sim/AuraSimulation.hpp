#pragma once
#include <array>
#include "ActionSpec.hpp"
#include <string>
#include <string_view>
#include <vector>

#include "../../../src/app/BodyDiagnostics.hpp"
#include "aura/app/BodyJoint.hpp"
#include "aura/body/AuraBody3D.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/BodySelfCollision.hpp"
#include "aura/physics/SupportPolygon.hpp"

namespace aura::sim {
    struct BodyPartObservation {
        std::string name;
        math::Vec3 velocity;
    };

    struct JointObservation {
        std::string name;
        math::Quaternion relativeOrientation;
        math::Vec3 relativeAngularVelocity;
    };

    struct AuraObservation {
        math::Vec3 pelvisPosition;
        math::Vec3 pelvisVelocity;
        math::Quaternion pelvisOrientation;
        math::Vec3 pelvisAngularVelocity;

        math::Vec3 leftFootPosition;
        math::Vec3 rightFootPosition;

        math::Vec3 pelvisUp;

        math::Vec3 centerOfMassPosition;
        math::Vec3 centerOfMassVelocity;

        bool leftFootGrounded = false;
        bool rightFootGrounded = false;

        math::Vec3 leftFootVelocity{};
        math::Vec3 rightFootVelocity{};

        physics::SupportPolygon supportPolygon;

        std::vector<math::Vec3> leftFootContactPoints;
        std::vector<math::Vec3> rightFootContactPoints;

        std::vector<BodyPartObservation> bodyPartStates;
        std::vector<JointObservation> jointStates;

        bool centerOfMassInsideSupportPolygon = false;
        float supportMargin = 0.0f;
    };

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

        [[nodiscard]] math::Vec3 pelvisPosition() const;

        [[nodiscard]] math::Vec3 pelvisVelocity() const;

        [[nodiscard]] math::Quaternion pelvisOrientation() const;

        [[nodiscard]] math::Vec3 pelvisAngularVelocity() const;

        [[nodiscard]] AuraObservation observation() const;

        void setAction(const AuraAction &action);

        // Evaluation controls; changing a flag preserves targets and motor gains.
        void setMotorEnabled(std::string_view jointName, bool enabled);


        void step(const AuraAction &action);

        [[nodiscard]] const AuraAction &action() const;

        [[nodiscard]] float pelvisHeight() const;

        [[nodiscard]] bool isStanding() const;

        [[nodiscard]] float pelvisUprightness() const;

        [[nodiscard]] std::size_t steps() const;

        [[nodiscard]] static constexpr std::size_t episodeLimit() { return MAX_EPISODE_STEPS; }

        [[nodiscard]] bool isTruncated() const;

        [[nodiscard]] bool isDone() const;

    private:
        static constexpr std::size_t MAX_EPISODE_STEPS = 1200;
        body::AuraBody3D auraBody;
        body::AuraSkeleton3D skeleton;
        std::array<app::BodyJoint, 15> joints;
        std::array<std::size_t, actionSpecs.size()> actionJointIndices{};
        std::array<body::BodyPart3D *, 16> parts;
        std::vector<body::BodyCollisionPair> bodyPairs;
        physics::RoomBounds room;
        std::array<physics::RoomBody, 16> roomBodies;
        app::BodyDiagnosticHistory diagnosticHistory;
        double simulatedTime = 0.0;
        AuraAction currentAction;
        float initialPelvisHeight = 0.0f;
        std::size_t stepCount = 0;
    };
}
