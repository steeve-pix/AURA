#include "aura/body/ThighSelfCollision.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/SelfCollision.hpp"
#include "aura/physics/Impulse.hpp"
#include "aura/physics/Inertia.hpp"
#include "aura/physics/MechanicalState.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace aura::body {
    ThighNormalImpulseStep resolveThighNormalVelocity(AuraBody3D &b, float tolerance) {
        ThighNormalImpulseStep result;
        const auto left = physics::bodyPartCollisionCapsule(b.leftThigh);
        const auto right = physics::bodyPartCollisionCapsule(b.rightThigh);
        result.penetration = physics::capsulePenetration(left, right);
        if (result.penetration <= tolerance)return result;
        const auto points = physics::closestPointsBetweenSegments(left.pointA, left.pointB, right.pointA, right.pointB);
        const auto connector = points.pointB - points.pointA;
        if (connector.lengthSquared() < 1e-12f) {
            result.degenerateNormal = true;
            return result;
        }
        const auto normal = connector.normalized();
        auto &a = b.leftThigh.body;
        auto &c = b.rightThigh.body;
        const auto speed = [&] {
            return (physics::velocityAtWorldPoint(c, points.pointB) -
                    physics::velocityAtWorldPoint(a, points.pointA)).dot(normal);
        };
        result.normalSpeedBefore = result.normalSpeedAfter = speed();
        if (result.normalSpeedBefore >= 0)return result;
        const auto rotational = [&](const auto &rb, const auto &point) {
            const auto r = point - rb.position;
            return physics::applyInverseInertiaWorld(rb, r.cross(normal)).cross(r).dot(normal);
        };
        const float inverseMass = 1.0f / a.mass + 1.0f / c.mass + rotational(a, points.pointA) + rotational(
                                      c, points.pointB);
        if (!(inverseMass > 0) || !std::isfinite(inverseMass))return result;
        const auto energy = [&] {
            const auto x = physics::mechanicalState(a), y = physics::mechanicalState(c);
            return double(x.linearKinetic) + x.rotationalKinetic + double(y.linearKinetic) + y.rotationalKinetic;
        };
        const auto momentum = [&] {
            return std::array<double, 3>{
                double(a.mass) * a.velocity.x + double(c.mass) * c.velocity.x,
                double(a.mass) * a.velocity.y + double(c.mass) * c.velocity.y,
                double(a.mass) * a.velocity.z + double(c.mass) * c.velocity.z
            };
        };
        const double beforeEnergy = energy();
        const auto beforeMomentum = momentum();
        result.impulseMagnitude = -result.normalSpeedBefore / inverseMass;
        const auto impulse = normal * result.impulseMagnitude;
        physics::applyImpulseAtPoint(a, -impulse, points.pointA);
        physics::applyImpulseAtPoint(c, impulse, points.pointB);
        result.applied = true;
        result.normalSpeedAfter = speed();
        result.kineticEnergyChange = energy() - beforeEnergy;
        result.predictedWork = double(result.normalSpeedBefore) * result.impulseMagnitude +
                               0.5 * inverseMass * double(result.impulseMagnitude) * result.impulseMagnitude;
        const auto afterMomentum = momentum();
        for (size_t i = 0; i < 3; ++i)result.momentumChange[i] = afterMomentum[i] - beforeMomentum[i];
        return result;
    }

    bool floorMoveIsNoWorse(float beforeY, float afterY, float tolerance) {
        return beforeY >= -tolerance ? afterY >= -tolerance : afterY >= beforeY - tolerance;
    }

    namespace {
        float penetration(const AuraBody3D &b) {
            return physics::capsulePenetration(physics::bodyPartCollisionCapsule(b.leftThigh),
                                               physics::bodyPartCollisionCapsule(b.rightThigh));
        }

        std::array<float, 6> gaps(const AuraBody3D &b, const AuraSkeleton3D &s) {
            const std::array<const BodyPart3D *, 6> parents{
                &b.pelvis, &b.leftThigh, &b.leftShin, &b.pelvis, &b.rightThigh, &b.rightShin
            };
            const std::array<const BodyPart3D *, 6> children{
                &b.leftThigh, &b.leftShin, &b.leftFoot, &b.rightThigh, &b.rightShin, &b.rightFoot
            };
            const std::array<const Joint3D *, 6> joints{
                &s.leftHip, &s.leftKnee, &s.leftAnkle, &s.rightHip, &s.rightKnee, &s.rightAnkle
            };
            std::array<float, 6> result{};
            for (size_t i = 0; i < 6; ++i)
                result[i] = (localToWorldPoint(*children[i], joints[i]->localAnchorB) - localToWorldPoint(
                                 *parents[i], joints[i]->localAnchorA)).length();
            return result;
        }

        ThighCandidateAudit inspect(const AuraBody3D &b, const AuraSkeleton3D &s,
                                    const std::array<float, 6> &originalGaps,
                                    const std::array<float, 6> *beforeY = nullptr) {
            const std::array<const BodyPart3D *, 6> parents{
                &b.pelvis, &b.leftThigh, &b.leftShin, &b.pelvis, &b.rightThigh, &b.rightShin
            };
            const std::array<const BodyPart3D *, 6> children{
                &b.leftThigh, &b.leftShin, &b.leftFoot, &b.rightThigh, &b.rightShin, &b.rightFoot
            };
            const std::array<const Joint3D *, 6> joints{
                &s.leftHip, &s.leftKnee, &s.leftAnkle, &s.rightHip, &s.rightKnee, &s.rightAnkle
            };
            const auto candidateGaps = gaps(b, s);
            ThighCandidateAudit result;
            result.leftMinimumY = result.rightMinimumY = 1e30f;
            for (size_t i = 0; i < 6; ++i) {
                const bool left = i < 3;
                const float y = physics::lowestPoint(children[i]->body, children[i]->size, children[i]->floorShape).y;
                result.lowestY[i] = y;
                auto &minimum = left ? result.leftMinimumY : result.rightMinimumY;
                minimum = std::min(minimum, y);
                if (i == 2) result.leftFootY = y;
                if (i == 5) result.rightFootY = y;
                if (beforeY ? !floorMoveIsNoWorse((*beforeY)[i], y) : y < -1e-6f) result.rejections |= i == 2
                    ? LeftFootFloor
                    : i == 5
                          ? RightFootFloor
                          : left
                                ? LeftOtherLegFloor
                                : RightOtherLegFloor;
                const float error = std::abs(jointAngleError(*parents[i], *children[i], *joints[i]));
                result.jointLimitErrors[i] = error;
                auto &limit = left ? result.leftLimitError : result.rightLimitError;
                limit = std::max(limit, error);
                if (error > 1e-5f) result.rejections |= left ? LeftJointLimit : RightJointLimit;
                if (std::abs(candidateGaps[i] - originalGaps[i]) > 1e-5f) result.rejections |= AnchorGapDrift;
            }
            return result;
        }

        // Bounded SwingTwist experiment: reuse the copied replay's knee search.
        // The thigh/hip stay fixed; every trial starts from the same shin/foot pose.
        bool compensateSwingFoot(AuraBody3D &candidate, const AuraSkeleton3D &s, bool left,
                                 const ThighCandidateAudit &initial, const std::array<float, 6> &originalGaps,
                                 float &appliedAngle) {
            const auto input = candidate;
            const auto &thigh = left ? input.leftThigh : input.rightThigh;
            const auto &knee = left ? s.leftKnee : s.rightKnee;
            const int first = left ? 0 : 3, foot = first + 2;
            const auto pivot = localToWorldPoint(thigh, knee.localAnchorA);
            const auto axis = thigh.body.orientation.normalized().rotate(knee.hingeAxis.normalized()).normalized();
            const auto evaluate = [&](double angle, AuraBody3D &copy) {
                copy = input;
                auto &shin = left ? copy.leftShin : copy.rightShin;
                rotateSubtreeAroundWorldPoint(s.collectComponent(copy, shin, knee), pivot,
                                              math::Quaternion::fromAxisAngle(axis, static_cast<float>(angle)));
                return inspect(copy, s, originalGaps, &initial.lowestY);
            };
            const auto feasible = [&](const ThighCandidateAudit &m) {
                for (int i = first; i < first + 3; ++i)
                    if (!floorMoveIsNoWorse(initial.lowestY[i], m.lowestY[i]) || m.jointLimitErrors[i] > 1e-5f)return
                            false;
                return (m.rejections & AnchorGapDrift) == 0;
            };
            constexpr double probe = 0.001;
            AuraBody3D copy;
            const auto positive = evaluate(probe, copy), negative = evaluate(-probe, copy);
            const double direction = positive.lowestY[foot] >= negative.lowestY[foot] ? 1.0 : -1.0;
            const auto &inputFoot = left ? input.leftFoot : input.rightFoot;
            const float oldFootY = physics::lowestPoint(inputFoot.body, inputFoot.size, inputFoot.floorShape).y;
            if (std::max(positive.lowestY[foot], negative.lowestY[foot]) <= oldFootY)return false;
            const float angle = relativeJointAngle(thigh, left ? input.leftShin : input.rightShin, knee);
            const double available = direction > 0 ? knee.maxAngle - angle : angle - knee.minAngle;
            double low = 0, high = std::min(probe, available);
            auto best = input;
            bool found = false;
            while (high > 0) {
                if (feasible(evaluate(direction * high, copy))) {
                    best = copy;
                    found = true;
                    break;
                }
                if (high >= available)break;
                low = high;
                high = std::min(high * 2.0, available);
            }
            if (!found)return false;
            for (int i = 0; i < 40; ++i) {
                const double mid = (low + high) * 0.5;
                if (feasible(evaluate(direction * mid, copy))) {
                    high = mid;
                    best = copy;
                } else low = mid;
            }
            candidate = best;
            appliedAngle = static_cast<float>(direction * high);
            return true;
        }

        // Work on a copy of the already hip-rotated pose. Knee motion leaves both
        // thighs untouched. Probe both hinge directions and retain a feasible
        // upper bracket during bisection; never apply an unverified trial.
        bool compensateFoot(AuraBody3D &candidate, const AuraSkeleton3D &s, bool left,
                            const ThighCandidateAudit &initial, const std::array<float, 6> &originalGaps,
                            float &appliedAngle) {
            const auto input = candidate;
            const auto &knee = left ? s.leftKnee : s.rightKnee;
            const auto &thigh = left ? input.leftThigh : input.rightThigh;
            const auto pivot = localToWorldPoint(thigh, knee.localAnchorA);
            const auto axis = thigh.body.orientation.normalized().rotate(knee.hingeAxis.normalized()).normalized();
            const int first = left ? 0 : 3, foot = first + 2;
            constexpr float probe = 0.001f, maximumAngle = 0.01f;
            auto best = input;
            float bestAngle = 0;
            bool found = false;
            const auto evaluate = [&](float angle, AuraBody3D &copy) {
                copy = input;
                auto &shin = left ? copy.leftShin : copy.rightShin;
                rotateSubtreeAroundWorldPoint(s.collectComponent(copy, shin, knee), pivot,
                                              math::Quaternion::fromAxisAngle(axis, angle));
                return inspect(copy, s, originalGaps, &initial.lowestY);
            };
            const auto legSafe = [&](const ThighCandidateAudit &m) {
                for (int i = first; i < first + 3; ++i)
                    if (!floorMoveIsNoWorse(initial.lowestY[i], m.lowestY[i]) || m.jointLimitErrors[i] > 1e-5f) return
                            false;
                return (m.rejections & AnchorGapDrift) == 0;
            };
            const auto &inputFoot = left ? input.leftFoot : input.rightFoot;
            const float hipFootY = physics::lowestPoint(inputFoot.body, inputFoot.size, inputFoot.floorShape).y;
            for (float sign: {-1.0f, 1.0f}) {
                AuraBody3D copy;
                auto m = evaluate(sign * probe, copy);
                if (m.lowestY[foot] <= hipFootY + 1e-8f) continue;
                float low = 0;
                for (int step = 1; step <= 10; ++step) {
                    const float high = std::min(maximumAngle, step * probe);
                    if (step != 1) m = evaluate(sign * high, copy);
                    if (legSafe(m)) {
                        float upper = high;
                        auto feasible = copy;
                        for (int iteration = 0; iteration < 12; ++iteration) {
                            const float mid = (low + upper) * 0.5f;
                            const auto trial = evaluate(sign * mid, copy);
                            if (legSafe(trial)) {
                                upper = mid;
                                feasible = copy;
                            } else low = mid;
                        }
                        if (!found || upper < std::abs(bestAngle)) {
                            found = true;
                            best = feasible;
                            bestAngle = sign * upper;
                        }
                        break;
                    }
                    low = high;
                }
            }
            if (found) {
                candidate = best;
                appliedAngle = bestAngle;
            }
            return found;
        }
    }

    ThighSwingCollisionStep correctThighOverlapCoupled(AuraBody3D &b, const AuraSkeleton3D &s,
                                                       float stepSize, float tolerance,
                                                       ThighSwingCollisionAudit *audit) {
        ThighSwingCollisionAudit stats;
        ThighSwingCollisionStep result;
        result.penetrationBefore = result.penetrationAfter = penetration(b);
        if (result.penetrationBefore <= tolerance || stepSize <= 0) {
            if (audit)*audit = stats;
            return result;
        }
        const auto input = b;
        struct Pose {
            float twist, y, z;
        };
        const auto pose = [&](const BodyPart3D &thigh, const Joint3D &hip) {
            const auto swing = relativeJointSwingAngles(input.pelvis, thigh, hip);
            return Pose{
                std::clamp(relativeJointTwistAngle(input.pelvis, thigh, hip), hip.minAngle, hip.maxAngle),
                std::clamp(swing.y, hip.minSwingY, hip.maxSwingY), std::clamp(swing.z, hip.minSwingZ, hip.maxSwingZ)
            };
        };
        const auto left = pose(input.leftThigh, s.leftHip), right = pose(input.rightThigh, s.rightHip);
        const auto preciseGaps = [&](const AuraBody3D &body) {
            const auto world = [](const BodyPart3D &part, const math::Vec3 &anchor) {
                const auto q = part.body.orientation.normalized();
                const auto p = q * math::Quaternion{0, anchor.x, anchor.y, anchor.z} * q.conjugate();
                return std::array<double, 3>{
                    double(part.body.position.x) + p.x, double(part.body.position.y) + p.y,
                    double(part.body.position.z) + p.z
                };
            };
            const std::array<const BodyPart3D *, 4> a{
                &body.leftThigh, &body.leftShin, &body.rightThigh, &body.rightShin
            };
            const std::array<const BodyPart3D *, 4> c{&body.leftShin, &body.leftFoot, &body.rightShin, &body.rightFoot};
            const std::array<const Joint3D *, 4> joints{&s.leftKnee, &s.leftAnkle, &s.rightKnee, &s.rightAnkle};
            std::array<double, 4> values{};
            for (size_t i = 0; i < 4; ++i) {
                const auto pa = world(*a[i], joints[i]->localAnchorA), pc = world(*c[i], joints[i]->localAnchorB);
                for (size_t k = 0; k < 3; ++k)values[i] += (pc[k] - pa[k]) * (pc[k] - pa[k]);
                values[i] = std::sqrt(values[i]);
            }
            return values;
        };
        const auto originalGaps = preciseGaps(input);
        const auto rebuild = [&](Pose lp, Pose rp) {
            auto candidate = input;
            const auto parent = input.pelvis.body.orientation.normalized();
            const auto target = [&](Pose p) {
                const math::Vec3 vector{0, p.y, p.z};
                const float angle = vector.length();
                const auto swing = angle == 0 ? math::Quaternion{} : math::Quaternion::fromAxisAngle(vector, angle);
                return (parent * swing * math::Quaternion::fromAxisAngle({1, 0, 0}, p.twist)).normalized();
            };
            const auto lt = target(lp), rt = target(rp);
            for (bool isLeft: {true, false}) {
                auto &thigh = isLeft ? candidate.leftThigh : candidate.rightThigh;
                const auto &hip = isLeft ? s.leftHip : s.rightHip;
                const auto component = s.collectComponent(candidate, thigh, hip);
                rotateSubtreeAroundWorldPoint(component, localToWorldPoint(candidate.pelvis, hip.localAnchorA),
                                              ((isLeft ? lt : rt) * thigh.body.orientation.normalized().conjugate()).
                                              normalized());
                translateSubtree(
                    component,
                    localToWorldPoint(candidate.pelvis, hip.localAnchorA) - localToWorldPoint(thigh, hip.localAnchorB));
            }
            return candidate;
        };
        const auto feasible = [&](const AuraBody3D &candidate) {
            for (bool isLeft: {true, false}) {
                const auto &hip = isLeft ? s.leftHip : s.rightHip;
                const auto &thigh = isLeft ? candidate.leftThigh : candidate.rightThigh;
                const auto swing = relativeJointSwingAngles(candidate.pelvis, thigh, hip);
                const auto twist = relativeJointTwistAngle(candidate.pelvis, thigh, hip);
                if (!std::isfinite(twist) || !std::isfinite(swing.y) || !std::isfinite(swing.z) ||
                    twist < hip.minAngle - 1e-6f || twist > hip.maxAngle + 1e-6f ||
                    swing.y < hip.minSwingY - 1e-6f || swing.y > hip.maxSwingY + 1e-6f ||
                    swing.z < hip.minSwingZ - 1e-6f || swing.z > hip.maxSwingZ + 1e-6f) {
                    ++stats.blockedBySwingLimit;
                    return false;
                }
            }
            const std::array<const BodyPart3D *, 6> old{
                &input.leftThigh, &input.leftShin, &input.leftFoot, &input.rightThigh, &input.rightShin,
                &input.rightFoot
            };
            const std::array<const BodyPart3D *, 6> moved{
                &candidate.leftThigh, &candidate.leftShin, &candidate.leftFoot, &candidate.rightThigh,
                &candidate.rightShin, &candidate.rightFoot
            };
            for (size_t i = 0; i < 6; ++i)
                if (!floorMoveIsNoWorse(
                    physics::lowestPoint(old[i]->body, old[i]->size, old[i]->floorShape).y,
                    physics::lowestPoint(moved[i]->body, moved[i]->size, moved[i]->floorShape).y)) {
                    ++stats.blockedByFloor;
                    return false;
                }
            const auto g = preciseGaps(candidate);
            for (size_t i = 0; i < 4; ++i)if (std::abs(g[i] - originalGaps[i]) > 1e-6) {
                ++stats.blockedByTwistOrAnchor;
                return false;
            }
            return true;
        };
        const auto hipGap = [&](const AuraBody3D &body) {
            return (localToWorldPoint(body.pelvis, s.leftHip.localAnchorA) - localToWorldPoint(
                        body.leftThigh, s.leftHip.localAnchorB)).length() +
                   (localToWorldPoint(body.pelvis, s.rightHip.localAnchorA) - localToWorldPoint(
                        body.rightThigh, s.rightHip.localAnchorB)).length();
        };
        // Hard repairs precede the neighborhood search. If this projection cannot
        // preserve floor/descendants, leave the live pose unchanged and report it.
        auto base = rebuild(left, right);
        if (!feasible(base)) {
            if (audit)*audit = stats;
            return result;
        }
        auto best = base;
        float bestPen = penetration(base), bestGap = hipGap(base), bestMotion = 0;
        for (int coordinate = 0; coordinate < 4; ++coordinate)
            for (float sign: {-1.0f, 1.0f}) {
                ++stats.candidates;
                auto lp = left, rp = right;
                float &x = coordinate == 0 ? lp.y : coordinate == 1 ? lp.z : coordinate == 2 ? rp.y : rp.z;
                x += sign * stepSize;
                const auto &hip = coordinate < 2 ? s.leftHip : s.rightHip;
                const float lower = coordinate % 2 == 0 ? hip.minSwingY : hip.minSwingZ;
                const float upper = coordinate % 2 == 0 ? hip.maxSwingY : hip.maxSwingZ;
                if (x < lower || x > upper) {
                    ++stats.blockedBySwingLimit;
                    continue;
                }
                auto candidate = rebuild(lp, rp);
                if (!feasible(candidate))continue;
                const float pen = penetration(candidate), gap = hipGap(candidate);
                const bool samePen = std::abs(pen - bestPen) <= 1e-7f, sameGap = std::abs(gap - bestGap) <= 1e-7f;
                if (pen < bestPen - 1e-7f || (
                        samePen && (gap < bestGap - 1e-7f || (sameGap && stepSize < bestMotion)))) {
                    best = candidate;
                    bestPen = pen;
                    bestGap = gap;
                    bestMotion = stepSize;
                    result.leftRequest = coordinate < 2 ? sign * stepSize : 0;
                    result.rightRequest = coordinate >= 2 ? sign * stepSize : 0;
                } else ++stats.noImprovement;
            }
        b = best;
        result.penetrationAfter = bestPen;
        result.corrected = bestMotion > 0;
        if (audit)*audit = stats;
        return result;
    }

    ThighSwingCollisionStep correctThighOverlapCoupledBudget(AuraBody3D &b, const AuraSkeleton3D &s,
                                                             int projectionIterations, float stepSize, float tolerance,
                                                             ThighSwingCollisionAudit *audit) {
        ThighSwingCollisionAudit total;
        ThighSwingCollisionStep result;
        result.penetrationBefore = result.penetrationAfter = penetration(b);
        for (int i = 0; i < projectionIterations && result.penetrationAfter > tolerance; ++i) {
            ThighSwingCollisionAudit search;
            const auto step = correctThighOverlapCoupled(b, s, stepSize, tolerance, &search);
            ++total.projectionCalls;
            total.acceptedProjections += step.corrected;
            total.leftAcceptedProjections += step.corrected && step.leftRequest != 0;
            total.rightAcceptedProjections += step.corrected && step.rightRequest != 0;
            total.noImprovementProjections += !step.corrected && search.noImprovement > 0;
            total.candidates += search.candidates;
            total.blockedBySwingLimit += search.blockedBySwingLimit;
            total.blockedByFloor += search.blockedByFloor;
            total.blockedByTwistOrAnchor += search.blockedByTwistOrAnchor;
            total.noImprovement += search.noImprovement;
            total.degenerate |= search.degenerate;
            result.penetrationAfter = step.penetrationAfter;
            result.corrected |= step.corrected;
            // Report the largest single accepted step, not cumulative motion.
            if (std::abs(step.leftRequest) > std::abs(result.leftRequest))result.leftRequest = step.leftRequest;
            if (std::abs(step.rightRequest) > std::abs(result.rightRequest))result.rightRequest = step.rightRequest;
            if (!step.corrected)break;
        }
        if (audit)*audit = total;
        return result;
    }

    ThighSwingCollisionStep correctThighOverlapWithSwing(AuraBody3D &b, const AuraSkeleton3D &s,
                                                         float maxRequest, float tolerance,
                                                         ThighSwingCollisionAudit *audit, bool detailCandidates,
                                                         bool compensateKnees) {
        ThighSwingCollisionAudit stats;
        ThighSwingCollisionStep result;
        result.penetrationBefore = result.penetrationAfter = penetration(b);
        if (result.penetrationBefore <= tolerance || maxRequest <= 0) {
            if (audit)*audit = stats;
            return result;
        }
        const auto l = physics::bodyPartCollisionCapsule(b.leftThigh), r = physics::bodyPartCollisionCapsule(
            b.rightThigh);
        const auto contact = physics::closestPointsBetweenSegments(l.pointA, l.pointB, r.pointA, r.pointB);
        const auto difference = contact.pointB - contact.pointA;
        if (difference.lengthSquared() < 1e-12f) {
            stats.degenerate = true;
            if (audit)*audit = stats;
            return result;
        }
        const auto normal = difference.normalized();
        const auto inverseParent = b.pelvis.body.orientation.normalized().conjugate();
        auto leftAxis = inverseParent.rotate(
            (contact.pointA - localToWorldPoint(b.pelvis, s.leftHip.localAnchorA)).cross(-normal));
        auto rightAxis = inverseParent.rotate(
            (contact.pointB - localToWorldPoint(b.pelvis, s.rightHip.localAnchorA)).cross(normal));
        // Collision asks for swing only. The API then explicitly retains twist.
        leftAxis.x = rightAxis.x = 0;
        leftAxis = leftAxis.normalized();
        rightAxis = rightAxis.normalized();
        const auto originalGaps = gaps(b, s);
        const auto initial = inspect(b, s, originalGaps);
        const float oldLeftTwist = relativeJointTwistAngle(b.pelvis, b.leftThigh, s.leftHip);
        const float oldRightTwist = relativeJointTwistAngle(b.pelvis, b.rightThigh, s.rightHip);
        const std::array steps{-maxRequest, -maxRequest * 0.5f, 0.0f, maxRequest * 0.5f, maxRequest};
        auto best = b;
        for (float dl: steps)
            for (float dr: steps) {
                if (dl == 0 && dr == 0) continue;
                ++stats.candidates;
                if ((dl != 0 && leftAxis.lengthSquared() == 0) || (dr != 0 && rightAxis.lengthSquared() == 0)) {
                    stats.degenerate = true;
                    continue;
                }
                if (detailCandidates && audit) {
                    const auto lp = previewJointSwing(b.pelvis, b.leftThigh, s.leftHip,
                                                      dl == 0 ? math::Vec3{0, 1, 0} : leftAxis, dl);
                    const auto rp = previewJointSwing(b.pelvis, b.rightThigh, s.rightHip,
                                                      dr == 0 ? math::Vec3{0, 1, 0} : rightAxis, dr);
                    auto hypothetical = b;
                    if (dl != 0 && lp.twistDefined)
                        rotateSubtreeAroundWorldPoint(
                            s.collectComponent(hypothetical, hypothetical.leftThigh, s.leftHip),
                            localToWorldPoint(b.pelvis, s.leftHip.localAnchorA), lp.worldRotation);
                    if (dr != 0 && rp.twistDefined)
                        rotateSubtreeAroundWorldPoint(
                            s.collectComponent(hypothetical, hypothetical.rightThigh, s.rightHip),
                            localToWorldPoint(b.pelvis, s.rightHip.localAnchorA), rp.worldRotation);
                    const auto geometry = inspect(hypothetical, s, originalGaps, &initial.lowestY);
                    stats.trials[stats.trialCount++] = {
                        dl, dr, penetration(hypothetical), lp.angles, rp.angles,
                        dl != 0 ? lp.blockedBounds : 0u, dr != 0 ? rp.blockedBounds : 0u,
                        (geometry.rejections & 15u) == 0,
                        penetration(hypothetical) < result.penetrationBefore - 1e-7f, lp.twistDefined && rp.twistDefined
                    };
                }
                auto candidate = b;
                bool accepted = true;
                if (dl != 0)
                    accepted = applyJointSwingWithComponent(candidate.pelvis, candidate.leftThigh, s.leftHip,
                                                            s.collectComponent(
                                                                candidate, candidate.leftThigh, s.leftHip), leftAxis,
                                                            dl);
                if (dr != 0)
                    accepted = applyJointSwingWithComponent(candidate.pelvis, candidate.rightThigh, s.rightHip,
                                                            s.collectComponent(
                                                                candidate, candidate.rightThigh, s.rightHip), rightAxis,
                                                            dr) && accepted;
                if (!accepted) {
                    ++stats.blockedBySwingLimit;
                    continue;
                }
                const float overlap = penetration(candidate);
                if (overlap >= result.penetrationBefore - 1e-7f) {
                    ++stats.noImprovement;
                    continue;
                }
                auto trial = inspect(candidate, s, originalGaps, &initial.lowestY);
                bool swingSafe = true;
                for (bool left: {true, false}) {
                    const auto &hip = left ? s.leftHip : s.rightHip;
                    const auto angles = relativeJointSwingAngles(candidate.pelvis,
                                                                 left ? candidate.leftThigh : candidate.rightThigh,
                                                                 hip);
                    swingSafe &= angles.y >= hip.minSwingY - 1e-6f && angles.y <= hip.maxSwingY + 1e-6f &&
                            angles.z >= hip.minSwingZ - 1e-6f && angles.z <= hip.maxSwingZ + 1e-6f;
                }
                if (!swingSafe) {
                    ++stats.blockedBySwingLimit;
                    continue;
                }
                bool floorBlocked = (trial.rejections & 15u) != 0;
                const bool constraintBlocked = (trial.rejections & ~15u) != 0 ||
                                               std::abs(relativeJointTwistAngle(candidate.pelvis, candidate.leftThigh,
                                                            s.leftHip) - oldLeftTwist) > 1e-5f ||
                                               std::abs(relativeJointTwistAngle(candidate.pelvis, candidate.rightThigh,
                                                            s.rightHip) - oldRightTwist) > 1e-5f;
                float leftKnee = 0, rightKnee = 0;
                const bool footOnly = floorBlocked && !constraintBlocked &&
                                      (trial.rejections & ~(LeftFootFloor | RightFootFloor)) == 0 &&
                                      (!(trial.rejections & LeftFootFloor) || dl != 0) && (
                                          !(trial.rejections & RightFootFloor) || dr != 0);
                stats.footOnlyCandidates += footOnly;
                if (compensateKnees && footOnly) {
                    ++stats.kneeSearchCandidates;
                    auto coupled = candidate;
                    bool restored = true;
                    if (trial.rejections & LeftFootFloor) restored = compensateSwingFoot(
                                                              coupled, s, true, initial, originalGaps, leftKnee);
                    if (restored && (trial.rejections & RightFootFloor)) restored = compensateSwingFoot(
                                                                             coupled, s, false, initial, originalGaps,
                                                                             rightKnee);
                    if (restored) {
                        const auto checked = inspect(coupled, s, originalGaps, &initial.lowestY);
                        restored = checked.rejections == 0 && penetration(coupled) == overlap;
                        if (restored) {
                            candidate = coupled;
                            trial = checked;
                            floorBlocked = false;
                        }
                    }
                    stats.kneeRecoveredCandidates += restored;
                    stats.kneeFailedCandidates += !restored;
                }
                stats.footOnlyRemaining += footOnly && floorBlocked;
                stats.blockedByFloor += floorBlocked;
                stats.blockedByTwistOrAnchor += constraintBlocked;
                if (floorBlocked || constraintBlocked) continue;
                if (overlap < result.penetrationAfter - 1e-7f) {
                    best = candidate;
                    result.penetrationAfter = overlap;
                    result.leftRequest = dl;
                    result.rightRequest = dr;
                    result.leftKneeRequest = leftKnee;
                    result.rightKneeRequest = rightKnee;
                    result.corrected = true;
                }
            }
        if (result.corrected) b = best;
        if (audit)*audit = stats;
        return result;
    }

    ThighCollisionStep correctThighCapsuleOverlap(AuraBody3D &b, const AuraSkeleton3D &s, float maxAngleStep,
                                                  float tolerance, ThighCollisionAudit *audit,
                                                  ThighFloorPolicy floorPolicy, bool compensateKnees) {
        if (audit) *audit = {};
        ThighCollisionStep result;
        result.penetrationBefore = result.penetrationAfter = penetration(b);
        if (result.penetrationBefore <= tolerance || maxAngleStep <= 0) return result;
        const auto l = physics::bodyPartCollisionCapsule(b.leftThigh), r = physics::bodyPartCollisionCapsule(
            b.rightThigh);
        const auto contact = physics::closestPointsBetweenSegments(l.pointA, l.pointB, r.pointA, r.pointB);
        const auto difference = contact.pointB - contact.pointA;
        if (difference.lengthSquared() < 1e-12f) {
            if (audit) audit->degenerateNormal = true;
            return result;
        } // No invented separation normal.
        const auto normal = difference.normalized();
        const auto lp = localToWorldPoint(b.pelvis, s.leftHip.localAnchorA), rp = localToWorldPoint(
            b.pelvis, s.rightHip.localAnchorA);
        const auto la = (contact.pointA - lp).cross(-normal).normalized(), ra = (contact.pointB - rp).cross(normal).
                normalized();
        if (audit) audit->degenerateAxis = la.lengthSquared() == 0 || ra.lengthSquared() == 0;
        const auto originalGaps = gaps(b, s);
        const auto initial = inspect(b, s, originalGaps);
        if (audit) audit->initial = initial;
        const std::array<float, 5> steps{-maxAngleStep, -maxAngleStep * 0.5f, 0, maxAngleStep * 0.5f, maxAngleStep};
        auto best = b;
        for (float dl: steps)
            for (float dr: steps) {
                if ((dl == 0 && dr == 0) || (dl != 0 && la.lengthSquared() == 0) || (
                        dr != 0 && ra.lengthSquared() == 0)) continue;
                auto candidate = b;
                const auto left = s.collectComponent(candidate, candidate.leftThigh, s.leftHip);
                const auto right = s.collectComponent(candidate, candidate.rightThigh, s.rightHip);
                if (dl != 0) rotateSubtreeAroundWorldPoint(left, lp, math::Quaternion::fromAxisAngle(la, dl));
                if (dr != 0) rotateSubtreeAroundWorldPoint(right, rp, math::Quaternion::fromAxisAngle(ra, dr));
                const float overlap = penetration(candidate);
                if (!audit && !(overlap < result.penetrationAfter - 1e-7f)) continue;
                // Inspect all trials for diagnostics, including those which do not improve.
                auto trial = inspect(candidate, s, originalGaps,
                                     floorPolicy == ThighFloorPolicy::NoWorsening ? &initial.lowestY : nullptr);
                trial.leftAngle = dl;
                trial.rightAngle = dr;
                trial.penetration = overlap;
                trial.improves = overlap < result.penetrationBefore - 1e-7f;
                bool feasible = trial.rejections == 0;
                // Class A only: all limits/anchors already valid; only moved feet
                // violate floor feasibility. Class B never reaches this search.
                if (compensateKnees && floorPolicy == ThighFloorPolicy::NoWorsening && trial.improves &&
                    trial.rejections != 0 && (trial.rejections & ~(LeftFootFloor | RightFootFloor)) == 0 &&
                    (!(trial.rejections & LeftFootFloor) || dl != 0) && (
                        !(trial.rejections & RightFootFloor) || dr != 0)) {
                    trial.kneeAttempted = true;
                    auto coupled = candidate;
                    bool solved = true;
                    if (trial.rejections & LeftFootFloor)
                        solved = compensateFoot(coupled, s, true, initial, originalGaps, trial.leftKneeAngle);
                    if (solved && (trial.rejections & RightFootFloor))
                        solved = compensateFoot(coupled, s, false, initial, originalGaps, trial.rightKneeAngle);
                    if (solved && inspect(coupled, s, originalGaps, &initial.lowestY).rejections == 0 &&
                        penetration(coupled) == overlap) {
                        feasible = true;
                        trial.kneeFeasible = true;
                        candidate = coupled;
                    }
                }
                if (audit) {
                    audit->candidates[audit->candidateCount++] = trial;
                    if (trial.improves) {
                        ++audit->improvingCandidates;
                        audit->improvingRejections |= trial.rejections;
                        if (feasible) ++audit->feasibleImprovingCandidates;
                    }
                }
                if (overlap < result.penetrationAfter - 1e-7f && feasible) {
                    best = candidate;
                    result.penetrationAfter = overlap;
                    result.leftAngle = dl;
                    result.rightAngle = dr;
                    result.leftKneeAngle = trial.kneeFeasible ? trial.leftKneeAngle : 0;
                    result.rightKneeAngle = trial.kneeFeasible ? trial.rightKneeAngle : 0;
                    result.corrected = true;
                }
            }
        if (result.corrected) b = best;
        return result;
    }
}
