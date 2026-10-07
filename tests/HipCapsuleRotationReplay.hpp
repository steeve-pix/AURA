#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <ostream>
#include <utility>
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/SelfCollision.hpp"

namespace aura::test {
    struct HipRotationMetrics {
        float penetration{};
        std::array<float,6> gaps{}, angles{};
        std::array<float,2> feetY{};
        float minimumLegY{}, maximumLimitError{};
    };
    struct HipRotationResult {
        body::AuraBody3D body;
        HipRotationMetrics initial, final;
        int acceptedSteps{};
        bool separated{}, velocitiesUnchanged{};
        float maximumGap{}, maximumDescendantAngleDrift{};
        float totalLeftRotation{},totalRightRotation{};
    };
    inline HipRotationMetrics hipRotationMetrics(const body::AuraBody3D &b,const body::AuraSkeleton3D &s) {
        HipRotationMetrics m;
        m.penetration=physics::capsulePenetration(physics::bodyPartCollisionCapsule(b.leftThigh),
                                                physics::bodyPartCollisionCapsule(b.rightThigh));
        const std::array<const body::BodyPart3D*,6> a{&b.pelvis,&b.leftThigh,&b.leftShin,&b.pelvis,&b.rightThigh,&b.rightShin};
        const std::array<const body::BodyPart3D*,6> child{&b.leftThigh,&b.leftShin,&b.leftFoot,&b.rightThigh,&b.rightShin,&b.rightFoot};
        const std::array<const body::Joint3D*,6> joints{&s.leftHip,&s.leftKnee,&s.leftAnkle,&s.rightHip,&s.rightKnee,&s.rightAnkle};
        m.minimumLegY=std::numeric_limits<float>::infinity();
        for (std::size_t i=0;i<6;++i) {
            m.gaps[i]=(body::localToWorldPoint(*child[i],joints[i]->localAnchorB)-
                       body::localToWorldPoint(*a[i],joints[i]->localAnchorA)).length();
            m.angles[i]=body::relativeJointAngle(*a[i],*child[i],*joints[i]);
            m.maximumLimitError=std::max(m.maximumLimitError,std::abs(body::jointAngleError(*a[i],*child[i],*joints[i])));
            m.minimumLegY=std::min(m.minimumLegY,physics::lowestPoint(child[i]->body,child[i]->size).y);
        }
        m.feetY={physics::lowestPoint(b.leftFoot.body,b.leftFoot.size).y,physics::lowestPoint(b.rightFoot.body,b.rightFoot.size).y};
        return m;
    }
    // Copied pose experiment only. No integration, contact/velocity response,
    // positional repairs, body resizing, or changes to the runtime solver.
    inline HipRotationResult runHipCapsuleRotationReplay(
        const body::AuraBody3D &input,const body::AuraSkeleton3D &s,std::ostream &out) {
        HipRotationResult result;
        result.body=input;
        result.initial=hipRotationMetrics(input,s);
        result.maximumGap=*std::max_element(result.initial.gaps.begin(),result.initial.gaps.end());
        const auto leftPivot=body::localToWorldPoint(input.pelvis,s.leftHip.localAnchorA);
        const auto rightPivot=body::localToWorldPoint(input.pelvis,s.rightHip.localAnchorA);
        constexpr std::array steps{-0.02f,-0.01f,-0.005f,0.0f,0.005f,0.01f,0.02f};
        constexpr int maximumSteps=128;
        constexpr float floorNoise=1e-6f, limitNoise=1e-5f, geometryNoise=1e-5f;
        out << std::scientific << std::setprecision(9)
            << "hipRotation,step,penetration,leftHipGap,leftKneeGap,leftAnkleGap,rightHipGap,rightKneeGap,rightAnkleGap,leftFootY,rightFootY,minLegY,maxLimitError,leftAngleStep,rightAngleStep\n";
        const auto sample=[&](int step,const auto &m,float l,float r) {
            out << "hipRotation," << step << ',' << m.penetration;
            for (const auto gap:m.gaps) out << ',' << gap;
            out << ',' << m.feetY[0] << ',' << m.feetY[1] << ',' << m.minimumLegY
                << ',' << m.maximumLimitError << ',' << l << ',' << r << '\n';
        };
        sample(0,result.initial,0,0);
        for (int iteration=0;iteration<maximumSteps;++iteration) {
            const auto old=hipRotationMetrics(result.body,s);
            if (old.penetration==0) break;
            const auto left=physics::bodyPartCollisionCapsule(result.body.leftThigh);
            const auto right=physics::bodyPartCollisionCapsule(result.body.rightThigh);
            const auto contact=physics::closestPointsBetweenSegments(left.pointA,left.pointB,right.pointA,right.pointB);
            const auto difference=contact.pointB-contact.pointA;
            if (difference.lengthSquared()<1e-12f) {
                out << "hipRotationBlocked,reason=coincident closest points need a normal policy\n"; break;
            }
            const auto normal=difference.normalized();
            // Both axes already encode opposite separation forces. Equal positive
            // angles about these axes are NOT an extra sign flip on the right.
            const auto leftAxis=(contact.pointA-leftPivot).cross(-normal).normalized();
            const auto rightAxis=(contact.pointB-rightPivot).cross(normal).normalized();
            if (iteration==0)
                out << "hipRotationAxes,left=" << leftAxis.x << '/' << leftAxis.y << '/' << leftAxis.z
                    << ",right=" << rightAxis.x << '/' << rightAxis.y << '/' << rightAxis.z << '\n';
            float bestPenetration=old.penetration,bestLeft=0,bestRight=0;
            auto best=result.body;
            int floorRejected=0,limitRejected=0,geometryRejected=0;
            for (float dl:steps) for (float dr:steps) {
                if (dl==0 && dr==0) continue;
                if ((dl!=0 && leftAxis.lengthSquared()==0) || (dr!=0 && rightAxis.lengthSquared()==0)) continue;
                auto candidate=result.body;
                const auto leftParts=s.collectComponent(candidate,candidate.leftThigh,s.leftHip);
                const auto rightParts=s.collectComponent(candidate,candidate.rightThigh,s.rightHip);
                if (dl!=0) body::rotateSubtreeAroundWorldPoint(leftParts,leftPivot,math::Quaternion::fromAxisAngle(leftAxis,dl));
                if (dr!=0) body::rotateSubtreeAroundWorldPoint(rightParts,rightPivot,math::Quaternion::fromAxisAngle(rightAxis,dr));
                const auto m=hipRotationMetrics(candidate,s);
                const bool floorSafe=m.minimumLegY>=-floorNoise;
                const bool limitSafe=m.maximumLimitError<=limitNoise;
                bool geometrySafe=true;
                for (std::size_t i=0;i<6;++i)
                    geometrySafe &= std::abs(m.gaps[i]-result.initial.gaps[i])<=geometryNoise;
                floorRejected+=!floorSafe;limitRejected+=!limitSafe;geometryRejected+=!geometrySafe;
                if (iteration==0)
                    out << "hipRotationTrial," << dl << ',' << dr << ',' << m.penetration << ','
                        << floorSafe << ',' << limitSafe << ',' << geometrySafe << '\n';
                if (floorSafe && limitSafe && geometrySafe && m.penetration<bestPenetration-1e-7f) {
                    bestPenetration=m.penetration;bestLeft=dl;bestRight=dr;best=std::move(candidate);
                }
            }
            if (bestLeft==0 && bestRight==0) {
                out << "hipRotationBlocked,floorRejected=" << floorRejected << ",limitRejected=" << limitRejected
                    << ",geometryRejected=" << geometryRejected << '\n'; break;
            }
            result.body=std::move(best);++result.acceptedSteps;
            result.totalLeftRotation+=std::abs(bestLeft); result.totalRightRotation+=std::abs(bestRight);
            const auto m=hipRotationMetrics(result.body,s);
            result.maximumGap=std::max(result.maximumGap,*std::max_element(m.gaps.begin(),m.gaps.end()));
            for (std::size_t i:{1u,2u,4u,5u})
                result.maximumDescendantAngleDrift=std::max(result.maximumDescendantAngleDrift,std::abs(m.angles[i]-result.initial.angles[i]));
            sample(result.acceptedSteps,m,bestLeft,bestRight);
        }
        result.final=hipRotationMetrics(result.body,s);
        result.separated=result.final.penetration==0;
        auto before=input;
        auto oldParts=s.collectComponent(before,before.torso,s.head);
        auto newParts=s.collectComponent(result.body,result.body.torso,s.head);
        oldParts.push_back(&before.head);newParts.push_back(&result.body.head);
        result.velocitiesUnchanged=true;
        for (std::size_t i=0;i<oldParts.size();++i)
            result.velocitiesUnchanged &= (oldParts[i]->body.velocity-newParts[i]->body.velocity).lengthSquared()==0 &&
                (oldParts[i]->body.angularVelocity-newParts[i]->body.angularVelocity).lengthSquared()==0;
        out << "hipRotationResult,separated=" << result.separated << ",steps=" << result.acceptedSteps
            << ",penetration=" << result.final.penetration << ",maximumGap=" << result.maximumGap
            << ",descendantAngleDrift=" << result.maximumDescendantAngleDrift
            << ",leftHipAngle=" << result.final.angles[0] << ",rightHipAngle=" << result.final.angles[3]
            << ",totalLeftRotation=" << result.totalLeftRotation << ",totalRightRotation=" << result.totalRightRotation
            << ",velocitiesUnchanged=" << result.velocitiesUnchanged << '\n';
        return result;
    }
}
