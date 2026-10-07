#pragma once
#include "HipCapsuleRotationReplay.hpp"
#include "aura/body/ThighSelfCollision.hpp"

namespace aura::test {
    struct ThighKneeReplay {
        body::AuraBody3D hipOnly, coupled;
        HipRotationMetrics initial, hipMetrics, coupledMetrics;
        float hipAngle{}, kneeAngle{};
        bool left{}, rescued{}, velocitiesUnchanged{}, gapsPreserved{};
        int kneeTrials{};
    };
    // Frozen-pose experiment only. The hip uses the live torque-derived axis;
    // the knee rotates shin + foot about its transformed local hinge axis.
    // No integration, impulses, room/floor repairs or live multi-joint response.
    inline ThighKneeReplay replayThighWithKnee(const body::AuraBody3D& input,
        const body::AuraSkeleton3D& s,bool left,float hipAngle) {
        ThighKneeReplay result;
        result.left=left;result.hipAngle=hipAngle;result.initial=hipRotationMetrics(input,s);
        result.hipOnly=input;
        const auto l=physics::bodyPartCollisionCapsule(input.leftThigh),r=physics::bodyPartCollisionCapsule(input.rightThigh);
        const auto points=physics::closestPointsBetweenSegments(l.pointA,l.pointB,r.pointA,r.pointB);
        const auto delta=points.pointB-points.pointA;
        if (delta.lengthSquared()<1e-12f) return result;
        const auto normal=delta.normalized();
        const auto& hip=left?s.leftHip:s.rightHip;
        const auto pivot=body::localToWorldPoint(input.pelvis,hip.localAnchorA);
        const auto axis=((left?points.pointA:points.pointB)-pivot).cross(left?-normal:normal).normalized();
        auto& thigh=left?result.hipOnly.leftThigh:result.hipOnly.rightThigh;
        body::rotateSubtreeAroundWorldPoint(s.collectComponent(result.hipOnly,thigh,hip),pivot,
                                           math::Quaternion::fromAxisAngle(axis,hipAngle));
        result.hipMetrics=hipRotationMetrics(result.hipOnly,s);
        result.coupled=result.hipOnly;result.coupledMetrics=result.hipMetrics;
        const std::array<const body::BodyPart3D*,6> inputLeg{&input.leftThigh,&input.leftShin,&input.leftFoot,&input.rightThigh,&input.rightShin,&input.rightFoot};
        std::array<float,6> inputY{};
        for (size_t i=0;i<6;++i) inputY[i]=physics::lowestPoint(inputLeg[i]->body,inputLeg[i]->size).y;
        const auto& knee=left?s.leftKnee:s.rightKnee;
        const auto& hipThigh=left?result.hipOnly.leftThigh:result.hipOnly.rightThigh;
        const auto kneePivot=body::localToWorldPoint(hipThigh,knee.localAnchorA);
        const auto kneeAxis=hipThigh.body.orientation.normalized().rotate(knee.hingeAxis.normalized()).normalized();
        // Search smallest magnitude first, bounded to the same 0.01 rad budget.
        for (int magnitude=1;magnitude<=20 && !result.rescued;++magnitude) for (int sign:{-1,1}) {
            ++result.kneeTrials;
            const float angle=sign*magnitude*0.0005f;
            auto candidate=result.hipOnly;
            auto& shin=left?candidate.leftShin:candidate.rightShin;
            body::rotateSubtreeAroundWorldPoint(s.collectComponent(candidate,shin,knee),kneePivot,
                                               math::Quaternion::fromAxisAngle(kneeAxis,angle));
            const auto m=hipRotationMetrics(candidate,s);
            const std::array<const body::BodyPart3D*,6> legs{&candidate.leftThigh,&candidate.leftShin,&candidate.leftFoot,&candidate.rightThigh,&candidate.rightShin,&candidate.rightFoot};
            bool floorSafe=true,gapsSafe=true;
            for (size_t i=0;i<6;++i) {
                floorSafe &= body::floorMoveIsNoWorse(inputY[i],physics::lowestPoint(legs[i]->body,legs[i]->size).y);
                gapsSafe &= std::abs(m.gaps[i]-result.initial.gaps[i])<=1e-5f;
            }
            if (floorSafe && gapsSafe && m.maximumLimitError<=1e-5f && m.penetration<result.initial.penetration-1e-7f) {
                result.rescued=true;result.coupled=candidate;result.coupledMetrics=m;
                result.kneeAngle=angle;result.gapsPreserved=gapsSafe;
                break;
            }
        }
        auto original=input;
        auto oldParts=s.collectComponent(original,original.torso,s.head);oldParts.push_back(&original.head);
        auto newParts=s.collectComponent(result.coupled,result.coupled.torso,s.head);newParts.push_back(&result.coupled.head);
        result.velocitiesUnchanged=true;
        for (size_t i=0;i<oldParts.size();++i)
            result.velocitiesUnchanged &= (oldParts[i]->body.velocity-newParts[i]->body.velocity).lengthSquared()==0 &&
                (oldParts[i]->body.angularVelocity-newParts[i]->body.angularVelocity).lengthSquared()==0;
        return result;
    }
}
