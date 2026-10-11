#pragma once
#include <span>
#include "BodyPart3D.hpp"
#include "Joint3D.hpp"
#include "aura/math/Vec3.hpp"

namespace aura::body {
    struct JointSwingTwist {
        // Relative orientation = swing * twist, in part A's local frame.
        math::Quaternion swing;
        math::Quaternion twist;
        bool twistDefined = true;
    };

    JointSwingTwist relativeJointSwingTwist(const BodyPart3D &partA,
                                            const BodyPart3D &partB, const Joint3D &joint);

    float relativeJointTwistAngle(const BodyPart3D &partA, const BodyPart3D &partB,
                                  const Joint3D &joint);

    math::Quaternion relativeJointSwing(const BodyPart3D &partA, const BodyPart3D &partB,
                                        const Joint3D &joint);

    // Shortest swing rotation vector, not Euler angles; expressed in A's frame.
    math::Vec3 jointSwingRotationVector(const math::Quaternion &swing);

    // Remove transverse swing from an actual hinge, retaining twist. Pose only;
    // rotate the child cut-component about its own joint anchor.
    bool correctHingeAxisWithComponent(const BodyPart3D &parent, BodyPart3D &child,
                                       const Joint3D &joint, std::span<BodyPart3D *const> component);

    struct SwingAngles {
        float y{}, z{};
    };

    SwingAngles relativeJointSwingAngles(const BodyPart3D &partA, const BodyPart3D &partB,
                                         const Joint3D &joint);

    float relativeJointSwingY(const BodyPart3D &partA, const BodyPart3D &partB,
                              const Joint3D &joint);

    float relativeJointSwingZ(const BodyPart3D &partA, const BodyPart3D &partB,
                              const Joint3D &joint);

    enum SwingBound : unsigned { SwingYMin = 1u, SwingYMax = 2u, SwingZMin = 4u, SwingZMax = 8u };

    struct JointSwingProbe {
        math::Quaternion worldRotation;
        SwingAngles angles;
        unsigned blockedBounds{};
        bool twistDefined{};
    };

    // Read-only request preview. A blocked rotation is for diagnostics only;
    // applying it would bypass joint limits.
    JointSwingProbe previewJointSwing(const BodyPart3D &parent, const BodyPart3D &child,
                                      const Joint3D &joint, const math::Vec3 &localAxis, float angle);

    // Pose-only swing probe about an A-local axis. Retains original twist and
    // rejects undefined twist or a Y/Z swing-limit violation. Component must
    // contain B and exclude A; caller handles floor/self-collision feasibility.
    // This first bounded API supports an X main axis only.
    bool applyJointSwingWithComponent(const BodyPart3D &partA, BodyPart3D &partB,
                                      const Joint3D &joint, std::span<BodyPart3D *const> component,
                                      const math::Vec3 &localProbeAxis, float probeAngle);

    // Independently clamp swing rotation-vector Y/Z; preserve original twist
    // and the already-in-range coordinate. No velocity or floor response.
    bool correctJointSwingWithComponent(const BodyPart3D &partA, BodyPart3D &partB,
                                        const Joint3D &joint, std::span<BodyPart3D *const> component);

    math::Vec3 localToWorldPoint(const BodyPart3D &part, const math::Vec3 &localPoint);

    // The rotation is in world space; update position and orientation together.
    void rotateBodyAroundWorldPoint(BodyPart3D &part, const math::Vec3 &pivot,
                                    const math::Quaternion &rotation);

    // Callers supply the child subtree explicitly, without duplicate parts.
    void rotateSubtreeAroundWorldPoint(std::span<BodyPart3D *const> parts,
                                       const math::Vec3 &pivot, const math::Quaternion &rotation);

    void translateSubtree(std::span<BodyPart3D *const> parts, const math::Vec3 &delta);

    void translateSubtreeVelocity(std::span<BodyPart3D *const> parts, const math::Vec3 &deltaVelocity);

    void rotateSubtreeVelocityAroundWorldPoint(std::span<BodyPart3D *const> parts,
                                               const math::Vec3 &pivot, const math::Vec3 &deltaOmega);

    // Signed twist of part B relative to part A, in radians within [-pi, pi].
    // At a 180-degree swing perpendicular to the axis, twist is undefined; returns zero.
    float relativeJointAngle(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint);

    float jointAngleError(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint);

    float jointMotorError(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint);

    float jointMotorTorque(const BodyPart3D &partA, const BodyPart3D &partB, const Joint3D &joint);
}
