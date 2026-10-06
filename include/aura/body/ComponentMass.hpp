#pragma once

#include <span>
#include "aura/body/BodyPart3D.hpp"

namespace aura::body {
    // Supply distinct parts with finite positive masses. Empty mass is zero;
    // an empty center of mass is undefined and throws std::invalid_argument.
    float componentMass(std::span<BodyPart3D *const> parts);
    math::Vec3 componentCenterOfMass(std::span<BodyPart3D *const> parts);
    math::Vec3 componentCenterOfMassVelocity(std::span<BodyPart3D *const> parts);
    // World-space spin plus orbital angular momentum, about the component COM.
    math::Vec3 componentAngularMomentum(std::span<BodyPart3D *const> parts);
    // Intrinsic local diagonal inertia plus the parallel-axis contribution.
    // The world axis is normalized internally; a zero axis is invalid.
    float componentMomentOfInertiaAboutAxis(
        std::span<BodyPart3D *const> parts,
        const math::Vec3 &pivot,
        const math::Vec3 &worldAxis);
}
