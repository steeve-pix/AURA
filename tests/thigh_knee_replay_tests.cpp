#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include "ThighKneeReplay.hpp"

int main(int argc,char** argv) {
    using namespace aura;
    if (argc!=2) return 2;
    auto input=body::createAuraBody3D();
    const auto skeleton=body::createAuraSkeleton3D(input);
    auto parts=skeleton.collectComponent(input,input.torso,skeleton.head);parts.push_back(&input.head);
    std::ifstream file(argv[1]);std::string line;std::set<std::string> loaded;
    bool left=false,choiceLoaded=false;float hipAngle=0;
    while (std::getline(file,line)) {
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line);std::string tag,name;row>>tag;
        if (tag=="kneeReplayChoice") {int side;row>>side>>hipAngle;left=side!=0;choiceLoaded=bool(row);continue;}
        if (tag!="kneeReplayInput") continue;
        row>>name;
        const auto it=std::find_if(parts.begin(),parts.end(),[&](const auto* p){return p->name==name;});
        if (it==parts.end() || !loaded.insert(name).second) return 2;
        auto& p=**it;auto& b=p.body;
        row>>p.size.x>>p.size.y>>p.size.z>>b.mass>>b.position.x>>b.position.y>>b.position.z
           >>b.orientation.w>>b.orientation.x>>b.orientation.y>>b.orientation.z
           >>b.velocity.x>>b.velocity.y>>b.velocity.z>>b.angularVelocity.x>>b.angularVelocity.y>>b.angularVelocity.z
           >>b.momentOfInertia.x>>b.momentOfInertia.y>>b.momentOfInertia.z;
        if (!row) return 2;
    }
    if (!choiceLoaded || loaded.size()!=16) return 2;
    bool passed=true;
    const auto check=[&](bool condition,const char* message){if(!condition){std::cerr<<"FAIL "<<message<<'\n';passed=false;}};
    const auto replay=test::replayThighWithKnee(input,skeleton,left,hipAngle);
    const int j=left?0:3,f=left?0:1;
    check(replay.hipMetrics.penetration<replay.initial.penetration,"hip separates thighs");
    check(replay.hipMetrics.maximumLimitError<=1e-5f,"hip-only trial respects all leg limits");
    check(!body::floorMoveIsNoWorse(replay.initial.feetY[f],replay.hipMetrics.feetY[f]),"hip-only trial is foot-floor blocked");
    check(replay.rescued,"small knee compensation rescues the frozen hip candidate");
    check(std::abs(replay.kneeAngle)<=0.0100001f,"knee stays within experiment angle budget");
    check(replay.coupledMetrics.penetration==replay.hipMetrics.penetration &&
          replay.coupledMetrics.angles[j]==replay.hipMetrics.angles[j],"knee changes neither thigh overlap nor hip angle");
    check(replay.coupledMetrics.maximumLimitError<=1e-5f,"coupled trial respects all leg limits");
    check(replay.velocitiesUnchanged && replay.gapsPreserved,"pose-only correction preserves motion and attachment");
    const std::array<const body::BodyPart3D*,6> oldLeg{&input.leftThigh,&input.leftShin,&input.leftFoot,&input.rightThigh,&input.rightShin,&input.rightFoot};
    const std::array<const body::BodyPart3D*,6> newLeg{&replay.coupled.leftThigh,&replay.coupled.leftShin,&replay.coupled.leftFoot,&replay.coupled.rightThigh,&replay.coupled.rightShin,&replay.coupled.rightFoot};
    for (size_t i=0;i<6;++i) {
        check(physics::lowestPoint(oldLeg[i]->body,oldLeg[i]->size).y>=-1e-6f,"saved input leg pose floor-valid");
        check(body::floorMoveIsNoWorse(physics::lowestPoint(oldLeg[i]->body,oldLeg[i]->size).y,
                                      physics::lowestPoint(newLeg[i]->body,newLeg[i]->size).y),"no leg body acquires or worsens floor penetration");
        check(std::abs(replay.coupledMetrics.gaps[i]-replay.initial.gaps[i])<=1e-5f,"all six leg anchors preserved");
    }
    const auto& beforeShin=left?input.leftShin:input.rightShin;
    const auto& beforeFoot=left?input.leftFoot:input.rightFoot;
    const auto& afterShin=left?replay.coupled.leftShin:replay.coupled.rightShin;
    const auto& afterFoot=left?replay.coupled.leftFoot:replay.coupled.rightFoot;
    const auto oldQ=(beforeShin.body.orientation.normalized().conjugate()*beforeFoot.body.orientation.normalized()).normalized();
    const auto newQ=(afterShin.body.orientation.normalized().conjugate()*afterFoot.body.orientation.normalized()).normalized();
    const float sign=oldQ.w*newQ.w+oldQ.x*newQ.x+oldQ.y*newQ.y+oldQ.z*newQ.z>=0?1:-1;
    check(std::pow(oldQ.w-sign*newQ.w,2)+std::pow(oldQ.x-sign*newQ.x,2)+std::pow(oldQ.y-sign*newQ.y,2)+std::pow(oldQ.z-sign*newQ.z,2)<9e-12,"complete ankle relative orientation preserved");
    auto live=input;
    body::ThighCollisionAudit liveAudit;
    const auto liveStep=body::correctThighCapsuleOverlap(live,skeleton,0.01f,0.001f,&liveAudit,body::ThighFloorPolicy::NoWorsening,true);
    check(liveStep.corrected && (liveStep.leftKneeAngle!=0 || liveStep.rightKneeAngle!=0),"production Class-A path rescues copied blocked pose");
    check(liveStep.penetrationAfter<liveStep.penetrationBefore,"production coupled step separates thighs");
    check(std::abs(liveStep.leftKneeAngle)<=0.01f && std::abs(liveStep.rightKneeAngle)<=0.01f,"production knee angle cap");
    auto liveParts=skeleton.collectComponent(live,live.torso,skeleton.head);liveParts.push_back(&live.head);
    for (size_t i=0;i<parts.size();++i) {
        check(body::floorMoveIsNoWorse(physics::lowestPoint(parts[i]->body,parts[i]->size).y,
                                      physics::lowestPoint(liveParts[i]->body,liveParts[i]->size).y),"production no-worsening floor feasibility");
        check((parts[i]->body.velocity-liveParts[i]->body.velocity).lengthSquared()==0 &&
              (parts[i]->body.angularVelocity-liveParts[i]->body.angularVelocity).lengthSquared()==0,"production response is pose-only");
    }
    const auto liveMetrics=test::hipRotationMetrics(live,skeleton);
    check(liveMetrics.maximumLimitError<=1e-5f,"production leg limits");
    for (size_t i=0;i<6;++i) check(std::abs(liveMetrics.gaps[i]-replay.initial.gaps[i])<=1e-5f,"production leg anchor preservation");
    int classBTrials=0;
    for (int i=0;i<liveAudit.candidateCount;++i) {
        const auto& trial=liveAudit.candidates[i];
        if (trial.jointLimitErrors[0]>1e-5f || trial.jointLimitErrors[3]>1e-5f) {
            ++classBTrials;check(!trial.kneeAttempted,"Class B never tries knee compensation");
        }
    }
    check(classBTrials>0,"fixture exercises Class-B rejection");
    auto withoutAudit=input;
    const auto withoutAuditStep=body::correctThighCapsuleOverlap(withoutAudit,skeleton,0.01f,0.001f,nullptr,body::ThighFloorPolicy::NoWorsening,true);
    check(withoutAuditStep.leftAngle==liveStep.leftAngle && withoutAuditStep.rightAngle==liveStep.rightAngle &&
          withoutAuditStep.leftKneeAngle==liveStep.leftKneeAngle && withoutAuditStep.rightKneeAngle==liveStep.rightKneeAngle,"app and diagnostic choose identical coupled steps");
    auto noAuditParts=skeleton.collectComponent(withoutAudit,withoutAudit.torso,skeleton.head);noAuditParts.push_back(&withoutAudit.head);
    for (size_t i=0;i<liveParts.size();++i) {
        const auto& a=liveParts[i]->body;const auto& b=noAuditParts[i]->body;
        check((a.position-b.position).lengthSquared()==0 && a.orientation.w==b.orientation.w &&
              a.orientation.x==b.orientation.x && a.orientation.y==b.orientation.y && a.orientation.z==b.orientation.z,"audit does not change coupled endpoint pose");
    }
    auto lessKnee=live;
    const bool adjustedLeft=liveStep.leftKneeAngle!=0;
    const auto& adjustedJoint=adjustedLeft?skeleton.leftKnee:skeleton.rightKnee;
    auto& adjustedThigh=adjustedLeft?lessKnee.leftThigh:lessKnee.rightThigh;
    auto& adjustedShin=adjustedLeft?lessKnee.leftShin:lessKnee.rightShin;
    const float selectedKnee=adjustedLeft?liveStep.leftKneeAngle:liveStep.rightKneeAngle;
    const auto kneeAxis=adjustedThigh.body.orientation.normalized().rotate(adjustedJoint.hingeAxis.normalized()).normalized();
    body::rotateSubtreeAroundWorldPoint(skeleton.collectComponent(lessKnee,adjustedShin,adjustedJoint),
        body::localToWorldPoint(adjustedThigh,adjustedJoint.localAnchorA),
        math::Quaternion::fromAxisAngle(kneeAxis,-std::copysign(1e-6f,selectedKnee)));
    const auto& lessFoot=adjustedLeft?lessKnee.leftFoot:lessKnee.rightFoot;
    const auto& inputFoot=adjustedLeft?input.leftFoot:input.rightFoot;
    check(!body::floorMoveIsNoWorse(physics::lowestPoint(inputFoot.body,inputFoot.size).y,
                                  physics::lowestPoint(lessFoot.body,lessFoot.size).y),"refined knee step lies at the feasible floor boundary");
    auto plain=input;
    const auto plainStep=body::correctThighCapsuleOverlap(plain,skeleton);
    check(!plainStep.corrected,"baseline hip-only copied pose remains blocked");
    std::cout << "production hip=" << liveStep.leftAngle << '/' << liveStep.rightAngle
              << " knees=" << liveStep.leftKneeAngle << '/' << liveStep.rightKneeAngle << '\n';
    std::cout << "hipRotation=" << hipAngle << " kneeRotation=" << replay.kneeAngle
              << " footY=" << replay.hipMetrics.feetY[f] << " -> " << replay.coupledMetrics.feetY[f] << '\n';
    return passed?0:1;
}
