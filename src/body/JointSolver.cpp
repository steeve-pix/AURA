#include "aura/body/JointSolver.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/BodySelfCollision.hpp"
#include "aura/body/ComponentPosition.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/LocalJointVelocity.hpp"
#include <array>

namespace aura::body {
    namespace {
        // Keep body-specific pivot/floor policies here, outside application input
        // and rendering. Historical correction helpers remain available to replays.
        void projectAngle(AuraBody3D& body, const AuraSkeleton3D& skeleton,
                          BodyPart3D& parent, BodyPart3D& child, const Joint3D& joint) {
            if (&joint == &skeleton.neck) {
                correctNeckAngleAroundPivot(body, skeleton, joint);
            } else if (&joint == &skeleton.head) {
                correctHeadAngleAroundPivot(body, skeleton, joint);
            } else if (&joint == &skeleton.leftShoulder) {
                correctLeftShoulderAngleAroundPivot(body, skeleton, joint);
            } else {
                const std::array branches{
                    std::pair{&skeleton.rightShoulder, MajorBodyBranch3D::RightShoulder},
                    std::pair{&skeleton.leftHip, MajorBodyBranch3D::LeftHip},
                    std::pair{&skeleton.rightHip, MajorBodyBranch3D::RightHip},
                    std::pair{&skeleton.rightKnee, MajorBodyBranch3D::RightKnee},
                    std::pair{&skeleton.rightElbow, MajorBodyBranch3D::RightElbow},
                    std::pair{&skeleton.leftElbow, MajorBodyBranch3D::LeftElbow},
                    std::pair{&skeleton.leftKnee, MajorBodyBranch3D::LeftKnee}
                };
                for (const auto& [connection, branch] : branches) {
                    if (&joint != connection) continue;
                    // Swing bounds apply only to hips; other branches retain their
                    // existing hinge projection and floor policy.
                    correctBranchAngleAroundPivot(body, skeleton, joint, branch, true);
                    return;
                }
                correctJointAngle(parent, child, joint);
            }
        }
    }

    void solveBodyJoint(AuraBody3D& body, const AuraSkeleton3D& skeleton,
                        BodyPart3D& parent, BodyPart3D& child, const Joint3D& joint) {
        projectBodyHingeAxis(body, skeleton, parent, child, joint);
        projectAngle(body, skeleton, parent, child, joint);
        correctJointPositionWithComponents(body, skeleton, parent, child, joint);
        for (int i = 0; i < jointVelocityIterations; ++i) {
            correctLocalJointVelocity(parent, child, joint);
            correctLocalJointHingeAngularVelocity(parent, child, joint);
            // Last: anchor impulses can reintroduce outward limit velocity.
            correctLocalJointAngularLimitVelocity(parent, child, joint);
        }
    }
}
