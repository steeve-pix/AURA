#include "aura/body/ThighSelfCollision.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/SelfCollision.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace aura::body {
    bool floorMoveIsNoWorse(float beforeY,float afterY,float tolerance) {
        return beforeY >= -tolerance ? afterY >= -tolerance : afterY >= beforeY - tolerance;
    }
    namespace {
        float penetration(const AuraBody3D& b) {
            return physics::capsulePenetration(physics::bodyPartCollisionCapsule(b.leftThigh),
                                               physics::bodyPartCollisionCapsule(b.rightThigh));
        }
        std::array<float,6> gaps(const AuraBody3D& b,const AuraSkeleton3D& s) {
            const std::array<const BodyPart3D*,6> parents{&b.pelvis,&b.leftThigh,&b.leftShin,&b.pelvis,&b.rightThigh,&b.rightShin};
            const std::array<const BodyPart3D*,6> children{&b.leftThigh,&b.leftShin,&b.leftFoot,&b.rightThigh,&b.rightShin,&b.rightFoot};
            const std::array<const Joint3D*,6> joints{&s.leftHip,&s.leftKnee,&s.leftAnkle,&s.rightHip,&s.rightKnee,&s.rightAnkle};
            std::array<float,6> result{};
            for (size_t i=0;i<6;++i) result[i]=(localToWorldPoint(*children[i],joints[i]->localAnchorB)-localToWorldPoint(*parents[i],joints[i]->localAnchorA)).length();
            return result;
        }
        ThighCandidateAudit inspect(const AuraBody3D& b,const AuraSkeleton3D& s,
                                   const std::array<float,6>& originalGaps,
                                   const std::array<float,6>* beforeY = nullptr) {
            const std::array<const BodyPart3D*,6> parents{&b.pelvis,&b.leftThigh,&b.leftShin,&b.pelvis,&b.rightThigh,&b.rightShin};
            const std::array<const BodyPart3D*,6> children{&b.leftThigh,&b.leftShin,&b.leftFoot,&b.rightThigh,&b.rightShin,&b.rightFoot};
            const std::array<const Joint3D*,6> joints{&s.leftHip,&s.leftKnee,&s.leftAnkle,&s.rightHip,&s.rightKnee,&s.rightAnkle};
            const auto candidateGaps=gaps(b,s);
            ThighCandidateAudit result;
            result.leftMinimumY=result.rightMinimumY=1e30f;
            for (size_t i=0;i<6;++i) {
                const bool left=i<3;
                const float y=physics::lowestPoint(children[i]->body,children[i]->size).y;
                result.lowestY[i]=y;
                auto& minimum=left?result.leftMinimumY:result.rightMinimumY;
                minimum=std::min(minimum,y);
                if (i==2) result.leftFootY=y;
                if (i==5) result.rightFootY=y;
                if (beforeY ? !floorMoveIsNoWorse((*beforeY)[i],y) : y < -1e-6f) result.rejections |= i==2?LeftFootFloor:i==5?RightFootFloor:left?LeftOtherLegFloor:RightOtherLegFloor;
                const float error=std::abs(jointAngleError(*parents[i],*children[i],*joints[i]));
                result.jointLimitErrors[i]=error;
                auto& limit=left?result.leftLimitError:result.rightLimitError;
                limit=std::max(limit,error);
                if (error>1e-5f) result.rejections |= left?LeftJointLimit:RightJointLimit;
                if (std::abs(candidateGaps[i]-originalGaps[i])>1e-5f) result.rejections |= AnchorGapDrift;
            }
            return result;
        }
        // Work on a copy of the already hip-rotated pose. Knee motion leaves both
        // thighs untouched. Probe both hinge directions and retain a feasible
        // upper bracket during bisection; never apply an unverified trial.
        bool compensateFoot(AuraBody3D& candidate,const AuraSkeleton3D& s,bool left,
                            const ThighCandidateAudit& initial,const std::array<float,6>& originalGaps,
                            float& appliedAngle) {
            const auto input=candidate;
            const auto& knee=left?s.leftKnee:s.rightKnee;
            const auto& thigh=left?input.leftThigh:input.rightThigh;
            const auto pivot=localToWorldPoint(thigh,knee.localAnchorA);
            const auto axis=thigh.body.orientation.normalized().rotate(knee.hingeAxis.normalized()).normalized();
            const int first=left?0:3, foot=first+2;
            constexpr float probe=0.001f, maximumAngle=0.01f;
            auto best=input;float bestAngle=0;bool found=false;
            const auto evaluate=[&](float angle,AuraBody3D& copy) {
                copy=input;
                auto& shin=left?copy.leftShin:copy.rightShin;
                rotateSubtreeAroundWorldPoint(s.collectComponent(copy,shin,knee),pivot,
                                             math::Quaternion::fromAxisAngle(axis,angle));
                return inspect(copy,s,originalGaps,&initial.lowestY);
            };
            const auto legSafe=[&](const ThighCandidateAudit& m) {
                for (int i=first;i<first+3;++i)
                    if (!floorMoveIsNoWorse(initial.lowestY[i],m.lowestY[i]) || m.jointLimitErrors[i]>1e-5f) return false;
                return (m.rejections&AnchorGapDrift)==0;
            };
            const float hipFootY=physics::lowestPoint((left?input.leftFoot:input.rightFoot).body,
                                                      (left?input.leftFoot:input.rightFoot).size).y;
            for (float sign:{-1.0f,1.0f}) {
                AuraBody3D copy;
                auto m=evaluate(sign*probe,copy);
                if (m.lowestY[foot]<=hipFootY+1e-8f) continue;
                float low=0;
                for (int step=1;step<=10;++step) {
                    const float high=std::min(maximumAngle,step*probe);
                    if (step!=1) m=evaluate(sign*high,copy);
                    if (legSafe(m)) {
                        float upper=high;auto feasible=copy;
                        for (int iteration=0;iteration<12;++iteration) {
                            const float mid=(low+upper)*0.5f;
                            const auto trial=evaluate(sign*mid,copy);
                            if (legSafe(trial)) {upper=mid;feasible=copy;} else low=mid;
                        }
                        if (!found || upper<std::abs(bestAngle)) {found=true;best=feasible;bestAngle=sign*upper;}
                        break;
                    }
                    low=high;
                }
            }
            if (found) {candidate=best;appliedAngle=bestAngle;}
            return found;
        }
    }
    ThighCollisionStep correctThighCapsuleOverlap(AuraBody3D& b,const AuraSkeleton3D& s,float maxAngleStep,float tolerance,ThighCollisionAudit* audit,ThighFloorPolicy floorPolicy,bool compensateKnees) {
        if (audit) *audit={};
        ThighCollisionStep result;
        result.penetrationBefore=result.penetrationAfter=penetration(b);
        if (result.penetrationBefore<=tolerance || maxAngleStep<=0) return result;
        const auto l=physics::bodyPartCollisionCapsule(b.leftThigh), r=physics::bodyPartCollisionCapsule(b.rightThigh);
        const auto contact=physics::closestPointsBetweenSegments(l.pointA,l.pointB,r.pointA,r.pointB);
        const auto difference=contact.pointB-contact.pointA;
        if (difference.lengthSquared()<1e-12f) { if (audit) audit->degenerateNormal=true; return result; } // No invented separation normal.
        const auto normal=difference.normalized();
        const auto lp=localToWorldPoint(b.pelvis,s.leftHip.localAnchorA),rp=localToWorldPoint(b.pelvis,s.rightHip.localAnchorA);
        const auto la=(contact.pointA-lp).cross(-normal).normalized(),ra=(contact.pointB-rp).cross(normal).normalized();
        if (audit) audit->degenerateAxis=la.lengthSquared()==0 || ra.lengthSquared()==0;
        const auto originalGaps=gaps(b,s);
        const auto initial=inspect(b,s,originalGaps);
        if (audit) audit->initial=initial;
        const std::array<float,5> steps{-maxAngleStep,-maxAngleStep*0.5f,0,maxAngleStep*0.5f,maxAngleStep};
        auto best=b;
        for (float dl:steps) for (float dr:steps) {
            if ((dl==0 && dr==0) || (dl!=0 && la.lengthSquared()==0) || (dr!=0 && ra.lengthSquared()==0)) continue;
            auto candidate=b;
            const auto left=s.collectComponent(candidate,candidate.leftThigh,s.leftHip);
            const auto right=s.collectComponent(candidate,candidate.rightThigh,s.rightHip);
            if (dl!=0) rotateSubtreeAroundWorldPoint(left,lp,math::Quaternion::fromAxisAngle(la,dl));
            if (dr!=0) rotateSubtreeAroundWorldPoint(right,rp,math::Quaternion::fromAxisAngle(ra,dr));
            const float overlap=penetration(candidate);
            if (!audit && !(overlap<result.penetrationAfter-1e-7f)) continue;
            // Inspect all trials for diagnostics, including those which do not improve.
            auto trial=inspect(candidate,s,originalGaps,
                floorPolicy==ThighFloorPolicy::NoWorsening ? &initial.lowestY : nullptr);
            trial.leftAngle=dl;trial.rightAngle=dr;trial.penetration=overlap;
            trial.improves=overlap<result.penetrationBefore-1e-7f;
            bool feasible=trial.rejections==0;
            // Class A only: all limits/anchors already valid; only moved feet
            // violate floor feasibility. Class B never reaches this search.
            if (compensateKnees && floorPolicy==ThighFloorPolicy::NoWorsening && trial.improves &&
                trial.rejections!=0 && (trial.rejections&~(LeftFootFloor|RightFootFloor))==0 &&
                (!(trial.rejections&LeftFootFloor) || dl!=0) && (!(trial.rejections&RightFootFloor) || dr!=0)) {
                trial.kneeAttempted=true;
                auto coupled=candidate;
                bool solved=true;
                if (trial.rejections&LeftFootFloor)
                    solved=compensateFoot(coupled,s,true,initial,originalGaps,trial.leftKneeAngle);
                if (solved && (trial.rejections&RightFootFloor))
                    solved=compensateFoot(coupled,s,false,initial,originalGaps,trial.rightKneeAngle);
                if (solved && inspect(coupled,s,originalGaps,&initial.lowestY).rejections==0 && penetration(coupled)==overlap) {
                    feasible=true;trial.kneeFeasible=true;candidate=coupled;
                }
            }
            if (audit) {
                audit->candidates[audit->candidateCount++]=trial;
                if (trial.improves) {
                    ++audit->improvingCandidates;
                    audit->improvingRejections |= trial.rejections;
                    if (feasible) ++audit->feasibleImprovingCandidates;
                }
            }
            if (overlap<result.penetrationAfter-1e-7f && feasible) {
                best=candidate;result.penetrationAfter=overlap;result.leftAngle=dl;result.rightAngle=dr;
                result.leftKneeAngle=trial.kneeFeasible?trial.leftKneeAngle:0;
                result.rightKneeAngle=trial.kneeFeasible?trial.rightKneeAngle:0;result.corrected=true;
            }
        }
        if (result.corrected) b=best;
        return result;
    }
}
