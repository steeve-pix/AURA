#include <cmath>
#include <iostream>
#include "aura/body/ComponentPosition.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"

int main() {
    using namespace aura;
    bool passed=true;
    const auto check=[&](bool ok,const char *message){if(!ok){passed=false;std::cerr<<"FAIL "<<message<<'\n';}};
    auto body=body::createAuraBody3D();
    const auto skeleton=body::createAuraSkeleton3D(body);
    const auto all=skeleton.collectComponent(body,body.torso,skeleton.rightWrist);
    for(auto *part:all) part->body.position.y+=10;
    body.rightHand.body.position.y=0.5f*body.rightHand.size.y;
    const auto handAnchor=body::localToWorldPoint(body.rightHand,skeleton.rightWrist.localAnchorB);
    body.rightForearm.body.position=handAnchor-skeleton.rightWrist.localAnchorA-math::Vec3{0,0.02f,0};
    body.rightForearm.body.velocity={1,2,3}; body.rightHand.body.angularVelocity={3,2,1};
    const auto oldHand=body.rightHand.body;
    const auto oldForearm=body.rightForearm.body;
    const auto oldElbowGap=body::localToWorldPoint(body.rightForearm,skeleton.rightElbow.localAnchorB)-
        body::localToWorldPoint(body.rightUpperArm,skeleton.rightElbow.localAnchorA);
    const auto result=body::correctJointPositionWithComponents(body,skeleton,body.rightForearm,body.rightHand,skeleton.rightWrist);
    check(result.choice==body::ComponentPositionChoice::Parent,"planted hand redirects correction to opposite component");
    check(result.gapAfter<1e-5f,"wrist repaired");
    check(physics::lowestPoint(body.rightHand.body,body.rightHand.size).y>=-1e-6f,"hand floor preserved");
    check((body.rightHand.body.position-oldHand.position).lengthSquared()==0,"hand remains fixed");
    check((body.rightForearm.body.velocity-oldForearm.velocity).lengthSquared()==0 &&
          (body.rightHand.body.angularVelocity-oldHand.angularVelocity).lengthSquared()==0,"velocities unchanged");
    const auto newElbowGap=body::localToWorldPoint(body.rightForearm,skeleton.rightElbow.localAnchorB)-
        body::localToWorldPoint(body.rightUpperArm,skeleton.rightElbow.localAnchorA);
    check((newElbowGap-oldElbowGap).length()<2e-6f,"opposite component preserves internal elbow relationship");
    // Hand may lift: floor is unilateral.
    body.rightForearm.body.position.y+=0.03f;
    const auto lift=body::correctJointPositionWithComponents(body,skeleton,body.rightForearm,body.rightHand,skeleton.rightWrist);
    check(lift.choice==body::ComponentPositionChoice::Child,"upward child correction allowed");
    check(physics::lowestPoint(body.rightHand.body,body.rightHand.size).y>0.02f,"contact releases upward");
    // Rotation-independent floor clearance uses world corners.
    body.rightHand.body.orientation=math::Quaternion::fromAxisAngle({1,0,0},0.5f);
    body.rightHand.body.position.y-=physics::lowestPoint(body.rightHand.body,body.rightHand.size).y;
    const auto anchor=body::localToWorldPoint(body.rightHand,skeleton.rightWrist.localAnchorB);
    body.rightForearm.body.position=anchor-skeleton.rightWrist.localAnchorA-math::Vec3{0,0.02f,0};
    const auto tilted=body::correctJointPositionWithComponents(body,skeleton,body.rightForearm,body.rightHand,skeleton.rightWrist);
    check(tilted.choice==body::ComponentPositionChoice::Parent && tilted.gapAfter<1e-5f,"tilted support uses actual lowest corner");
    return passed?0:1;
}
