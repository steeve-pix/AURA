#pragma once
#include "aura/body/AuraSkeleton3D.hpp"

namespace aura::body {
    enum class ComponentPositionChoice { Child, Parent, MultipleContacts };

    struct ComponentPositionResult {
        ComponentPositionChoice choice = ComponentPositionChoice::Child;
        math::Vec3 translation{};
        float gapBefore = 0.0f;
        float gapAfter = 0.0f;
    };

    struct DirectionalPositionResult {
        math::Vec3 translationA{}, translationB{};
        float gapBefore{}, gapAfter{};
        bool applied = false;
    };

    // Flat-floor experiment: mass-balance X/Z; constrain Y by both components'
    // no-worsening floor bounds. Changes positions only. Does not evaluate
    // self-collision, walls or rotational limits; caller must handle those.
    DirectionalPositionResult correctJointPositionDirectionally(
        AuraBody3D &body, const AuraSkeleton3D &skeleton,
        BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);

    // Y=0 geometric projection only. Prefer the whole child-side component;
    // otherwise move the parent-side component. Contacts may lift. Reject new
    // penetration and worsening existing penetration (1e-6 roundoff tolerance).
    // If neither full translation is feasible, leave the pose unchanged and
    // report MultipleContacts; no velocity, orientation or force changes.
    ComponentPositionResult correctJointPositionWithComponents(
        AuraBody3D &body, const AuraSkeleton3D &skeleton,
        BodyPart3D &partA, BodyPart3D &partB, const Joint3D &joint);
}
