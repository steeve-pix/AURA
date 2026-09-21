#pragma once
#include "Body2D.hpp"
#include "BodyGeometry.hpp"
#include "Joint2D.hpp"
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
}
