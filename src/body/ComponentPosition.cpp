#include "aura/body/ComponentPosition.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include <algorithm>

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
}
