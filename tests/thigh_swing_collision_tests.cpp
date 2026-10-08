#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include "aura/body/ThighSelfCollision.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/SelfCollision.hpp"

int main() {
    using namespace aura;
    bool passed=true;
    const auto check=[&](bool condition,const char* message) {
        if(!condition) {std::cerr << "FAIL " << message << '\n';passed=false;}
    };
    auto input=body::createAuraBody3D();
    auto skeleton=body::createAuraSkeleton3D(input);
    auto parts=skeleton.collectComponent(input,input.torso,skeleton.head);parts.push_back(&input.head);
    for(auto* part:parts) {part->body.position.y+=10;part->body.velocity={1,2,3};part->body.angularVelocity={4,5,6};}
    for(bool left:{true,false}) {
        auto& thigh=left?input.leftThigh:input.rightThigh;
        const auto& hip=left?skeleton.leftHip:skeleton.rightHip;
        const auto target=input.pelvis.body.orientation*math::Quaternion::fromAxisAngle({0,0,1},left?0.25f:-0.25f)*
                          math::Quaternion::fromAxisAngle({1,0,0},0.5f);
        body::rotateSubtreeAroundWorldPoint(skeleton.collectComponent(input,thigh,hip),
            body::localToWorldPoint(input.pelvis,hip.localAnchorA),target*thigh.body.orientation.conjugate());
    }
    const auto original=input;
    body::ThighSwingCollisionAudit audit;
    const auto step=body::correctThighOverlapWithSwing(input,skeleton,0.01f,0.001f,&audit);
    check(step.penetrationBefore>0.001f && step.corrected && step.penetrationAfter<step.penetrationBefore,
          "legal overlapping thighs separate through bounded swing requests");
    check(std::abs(step.leftRequest)<=0.01f && std::abs(step.rightRequest)<=0.01f,"per-hip requests respect the iteration cap");
    auto after=skeleton.collectComponent(input,input.torso,skeleton.head);after.push_back(&input.head);
    auto beforeCopy=original;
    auto before=skeleton.collectComponent(beforeCopy,beforeCopy.torso,skeleton.head);before.push_back(&beforeCopy.head);
    for(size_t i=0;i<after.size();++i) {
        check((after[i]->body.velocity-before[i]->body.velocity).lengthSquared()==0 &&
              (after[i]->body.angularVelocity-before[i]->body.angularVelocity).lengthSquared()==0,"all sixteen velocities are unchanged");
        check(body::floorMoveIsNoWorse(physics::lowestPoint(before[i]->body,before[i]->size).y,
                                      physics::lowestPoint(after[i]->body,after[i]->size).y),"no physical box worsens floor penetration");
    }
    for(bool left:{true,false}) {
        auto& thigh=left?input.leftThigh:input.rightThigh;
        auto& shin=left?input.leftShin:input.rightShin;
        auto& foot=left?input.leftFoot:input.rightFoot;
        const auto& hip=left?skeleton.leftHip:skeleton.rightHip;
        const auto swing=body::relativeJointSwingAngles(input.pelvis,thigh,hip);
        check(swing.y>=hip.minSwingY-1e-6f && swing.y<=hip.maxSwingY+1e-6f &&
              swing.z>=hip.minSwingZ-1e-6f && swing.z<=hip.maxSwingZ+1e-6f,"accepted swing remains legal");
        check(std::abs(body::relativeJointTwistAngle(input.pelvis,thigh,hip)-0.5f)<1e-6f,"collision separation retains X twist");
        const std::array<const body::BodyPart3D*,3> a{&input.pelvis,&thigh,&shin},b{&thigh,&shin,&foot};
        const std::array<const body::Joint3D*,3> j{&hip,left?&skeleton.leftKnee:&skeleton.rightKnee,left?&skeleton.leftAnkle:&skeleton.rightAnkle};
        for(size_t i=0;i<3;++i) check((body::localToWorldPoint(*a[i],j[i]->localAnchorA)-
            body::localToWorldPoint(*b[i],j[i]->localAnchorB)).length()<1e-5f,"hip/knee/ankle anchors remain attached");
    }
    auto withoutAudit=original;
    const auto plain=body::correctThighOverlapWithSwing(withoutAudit,skeleton);
    check(plain.leftRequest==step.leftRequest && plain.rightRequest==step.rightRequest && plain.penetrationAfter==step.penetrationAfter,
          "audit does not affect the selected correction");
    auto detailedPose=original;
    body::ThighSwingCollisionAudit details;
    const auto detailed=body::correctThighOverlapWithSwing(detailedPose,skeleton,0.01f,0.001f,&details,true);
    check(details.trialCount==24 && detailed.leftRequest==step.leftRequest && detailed.rightRequest==step.rightRequest &&
          detailed.penetrationAfter==step.penetrationAfter,"hypothetical rejected-candidate diagnostics do not change selection");
    const auto upperY=body::previewJointSwing(original.pelvis,original.leftThigh,skeleton.leftHip,{0,1,0},1.0f);
    check(upperY.twistDefined && (upperY.blockedBounds&body::SwingYMax),"preview identifies the exhausted Y upper bound");
    auto blocked=original;
    auto fixed=skeleton;
    for(bool left:{true,false}) {
        auto& hip=left?fixed.leftHip:fixed.rightHip;
        const auto swing=body::relativeJointSwingAngles(blocked.pelvis,left?blocked.leftThigh:blocked.rightThigh,hip);
        hip.minSwingY=swing.y-1e-6f;hip.maxSwingY=swing.y+1e-6f;
        hip.minSwingZ=swing.z-1e-6f;hip.maxSwingZ=swing.z+1e-6f;
    }
    const auto rejected=body::correctThighOverlapWithSwing(blocked,fixed,0.01f,0.001f,&audit);
    check(!rejected.corrected && audit.blockedBySwingLimit>0,"saturated swing bounds reject separation requests");
    check((blocked.leftThigh.body.position-original.leftThigh.body.position).lengthSquared()==0 &&
          (blocked.rightThigh.body.position-original.rightThigh.body.position).lengthSquared()==0,"rejected search does not move bodies");
    auto blockedWithKnee=original;
    body::ThighSwingCollisionAudit saturatedAudit;
    const auto saturated=body::correctThighOverlapWithSwing(blockedWithKnee,fixed,0.01f,0.001f,&saturatedAudit,false,true);
    check(!saturated.corrected && saturatedAudit.kneeSearchCandidates==0,
          "knee compensation cannot bypass saturated hip swing bounds");
    auto floorPose=original;
    auto floorParts=skeleton.collectComponent(floorPose,floorPose.torso,skeleton.head);floorParts.push_back(&floorPose.head);
    float minY=1e30f;for(auto* part:floorParts) minY=std::min(minY,physics::lowestPoint(part->body,part->size).y);
    body::translateSubtree(floorParts,{0,-minY,0});
    const auto floorBefore=floorPose;
    body::correctThighOverlapWithSwing(floorPose,skeleton,0.01f,0.001f,&audit);
    check(audit.blockedByFloor>0,"contact pose exercises floor rejection");
    check(body::floorMoveIsNoWorse(physics::lowestPoint(floorBefore.leftFoot.body,floorBefore.leftFoot.size).y,
        physics::lowestPoint(floorPose.leftFoot.body,floorPose.leftFoot.size).y) &&
          body::floorMoveIsNoWorse(physics::lowestPoint(floorBefore.rightFoot.body,floorBefore.rightFoot.size).y,
        physics::lowestPoint(floorPose.rightFoot.body,floorPose.rightFoot.size).y),"search respects planted feet");
    auto coincident=original;
    coincident.leftThigh.body.position=coincident.rightThigh.body.position;
    coincident.leftThigh.body.orientation=coincident.rightThigh.body.orientation;
    check(!body::correctThighOverlapWithSwing(coincident,skeleton,0.01f,0.001f,&audit).corrected && audit.degenerate,
          "coincident capsule axes do not invent a separation normal");
    return passed?0:1;
}
