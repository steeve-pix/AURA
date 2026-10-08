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
    if(argc!=2)return 2;
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
