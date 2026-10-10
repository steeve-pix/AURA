#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include "HipCapsuleRotationReplay.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/ComponentPosition.hpp"
#include "aura/body/ThighSelfCollision.hpp"

using namespace aura;
namespace {
    auto parts(body::AuraBody3D& b,const body::AuraSkeleton3D& s) {
        auto p=s.collectComponent(b,b.torso,s.head);p.push_back(&b.head);return p;
    }
    // Preserve the 1e-6 geometry tolerance while avoiding float point-addition
    // quantization in a difference between nearly equal joint gap lengths.
    std::array<double,4> preciseDescendantGaps(const body::AuraBody3D& b,const body::AuraSkeleton3D& s) {
        const auto point=[](const auto& part,const auto& anchor) {
            const auto q=part.body.orientation.normalized();
            const auto v=q*math::Quaternion{0,anchor.x,anchor.y,anchor.z}*q.conjugate();
            return std::array<double,3>{double(part.body.position.x)+v.x,double(part.body.position.y)+v.y,double(part.body.position.z)+v.z};
        };
        const std::array<const body::BodyPart3D*,4> a{&b.leftThigh,&b.leftShin,&b.rightThigh,&b.rightShin};
        const std::array<const body::BodyPart3D*,4> c{&b.leftShin,&b.leftFoot,&b.rightShin,&b.rightFoot};
        const std::array<const body::Joint3D*,4> j{&s.leftKnee,&s.leftAnkle,&s.rightKnee,&s.rightAnkle};
        std::array<double,4> result{};
        for(size_t i=0;i<4;++i) {
            const auto pa=point(*a[i],j[i]->localAnchorA),pc=point(*c[i],j[i]->localAnchorB);double squared=0;
            for(size_t k=0;k<3;++k)squared+=(pc[k]-pa[k])*(pc[k]-pa[k]);
            result[i]=std::sqrt(squared);
        }
        return result;
    }
    struct HipPose {float twist{},y{},z{};};
    HipPose hipPose(const body::AuraBody3D& b,const body::AuraSkeleton3D& s,bool left) {
        const auto& t=left?b.leftThigh:b.rightThigh;const auto& j=left?s.leftHip:s.rightHip;
        const auto swing=body::relativeJointSwingAngles(b.pelvis,t,j);
        return {body::relativeJointTwistAngle(b.pelvis,t,j),swing.y,swing.z};
    }
    // Absolute swing/twist reconstruction from one unchanged parent frame.
    // Both leg targets are calculated before either component is rotated.
    void reconstruct(body::AuraBody3D& b,const body::AuraSkeleton3D& s,HipPose left,HipPose right) {
        const auto parent=b.pelvis.body.orientation.normalized();
        const auto target=[&](HipPose p) {
            const math::Vec3 vector{0,p.y,p.z};const float a=vector.length();
            const auto swing=a==0?math::Quaternion{}:math::Quaternion::fromAxisAngle(vector,a);
            return (parent*swing*math::Quaternion::fromAxisAngle({1,0,0},p.twist)).normalized();
        };
        const auto l=target(left),r=target(right);
        const auto lp=body::localToWorldPoint(b.pelvis,s.leftHip.localAnchorA);
        const auto rp=body::localToWorldPoint(b.pelvis,s.rightHip.localAnchorA);
        body::rotateSubtreeAroundWorldPoint(s.collectComponent(b,b.leftThigh,s.leftHip),lp,
            (l*b.leftThigh.body.orientation.normalized().conjugate()).normalized());
        body::rotateSubtreeAroundWorldPoint(s.collectComponent(b,b.rightThigh,s.rightHip),rp,
            (r*b.rightThigh.body.orientation.normalized().conjugate()).normalized());
    }
    float low(const body::BodyPart3D& p) {return physics::lowestPoint(p.body,p.size).y;}
    bool floorSafe(body::AuraBody3D& b,body::AuraBody3D& original,const body::AuraSkeleton3D& s) {
        auto p=parts(b,s),o=parts(original,s);
        for(size_t i=0;i<p.size();++i)if(!body::floorMoveIsNoWorse(low(*o[i]),low(*p[i])))return false;
        return true;
    }
    bool hipLimits(const body::AuraBody3D& b,const body::AuraSkeleton3D& s) {
        for(bool left:{true,false}) {
            const auto p=hipPose(b,s,left);const auto& j=left?s.leftHip:s.rightHip;
            if(p.twist<j.minAngle-1e-6f || p.twist>j.maxAngle+1e-6f || p.y<j.minSwingY-1e-6f ||
               p.y>j.maxSwingY+1e-6f || p.z<j.minSwingZ-1e-6f || p.z>j.maxSwingZ+1e-6f)return false;
        }
        return true;
    }
    bool compensateFoot(body::AuraBody3D& candidate,const body::AuraSkeleton3D& s,
                        const body::AuraBody3D& original,bool left,float& change) {
        const auto& oldFoot=left?original.leftFoot:original.rightFoot;
        const float allowed=low(oldFoot)>=-1e-6f?-1e-6f:low(oldFoot)-1e-6f;
        auto& foot=left?candidate.leftFoot:candidate.rightFoot;
        if(low(foot)>=allowed)return true;
        const auto& knee=left?s.leftKnee:s.rightKnee;
        const auto initial=candidate;
        const auto& thigh=left?initial.leftThigh:initial.rightThigh;
        const auto& shin=left?initial.leftShin:initial.rightShin;
        const float angle=body::relativeJointAngle(thigh,shin,knee);
        const auto pivot=body::localToWorldPoint(thigh,knee.localAnchorA);
        const auto axis=thigh.body.orientation.normalized().rotate(knee.hingeAxis.normalized()).normalized();
        const auto trial=[&](float delta) {
            auto copy=initial;auto& root=left?copy.leftShin:copy.rightShin;
            body::rotateSubtreeAroundWorldPoint(s.collectComponent(copy,root,knee),pivot,
                                              math::Quaternion::fromAxisAngle(axis,delta));
            return copy;
        };
        bool found=false;float bestDelta=0;auto best=initial;
        for(float sign:{-1.0f,1.0f}) {
            const float capacity=sign>0?knee.maxAngle-angle:angle-knee.minAngle;
            if(capacity<=0)continue;
            const float probe=std::min(0.001f,capacity);auto copy=trial(sign*probe);
            if(low(left?copy.leftFoot:copy.rightFoot)<=low(foot))continue;
            float lower=0,upper=probe;
            for(;;) {
                copy=trial(sign*upper);
                if(low(left?copy.leftFoot:copy.rightFoot)>=allowed) {
                    auto feasible=copy;
                    for(int i=0;i<32;++i) {
                        const float mid=(lower+upper)*0.5f;auto midCopy=trial(sign*mid);
                        if(low(left?midCopy.leftFoot:midCopy.rightFoot)>=allowed) {upper=mid;feasible=midCopy;}
                        else lower=mid;
                    }
                    const auto& ct=left?feasible.leftThigh:feasible.rightThigh;
                    const auto& cs=left?feasible.leftShin:feasible.rightShin;
                    const float finalAngle=body::relativeJointAngle(ct,cs,knee);
                    if(finalAngle>=knee.minAngle-1e-6f && finalAngle<=knee.maxAngle+1e-6f &&
                       (!found || upper<std::abs(bestDelta))) {found=true;bestDelta=sign*upper;best=feasible;}
                    break;
                }
                if(upper>=capacity)break;
                lower=upper;upper=std::min(capacity,upper*2);
            }
        }
        if(found){candidate=best;change=bestDelta;}return found;
    }
    void sample(const char* name,const body::AuraBody3D& b,const body::AuraSkeleton3D& s) {
        const auto m=test::hipRotationMetrics(b,s);const auto l=hipPose(b,s,true),r=hipPose(b,s,false);
        std::cout<<"twoHipPose,"<<name<<','<<m.penetration<<','<<m.gaps[0]<<','<<m.gaps[3]
            <<','<<std::max(m.gaps[0],m.gaps[3])<<','<<m.feetY[0]<<','<<m.feetY[1]
            <<','<<l.twist<<','<<l.y<<','<<l.z<<','<<r.twist<<','<<r.y<<','<<r.z<<'\n';
    }
}
int main(int argc,char** argv) {
    const bool local=argc==3 && std::string(argv[2])=="--local";
    if(argc!=2 && !local)return 2;
    auto input=body::createAuraBody3D();const auto s=body::createAuraSkeleton3D(input);
    auto ip=parts(input,s);std::ifstream file(argv[1]);std::string line;std::set<std::string> loaded;
    while(std::getline(file,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);std::string tag,name;row>>tag;
        if(tag=="thighJointGeometryLargest")continue;
        if(tag!="thighJointGeometryInput")return 2;
        row>>name;const auto it=std::find_if(ip.begin(),ip.end(),[&](auto* p){return p->name==name;});
        if(it==ip.end() || !loaded.insert(name).second)return 2;
        auto& p=**it;auto& b=p.body;
        row>>p.size.x>>p.size.y>>p.size.z>>b.mass>>b.position.x>>b.position.y>>b.position.z
            >>b.orientation.w>>b.orientation.x>>b.orientation.y>>b.orientation.z
            >>b.velocity.x>>b.velocity.y>>b.velocity.z>>b.angularVelocity.x>>b.angularVelocity.y>>b.angularVelocity.z
            >>b.momentOfInertia.x>>b.momentOfInertia.y>>b.momentOfInertia.z;
        if(!row)return 2;
    }
    if(loaded.size()!=16)return 2;
    bool passed=true;const auto check=[&](bool v,const char* label){if(!v){passed=false;std::cerr<<"FAIL "<<label<<'\n';}};
    auto serial=input;
    for(bool left:{true,false}) {
        const auto& hip=left?s.leftHip:s.rightHip;auto& thigh=left?serial.leftThigh:serial.rightThigh;
        body::correctBranchAngleAroundPivot(serial,s,hip,left?body::MajorBodyBranch3D::LeftHip:body::MajorBodyBranch3D::RightHip,true);
        body::correctJointPositionWithComponents(serial,s,serial.pelvis,thigh,hip);
    }
    auto l=hipPose(input,s,true),r=hipPose(input,s,false);
    const auto clamp=[](HipPose p,const body::Joint3D& j) {return HipPose{
        std::clamp(p.twist,j.minAngle,j.maxAngle),std::clamp(p.y,j.minSwingY,j.maxSwingY),std::clamp(p.z,j.minSwingZ,j.maxSwingZ)};};
    l=clamp(l,s.leftHip);r=clamp(r,s.rightHip);
    auto mandatory=input;reconstruct(mandatory,s,l,r);
    if(local) {
        auto current=mandatory;auto currentL=l,currentR=r;float accumulatedLKnee=0,accumulatedRKnee=0;
        int evaluations=0,accepted=0,blockedFloor=0,blockedLimits=0,blockedGeometry=0,stalled=0;
        int coupledKnee=0,firstConverged=0;float maxHipStep=0,maxKneeStep=0;double maximumPreciseGapDrift=0;bool stallReported=false;
        const auto originalMetrics=test::hipRotationMetrics(input,s);
        const auto originalPreciseGaps=preciseDescendantGaps(input,s);
        const auto cost=[](const auto& m) {return m.gaps[0]+m.gaps[3];};
        const auto coordinates=[&](const auto& b) {
            const auto a=hipPose(b,s,true),c=hipPose(b,s,false);
            return std::array<float,4>{a.y,a.z,c.y,c.z};
        };
        const auto emit=[&](int iteration,bool changed,const auto& b,float motion) {
            const auto m=test::hipRotationMetrics(b,s);const auto x=coordinates(b);
            std::cout<<"localHipStep,"<<iteration<<','<<changed<<','<<m.penetration<<','<<m.gaps[0]<<','<<m.gaps[3];
            for(float value:x)std::cout<<','<<value;
            std::cout<<','<<std::min(m.feetY[0],m.feetY[1])<<','<<motion<<'\n';
        };
        std::cout<<std::scientific<<std::setprecision(9)
            <<"localHipStep,iteration,accepted,penetration,leftHipGap,rightHipGap,leftSwingY,leftSwingZ,rightSwingY,rightSwingZ,minFootY,hipCoordinateStep\n";
        emit(0,false,current,0);
        for(int iteration=1;iteration<=32;++iteration) {
            const auto before=current;const auto beforeMetrics=test::hipRotationMetrics(before,s);
            auto best=before;auto bestMetrics=beforeMetrics;float bestMotion=0,bestHipStep=0,bestDL=0,bestDR=0;
            bool improved=false;auto bestL=currentL,bestR=currentR;std::vector<std::string> neighbors;
            const auto record=[&](int coordinate,float sign,const char* status,const auto* candidate) {
                std::ostringstream row;row<<std::scientific<<std::setprecision(9)<<"localHipNeighbor,"<<iteration
                    <<','<<coordinate<<','<<sign<<','<<status;
                if(candidate) {
                    const auto m=test::hipRotationMetrics(*candidate,s);float maxDrift=0;
                    for(size_t i:{size_t(1),size_t(2),size_t(4),size_t(5)})maxDrift=std::max(maxDrift,std::abs(m.gaps[i]-originalMetrics.gaps[i]));
                    row<<','<<m.penetration<<','<<cost(m)<<','<<std::min(m.feetY[0],m.feetY[1])<<','<<maxDrift;
                    auto copy=*candidate;std::string floorPart="none";float floorBefore=0,floorAfter=0;
                    for(auto* p:parts(copy,s)) {
                        const auto old=std::find_if(ip.begin(),ip.end(),[&](auto* q){return q->name==p->name;});
                        if(!body::floorMoveIsNoWorse(low(**old),low(*p))) {floorPart=p->name;floorBefore=low(**old);floorAfter=low(*p);break;}
                    }
                    row<<','<<floorPart<<','<<floorBefore<<','<<floorAfter;
                    const auto exact=preciseDescendantGaps(*candidate,s),old=preciseDescendantGaps(input,s);double exactDrift=0;
                    for(size_t i=0;i<4;++i)exactDrift=std::max(exactDrift,std::abs(exact[i]-old[i]));
                    row<<','<<exactDrift;


                } else row<<",nan,nan,nan,nan,none,nan,nan,nan";
                neighbors.push_back(row.str());
            };
            for(int coordinate=0;coordinate<4;++coordinate)for(float sign:{-1.0f,1.0f}) {
                ++evaluations;
                auto a=currentL,b=currentR;
                // Canonicalize boundary roundoff; the requested neighbor still
                // changes exactly one swing coordinate by +/-0.01 rad.
                a=clamp(a,s.leftHip);b=clamp(b,s.rightHip);
                float& x=coordinate==0?a.y:coordinate==1?a.z:coordinate==2?b.y:b.z;x+=sign*0.01f;
                if(a.y<s.leftHip.minSwingY || a.y>s.leftHip.maxSwingY || a.z<s.leftHip.minSwingZ || a.z>s.leftHip.maxSwingZ ||
                   b.y<s.rightHip.minSwingY || b.y>s.rightHip.maxSwingY || b.z<s.rightHip.minSwingZ || b.z>s.rightHip.maxSwingZ) {
                    ++blockedLimits;record(coordinate,sign,"swingLimit",static_cast<const body::AuraBody3D*>(nullptr));continue;
                }
                // Absolute swing coordinates describe the current pose, but
                // reference geometry is fixed throughout this copied solve.
                // Reconstruct instead of accumulating float-position rotations.
                auto candidate=mandatory;reconstruct(candidate,s,a,b);
                for(bool left:{true,false}) {
                    auto& thigh=left?candidate.leftThigh:candidate.rightThigh;const auto& hip=left?s.leftHip:s.rightHip;
                    const auto error=body::localToWorldPoint(candidate.pelvis,hip.localAnchorA)-body::localToWorldPoint(thigh,hip.localAnchorB);
                    body::translateSubtree(s.collectComponent(candidate,thigh,hip),error);
                }
                for(bool left:{true,false}) {
                    const float delta=left?accumulatedLKnee:accumulatedRKnee;
                    if(delta==0)continue;
                    auto& thigh=left?candidate.leftThigh:candidate.rightThigh;
                    auto& shin=left?candidate.leftShin:candidate.rightShin;
                    const auto& knee=left?s.leftKnee:s.rightKnee;
                    const auto pivot=body::localToWorldPoint(thigh,knee.localAnchorA);
                    const auto axis=thigh.body.orientation.normalized().rotate(knee.hingeAxis.normalized()).normalized();
                    body::rotateSubtreeAroundWorldPoint(s.collectComponent(candidate,shin,knee),pivot,
                        math::Quaternion::fromAxisAngle(axis,delta));
                }
                bool nonFootSafe=true;
                for(auto* p:parts(candidate,s))if(p!=&candidate.leftFoot && p!=&candidate.rightFoot) {
                    const auto old=std::find_if(ip.begin(),ip.end(),[&](auto* q){return q->name==p->name;});
                    nonFootSafe &= body::floorMoveIsNoWorse(low(**old),low(*p));
                }
                float dl=0,dr=0;
                if(!nonFootSafe || !compensateFoot(candidate,s,input,true,dl) || !compensateFoot(candidate,s,input,false,dr) ||
                   !floorSafe(candidate,input,s)) {++blockedFloor;record(coordinate,sign,"floor",&candidate);continue;}
                if(!hipLimits(candidate,s)) {++blockedLimits;record(coordinate,sign,"hipLimit",&candidate);continue;}
                const auto m=test::hipRotationMetrics(candidate,s);bool descendants=true;
                const auto precise=preciseDescendantGaps(candidate,s);
                for(size_t i=0;i<4;++i)descendants &= std::abs(precise[i]-originalPreciseGaps[i])<=1e-6;
                if(!descendants) {++blockedGeometry;record(coordinate,sign,"descendantGap",&candidate);continue;}
                record(coordinate,sign,"feasible",&candidate);
                const float motion=0.01f+std::abs(dl)+std::abs(dr);
                // Strict lexicographic improvement: no weighted error sum.
                const bool lessPen=m.penetration<bestMetrics.penetration-1e-7f;
                const bool samePen=std::abs(m.penetration-bestMetrics.penetration)<=1e-7f;
                const bool lessGap=cost(m)<cost(bestMetrics)-1e-7f;
                const bool sameGap=std::abs(cost(m)-cost(bestMetrics))<=1e-7f;
                if(lessPen || (samePen && (lessGap || (sameGap && motion<bestMotion)))) {
                    best=candidate;bestMetrics=m;bestMotion=motion;bestHipStep=0.01f;bestDL=dl;bestDR=dr;bestL=a;bestR=b;improved=true;
                }
            }
            if(improved) {
                current=best;currentL=bestL;currentR=bestR;accumulatedLKnee+=bestDL;accumulatedRKnee+=bestDR;
                ++accepted;coupledKnee+=bestDL!=0 || bestDR!=0;
                maxHipStep=std::max(maxHipStep,bestHipStep);maxKneeStep=std::max({maxKneeStep,std::abs(bestDL),std::abs(bestDR)});
            } else {
                ++stalled;
                if(!stallReported && beforeMetrics.penetration>0.001f) {
                    std::cout<<"localHipNeighbor,iteration,coordinate,sign,status,penetration,totalHipGap,minFootY,maxDescendantGapDrift,floorPart,floorBefore,floorAfter,preciseDescendantGapDrift\n";
                    for(const auto& row:neighbors)std::cout<<row<<'\n';
                    stallReported=true;
                }
            }
            const auto m=test::hipRotationMetrics(current,s);
            check(hipLimits(current,s) && floorSafe(current,input,s),"every local step satisfies hard hip/floor requirements");
            check(m.penetration<=beforeMetrics.penetration+1e-7f,"accepted penetration is non-increasing");
            const auto cp=parts(current,s);
            for(size_t i=0;i<cp.size();++i)check((cp[i]->body.velocity-ip[i]->body.velocity).lengthSquared()==0 &&
                (cp[i]->body.angularVelocity-ip[i]->body.angularVelocity).lengthSquared()==0,"local geometry never modifies velocities");
            const auto precise=preciseDescendantGaps(current,s);
            for(size_t i=0;i<4;++i)maximumPreciseGapDrift=std::max(maximumPreciseGapDrift,std::abs(precise[i]-originalPreciseGaps[i]));
            if(firstConverged==0 && m.penetration<=0.001f)firstConverged=iteration;
            for(size_t i=0;i<4;++i)check(std::abs(precise[i]-originalPreciseGaps[i])<=1e-6,
                "descendant anchor relationships stay preserved at unchanged tolerance");
            for(size_t i:{size_t(2),size_t(5)})
                check(std::abs(m.angles[i]-originalMetrics.angles[i])<=1e-6f,"ankle orientation remains preserved");
            const auto x=coordinates(current),old=coordinates(before);int changed=0;
            for(size_t i=0;i<4;++i) {
                changed+=std::abs(x[i]-old[i])>1e-6f;
                check(std::abs(x[i]-old[i])<=0.010001f,"accepted swing coordinate change is bounded");
            }
            check(changed<=1,"at most one neighboring hip coordinate is accepted per iteration");
            check(std::abs(hipPose(current,s,true).twist-l.twist)<1e-6f && std::abs(hipPose(current,s,false).twist-r.twist)<1e-6f,
                  "local swings retain both legal twists");
            emit(iteration,improved,current,bestHipStep);
        }
        const auto finalMetrics=test::hipRotationMetrics(current,s);
        check(finalMetrics.penetration<=0.001f,"bounded local search resolves the saved thigh collision within 32 iterations");
        check(finalMetrics.gaps[0]<=1e-6f && finalMetrics.gaps[3]<=1e-6f,"local search closes both hip anchors");
        std::cout<<"localHipSummary,evaluations="<<evaluations<<",accepted="<<accepted<<",stalled="<<stalled
            <<",limitRejected="<<blockedLimits<<",floorRejected="<<blockedFloor<<",geometryRejected="<<blockedGeometry
            <<",firstConvergedIteration="<<firstConverged<<",maximumPreciseDescendantGapDrift="<<maximumPreciseGapDrift
            <<",kneeCoupled="<<coupledKnee<<",maxHipStep="<<maxHipStep<<",maxKneeStep="<<maxKneeStep
            <<",converged="<<(test::hipRotationMetrics(current,s).penetration<=0.001f)<<",passed="<<passed<<'\n';
        // Exercise the actual runtime helper independently of the copied search.
        auto liveCopy=input;int liveAccepted=0;
        for(int i=0;i<32;++i) {
            auto before=liveCopy;
            body::ThighSwingCollisionAudit audit;
            const auto step=body::correctThighOverlapCoupled(liveCopy,s,0.01f,0.001f,&audit);
            liveAccepted+=step.corrected;
            check(audit.candidates<=8,"production helper evaluates at most eight neighbors per call");
            check(std::abs(step.leftRequest)<=0.010001f && std::abs(step.rightRequest)<=0.010001f,
                "production helper accepts at most one bounded coordinate neighbor");
            check(step.leftRequest==0 || step.rightRequest==0,"production helper never accepts two coordinate moves");
            check(hipLimits(liveCopy,s) && floorSafe(liveCopy,before,s),"production helper respects limits and floor");
            const auto relative=[](const auto& parent,const auto& child) {
                return (parent.body.orientation.normalized().conjugate()*child.body.orientation.normalized()).normalized();
            };
            const std::array oldRelations{relative(before.leftThigh,before.leftShin),relative(before.leftShin,before.leftFoot),
                relative(before.rightThigh,before.rightShin),relative(before.rightShin,before.rightFoot)};
            const std::array newRelations{relative(liveCopy.leftThigh,liveCopy.leftShin),relative(liveCopy.leftShin,liveCopy.leftFoot),
                relative(liveCopy.rightThigh,liveCopy.rightShin),relative(liveCopy.rightShin,liveCopy.rightFoot)};
            for(size_t k=0;k<4;++k) {
                const auto delta=(newRelations[k]*oldRelations[k].conjugate()).normalized();
                check(2*std::atan2(std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z),std::abs(delta.w))<1e-6,
                    "production helper preserves full descendant relative orientation");
            }
            const auto previous=preciseDescendantGaps(before,s),after=preciseDescendantGaps(liveCopy,s);
            for(size_t k=0;k<4;++k)check(std::abs(after[k]-previous[k])<=1e-6,"production helper preserves descendants");
            const auto a=parts(liveCopy,s);auto old=before;const auto c=parts(old,s);
            for(size_t k=0;k<a.size();++k)check((a[k]->body.velocity-c[k]->body.velocity).lengthSquared()==0 &&
                (a[k]->body.angularVelocity-c[k]->body.angularVelocity).lengthSquared()==0,"production geometry leaves velocities untouched");
        }
        for(int budget:{1,2,4}) {
            auto budgetCopy=input,manualCopy=input;
            body::ThighSwingCollisionAudit budgetAudit;
            const auto bounded=body::correctThighOverlapCoupledBudget(budgetCopy,s,budget,0.01f,0.001f,&budgetAudit);
            int manualCalls=0,manualAccepted=0;
            for(int i=0;i<budget;++i) {
                if(test::hipRotationMetrics(manualCopy,s).penetration<=0.001f)break;
                const auto single=body::correctThighOverlapCoupled(manualCopy,s);
                ++manualCalls;manualAccepted+=single.corrected;
                if(!single.corrected)break;
            }
            check(budgetAudit.projectionCalls==manualCalls && budgetAudit.acceptedProjections==manualAccepted,
                "fixed budget exactly matches bounded single-step sequencing");
            check(budgetAudit.projectionCalls<=budget && budgetAudit.candidates<=8*budget,
                "projection and candidate budgets cannot be exceeded");
            check(std::abs(bounded.leftRequest)<=0.010001f && std::abs(bounded.rightRequest)<=0.010001f,
                "budget reporting retains the single-step cap");
            const auto bp=parts(budgetCopy,s),mp=parts(manualCopy,s);
            for(size_t k=0;k<bp.size();++k) {
                const auto& a=bp[k]->body;const auto& c=mp[k]->body;
                check((a.position-c.position).lengthSquared()==0 && a.orientation.w==c.orientation.w &&
                    a.orientation.x==c.orientation.x && a.orientation.y==c.orientation.y && a.orientation.z==c.orientation.z,
                    "fixed budget changes no mechanics versus identical single-step calls");
                check((a.velocity-ip[k]->body.velocity).lengthSquared()==0 &&
                    (a.angularVelocity-ip[k]->body.angularVelocity).lengthSquared()==0,"all budget modes remain pose-only");
            }
        }
        auto neutral=body::createAuraBody3D();auto untouched=neutral;
        body::ThighSwingCollisionAudit neutralAudit;
        const auto inactive=body::correctThighOverlapCoupled(neutral,s,0.01f,0.001f,&neutralAudit);
        body::ThighSwingCollisionAudit emptyBudgetAudit;
        body::correctThighOverlapCoupledBudget(neutral,s,4,0.01f,0.001f,&emptyBudgetAudit);
        check(emptyBudgetAudit.projectionCalls==0,"budget wrapper skips all searches for separated thighs");
        check(!inactive.corrected && neutralAudit.candidates==0,"separated thighs skip the coupled search");
        const auto np=parts(neutral,s),up=parts(untouched,s);
        for(size_t k=0;k<np.size();++k)check((np[k]->body.position-up[k]->body.position).lengthSquared()==0 &&
            np[k]->body.orientation.w==up[k]->body.orientation.w && np[k]->body.orientation.x==up[k]->body.orientation.x &&
            np[k]->body.orientation.y==up[k]->body.orientation.y && np[k]->body.orientation.z==up[k]->body.orientation.z,
            "no-overlap helper leaves all poses exactly unchanged");
        const auto liveMetrics=test::hipRotationMetrics(liveCopy,s);
        check(liveMetrics.penetration<=0.001f,"production helper converges on saved fixture within 32 calls");
        std::cout<<"productionLocalHipSummary,accepted="<<liveAccepted<<",penetration="<<liveMetrics.penetration
            <<",maxHipGap="<<std::max(liveMetrics.gaps[0],liveMetrics.gaps[3])<<",minFootY="<<std::min(liveMetrics.feetY[0],liveMetrics.feetY[1])
            <<",passed="<<passed<<'\n';
        return passed?0:1;
    }
    bool found=false;auto best=mandatory;float bestPen=1e30f,bestGap=1e30f;
    HipPose bestL=l,bestR=r;float bestLKnee=0,bestRKnee=0;
    int evaluated=0,feasible=0,kneeRecoveries=0;bool lastImproved=false;
    const auto evaluate=[&](HipPose lp,HipPose rp) {
        ++evaluated;auto candidate=mandatory;reconstruct(candidate,s,lp,rp);
        // Geometry search repairs the two hip anchors together, without moving
        // the pelvis. Descendant gaps are carried rigidly, not silently repaired.
        for(bool left:{true,false}) {
            auto& thigh=left?candidate.leftThigh:candidate.rightThigh;const auto& hip=left?s.leftHip:s.rightHip;
            const auto error=body::localToWorldPoint(candidate.pelvis,hip.localAnchorA)-body::localToWorldPoint(thigh,hip.localAnchorB);
            body::translateSubtree(s.collectComponent(candidate,thigh,hip),error);
        }
        float dl=0,dr=0;
        // Knee adjustment is attempted only when feet alone block the floor.
        for(auto* p:parts(candidate,s))if(p!=&candidate.leftFoot && p!=&candidate.rightFoot) {
            const auto old=std::find_if(ip.begin(),ip.end(),[&](auto* q){return q->name==p->name;});
            if(!body::floorMoveIsNoWorse(low(**old),low(*p)))return;
        }
        if(!compensateFoot(candidate,s,input,true,dl) || !compensateFoot(candidate,s,input,false,dr))return;
        if(!hipLimits(candidate,s) || !floorSafe(candidate,input,s))return;
        const auto m=test::hipRotationMetrics(candidate,s),original=test::hipRotationMetrics(input,s);
        for(size_t i:{size_t(1),size_t(2),size_t(4),size_t(5)})if(std::abs(m.gaps[i]-original.gaps[i])>1e-6f)return;
        ++feasible;kneeRecoveries+=dl!=0 || dr!=0;
        const float gap=std::max(m.gaps[0],m.gaps[3]);
        // Primary penetration; anchor error is considered only within detector
        // roundoff (1e-7 length units). Knee motion is never part of the objective.
        if(!found || m.penetration<bestPen-1e-7f || (std::abs(m.penetration-bestPen)<=1e-7f && gap<bestGap)) {
            found=true;best=candidate;bestPen=m.penetration;bestGap=gap;bestL=lp;bestR=rp;bestLKnee=dl;bestRKnee=dr;lastImproved=true;
        }
    };
    evaluate(l,r);
    // Full legal Y/Z domain coarse grid, followed by local coordinate refinement.
    // This is a sampled copied-pose search, not a global optimality proof.
    for(int ly=0;ly<=16;++ly)for(int lz=0;lz<=16;++lz)for(int ry=0;ry<=16;++ry)for(int rz=0;rz<=16;++rz) {
        const auto value=[](float lo,float hi,int i){return lo+(hi-lo)*i/16.0f;};
        evaluate({l.twist,value(s.leftHip.minSwingY,s.leftHip.maxSwingY,ly),value(s.leftHip.minSwingZ,s.leftHip.maxSwingZ,lz)},
                 {r.twist,value(s.rightHip.minSwingY,s.rightHip.maxSwingY,ry),value(s.rightHip.minSwingZ,s.rightHip.maxSwingZ,rz)});
    }
    if(found)for(float step:{0.025f,0.0125f,0.00625f,0.003125f})for(int sweep=0;sweep<8;++sweep) {
        lastImproved=false;const auto centerL=bestL,centerR=bestR;
        for(int coordinate=0;coordinate<4;++coordinate)for(float sign:{-1.0f,1.0f}) {
            auto a=centerL,b=centerR;
            float& x=coordinate==0?a.y:coordinate==1?a.z:coordinate==2?b.y:b.z;x+=step*sign;
            if(a.y<s.leftHip.minSwingY || a.y>s.leftHip.maxSwingY || a.z<s.leftHip.minSwingZ || a.z>s.leftHip.maxSwingZ ||
               b.y<s.rightHip.minSwingY || b.y>s.rightHip.maxSwingY || b.z<s.rightHip.minSwingZ || b.z>s.rightHip.maxSwingZ)continue;
            evaluate(a,b);
        }
        if(!lastImproved)break;
    }
    std::cout<<std::scientific<<std::setprecision(9)
        <<"twoHipPose,pose,penetration,leftHipGap,rightHipGap,maxHipGap,leftFootY,rightFootY,leftTwist,leftSwingY,leftSwingZ,rightTwist,rightSwingY,rightSwingZ\n";
    sample("original",input,s);sample("serialHipRepairs",serial,s);sample("mandatoryBothLimits",mandatory,s);
    if(found)sample("jointTwoHipSearch",best,s);
    std::cout<<"twoHipSearch,evaluated="<<evaluated<<",feasible="<<feasible<<",floorRecovered="<<kneeRecoveries
        <<",leftKneeDelta="<<bestLKnee<<",rightKneeDelta="<<bestRKnee<<",found="<<found<<'\n';
    check(hipLimits(mandatory,s),"mandatory reconstruction satisfies both hip limits");
    const auto originalL=hipPose(input,s,true),originalR=hipPose(input,s,false);
    const auto repairedL=hipPose(mandatory,s,true),repairedR=hipPose(mandatory,s,false);
    check(std::abs(repairedL.twist-originalL.twist)<1e-6f && std::abs(repairedL.z-originalL.z)<1e-6f &&
          std::abs(repairedR.y-originalR.y)<1e-6f && std::abs(repairedR.z-originalR.z)<1e-6f,
          "mandatory repairs preserve the unrelated swing/twist coordinates");
    const auto originalMetrics=test::hipRotationMetrics(input,s),mandatoryMetrics=test::hipRotationMetrics(mandatory,s);
    for(size_t i=0;i<6;++i)check(std::abs(mandatoryMetrics.gaps[i]-originalMetrics.gaps[i])<1e-6f,
                               "simultaneous pivot rotation preserves existing leg attachment gaps");
    check(floorSafe(mandatory,input,s),"mandatory limit reconstruction is floor feasible in this fixture");
    check(found && hipLimits(best,s) && floorSafe(best,input,s),"joint candidate satisfies hard hip and floor requirements");
    check(std::abs(test::hipRotationMetrics(serial,s).penetration-0.238516f)<1e-6f,"serial hip replay reproduces penetration");
    if(found) {
        const auto m=test::hipRotationMetrics(best,s),o=test::hipRotationMetrics(input,s);
        for(size_t i:{size_t(1),size_t(2),size_t(4),size_t(5)})check(std::abs(m.gaps[i]-o.gaps[i])<1e-6f,"descendant anchors preserved");
        for(size_t i:{size_t(2),size_t(5)})check(std::abs(m.angles[i]-o.angles[i])<1e-6f,"ankle angles preserved");
        if(bestLKnee==0)check(std::abs(m.angles[1]-o.angles[1])<1e-6f,"left knee orientation preserved without compensation");
        if(bestRKnee==0)check(std::abs(m.angles[4]-o.angles[4])<1e-6f,"right knee orientation preserved without compensation");
        const auto relative=[&](const auto& b,bool left,bool ankle) {
            const auto& a=ankle?(left?b.leftShin:b.rightShin):(left?b.leftThigh:b.rightThigh);
            const auto& c=ankle?(left?b.leftFoot:b.rightFoot):(left?b.leftShin:b.rightShin);
            return (a.body.orientation.normalized().conjugate()*c.body.orientation.normalized()).normalized();
        };
        for(bool left:{true,false})for(bool ankle:{true,false}) {
            if(!ankle && (left?bestLKnee:bestRKnee)!=0)continue;
            const auto a=relative(input,left,ankle),b=relative(best,left,ankle);
            const double dot=std::abs(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z);
            check(std::abs(1.0-dot)<1e-10,"descendant full relative orientation is preserved");
        }
        for(auto* copy:{&serial,&mandatory,&best}) {
            const auto cp=parts(*copy,s);
            for(size_t i=0;i<cp.size();++i)check((cp[i]->body.velocity-ip[i]->body.velocity).lengthSquared()==0 &&
                (cp[i]->body.angularVelocity-ip[i]->body.angularVelocity).lengthSquared()==0,"geometry leaves all velocities exactly unchanged");
        }
        check(std::abs(hipPose(best,s,true).twist-l.twist)<1e-6f && std::abs(hipPose(best,s,false).twist-r.twist)<1e-6f,
              "legal X twists are held fixed during swing search");
        check(m.penetration<=test::hipRotationMetrics(serial,s).penetration,"joint search improves over serial pose");
    }
    std::cout<<"twoHipReplayPassed="<<passed<<'\n';return passed?0:1;
}
