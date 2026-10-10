#include "aura/body/ComponentPosition.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include <algorithm>
#include <limits>
#include <cmath>

namespace aura::body {
    ComponentPositionResult correctJointPositionWithComponents(
        AuraBody3D &body, const AuraSkeleton3D &skeleton,
        BodyPart3D &a, BodyPart3D &b, const Joint3D &joint) {
        const auto error = localToWorldPoint(b, joint.localAnchorB) - localToWorldPoint(a, joint.localAnchorA);
        ComponentPositionResult result;
        result.gapBefore = error.length();
        result.gapAfter = result.gapBefore;
        const auto child = skeleton.collectComponent(body, b, joint);
        const auto parent = skeleton.collectComponent(body, a, joint);
        const auto floorSafe = [](const auto &component, const math::Vec3 &delta) {
            for (const auto *part : component) {
                const float before = physics::lowestPoint(part->body, part->size).y;
                if (before + delta.y < std::min(0.0f, before) - 1e-6f) return false;
            }
            return true;
        };
        if (floorSafe(child, -error)) {
            result.choice = ComponentPositionChoice::Child;
            result.translation = -error;
            translateSubtree(child, -error);
        } else if (floorSafe(parent, error)) {
            result.choice = ComponentPositionChoice::Parent;
            result.translation = error;
            translateSubtree(parent, error);
        } else {
            result.choice = ComponentPositionChoice::MultipleContacts;
            return result;
        }
        result.gapAfter = (localToWorldPoint(b, joint.localAnchorB) - localToWorldPoint(a, joint.localAnchorA)).length();
        return result;
    }
    DirectionalPositionResult correctJointPositionDirectionally(
        AuraBody3D &body, const AuraSkeleton3D &skeleton,
        BodyPart3D &a, BodyPart3D &b, const Joint3D &joint) {
        const auto error = localToWorldPoint(b, joint.localAnchorB) - localToWorldPoint(a, joint.localAnchorA);
        DirectionalPositionResult result;
        result.gapBefore = result.gapAfter = error.length();
        const auto componentA = skeleton.collectComponent(body, a, joint);
        const auto componentB = skeleton.collectComponent(body, b, joint);
        const auto mass = [](const auto &component) {
            double sum = 0;
            for (const auto *part : component) sum += part->body.mass;
            return sum;
        };
        const double massA = mass(componentA), massB = mass(componentB);
        auto deltaA = error * static_cast<float>(massB / (massA + massB));
        auto deltaB = -error * static_cast<float>(massA / (massA + massB));
        constexpr float tolerance = 1e-6f;
        const auto floorLowerBound = [](const auto &component) {
            float bound = -std::numeric_limits<float>::infinity();
            for (const auto *part : component) {
                const float y = physics::lowestPoint(part->body, part->size).y;
                bound = std::max(bound, std::min(0.0f, y) - tolerance - y);
            }
            return bound;
        };
        // deltaA.y - deltaB.y = error.y. Both floor inequalities are lower
        // bounds on deltaB.y; choose the feasible value closest to mass balance.
        deltaB.y = std::max({deltaB.y, floorLowerBound(componentB), floorLowerBound(componentA) - error.y});
        deltaA.y = error.y + deltaB.y;
        // Validate actual float endpoint positions on copies before committing.
        // At a limiting floor height one ULP may require a common upward lift.
        for (int attempt = 0; attempt < 4; ++attempt) {
            float lift = 0;
            const auto check = [&](const auto &component, const math::Vec3 &delta) {
                for (const auto *part : component) {
                    auto candidate = part->body;
                    candidate.position += delta;
                    const float allowed = std::min(0.0f, physics::lowestPoint(part->body, part->size).y) - tolerance;
                    const float after = physics::lowestPoint(candidate, part->size).y;
                    if (after < allowed) {
                        const float ulp = std::nextafter(candidate.position.y, std::numeric_limits<float>::infinity()) - candidate.position.y;
                        lift = std::max(lift, std::max(allowed - after, ulp));
                    }
                }
            };
            check(componentA, deltaA); check(componentB, deltaB);
            if (lift == 0) {
                translateSubtree(componentA, deltaA);
                translateSubtree(componentB, deltaB);
                result.translationA = deltaA; result.translationB = deltaB;
                result.applied = true;
                result.gapAfter = (localToWorldPoint(b, joint.localAnchorB) - localToWorldPoint(a, joint.localAnchorA)).length();
                return result;
            }
            deltaA.y += lift; deltaB.y += lift;
        }
        return result; // No partial application if float feasibility failed.
    }

}
