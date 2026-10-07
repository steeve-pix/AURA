#pragma once
#include "HipCapsuleRotationReplay.hpp"
#include "aura/body/ThighSelfCollision.hpp"

namespace aura::test {
    struct SharedHipTrial {
        float leftRotation{},rightRotation{};
        HipRotationMetrics metrics;
        bool floorSafe{},limitsSafe{},gapsSafe{},feasible{},improves{};
    };
    struct SharedHipReplay {
        HipRotationMetrics initial;
        math::Vec3 leftAxis,rightAxis;
        SharedHipTrial leftOnly,rightOnly,symmetric,oppositeSigns;
        SharedHipTrial bestLeft,bestRight,bestShared,bestUnconstrained;
        int trials{},feasibleTrials{},improvingFeasibleTrials{};
        bool velocitiesUnchanged=true;
    };
    // Copied geometry only. Axes already encode opposite separating forces;
    // positive angles about both axes separate locally. No knee/pelvis motion.
    inline SharedHipReplay replaySharedHips(const body::AuraBody3D& input,
        const body::AuraSkeleton3D& s,std::ostream& out) {
        SharedHipReplay result;
        result.initial=hipRotationMetrics(input,s);
        const auto l=physics::bodyPartCollisionCapsule(input.leftThigh),r=physics::bodyPartCollisionCapsule(input.rightThigh);
        const auto closest=physics::closestPointsBetweenSegments(l.pointA,l.pointB,r.pointA,r.pointB);
        const auto difference=closest.pointB-closest.pointA;
        if (difference.lengthSquared()<1e-12f) return result;
        const auto normal=difference.normalized();
        const auto lp=body::localToWorldPoint(input.pelvis,s.leftHip.localAnchorA),rp=body::localToWorldPoint(input.pelvis,s.rightHip.localAnchorA);
        result.leftAxis=(closest.pointA-lp).cross(-normal).normalized();
        result.rightAxis=(closest.pointB-rp).cross(normal).normalized();
        const std::array<const body::BodyPart3D*,6> original{&input.leftThigh,&input.leftShin,&input.leftFoot,&input.rightThigh,&input.rightShin,&input.rightFoot};
        std::array<float,6> originalY{};
        for (size_t i=0;i<6;++i) originalY[i]=physics::lowestPoint(original[i]->body,original[i]->size).y;
        const auto evaluate=[&](float dl,float dr) {
            auto copy=input;
            if (dl!=0) body::rotateSubtreeAroundWorldPoint(s.collectComponent(copy,copy.leftThigh,s.leftHip),lp,
                math::Quaternion::fromAxisAngle(result.leftAxis,dl));
            if (dr!=0) body::rotateSubtreeAroundWorldPoint(s.collectComponent(copy,copy.rightThigh,s.rightHip),rp,
                math::Quaternion::fromAxisAngle(result.rightAxis,dr));
            SharedHipTrial t;
            t.leftRotation=dl;t.rightRotation=dr;t.metrics=hipRotationMetrics(copy,s);
            t.floorSafe=t.gapsSafe=true;t.limitsSafe=t.metrics.maximumLimitError<=1e-5f;
            const std::array<const body::BodyPart3D*,6> legs{&copy.leftThigh,&copy.leftShin,&copy.leftFoot,&copy.rightThigh,&copy.rightShin,&copy.rightFoot};
            for (size_t i=0;i<6;++i) {
                t.floorSafe &= body::floorMoveIsNoWorse(originalY[i],physics::lowestPoint(legs[i]->body,legs[i]->size).y);
                t.gapsSafe &= std::abs(t.metrics.gaps[i]-result.initial.gaps[i])<=1e-5f;
                result.velocitiesUnchanged &= (legs[i]->body.velocity-original[i]->body.velocity).lengthSquared()==0 &&
                    (legs[i]->body.angularVelocity-original[i]->body.angularVelocity).lengthSquared()==0;
            }
            t.feasible=t.floorSafe && t.limitsSafe && t.gapsSafe;
            t.improves=t.metrics.penetration<result.initial.penetration-1e-7f;
            return t;
        };
        result.leftOnly=evaluate(0.005f,0);result.rightOnly=evaluate(0,0.005f);
        result.symmetric=evaluate(0.005f,0.005f);result.oppositeSigns=evaluate(0.005f,-0.005f);
        result.bestLeft=result.bestRight=result.bestShared=result.bestUnconstrained=evaluate(0,0);
        // Fine grid includes the existing +/-0.005 and +/-0.01 trials, with
        // unequal shares and smaller safe fractions. Same per-leg angular cap.
        for (int il=-40;il<=40;++il) for (int ir=-40;ir<=40;++ir) {
            const auto t=evaluate(il*0.00025f,ir*0.00025f);
            ++result.trials;result.feasibleTrials+=t.feasible;result.improvingFeasibleTrials+=t.feasible&&t.improves;
            if (t.metrics.penetration<result.bestUnconstrained.metrics.penetration-1e-7f) result.bestUnconstrained=t;
            if (!t.feasible) continue;
            if (ir==0 && t.metrics.penetration<result.bestLeft.metrics.penetration-1e-7f) result.bestLeft=t;
            if (il==0 && t.metrics.penetration<result.bestRight.metrics.penetration-1e-7f) result.bestRight=t;
            if (il!=0 && ir!=0 && t.metrics.penetration<result.bestShared.metrics.penetration-1e-7f) result.bestShared=t;
        }
        const auto emit=[&](const char* label,const auto& t) {
            out << "sharedHipTrial,case=" << label << ",leftRotation=" << t.leftRotation << ",rightRotation=" << t.rightRotation
                << ",penetration=" << t.metrics.penetration << ",leftHipAngle=" << t.metrics.angles[0] << ",rightHipAngle=" << t.metrics.angles[3]
                << ",leftFootY=" << t.metrics.feetY[0] << ",rightFootY=" << t.metrics.feetY[1]
                << ",floorSafe=" << t.floorSafe << ",limitsSafe=" << t.limitsSafe << ",gapsSafe=" << t.gapsSafe
                << ",feasible=" << t.feasible << ",improves=" << t.improves;
            for (size_t i=0;i<6;++i) out << ",gap" << i << '=' << t.metrics.gaps[i];
            out << '\n';
        };
        out << std::scientific << std::setprecision(9);
        emit("before",evaluate(0,0));emit("leftOnly",result.leftOnly);emit("rightOnly",result.rightOnly);
        emit("symmetricSeparating",result.symmetric);emit("oppositeSigns",result.oppositeSigns);
        emit("bestLeft",result.bestLeft);emit("bestRight",result.bestRight);emit("bestShared",result.bestShared);
        emit("bestWithoutFeasibility",result.bestUnconstrained);
        out << "sharedHipSummary,trials=" << result.trials << ",feasibleTrials=" << result.feasibleTrials
            << ",improvingFeasibleTrials=" << result.improvingFeasibleTrials << ",velocitiesUnchanged=" << result.velocitiesUnchanged << '\n';
        return result;
    }
}
