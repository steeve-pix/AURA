#pragma once
#include "HipCapsuleRotationReplay.hpp"
#include "aura/body/ThighSelfCollision.hpp"

namespace aura::test {
    struct HipZTrial {
        body::AuraBody3D pose;
        HipRotationMetrics metrics;
        float leftZ{}, rightZ{};
        bool floorSafe=true, xFixed=true, gapsSafe=true, velocitiesUnchanged=true, swingAllowed=true;
    };
    struct HipZReplay {
        HipZTrial before, positiveLeft, negativeLeft, best;
        HipZTrial fixedPositiveLeft, fixedNegativeLeft;
        bool separated=false;
    };
    // Test-only pelvis-local Z rotation. All candidates start from the saved
    // pose; the pelvis and velocities remain untouched.
    inline HipZReplay replayHipZ(const body::AuraBody3D& input,
                                 const body::AuraSkeleton3D& skeleton,
                                 std::ostream& out) {
        HipZReplay result;
        const auto initial = hipRotationMetrics(input, skeleton);
        const auto worldZ = input.pelvis.body.orientation.normalized().rotate({0,0,1});
        const auto evaluate = [&](float leftZ, float rightZ, bool retainX=false) {
            HipZTrial trial;
            trial.pose=input; trial.leftZ=leftZ; trial.rightZ=rightZ;
            const auto rotateLeg = [&](body::BodyPart3D& thigh, const body::Joint3D& joint, float angle) {
                if (angle==0) return;
                auto rotation=math::Quaternion::fromAxisAngle(worldZ,angle);
                if (retainX) {
                    trial.swingAllowed &= body::applyJointSwingWithComponent(trial.pose.pelvis,thigh,joint,
                        skeleton.collectComponent(trial.pose,thigh,joint),{0,0,1},angle);
                    return;
                }
                body::rotateSubtreeAroundWorldPoint(skeleton.collectComponent(trial.pose,thigh,joint),
                    body::localToWorldPoint(trial.pose.pelvis,joint.localAnchorA),
                    rotation);
            };
            rotateLeg(trial.pose.leftThigh,skeleton.leftHip,leftZ);
            rotateLeg(trial.pose.rightThigh,skeleton.rightHip,rightZ);
            trial.metrics=hipRotationMetrics(trial.pose,skeleton);
            const std::array<const body::BodyPart3D*,6> before{&input.leftThigh,&input.leftShin,&input.leftFoot,&input.rightThigh,&input.rightShin,&input.rightFoot};
            const std::array<const body::BodyPart3D*,6> after{&trial.pose.leftThigh,&trial.pose.leftShin,&trial.pose.leftFoot,&trial.pose.rightThigh,&trial.pose.rightShin,&trial.pose.rightFoot};
            for (size_t i=0;i<6;++i) {
                trial.floorSafe &= body::floorMoveIsNoWorse(physics::lowestPoint(before[i]->body,before[i]->size).y,
                    physics::lowestPoint(after[i]->body,after[i]->size).y);
                trial.gapsSafe &= std::abs(trial.metrics.gaps[i]-initial.gaps[i])<=1e-5f;
                trial.velocitiesUnchanged &= (before[i]->body.velocity-after[i]->body.velocity).lengthSquared()==0 &&
                    (before[i]->body.angularVelocity-after[i]->body.angularVelocity).lengthSquared()==0;
            }
            trial.xFixed=std::abs(trial.metrics.angles[0]-initial.angles[0])<=1e-5f &&
                         std::abs(trial.metrics.angles[3]-initial.angles[3])<=1e-5f;
            return trial;
        };
        result.before=evaluate(0,0);
        result.positiveLeft=evaluate(0.01f,-0.01f);
        result.negativeLeft=evaluate(-0.01f,0.01f);
        result.fixedPositiveLeft=evaluate(0.01f,-0.01f,true);
        result.fixedNegativeLeft=evaluate(-0.01f,0.01f,true);
        result.best=result.before;
        const auto emit = [&](const char* label,const HipZTrial& t) {
            out << "hipZTrial,case=" << label << ",leftZ=" << t.leftZ << ",rightZ=" << t.rightZ
                << ",penetration=" << t.metrics.penetration << ",leftX=" << t.metrics.angles[0]
                << ",rightX=" << t.metrics.angles[3] << ",leftFootY=" << t.metrics.feetY[0]
                << ",rightFootY=" << t.metrics.feetY[1] << ",floorSafe=" << t.floorSafe
                << ",xFixed=" << t.xFixed << ",swingAllowed=" << t.swingAllowed
                << ",leftSwingZ=" << body::relativeJointSwingZ(t.pose.pelvis,t.pose.leftThigh,skeleton.leftHip)
                << ",rightSwingZ=" << body::relativeJointSwingZ(t.pose.pelvis,t.pose.rightThigh,skeleton.rightHip)
                << ",maxLimitError=" << t.metrics.maximumLimitError
                << ",gapsSafe=" << t.gapsSafe << ",velocitiesUnchanged=" << t.velocitiesUnchanged;
            for(size_t i=0;i<6;++i) out << ",gap" << i << '=' << t.metrics.gaps[i];
            out << '\n';
        };
        out << std::scientific << std::setprecision(9);
        emit("before",result.before);emit("plusMinus001",result.positiveLeft);emit("minusPlus001",result.negativeLeft);
        emit("fixedXPlusMinus001",result.fixedPositiveLeft);emit("fixedXMinusPlus001",result.fixedNegativeLeft);
        const auto feasible=[](const HipZTrial& t) {
            return t.swingAllowed && t.floorSafe && t.xFixed && t.gapsSafe && t.metrics.maximumLimitError<=1e-5f;
        };
        // Search outward directions with unequal sharing. Minimize the largest
        // requested Z probe; ratios spaced 0.05, bounded to 0.25 rad per hip.
        // First sampled feasible separation brackets a local boundary; this is
        // not a global optimizer over every 3D hip motion.
        float bestBudget=std::numeric_limits<float>::infinity();
        for (int dominant=0;dominant<2;++dominant) for(int share=0;share<=20;++share) {
            const float ratio=share*0.05f;
            const float leftShare=dominant==0?1.0f:ratio;
            const float rightShare=dominant==0?ratio:1.0f;
            for(int step=1;step<=250;++step) {
                const float budget=step*0.001f;
                if (budget>bestBudget+0.001f) break;
                auto t=evaluate(-budget*leftShare,budget*rightShare,true);
                if(feasible(t) && t.metrics.penetration<result.best.metrics.penetration) result.best=t;
                if (!feasible(t) || t.metrics.penetration!=0) continue;
                float low=(step-1)*0.001f,high=budget;
                auto endpoint=t;
                for(int round=0;round<20;++round) {
                    const float middle=(low+high)*0.5f;
                    auto probe=evaluate(-middle*leftShare,middle*rightShare,true);
                    if(feasible(probe) && probe.metrics.penetration==0) { high=middle;endpoint=probe; }
                    else low=middle;
                }
                if(high<bestBudget) {bestBudget=high;result.best=endpoint;result.separated=true;}
                break;
            }
        }
        emit("bestFixedX",result.best);
        out << "hipZSummary,separated=" << result.separated << ",searchCap=0.25,shareSpacing=0.05\n";
        return result;
    }
}
