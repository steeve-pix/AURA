#pragma once
// Isolated pose-only experiment. Never called by the application/live solver.
#include "AngularCOMProjectionReplay.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/physics/RoomCollision.hpp"

namespace angular_recenter_replay {
using namespace aura;
inline math::Vec3 recenter(body::AuraBody3D& pose,const std::array<double,3>& before) {
    const auto after=joint_com_replay::center(pose);
    const math::Vec3 delta{float(before[0]-after[0]),0,float(before[2]-after[2])};
    body::translateSubtree(joint_com_replay::parts(pose),delta);
    return delta;
}
inline float gap(body::AuraBody3D& pose,const angular_com_replay::Edge& e) {
    const auto p=joint_com_replay::parts(pose);
    return (body::localToWorldPoint(*p[e.b],e.joint->localAnchorB)-body::localToWorldPoint(*p[e.a],e.joint->localAnchorA)).length();
}
struct Preservation {
    double maxGap=0,maxRelativeRotation=0,maxSphereDistance=0,maxCapsuleDistance=0,maxFloorDelta=0;
    bool velocities=true,orientations=true,allYExact=true;
};
inline Preservation compare(body::AuraBody3D& before,body::AuraBody3D& after,const body::AuraSkeleton3D& s) {
    Preservation result;const auto a=joint_com_replay::parts(before),b=joint_com_replay::parts(after);
    for(const auto& e:angular_com_replay::edges(s)) {
        result.maxGap=std::max(result.maxGap,std::abs(double(gap(after,e)-gap(before,e))));
        const auto relative=[&](auto p){return p[e.a]->body.orientation.normalized().conjugate()*p[e.b]->body.orientation.normalized();};
        result.maxRelativeRotation=std::max(result.maxRelativeRotation,angular_com_replay::rotationDistance(relative(a),relative(b)));
    }
    const auto same=[](auto x,auto y){return x.x==y.x&&x.y==y.y&&x.z==y.z;};
    for(size_t i=0;i<a.size();++i) {
        const auto& x=a[i]->body;const auto& y=b[i]->body;
        result.velocities &= same(x.velocity,y.velocity)&&same(x.angularVelocity,y.angularVelocity);
        result.orientations &= x.orientation.w==y.orientation.w&&x.orientation.x==y.orientation.x&&x.orientation.y==y.orientation.y&&x.orientation.z==y.orientation.z;
        result.allYExact &= x.position.y==y.position.y;
        result.maxFloorDelta=std::max(result.maxFloorDelta,std::abs(double(physics::lowestPoint(x,a[i]->size).y-physics::lowestPoint(y,b[i]->size).y)));
        for(size_t j=i+1;j<a.size();++j) {
            if(!physics::shouldSelfCollide(*a[i],*a[j],before,s))continue;
            result.maxSphereDistance=std::max(result.maxSphereDistance,std::abs(double((a[j]->body.position-a[i]->body.position).length()-(b[j]->body.position-b[i]->body.position).length())));
            const auto distance=[](auto* x,auto* y) {
                const auto ca=physics::bodyPartCollisionCapsule(*x),cb=physics::bodyPartCollisionCapsule(*y);
                const auto closest=physics::closestPointsBetweenSegments(ca.pointA,ca.pointB,cb.pointA,cb.pointB);
                return (closest.pointB-closest.pointA).length();
            };
            result.maxCapsuleDistance=std::max(result.maxCapsuleDistance,std::abs(double(distance(a[i],a[j])-distance(b[i],b[j]))));
        }
    }
    return result;
}
inline float roomError(body::AuraBody3D& b) {
    float error=0;for(const auto* p:joint_com_replay::parts(b))error=std::max(error,physics::roomPenetration(p->body,p->size,{}));return error;
}
inline void angularOperation(body::AuraBody3D& b,const body::AuraSkeleton3D& s,const angular_com_replay::Edge& e) {
    const auto p=joint_com_replay::parts(b);const auto& j=*e.joint;
    if(e.joint==&s.neck)body::correctNeckAngleAroundPivot(b,s,j);
    else if(e.joint==&s.head)body::correctHeadAngleAroundPivot(b,s,j);
    else if(e.joint==&s.leftShoulder)body::correctLeftShoulderAngleAroundPivot(b,s,j);
    else if(e.joint==&s.rightShoulder)body::correctBranchAngleAroundPivot(b,s,j,body::MajorBodyBranch3D::RightShoulder);
    else if(e.joint==&s.leftHip)body::correctBranchAngleAroundPivot(b,s,j,body::MajorBodyBranch3D::LeftHip,true);
    else if(e.joint==&s.rightHip)body::correctBranchAngleAroundPivot(b,s,j,body::MajorBodyBranch3D::RightHip,true);
    else if(e.joint==&s.rightElbow)body::correctRightElbowAngleAroundPivot(b,s,j,s.rightWrist);
    else if(e.joint==&s.rightKnee)body::correctRightKneeAngleAroundPivot(b,s,j,s.rightAnkle);
    else if(e.joint==&s.leftElbow)body::correctBranchAngleAroundPivot(b,s,j,body::MajorBodyBranch3D::LeftElbow);
    else if(e.joint==&s.leftKnee)body::correctBranchAngleAroundPivot(b,s,j,body::MajorBodyBranch3D::LeftKnee);
    else body::correctJointAngle(*p[e.a],*p[e.b],j);
}
inline bool report(body::AuraBody3D input,body::AuraBody3D captured,const body::AuraSkeleton3D& skeleton,const angular_com_replay::Edge& cut) {
    auto angular=input;angularOperation(angular,skeleton,cut);
    const auto replayCheck=compare(captured,angular,skeleton);
    double replayPositionError=0;const auto capturedParts=joint_com_replay::parts(captured),angularParts=joint_com_replay::parts(angular);
    for(size_t i=0;i<capturedParts.size();++i)replayPositionError=std::max(replayPositionError,double((capturedParts[i]->body.position-angularParts[i]->body.position).length()));
    const auto initialCOM=joint_com_replay::center(input);
    auto centered=angular;const auto translation=recenter(centered,initialCOM);
    const auto preserved=compare(angular,centered,skeleton);
    auto productionFull=angular;
    const auto fullAudit=body::recenterHorizontallyWithinRoom(productionFull,initialCOM);
    double fullReferenceError=0;
    const auto productionFullParts=joint_com_replay::parts(productionFull),centeredParts=joint_com_replay::parts(centered);
    for(size_t i=0;i<centeredParts.size();++i)fullReferenceError=std::max(fullReferenceError,double((productionFullParts[i]->body.position-centeredParts[i]->body.position).length()));
    double maxOtherGapChange=0;
    for(const auto& e:angular_com_replay::edges(skeleton))if(e.joint!=cut.joint)
        maxOtherGapChange=std::max(maxOtherGapChange,std::abs(double(gap(angular,e)-gap(centered,e))));
    std::cout<<std::scientific<<std::setprecision(12)<<"angularRecenterTranslation,"<<translation.x<<','<<translation.y<<','<<translation.z<<'\n';
    const auto print=[&](const char* label,body::AuraBody3D& pose) {
        const auto com=joint_com_replay::center(pose);const auto p=joint_com_replay::parts(pose);
        float low=std::numeric_limits<float>::infinity();for(auto* part:p)low=std::min(low,physics::lowestPoint(part->body,part->size).y);
        std::cout<<"angularRecenterCandidate,"<<label<<",twist="<<body::relativeJointTwistAngle(*p[cut.a],*p[cut.b],*cut.joint)
            <<",deltaCOM="<<com[0]-initialCOM[0]<<','<<com[1]-initialCOM[1]<<','<<com[2]-initialCOM[2]
            <<",horizontal="<<std::hypot(com[0]-initialCOM[0],com[2]-initialCOM[2])<<",hipGap="<<gap(pose,cut)
            <<",minimumY="<<low<<",roomPenetration="<<roomError(pose)<<",thighPenetration="<<joint_com_replay::thighPenetration(pose)<<'\n';
    };
    print("angularOnly",angular);print("angularPlusRecenter",centered);
    std::cout<<"angularRecenterPreservation,maxOtherGapChange="<<maxOtherGapChange<<",maxRelativeRotation="<<preserved.maxRelativeRotation
        <<",maxSphereDistanceChange="<<preserved.maxSphereDistance<<",maxCapsuleDistanceChange="<<preserved.maxCapsuleDistance
        <<",allYExact="<<preserved.allYExact<<",maxFloorDelta="<<preserved.maxFloorDelta<<",velocitiesExact="<<preserved.velocities
        <<",orientationsExact="<<preserved.orientations<<",liveReplayPositionError="<<replayPositionError<<'\n';
    auto raw=input,recentered=input;double maxResidual=0,maxStepGapDrift=0,maxStepCapsuleDrift=0,maxRelativeDrift=0;
    bool stepInvariant=true;size_t operations=0,changed=0;const auto edges=angular_com_replay::edges(skeleton);
    // Frozen pose-only sequence: 16 forward/reverse angular sweeps, without
    // position/velocity constraints, integration, motors, or contact impulses.
    for(int sweep=0;sweep<16;++sweep) {
        for(int reverse=0;reverse<2;++reverse)for(size_t k=0;k<edges.size();++k) {
            const auto& e=edges[reverse?edges.size()-1-k:k];
            angularOperation(raw,skeleton,e);
            const auto before=joint_com_replay::center(recentered);
            angularOperation(recentered,skeleton,e);auto projected=recentered;
            const auto d=recenter(recentered,before);if(d.lengthSquared()!=0)++changed;
            const auto check=compare(projected,recentered,skeleton);
            stepInvariant &= check.velocities&&check.orientations&&check.allYExact&&check.maxFloorDelta==0;
            maxStepGapDrift=std::max(maxStepGapDrift,check.maxGap);maxStepCapsuleDrift=std::max(maxStepCapsuleDrift,check.maxCapsuleDistance);
            const auto after=joint_com_replay::center(recentered);
            if(d.lengthSquared()!=0)std::cout<<"angularRecenterStep,sweep="<<sweep+1<<",direction="<<(reverse?"backward":"forward")
                <<",joint="<<e.name<<",translation="<<d.x<<','<<d.y<<','<<d.z
                <<",horizontalCOMResidual="<<std::hypot(after[0]-before[0],after[2]-before[2])<<'\n';
            maxResidual=std::max(maxResidual,std::hypot(after[0]-before[0],after[2]-before[2]));
            ++operations;
        }
        const auto rawCOM=joint_com_replay::center(raw),centeredCOM=joint_com_replay::center(recentered);
        const auto geometry=compare(raw,recentered,skeleton);maxRelativeDrift=std::max(maxRelativeDrift,geometry.maxRelativeRotation);
        std::cout<<"angularRecenterSequence,sweep="<<sweep+1<<",rawHorizontal="<<std::hypot(rawCOM[0]-initialCOM[0],rawCOM[2]-initialCOM[2])
            <<",recenteredHorizontal="<<std::hypot(centeredCOM[0]-initialCOM[0],centeredCOM[2]-initialCOM[2])
            <<",rawDeltaY="<<rawCOM[1]-initialCOM[1]<<",recenteredDeltaY="<<centeredCOM[1]-initialCOM[1]
            <<",relativeRotationDifference="<<geometry.maxRelativeRotation<<",gapDifference="<<geometry.maxGap
            <<",roomPenetration="<<roomError(recentered)<<'\n';
    }
    // Deliberately place the corrected pose 0.1 m from the -X wall. Its
    // requested -0.409 m recenter cannot be applied in full.
    auto wall=angular;
    float minimumX=std::numeric_limits<float>::infinity();
    for(auto* p:joint_com_replay::parts(wall))for(const auto& corner:physics::cubeCorners(p->body,p->size))minimumX=std::min(minimumX,corner.x);
    const math::Vec3 wallSetup{-59.9f-minimumX,0,0};
    body::translateSubtree(joint_com_replay::parts(wall),wallSetup);
    auto wallBefore=wall;
    const auto wallStart=body::wholeBodyCenterOfMass(wall);
    auto wallTarget=wallStart;wallTarget[0]+=translation.x;wallTarget[2]+=translation.z;
    auto full=wall;body::translateSubtree(joint_com_replay::parts(full),translation);
    const auto feasible=body::recenterHorizontallyWithinRoom(wall,wallTarget);
    auto beyondSafe=wallBefore;
    body::translateSubtree(joint_com_replay::parts(beyondSafe),translation*std::min(1.0f,feasible.alpha+1e-4f));
    const auto wallEnd=body::wholeBodyCenterOfMass(wall);
    const auto wallPreserved=compare(wallBefore,wall,skeleton);
    const double requested=std::hypot(translation.x,translation.z);
    const double residual=std::hypot(wallEnd[0]-wallTarget[0],wallEnd[2]-wallTarget[2]);
    std::cout<<"wallRecenterReplay,fullRoomPenetration="<<roomError(full)<<",alpha="<<feasible.alpha
        <<",applied="<<feasible.applied.x<<','<<feasible.applied.y<<','<<feasible.applied.z
        <<",roomPenetration="<<roomError(wall)<<",largerFractionPenetration="<<roomError(beyondSafe)<<",fullSafeReferenceError="<<fullReferenceError<<",requestedHorizontal="<<requested<<",residualHorizontal="<<residual
        <<",maxGapChange="<<wallPreserved.maxGap<<",maxCapsuleDistanceChange="<<wallPreserved.maxCapsuleDistance
        <<",velocitiesExact="<<wallPreserved.velocities<<",orientationsExact="<<wallPreserved.orientations<<",allYExact="<<wallPreserved.allYExact<<'\n';
    const bool wallPassed=fullAudit.alpha==1&&fullReferenceError<1e-6&&roomError(beyondSafe)>0&&feasible.initiallyWallValid&&feasible.alpha>0&&feasible.alpha<1&&roomError(wall)==0&&roomError(full)>0&&residual<requested&&
        wallPreserved.velocities&&wallPreserved.orientations&&wallPreserved.allYExact&&wallPreserved.maxGap<2e-5&&wallPreserved.maxCapsuleDistance<2e-5;
    const auto end=joint_com_replay::center(centered),endSequence=joint_com_replay::center(recentered);
    const bool passed=wallPassed&&replayPositionError<1e-6&&replayCheck.maxRelativeRotation<1e-6&&
        std::hypot(end[0]-initialCOM[0],end[2]-initialCOM[2])<1e-6&&preserved.maxGap<1e-6&&
        preserved.maxCapsuleDistance<1e-6&&preserved.velocities&&preserved.orientations&&preserved.allYExact&&preserved.maxFloorDelta==0&&
        stepInvariant&&maxStepGapDrift<2e-6&&maxStepCapsuleDrift<2e-6&&maxRelativeDrift<2e-6&&
        std::hypot(endSequence[0]-initialCOM[0],endSequence[2]-initialCOM[2])<1e-5;
    std::cout<<"angularRecenterResult,operations="<<operations<<",nonzeroRecenters="<<changed<<",maxPerOperationCOMResidual="<<maxResidual
        <<",maxStepGapChange="<<maxStepGapDrift<<",maxStepCapsuleDistanceChange="<<maxStepCapsuleDrift<<",stepInvariants="<<stepInvariant
        <<",passed="<<passed<<'\n';
    return passed;
}
}
