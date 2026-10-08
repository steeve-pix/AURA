#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include "aura/body/JointGeometry.hpp"
#include "aura/body/AuraBody3D.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/ThighSelfCollision.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/SelfCollision.hpp"

using namespace aura;
namespace {
    float gap(const body::BodyPart3D& a,const body::BodyPart3D& b,const body::Joint3D& j) {
        return (body::localToWorldPoint(b,j.localAnchorB)-body::localToWorldPoint(a,j.localAnchorA)).length();
    }
    float lowest(const body::BodyPart3D& p) { return physics::lowestPoint(p.body,p.size).y; }
    float overlap(const body::AuraBody3D& b) {
        return physics::capsulePenetration(physics::bodyPartCollisionCapsule(b.leftThigh),physics::bodyPartCollisionCapsule(b.rightThigh));
    }
    std::array<float,10> measurements(const body::AuraBody3D& b,const body::AuraSkeleton3D& s) {
        const auto swing=body::relativeJointSwingAngles(b.pelvis,b.rightThigh,s.rightHip);
        return {overlap(b),body::relativeJointTwistAngle(b.pelvis,b.rightThigh,s.rightHip),swing.y,swing.z,
            body::relativeJointAngle(b.rightThigh,b.rightShin,s.rightKnee),lowest(b.rightFoot),
            gap(b.pelvis,b.rightThigh,s.rightHip),gap(b.rightThigh,b.rightShin,s.rightKnee),
            gap(b.rightShin,b.rightFoot,s.rightAnkle),body::relativeJointAngle(b.rightShin,b.rightFoot,s.rightAnkle)};
    }
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto input=body::createAuraBody3D();const auto skeleton=body::createAuraSkeleton3D(input);
    auto parts=skeleton.collectComponent(input,input.torso,skeleton.head);parts.push_back(&input.head);
    std::ifstream file(argv[1]);std::string line;std::set<std::string> loaded;
    math::Vec3 hipAxis;float hipRequest=0;bool requestLoaded=false;
    while(std::getline(file,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);std::string tag,name;row>>tag;
        if(tag=="swingKneeRequest") {row>>hipAxis.x>>hipAxis.y>>hipAxis.z>>hipRequest;requestLoaded=bool(row);continue;}
        if(tag!="swingKneeInput")return 2;row>>name;
        auto it=std::find_if(parts.begin(),parts.end(),[&](auto* p){return p->name==name;});
        if(it==parts.end() || !loaded.insert(name).second)return 2;
        auto& p=**it;auto& b=p.body;
        row>>p.size.x>>p.size.y>>p.size.z>>b.mass>>b.position.x>>b.position.y>>b.position.z
            >>b.orientation.w>>b.orientation.x>>b.orientation.y>>b.orientation.z
            >>b.velocity.x>>b.velocity.y>>b.velocity.z>>b.angularVelocity.x>>b.angularVelocity.y>>b.angularVelocity.z
            >>b.momentOfInertia.x>>b.momentOfInertia.y>>b.momentOfInertia.z;
        if(!row)return 2;
    }
    if(loaded.size()!=16 || !requestLoaded)return 2;
    bool passed=true;
    const auto check=[&](bool condition,const char* message) {if(!condition){std::cerr<<"FAIL "<<message<<'\n';passed=false;}};
    auto hip=input;
    check(body::applyJointSwingWithComponent(hip.pelvis,hip.rightThigh,skeleton.rightHip,
        skeleton.collectComponent(hip,hip.rightThigh,skeleton.rightHip),hipAxis,hipRequest),"recorded hip request is legal");
    const auto before=measurements(input,skeleton),hipOnly=measurements(hip,skeleton);
    constexpr float floorTolerance=1e-6f;
    const float allowedY=before[5]>=-floorTolerance?-floorTolerance:before[5]-floorTolerance;
    check(hipOnly[0]<before[0] && hipOnly[5]<allowedY,"recorded hip request improves collision but violates foot floor feasibility");

