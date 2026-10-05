#pragma once
#include <span>
#include "BodyPart3D.hpp"
#include "Joint3D.hpp"
#include "aura/math/Vec3.hpp"

namespace aura::body {
    math::Vec3 localToWorldPoint(const BodyPart3D &part, const math::Vec3 &localPoint);

    // The rotation is in world space; update position and orientation together.
    void rotateBodyAroundWorldPoint(BodyPart3D &part, const math::Vec3 &pivot,
                                    const math::Quaternion &rotation);

    // Callers supply the child subtree explicitly, without duplicate parts.
    void rotateSubtreeAroundWorldPoint(std::span<BodyPart3D *const> parts,
                                       const math::Vec3 &pivot, const math::Quaternion &rotation);
    void translateSubtree(std::span<BodyPart3D *const> parts, const math::Vec3 &delta);

    // Signed twist of part B relative to part A, in radians within [-pi, pi].
    // At a 180-degree swing perpendicular to the axis, twist is undefined; returns zero.
    float relativeJointAngle(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint);

    float jointAngleError(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint);

    float jointMotorError(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint);

    float jointMotorTorque(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint);
}
