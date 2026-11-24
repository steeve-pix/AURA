#pragma once
#include "aura/math/Vec3.hpp"
#include <string_view>

namespace aura::sim {
    enum class BalanceState { Balanced, Unbalanced, Fallen };

    std::string_view balanceStateName(BalanceState state);

    struct StandingEnvelope {
        double pelvisTiltDegrees, torsoTiltDegrees;
        double pelvisHeightRatio, torsoHeightRatio, comHeightRatio;
        double verticalSpeed, horizontalSpeed, pelvisAngularSpeed, torsoAngularSpeed;
        double minimumSupportMargin;
    };

    // Initial standing-task calibration, not a universal recovery/viability model.
    // Heights are relative to the neutral, feet-on-floor body geometry, not the drop.
    struct BalanceConfig {
        StandingEnvelope enter{15, 20, .8, .8, .8, .35, .35, .4, .4, 0};
        StandingEnvelope stay{20, 25, .75, .75, .75, .6, .5, .65, .65, -.02};
        double enterSeconds = .15, leaveSeconds = .05, collapseSeconds = .20;
        double collapseTorsoTiltDegrees = 60;
        double lowPelvisRatio = .35, lowTorsoRatio = .25, lowComRatio = .30;
        double contactPelvisRatio = .5, contactTorsoRatio = .35;
        double dangerousTiltDegrees = 45, dangerousDownwardSpeed = 2, dangerousAngularSpeed = 2;
    };

    struct BalanceMeasurements {
        double pelvisUp = 1, torsoUp = 1;
        double pelvisHeightRatio = 1, torsoHeightRatio = 1, comHeightRatio = 1;
        math::Vec3 comVelocity{};
        double pelvisAngularSpeed = 0, torsoAngularSpeed = 0;
        bool leftFootContact = false, rightFootContact = false;
        bool supportPolygonValid = false;
        double supportMargin = 0;
        bool torsoContact = false, headContact = false;
    };

    struct BalanceAssessment {
        BalanceState state = BalanceState::Unbalanced;
        double stateDuration = 0;
        double enteringDuration = 0, outsideDuration = 0, collapseDuration = 0;
        bool supported = false, enterEnvelope = false, stayEnvelope = false;
        bool deepCollapse = false, contactCollapse = false, dangerousMotion = false;
    };

    class BalanceClassifier {
    public:
        explicit BalanceClassifier(BalanceConfig config = {});

        void reset();

        void update(const BalanceMeasurements &measurements, double dt);

        const BalanceAssessment &assessment() const { return result; }
        const BalanceConfig &config() const { return settings; }

    private:
        BalanceConfig settings;
        BalanceAssessment result;
    };
}