    // Frozen hip; every knee trial starts from the same hip-only copy.
    const auto kneePivot=body::localToWorldPoint(hip.rightThigh,skeleton.rightKnee.localAnchorA);
    const auto kneeAxis=hip.rightThigh.body.orientation.normalized().rotate(skeleton.rightKnee.hingeAxis.normalized()).normalized();
    const auto trial=[&](double angle) {
        auto b=hip;
        body::rotateSubtreeAroundWorldPoint(skeleton.collectComponent(b,b.rightShin,skeleton.rightKnee),kneePivot,
            math::Quaternion::fromAxisAngle(kneeAxis,static_cast<float>(angle)));
        return b;
    };
    constexpr double probe=0.001;
    const auto positive=trial(probe),negative=trial(-probe);
    const float positiveY=lowest(positive.rightFoot),negativeY=lowest(negative.rightFoot);
    const double direction=positiveY>=negativeY?1.0:-1.0;
    const float kneeAngle=hipOnly[4];
    const double available=direction>0?skeleton.rightKnee.maxAngle-kneeAngle:kneeAngle-skeleton.rightKnee.minAngle;
    const auto feasible=[&](const body::AuraBody3D& b) {
        if(lowest(b.rightFoot)<allowedY || std::abs(body::jointAngleError(b.rightThigh,b.rightShin,skeleton.rightKnee))>1e-5f ||
           std::abs(body::jointAngleError(b.rightShin,b.rightFoot,skeleton.rightAnkle))>1e-5f)return false;
        return body::floorMoveIsNoWorse(lowest(input.rightThigh),lowest(b.rightThigh)) &&
            body::floorMoveIsNoWorse(lowest(input.rightShin),lowest(b.rightShin));
    };
    check(std::max(positiveY,negativeY)>hipOnly[5],"one knee probe raises the foot");
    double low=0,high=std::min(probe,available);auto compensated=hip;bool found=false;
    while(high>0) {
        auto candidate=trial(direction*high);
        if(feasible(candidate)) {compensated=candidate;found=true;break;}
        if(high>=available)break;
        low=high;high=std::min(high*2.0,available);
    }
    if(found) {
        // Retain the feasible endpoint because float world positions can step
        // discontinuously at the floor boundary; never apply a failing midpoint.
        for(int i=0;i<40;++i) {
            const double mid=(low+high)*0.5;auto candidate=trial(direction*mid);
            if(feasible(candidate)) {high=mid;compensated=candidate;} else low=mid;
        }
    }
    const auto after=measurements(compensated,skeleton);
    std::cout<<std::scientific<<std::setprecision(9)
        <<"kneeSwingProbe,positiveFootY="<<positiveY<<",negativeFootY="<<negativeY<<",chosenSign="<<direction
        <<",allowedFootY="<<allowedY<<",feasible="<<found<<",kneeCorrection="<<static_cast<float>(direction*high)
        <<",infeasibleLowerMagnitude="<<low<<'\n';
    const std::array names{"thighPenetration","hipTwistX","hipSwingY","hipSwingZ","kneeAngle","footLowestY",
        "hipGap","kneeGap","ankleGap","ankleAngle"};
    std::cout<<"measurement,before,hipOnly,hipPlusKnee\n";
    for(size_t i=0;i<names.size();++i)std::cout<<names[i]<<','<<before[i]<<','<<hipOnly[i]<<','<<after[i]<<'\n';
    check(found,"knee compensation restores floor feasibility within the knee range");
    check(after[0]==hipOnly[0],"knee compensation leaves the thigh penetration exactly unchanged");
    check(after[1]==hipOnly[1] && after[2]==hipOnly[2] && after[3]==hipOnly[3],"corrected hip pose remains exactly fixed");
    check(std::abs(after[9]-hipOnly[9])<1e-6f,"ankle relative angle is preserved");
    for(size_t i=6;i<9;++i)check(after[i]<1e-6f,"all right-leg anchors remain attached");
    check(found && !feasible(trial(direction*low)),"the final smaller bracket endpoint remains infeasible");
    const auto swing=body::relativeJointSwingAngles(compensated.pelvis,compensated.rightThigh,skeleton.rightHip);
    check(swing.y>=skeleton.rightHip.minSwingY && swing.y<=skeleton.rightHip.maxSwingY &&
        swing.z>=skeleton.rightHip.minSwingZ && swing.z<=skeleton.rightHip.maxSwingZ,"hip swing bounds remain respected");
    check(std::abs(body::jointAngleError(compensated.pelvis,compensated.rightThigh,skeleton.rightHip))<=1e-5f &&
        std::abs(body::jointAngleError(compensated.rightThigh,compensated.rightShin,skeleton.rightKnee))<=1e-5f &&
        std::abs(body::jointAngleError(compensated.rightShin,compensated.rightFoot,skeleton.rightAnkle))<=1e-5f,
        "hip/knee/ankle twist limits remain valid");
    const auto leftThighBefore=physics::bodyPartCollisionCapsule(hip.leftThigh);
    const auto rightThighBefore=physics::bodyPartCollisionCapsule(hip.rightThigh);
    const auto leftThighAfter=physics::bodyPartCollisionCapsule(compensated.leftThigh);
    const auto rightThighAfter=physics::bodyPartCollisionCapsule(compensated.rightThigh);
    check((leftThighBefore.pointA-leftThighAfter.pointA).lengthSquared()==0 &&
        (leftThighBefore.pointB-leftThighAfter.pointB).lengthSquared()==0 &&
        (rightThighBefore.pointA-rightThighAfter.pointA).lengthSquared()==0 &&
        (rightThighBefore.pointB-rightThighAfter.pointB).lengthSquared()==0,
        "both thigh capsule segments are exactly unchanged by knee compensation");
    auto finalParts=skeleton.collectComponent(compensated,compensated.torso,skeleton.head);finalParts.push_back(&compensated.head);
    for(size_t i=0;i<parts.size();++i) {
        check((finalParts[i]->body.velocity-parts[i]->body.velocity).lengthSquared()==0 &&
            (finalParts[i]->body.angularVelocity-parts[i]->body.angularVelocity).lengthSquared()==0,"all sixteen velocities are exactly unchanged");
        if(parts[i]!=&input.rightThigh && parts[i]!=&input.rightShin && parts[i]!=&input.rightFoot) {
            const auto& a=parts[i]->body;const auto& b=finalParts[i]->body;
            check((a.position-b.position).lengthSquared()==0 && a.orientation.w==b.orientation.w &&
                a.orientation.x==b.orientation.x && a.orientation.y==b.orientation.y && a.orientation.z==b.orientation.z,
                "all other thirteen poses and their internal anchors are unchanged");
        }
        check(body::floorMoveIsNoWorse(lowest(*parts[i]),lowest(*finalParts[i])),"no body acquires or worsens floor penetration");
    }
    auto off=input,on=input;
    body::ThighSwingCollisionAudit offAudit,onAudit;
    const auto rejected=body::correctThighOverlapWithSwing(off,skeleton,0.01f,0.001f,&offAudit);
    const auto accepted=body::correctThighOverlapWithSwing(on,skeleton,0.01f,0.001f,&onAudit,false,true);
    check(!rejected.corrected && offAudit.footOnlyCandidates>0 && offAudit.footOnlyRemaining==offAudit.footOnlyCandidates,
        "default-off solver retains the saved foot-only block");
    check(accepted.corrected && accepted.leftRequest==0 && accepted.rightRequest==hipRequest &&
        accepted.leftKneeRequest==0 && accepted.rightKneeRequest==static_cast<float>(direction*high),
        "opt-in production search matches the copied hip plus smallest knee correction");
    check(accepted.penetrationAfter==after[0] && onAudit.kneeRecoveredCandidates>0,
        "production knee compensation preserves the copied collision improvement");
    const auto production=measurements(on,skeleton);
    check(production==after,"production and isolated replay endpoints agree exactly");
    auto productionParts=skeleton.collectComponent(on,on.torso,skeleton.head);productionParts.push_back(&on.head);
    for(size_t i=0;i<parts.size();++i)
        check((productionParts[i]->body.velocity-parts[i]->body.velocity).lengthSquared()==0 &&
            (productionParts[i]->body.angularVelocity-parts[i]->body.angularVelocity).lengthSquared()==0,
            "production coupled projection changes no body velocity");
    auto plain=input;
    const auto plainStep=body::correctThighOverlapWithSwing(plain,skeleton,0.01f,0.001f,nullptr,false,true);
    check(plainStep.leftRequest==accepted.leftRequest && plainStep.rightRequest==accepted.rightRequest &&
        plainStep.rightKneeRequest==accepted.rightKneeRequest && measurements(plain,skeleton)==production,
        "audit does not affect compensation selection");
    std::cout<<"replayPassed="<<passed<<'\n';return passed?0:1;
}
