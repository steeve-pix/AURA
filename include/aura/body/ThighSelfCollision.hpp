#pragma once
#include <array>
#include <cstdint>
#include "AuraBody3D.hpp"
#include "AuraSkeleton3D.hpp"

namespace aura::body {
    enum class ThighFloorPolicy { NoWorsening, StrictClearance };
    // Flat floor at Y=0. Existing penetration may persist, but may not deepen.
    bool floorMoveIsNoWorse(float beforeY, float afterY, float tolerance = 1e-6f);
    struct ThighCollisionStep {
        float penetrationBefore{}, penetrationAfter{};
        float leftAngle{}, rightAngle{};
        float leftKneeAngle{}, rightKneeAngle{};
        bool corrected{};
    };
    enum ThighCandidateRejection : std::uint32_t {
        LeftFootFloor = 1u << 0, RightFootFloor = 1u << 1,
        LeftOtherLegFloor = 1u << 2, RightOtherLegFloor = 1u << 3,
        LeftJointLimit = 1u << 4, RightJointLimit = 1u << 5,
        AnchorGapDrift = 1u << 6
    };
    struct ThighCandidateAudit {
        float leftAngle{}, rightAngle{}, penetration{};
        float leftFootY{}, rightFootY{}, leftMinimumY{}, rightMinimumY{};
        float leftLimitError{}, rightLimitError{};
        std::array<float,6> lowestY{};
        std::array<float,6> jointLimitErrors{}; // left hip/knee/ankle, right hip/knee/ankle
        std::uint32_t rejections{};
        bool improves{};
        bool kneeAttempted{}, kneeFeasible{};
        float leftKneeAngle{}, rightKneeAngle{};
    };
    struct ThighCollisionAudit {
        std::array<ThighCandidateAudit,24> candidates{};
        ThighCandidateAudit initial;
        int candidateCount{}, improvingCandidates{}, feasibleImprovingCandidates{};
        std::uint32_t improvingRejections{};
        bool degenerateNormal{}, degenerateAxis{};
        // New collisions are not part of this single-pair search's feasibility policy.
    };
    // Experimental single-pair pose projection. One bounded search per call;
    // preserves velocities and rejects new/worsened floor penetration or leg twist-limit errors.
    // This checks the existing twist limits, not a full anatomical hip constraint.
    ThighCollisionStep correctThighCapsuleOverlap(
        AuraBody3D& body, const AuraSkeleton3D& skeleton,
        float maxAngleStep = 0.01f, float penetrationTolerance = 0.001f,
        ThighCollisionAudit* audit = nullptr,
        ThighFloorPolicy floorPolicy = ThighFloorPolicy::NoWorsening,
        bool compensateKnees = false);
}
