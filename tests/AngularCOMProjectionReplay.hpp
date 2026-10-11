#pragma once
// Copied-state search only. No production angular geometry is changed.
#include "JointCOMProjectionReplay.hpp"
#include "aura/body/Joint3D.hpp"
#include "aura/body/JointConstraint.hpp"
#include <limits>

namespace angular_com_replay {
    using namespace aura;

    struct Edge {
        size_t a, b;
        const body::Joint3D *joint;
        const char *name;
    };

    inline auto edges(const body::AuraSkeleton3D &s) {
        return std::array<Edge, 15>{
            {
                {2, 3, &s.waist, "waist"}, {2, 1, &s.neck, "neck"}, {1, 0, &s.head, "head"},
                {2, 4, &s.leftShoulder, "leftShoulder"}, {4, 5, &s.leftElbow, "leftElbow"},
                {5, 6, &s.leftWrist, "leftWrist"},
                {2, 7, &s.rightShoulder, "rightShoulder"}, {7, 8, &s.rightElbow, "rightElbow"},
                {8, 9, &s.rightWrist, "rightWrist"},
                {3, 10, &s.leftHip, "leftHip"}, {10, 11, &s.leftKnee, "leftKnee"}, {11, 12, &s.leftAnkle, "leftAnkle"},
                {3, 13, &s.rightHip, "rightHip"}, {13, 14, &s.rightKnee, "rightKnee"},
                {14, 15, &s.rightAnkle, "rightAnkle"}
            }
        };
    }

