#pragma once
#include <array>
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/BodyPart3D.hpp"
#include "aura/body/Joint3D.hpp"

namespace aura::app {
    struct BodyJoint {
        const char *name{};
        aura::body::BodyPart3D *partA{};
        aura::body::BodyPart3D *partB{};
        aura::body::Joint3D &constraint;
        bool motor = false;
        float markerRadius = 0.0f;
    };

    // Root outward: spine/head, left arm, right arm, left leg, right leg.
    // References belong to the supplied body/skeleton; neither may be moved.
    std::array<BodyJoint, 15> makeBodyJoints(body::AuraBody3D &body,
                                             body::AuraSkeleton3D &skeleton);
}
