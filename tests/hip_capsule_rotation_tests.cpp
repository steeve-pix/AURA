#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include "HipCapsuleRotationReplay.hpp"
#include "aura/body/ThighSelfCollision.hpp"

int main(int argc,char **argv) {
    using namespace aura;
    if (argc!=2) {std::cerr << "Missing replay fixture\n";return 2;}
    auto input=body::createAuraBody3D();
    // Anchors must be configured in the assembled pose before loading state.
    const auto skeleton=body::createAuraSkeleton3D(input);
    auto parts=skeleton.collectComponent(input,input.torso,skeleton.head);parts.push_back(&input.head);
    std::ifstream file(argv[1]);std::string line;std::set<std::string> loaded;
    while (std::getline(file,line)) {
        if (!line.starts_with("hipRotationInput,")) continue;
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line);std::string tag,name;row>>tag>>name;
        const auto found=std::find_if(parts.begin(),parts.end(),[&](const auto *part){return part->name==name;});
        if (found==parts.end() || !loaded.insert(name).second) {std::cerr << "Invalid or duplicate part\n";return 2;}
        auto &part=**found;auto &b=part.body;
        row>>part.size.x>>part.size.y>>part.size.z>>b.mass>>b.position.x>>b.position.y>>b.position.z
           >>b.orientation.w>>b.orientation.x>>b.orientation.y>>b.orientation.z
           >>b.velocity.x>>b.velocity.y>>b.velocity.z>>b.angularVelocity.x>>b.angularVelocity.y>>b.angularVelocity.z
           >>b.momentOfInertia.x>>b.momentOfInertia.y>>b.momentOfInertia.z;
        if (!row) {std::cerr << "Incomplete fixture\n";return 2;}
    }
    if (loaded.size()!=16) {std::cerr << "Incomplete body fixture\n";return 2;}
    bool passed=true;
    const auto check=[&](bool condition,const char *message) {
        if (!condition) {std::cerr << "FAIL " << message << '\n';passed=false;}
    };
    for (bool aligned:{false,true}) {
        auto reference=input;
        if (aligned) for (bool left:{true,false}) {
            const auto &joint=left?skeleton.leftKnee:skeleton.rightKnee;
            auto &thigh=left?reference.leftThigh:reference.rightThigh;
            auto &shin=left?reference.leftShin:reference.rightShin;
            const auto child=skeleton.collectComponent(reference,shin,joint);
            body::translateSubtree(child,body::localToWorldPoint(thigh,joint.localAnchorA)-
                                        body::localToWorldPoint(shin,joint.localAnchorB));
        }
        std::ostringstream trace;
        const auto result=test::runHipCapsuleRotationReplay(reference,skeleton,trace);
        check(std::abs(result.initial.penetration-0.5297266245f)<2e-6f,"reference thigh overlap matches attached fixture");
        check(result.separated && result.final.penetration==0,"hip-pivot rotations separate capsules");
        check(result.velocitiesUnchanged,"pose search does not change linear or angular velocities");
        check(result.final.minimumLegY>=-1e-6f && result.final.maximumLimitError<=1e-5f,"final legs respect floor and implemented twist limits");
        check(result.maximumDescendantAngleDrift<3e-6f,"knee and ankle relative angles preserved");
        for (std::size_t i=0;i<6;++i)
            check(std::abs(result.final.gaps[i]-result.initial.gaps[i])<3e-6f,"existing anchor relationships preserved");
        if (aligned) check(result.maximumGap<3e-6f,"fully attached reference stays attached throughout search");
        else check(result.initial.gaps[1]>0.007f && result.initial.gaps[4]>0.007f,"source pre-existing knee gaps recorded honestly");
        check((result.body.pelvis.body.position-reference.pelvis.body.position).lengthSquared()==0 &&
              result.body.pelvis.body.orientation.w==reference.pelvis.body.orientation.w &&
              result.body.pelvis.body.orientation.x==reference.pelvis.body.orientation.x &&
              result.body.pelvis.body.orientation.y==reference.pelvis.body.orientation.y &&
              result.body.pelvis.body.orientation.z==reference.pelvis.body.orientation.z,"pelvis and world hip pivots fixed");
        // Full relative quaternions, not only the measured hinge twist.
        const std::array<const body::BodyPart3D*,6> oldLeg{&reference.leftThigh,&reference.leftShin,&reference.leftFoot,
                                                        &reference.rightThigh,&reference.rightShin,&reference.rightFoot};
        const std::array<const body::BodyPart3D*,6> newLeg{&result.body.leftThigh,&result.body.leftShin,&result.body.leftFoot,
                                                        &result.body.rightThigh,&result.body.rightShin,&result.body.rightFoot};
        for (std::size_t i:{0u,1u,3u,4u}) {
            const auto a=(oldLeg[i]->body.orientation.normalized().conjugate()*oldLeg[i+1]->body.orientation.normalized()).normalized();
            const auto b=(newLeg[i]->body.orientation.normalized().conjugate()*newLeg[i+1]->body.orientation.normalized()).normalized();
            const double sign=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z>=0?1:-1;
            const double difference=std::pow(a.w-sign*b.w,2)+std::pow(a.x-sign*b.x,2)+
                                    std::pow(a.y-sign*b.y,2)+std::pow(a.z-sign*b.z,2);
            check(difference<9e-12,"complete descendant relative orientation preserved");
        }
        std::istringstream rows(trace.str());float previous=std::numeric_limits<float>::infinity();int count=0;
        while (std::getline(rows,line)) {
            if (!line.starts_with("hipRotation,") || line.starts_with("hipRotation,step,")) continue;
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            std::string tag;int step;float penetration;row>>tag>>step>>penetration;
            check(penetration<=previous,"accepted penetration decreases monotonically");previous=penetration;
            std::array<float,6> gaps{};for (auto &gap:gaps) row>>gap;
            float leftY,rightY,minY,limit,leftStep,rightStep;row>>leftY>>rightY>>minY>>limit>>leftStep>>rightStep;
            check(minY>=-1e-6f && limit<=1e-5f,"each accepted candidate respects floor and limits");
            ++count;
        }
        check(count==result.acceptedSteps+1,"all accepted search steps checked");
        std::cout << (aligned?"attached reference":"source copy") << ": steps=" << result.acceptedSteps
                  << " finalPenetration=" << result.final.penetration << " maxGap=" << result.maximumGap << '\n';
    }
    check(body::floorMoveIsNoWorse(-0.027495f,-0.027495f),"existing floor penetration may remain unchanged");
    check(body::floorMoveIsNoWorse(-0.027495f,-0.02f),"existing penetration may improve");
    check(!body::floorMoveIsNoWorse(-0.027495f,-0.035f),"existing penetration may not deepen");
    check(body::floorMoveIsNoWorse(0.1f,-1e-6f),"clear body permits only floor roundoff");
    check(!body::floorMoveIsNoWorse(0.1f,-2e-6f),"clear body cannot acquire penetration");
    check(!body::floorMoveIsNoWorse(-0.5e-6f,-1.25e-6f),"near-clear state uses absolute floor allowance");
    check(body::floorMoveIsNoWorse(-0.02f,-0.02f-0.5e-6f),"existing penetration permits roundoff");
    check(!body::floorMoveIsNoWorse(-0.02f,-0.02f-2e-6f),"existing penetration rejects worsening beyond roundoff");
    auto liveCopy=input;
    auto before=liveCopy;
    const auto step=body::correctThighCapsuleOverlap(liveCopy,skeleton);
    auto auditedCopy=before;
    body::ThighCollisionAudit audit;
    const auto auditedStep=body::correctThighCapsuleOverlap(auditedCopy,skeleton,0.01f,0.001f,&audit);
    check(audit.candidateCount==24 && audit.improvingCandidates>0,"all signed hip candidates audited");
    check(auditedStep.leftAngle==step.leftAngle && auditedStep.rightAngle==step.rightAngle &&
          auditedStep.penetrationAfter==step.penetrationAfter,"audit leaves chosen correction unchanged");
    auto auditedParts=skeleton.collectComponent(auditedCopy,auditedCopy.torso,skeleton.head);
    auditedParts.push_back(&auditedCopy.head);
    auto plainParts=skeleton.collectComponent(liveCopy,liveCopy.torso,skeleton.head);
    plainParts.push_back(&liveCopy.head);
    for (size_t i=0;i<plainParts.size();++i) {
        const auto& a=auditedParts[i]->body;const auto& b=plainParts[i]->body;
        check((a.position-b.position).lengthSquared()==0 && a.orientation.w==b.orientation.w &&
              a.orientation.x==b.orientation.x && a.orientation.y==b.orientation.y &&
              a.orientation.z==b.orientation.z,"audited endpoint poses exactly match");
    }
    int improving=0,feasibleImproving=0;std::uint32_t rejected=0;
    for (int i=0;i<audit.candidateCount;++i) {
        const auto& c=audit.candidates[i];
        check(c.improves==(c.penetration<step.penetrationBefore-1e-7f),"improvement classification uses input overlap");
        check(bool(c.rejections&body::LeftFootFloor)==(c.leftFootY<-1e-6f),"left foot floor classification");
        check(bool(c.rejections&body::RightFootFloor)==(c.rightFootY<-1e-6f),"right foot floor classification");
        check(bool(c.rejections&body::LeftJointLimit)==(c.leftLimitError>1e-5f),"left limit classification");
        check(bool(c.rejections&body::RightJointLimit)==(c.rightLimitError>1e-5f),"right limit classification");
        if (c.improves) {++improving;rejected|=c.rejections;feasibleImproving+=c.rejections==0;}
    }
    check(improving==audit.improvingCandidates && rejected==audit.improvingRejections &&
          feasibleImproving==audit.feasibleImprovingCandidates,"audit aggregation matches candidate rows");
    check(step.corrected && step.penetrationAfter<step.penetrationBefore,"live bounded step reduces overlap");
    check(std::abs(step.leftAngle)<=0.01f && std::abs(step.rightAngle)<=0.01f,"live angle budget");
    auto oldParts=skeleton.collectComponent(before,before.torso,skeleton.head);
    auto newParts=skeleton.collectComponent(liveCopy,liveCopy.torso,skeleton.head);
    oldParts.push_back(&before.head);newParts.push_back(&liveCopy.head);
    for (size_t i=0;i<oldParts.size();++i) {
        check((oldParts[i]->body.velocity-newParts[i]->body.velocity).lengthSquared()==0,"live linear velocities unchanged");
        check((oldParts[i]->body.angularVelocity-newParts[i]->body.angularVelocity).lengthSquared()==0,"live angular velocities unchanged");
    }
    const auto m=test::hipRotationMetrics(liveCopy,skeleton);
    check(m.minimumLegY>=-1e-6f && m.maximumLimitError<=1e-5f,"live floor and limits");
    const auto originalMetrics=test::hipRotationMetrics(before,skeleton);
    for (size_t i=0;i<6;++i) check(std::abs(m.gaps[i]-originalMetrics.gaps[i])<1e-5f,"live leg attachments preserved");
    auto neutral=body::createAuraBody3D();
    const auto neutralSkeleton=body::createAuraSkeleton3D(neutral);
    check(!body::correctThighCapsuleOverlap(neutral,neutralSkeleton).corrected,"separated neutral pose is untouched");
    auto blocked=input;
    auto blockedParts=skeleton.collectComponent(blocked,blocked.torso,skeleton.head);
    blockedParts.push_back(&blocked.head);
    for (auto* part:blockedParts) part->body.position.y-=100;
    const auto blockedPosition=blocked.leftThigh.body.position;
    body::ThighCollisionAudit blockedAudit;
    check(!body::correctThighCapsuleOverlap(blocked,skeleton,0.01f,0.001f,&blockedAudit,body::ThighFloorPolicy::StrictClearance).corrected,"floor-invalid candidates rejected");
    check((blockedAudit.initial.rejections&body::LeftFootFloor) &&
          (blockedAudit.initial.rejections&body::RightFootFloor),"pre-existing floor violations recorded separately");
    check((blockedAudit.improvingRejections&body::LeftFootFloor) &&
          (blockedAudit.improvingRejections&body::RightFootFloor),"blocked candidate floor reasons retained");
    check((blocked.leftThigh.body.position-blockedPosition).lengthSquared()==0,"blocked solve leaves pose unchanged");
    auto noWorseCopy=blocked;
    body::ThighCollisionAudit noWorseAudit;
    const auto noWorseStep=body::correctThighCapsuleOverlap(noWorseCopy,skeleton,0.01f,0.001f,&noWorseAudit);
    check(noWorseStep.corrected && noWorseStep.penetrationAfter<noWorseStep.penetrationBefore,"pre-existing floor violation no longer blocks all improving candidates");
    auto newBlockedParts=skeleton.collectComponent(noWorseCopy,noWorseCopy.torso,skeleton.head);newBlockedParts.push_back(&noWorseCopy.head);
    for (size_t i=0;i<blockedParts.size();++i) {
        check(body::floorMoveIsNoWorse(physics::lowestPoint(blockedParts[i]->body,blockedParts[i]->size).y,
                                      physics::lowestPoint(newBlockedParts[i]->body,newBlockedParts[i]->size).y),"selected projection never worsens any existing floor violation");
        check((blockedParts[i]->body.velocity-newBlockedParts[i]->body.velocity).lengthSquared()==0 &&
              (blockedParts[i]->body.angularVelocity-newBlockedParts[i]->body.angularVelocity).lengthSquared()==0,"no-worsening policy leaves velocities unchanged");
    }
    return passed?0:1;
}