    inline double rotationDistance(math::Quaternion x, math::Quaternion y) {
        auto q = (x.normalized() * y.normalized().conjugate()).normalized();
        return 2 * std::atan2(std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), std::abs(q.w));
    }

    inline double limitError(const body::BodyPart3D &a, const body::BodyPart3D &b, const body::Joint3D &j) {
        double error = std::abs(body::jointAngleError(a, b, j));
        if (j.type == body::JointType::SwingTwist) {
            const auto swing = body::relativeJointSwingAngles(a, b, j);
            error = std::max({
                error, double(j.minSwingY - swing.y), double(swing.y - j.maxSwingY), double(j.minSwingZ - swing.z),
                double(swing.z - j.maxSwingZ)
            });
        }
        return error;
    }

    inline bool report(body::AuraBody3D input, body::AuraBody3D actual, const body::AuraSkeleton3D &skeleton,
                       const Edge &cut) {
        auto original = joint_com_replay::parts(input), live = joint_com_replay::parts(actual);
        const auto relative = [](auto *a, auto *b) {
            return (a->body.orientation.normalized().conjugate() * b->body.orientation.normalized()).normalized();
        };
        const auto target = relative(live[cut.a], live[cut.b]);
        auto delta = (original[cut.a]->body.orientation.normalized() * target * original[cut.b]->body.orientation.
                      normalized().conjugate()).normalized();
        if (delta.w < 0)delta = {-delta.w, -delta.x, -delta.y, -delta.z};
        const double length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (length < 1e-12)return false;
        const double angle = 2 * std::atan2(length, delta.w);
        const math::Vec3 axis{float(delta.x / length), float(delta.y / length), float(delta.z / length)};
        // Use the same child-side world anchor as the live component correction.
        // A pre-existing cut gap is
        // retained in magnitude; rotating components is not an attachment repair.
        const auto pivot = body::localToWorldPoint(*original[cut.b], cut.joint->localAnchorB);
        auto ca = skeleton.collectComponent(input, *original[cut.a], *cut.joint), cb = skeleton.collectComponent(
            input, *original[cut.b], *cut.joint);
        double massA = 0, massB = 0;
        for (auto *p: ca)massA += p->body.mass;
        for (auto *p: cb)massB += p->body.mass;
        const auto start = joint_com_replay::center(input);
        struct Metrics {
            bool floor = true, strictLimits = true, noWorseLimits = true, unchanged = true;
            double horizontal = 0, full = 0, relativeError = 0, gapDrift = 0, internalRotationDrift = 0, maxLimit = 0,
                    minY = 1e30;
            std::array<double, 3> com{};
            float angle = 0;
        };
        const auto evaluate = [&](body::AuraBody3D &candidate, bool printBodies) {
            Metrics m;
            const auto ps = joint_com_replay::parts(candidate);
            const auto com = joint_com_replay::center(candidate);
            for (size_t k = 0; k < 3; ++k)m.com[k] = com[k] - start[k];
            m.horizontal = std::hypot(m.com[0], m.com[2]);
            m.full = std::hypot(m.horizontal, m.com[1]);
            m.relativeError = rotationDistance(relative(ps[cut.a], ps[cut.b]), target);
            m.angle = body::relativeJointAngle(*ps[cut.a], *ps[cut.b], *cut.joint);
            for (size_t i = 0; i < ps.size(); ++i) {
                const auto &old = original[i]->body;
                const auto &now = ps[i]->body;
                const float before = physics::lowestPoint(old, original[i]->size).y, after = physics::lowestPoint(
                    now, ps[i]->size).y;
                const bool safe = after >= std::min(0.0f, before) - 1e-6f;
                m.floor &= safe;
                m.minY = std::min(m.minY, double(after));
                const auto same = [](auto x, auto y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
                m.unchanged &= same(old.velocity, now.velocity) && same(old.angularVelocity, now.angularVelocity);
                if (printBodies)std::cout << "angularShareFloor," << ps[i]->name << ',' << before << ',' << after << ','
                                << safe << '\n';
            }
            for (const auto &e: edges(skeleton)) {
                const auto gap = [&](auto p) {
                    return (body::localToWorldPoint(*p[e.b], e.joint->localAnchorB) - body::localToWorldPoint(
                                *p[e.a], e.joint->localAnchorA)).length();
                };
                m.gapDrift = std::max(m.gapDrift, std::abs(double(gap(ps) - gap(original))));
                const double before = limitError(*original[e.a], *original[e.b], *e.joint), after = limitError(
                    *ps[e.a], *ps[e.b], *e.joint);
                m.maxLimit = std::max(m.maxLimit, after);
                m.strictLimits &= after <= 1e-6;
                if (e.joint != cut.joint) {
                    m.noWorseLimits &= after <= before + 1e-6;
                    m.internalRotationDrift = std::max(m.internalRotationDrift,
                                                       rotationDistance(relative(ps[e.a], ps[e.b]),
                                                                        relative(original[e.a], original[e.b])));
                } else m.noWorseLimits &= after <= 1e-6;
            }
            return m;
        };
        const auto trial = [&](double share) {
            auto copy = input;
            auto p = joint_com_replay::parts(copy);
            body::rotateSubtreeAroundWorldPoint(skeleton.collectComponent(copy, *p[cut.a], *cut.joint), pivot,
                                                math::Quaternion::fromAxisAngle(axis, float(-share * angle)));
            body::rotateSubtreeAroundWorldPoint(skeleton.collectComponent(copy, *p[cut.b], *cut.joint), pivot,
                                                math::Quaternion::fromAxisAngle(axis, float((1 - share) * angle)));
            return copy;
        };
        const auto print = [&](const char *label, double share, const Metrics &m) {
            std::cout << "angularShareCandidate," << label << ",s=" << share << ",deltaCOM=" << m.com[0] << ',' << m.com
                    [1] << ',' << m.com[2]
                    << ",horizontal=" << m.horizontal << ",full=" << m.full << ",floorSafe=" << m.floor << ",minimumY="
                    << m.minY
                    << ",allLimits=" << m.strictLimits << ",noWorseLimits=" << m.noWorseLimits << ",maxLimitError=" << m
                    .maxLimit
                    << ",angle=" << m.angle << ",targetRelativeError=" << m.relativeError << ",gapDrift=" << m.gapDrift
                    << ",internalRotationDrift=" << m.internalRotationDrift << ",velocitiesUnchanged=" << m.unchanged <<
                    '\n';
        };
        std::cout << std::scientific << std::setprecision(17) << "angularShareSetup,joint=" << cut.name << ",delta=" <<
                angle << ",axis=" << axis.x << ',' << axis.y << ',' << axis.z
                << ",pivot=" << pivot.x << ',' << pivot.y << ',' << pivot.z << ",massA=" << massA << ",massB=" << massB
                << '\n';
        const float angleBefore = body::relativeJointAngle(*original[cut.a], *original[cut.b], *cut.joint);
        const auto swingBefore = body::relativeJointSwingAngles(*original[cut.a], *original[cut.b], *cut.joint);
        const auto swingAfter = body::relativeJointSwingAngles(*live[cut.a], *live[cut.b], *cut.joint);
        const float cutGapBefore = (body::localToWorldPoint(*original[cut.b], cut.joint->localAnchorB) -
                                    body::localToWorldPoint(*original[cut.a], cut.joint->localAnchorA)).length();
        std::cout << "angularShareJoint,angleBefore=" << angleBefore << ",angleAfter=" << body::relativeJointAngle(
                    *live[cut.a], *live[cut.b], *cut.joint)
                << ",swingBefore=" << swingBefore.y << ',' << swingBefore.z << ",swingAfter=" << swingAfter.y << ',' <<
                swingAfter.z << ",cutGapBefore=" << cutGapBefore << '\n';
        print("live", -1, evaluate(actual, false));
        auto baseline = trial(0);
        auto baselineParts = joint_com_replay::parts(baseline);
        double baselinePoseDifference = 0;
        for (size_t i = 0; i < live.size(); ++i)
            baselinePoseDifference = std::max(baselinePoseDifference,
                                              double(
                                                  (baselineParts[i]->body.position - live[i]->body.position).length()));
        print("s0", 0, evaluate(baseline, false));
        double bestShare = -1, bestStrict = -1;
        Metrics bestMetrics, strictMetrics;
        bestMetrics.horizontal = strictMetrics.horizontal = std::numeric_limits<double>::infinity();
        size_t trials = 0, floorRejected = 0, strictRejected = 0;
        bool allPreserved = true;
        const auto search = [&](double s) {
            auto copy = trial(s);
            const auto m = evaluate(copy, false);
            const bool preserved = m.relativeError < 2e-6 && m.gapDrift < 2e-6 && m.internalRotationDrift < 2e-6 && m.
                                   unchanged;
            ++trials;
            floorRejected += !m.floor;
            strictRejected += !m.strictLimits;
            allPreserved &= preserved;
            std::cout << "angularShareTrial," << s << ',' << m.horizontal << ',' << m.floor << ',' << m.strictLimits <<
                    ',' << m.noWorseLimits << ',' << preserved << '\n';
            if (preserved && m.floor && m.noWorseLimits && m.horizontal < bestMetrics.horizontal) {
                bestShare = s;
                bestMetrics = m;
            }
            if (preserved && m.floor && m.strictLimits && m.horizontal < strictMetrics.horizontal) {
                bestStrict = s;
                strictMetrics = m;
            }
        };
        for (int i = 0; i <= 100; ++i)search(i * .01);
        if (bestShare >= 0) {
            const double lo = std::max(0.0, bestShare - .01), hi = std::min(1.0, bestShare + .01);
            for (int i = 0; i <= 200; ++i)search(lo + (hi - lo) * i / 200);
        }
        for (double share: {.1, .5, 1.0}) {
            auto copy = trial(share);
            print("referenceShare", share, evaluate(copy, false));
        }
        if (bestStrict >= 0)print("bestStrict", bestStrict, strictMetrics);
        if (bestShare >= 0) {
            print("bestPreserving", bestShare, bestMetrics);
            auto copy = trial(bestShare);
            evaluate(copy, true);
        }
        for (const auto &e: edges(skeleton)) {
            const double err = limitError(*original[e.a], *original[e.b], *e.joint);
            if (err > 1e-6)std::cout << "angularShareInputLimitViolation," << e.name << ',' << err << '\n';
        }
        std::cout << "angularShareAudit,trials=" << trials << ",floorRejected=" << floorRejected <<
                ",strictLimitRejected=" << strictRejected
                << ",allGeometryPreserved=" << allPreserved << ",s0LivePoseDifference=" << baselinePoseDifference <<
                '\n';
        std::cout << "angularShareResult,strictFeasible=" << (bestStrict >= 0) << ",preservingFeasible=" << (
            bestShare >= 0) << '\n';
        return bestShare >= 0 && allPreserved && baselinePoseDifference < 2e-6;
    }
}
