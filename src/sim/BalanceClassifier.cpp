#include "aura/sim/BalanceClassifier.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace aura::sim {
    namespace {
        double upAt(double degrees) { return std::cos(degrees * std::numbers::pi / 180.); }

        bool within(const BalanceMeasurements &m, const StandingEnvelope &e) {
            return (m.leftFootContact || m.rightFootContact) && m.supportPolygonValid &&
                   m.supportMargin >= e.minimumSupportMargin &&
                   m.pelvisUp >= upAt(e.pelvisTiltDegrees) && m.torsoUp >= upAt(e.torsoTiltDegrees) &&
                   m.pelvisHeightRatio >= e.pelvisHeightRatio && m.torsoHeightRatio >= e.torsoHeightRatio &&
                   m.comHeightRatio >= e.comHeightRatio && std::abs(m.comVelocity.y) <= e.verticalSpeed &&
                   std::hypot(m.comVelocity.x, m.comVelocity.z) <= e.horizontalSpeed &&
                   m.pelvisAngularSpeed <= e.pelvisAngularSpeed && m.torsoAngularSpeed <= e.torsoAngularSpeed;
        }

        void validate(const StandingEnvelope &e) {
            for (auto v: {
                     e.pelvisTiltDegrees, e.torsoTiltDegrees, e.pelvisHeightRatio, e.torsoHeightRatio,
                     e.comHeightRatio, e.verticalSpeed, e.horizontalSpeed, e.pelvisAngularSpeed, e.torsoAngularSpeed
                 })
                if (!std::isfinite(v) || v < 0) throw std::invalid_argument("Invalid standing envelope");
            if (e.pelvisTiltDegrees > 180 || e.torsoTiltDegrees > 180 || !std::isfinite(e.minimumSupportMargin))
                throw std::invalid_argument("Invalid standing envelope angle or margin");
        }
    }

    std::string_view balanceStateName(BalanceState state) {
        switch (state) {
            case BalanceState::Balanced: return "BALANCED";
            case BalanceState::Unbalanced: return "UNBALANCED";
            case BalanceState::Fallen: return "FALLEN";
        }
        return "UNBALANCED";
    }

    BalanceClassifier::BalanceClassifier(BalanceConfig config) : settings(config) {
        validate(settings.enter);
        validate(settings.stay);
        for (auto v: {
                 settings.enterSeconds, settings.leaveSeconds, settings.collapseSeconds,
                 settings.lowPelvisRatio, settings.lowTorsoRatio, settings.lowComRatio,
                 settings.contactPelvisRatio, settings.contactTorsoRatio,
                 settings.dangerousDownwardSpeed, settings.dangerousAngularSpeed
             })
            if (!std::isfinite(v) || v <= 0) throw std::invalid_argument("Invalid balance configuration");
        for (auto v: {settings.collapseTorsoTiltDegrees, settings.dangerousTiltDegrees})
            if (!std::isfinite(v) || v <= 0 || v > 180) throw std::invalid_argument("Invalid balance angle");
        const auto &a = settings.enter, &b = settings.stay;
        if (a.pelvisTiltDegrees > b.pelvisTiltDegrees || a.torsoTiltDegrees > b.torsoTiltDegrees ||
            a.pelvisHeightRatio < b.pelvisHeightRatio || a.torsoHeightRatio < b.torsoHeightRatio ||
            a.comHeightRatio < b.comHeightRatio || a.verticalSpeed > b.verticalSpeed ||
            a.horizontalSpeed > b.horizontalSpeed || a.pelvisAngularSpeed > b.pelvisAngularSpeed ||
            a.torsoAngularSpeed > b.torsoAngularSpeed || a.minimumSupportMargin < b.minimumSupportMargin)
            throw std::invalid_argument("Re-entry envelope must be stricter than retention envelope");
    }

    void BalanceClassifier::reset() { result = {}; }

    void BalanceClassifier::update(const BalanceMeasurements &m, double dt) {
        if (!std::isfinite(dt) || dt <= 0) throw std::invalid_argument("Balance tick must be finite and positive");
        // Invalid physical measurements are not evidence of a fall. The existing
        // encoder/diagnostic safety path handles them; classification stays passive.
        const bool valid = std::isfinite(m.pelvisUp) && std::isfinite(m.torsoUp) &&
                           std::isfinite(m.pelvisHeightRatio) && std::isfinite(m.torsoHeightRatio) && std::isfinite(
                               m.comHeightRatio) &&
                           std::isfinite(m.comVelocity.x) && std::isfinite(m.comVelocity.y) && std::isfinite(
                               m.comVelocity.z) &&
                           std::isfinite(m.pelvisAngularSpeed) && std::isfinite(m.torsoAngularSpeed) && std::isfinite(
                               m.supportMargin);
        const auto previous = result.state;
        result.supported = valid && (m.leftFootContact || m.rightFootContact) && m.supportPolygonValid;
        result.enterEnvelope = valid && within(m, settings.enter);
        result.stayEnvelope = valid && within(m, settings.stay);
        const bool tilted = valid && m.torsoUp <= upAt(settings.collapseTorsoTiltDegrees);
        result.deepCollapse = tilted && m.pelvisHeightRatio <= settings.lowPelvisRatio &&
                              m.torsoHeightRatio <= settings.lowTorsoRatio && m.comHeightRatio <= settings.lowComRatio;
        result.contactCollapse = tilted && (m.torsoContact || m.headContact) &&
                                 m.pelvisHeightRatio <= settings.contactPelvisRatio && m.torsoHeightRatio <= settings.
                                 contactTorsoRatio;
        result.dangerousMotion = valid && (m.pelvisUp <= upAt(settings.dangerousTiltDegrees) ||
                                           m.torsoUp <= upAt(settings.dangerousTiltDegrees) || m.comVelocity.y <= -
                                           settings.dangerousDownwardSpeed ||
                                           m.pelvisAngularSpeed >= settings.dangerousAngularSpeed || m.torsoAngularSpeed
                                           >= settings.dangerousAngularSpeed);
        if (previous != BalanceState::Fallen) {
            result.enteringDuration = result.enterEnvelope ? result.enteringDuration + dt : 0;
            result.outsideDuration = result.stayEnvelope ? 0 : result.outsideDuration + dt;
            result.collapseDuration = (result.deepCollapse || result.contactCollapse)
                                          ? result.collapseDuration + dt
                                          : 0;
            // Small time-comparison allowance is floating-point arithmetic only.
            if (result.collapseDuration + 1e-12 >= settings.collapseSeconds) result.state = BalanceState::Fallen;
            else if (previous == BalanceState::Balanced && result.outsideDuration + 1e-12 >= settings.leaveSeconds)
                result.state = BalanceState::Unbalanced;
            else if (previous == BalanceState::Unbalanced && result.enteringDuration + 1e-12 >= settings.enterSeconds)
                result.state = BalanceState::Balanced;
        }
        result.stateDuration = result.state == previous ? result.stateDuration + dt : 0;
    }
}
