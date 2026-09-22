#pragma once
#include "Body2D.hpp"
#include "BodyGeometry.hpp"
#include "Joint2D.hpp"
#include "aura/math/Math.hpp"
#include "aura/math/Vec2.hpp"

namespace aura::physics {
    inline math::Vec2 worldAnchorA(const Body2D &bodyA, const Joint2D &joint) noexcept {
        return localToWorldPoint(bodyA, joint.localAnchorA);
    }

    inline math::Vec2 worldAnchorB(const Body2D &bodyB, const Joint2D &joint) noexcept {
        return localToWorldPoint(bodyB, joint.localAnchorB);
    }

    inline math::Vec2 jointError(const Body2D &bodyA, const Body2D &bodyB, const Joint2D &joint) noexcept {
        return worldAnchorB(bodyB, joint) - worldAnchorA(bodyA, joint);
    }

    inline math::Vec2 jointRelativeVelocity(const Body2D &bodyA, const Body2D &bodyB, const Joint2D &joint) noexcept {
        const math::Vec2 anchorA =
                worldAnchorA(bodyA, joint);

        const math::Vec2 anchorB =
                worldAnchorB(bodyB, joint);

        const math::Vec2 velocityA =
                velocityAtWorldPoint(bodyA, anchorA);

        const math::Vec2 velocityB =
                velocityAtWorldPoint(bodyB, anchorB);

        return velocityB - velocityA;
    }

    inline float relativeJointAngle(const Body2D &bodyA, const Body2D &bodyB) noexcept {
        return math::normalizeAngle(bodyB.angle - bodyA.angle);
    }

    inline float jointAngleError(const Body2D &bodyA, const Body2D &bodyB, const Joint2D &joint) {
        const float angle =
                relativeJointAngle(bodyA, bodyB);

        if (angle < joint.minAngle)
            return angle - joint.minAngle;


        if (angle > joint.maxAngle)
            return angle - joint.maxAngle;

        return 0.0f;
    }

    inline float jointMotorError(const Body2D &bodyA, const Body2D &bodyB, const Joint2D &joint) {
        return math::normalizeAngle(joint.targetAngle - relativeJointAngle(bodyA, bodyB));
    }
}
