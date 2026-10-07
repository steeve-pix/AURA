#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include "aura/body/JointGeometry.hpp"

int main() {
    using namespace aura;
    bool passed=true;
    const auto check=[&](bool condition,const char* message) {
        if (!condition) {std::cerr << "FAIL " << message << '\n';passed=false;}
    };
    const auto sameRotation=[](math::Quaternion a,math::Quaternion b) {
        a=a.normalized();b=b.normalized();
        const float sign=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z>=0?1:-1;
        return std::pow(a.w-sign*b.w,2)+std::pow(a.x-sign*b.x,2)+
               std::pow(a.y-sign*b.y,2)+std::pow(a.z-sign*b.z,2)<1e-11;
    };
    body::BodyPart3D parent,child;
    body::Joint3D joint;
    joint.type=body::JointType::SwingTwist;
    parent.body.orientation=math::Quaternion::fromAxisAngle({1,2,3},0.6f);
    for(float twistAngle: {-2.0f,-0.8f,0.0f,0.8f,2.0f}) {
        const auto swing=math::Quaternion::fromAxisAngle({0,1,2},0.3f);
        const auto twist=math::Quaternion::fromAxisAngle({1,0,0},twistAngle);
        child.body.orientation=parent.body.orientation*swing*twist;
        const auto decomposition=body::relativeJointSwingTwist(parent,child,joint);
        check(decomposition.twistDefined,"ordinary swing/twist is well-defined");
        check(sameRotation(decomposition.swing*decomposition.twist,swing*twist),"swing times twist reconstructs relative orientation");
        check(sameRotation(decomposition.swing,swing),"decomposition recovers swing independently of twist");
        check(std::abs(body::relativeJointTwistAngle(parent,child,joint)-twistAngle)<1e-6f,"signed twist angle is recovered");
        const auto oldVector=body::jointSwingRotationVector(decomposition.swing);
        auto& q=child.body.orientation;q={-q.w*2,-q.x*2,-q.y*2,-q.z*2};
        check(std::abs(body::relativeJointTwistAngle(parent,child,joint)-twistAngle)<1e-6f,"quaternion sign and scale do not change twist");
        check((body::jointSwingRotationVector(body::relativeJointSwing(parent,child,joint))-oldVector).length()<1e-6f,
              "quaternion sign and scale do not change swing coordinates");
    }
    parent.body.orientation={};
    child.body.orientation=math::Quaternion::fromAxisAngle({0,0,1},0.3f)*math::Quaternion::fromAxisAngle({1,0,0},0.8f);
    child.body.position={0,-1,0};child.body.velocity={1,2,3};child.body.angularVelocity={4,5,6};
    joint.localAnchorA={};joint.localAnchorB=child.body.orientation.conjugate().rotate(-child.body.position);
    std::array component{&child};
    const auto oldPosition=child.body.position;const auto oldOrientation=child.body.orientation;
    check(!body::applyJointSwingWithComponent(parent,child,joint,component,{0,0,1},0.2f),"Z swing outside explicit bounds is rejected");
    check(!body::applyJointSwingWithComponent(parent,child,joint,component,{0,0,1},-1.0f),"negative Z swing below the lower bound is rejected");
    check((child.body.position-oldPosition).lengthSquared()==0 && sameRotation(child.body.orientation,oldOrientation),"rejected probe leaves pose unchanged");
    check(body::applyJointSwingWithComponent(parent,child,joint,component,{0,0,1},-0.1f),"in-range Z swing is accepted");
    check(std::abs(body::relativeJointSwingZ(parent,child,joint)-0.2f)<1e-6f,"swing coordinate measures absolute rotation, not probe delta");
    check(std::abs(body::relativeJointTwistAngle(parent,child,joint)-0.8f)<1e-6f,"swing update retains original twist");
    check((body::localToWorldPoint(child,joint.localAnchorB)-parent.body.position).length()<1e-6f,"swing projection retains world pivot");
    check((child.body.velocity-math::Vec3{1,2,3}).lengthSquared()==0 &&
          (child.body.angularVelocity-math::Vec3{4,5,6}).lengthSquared()==0,"swing projection changes pose only");
    joint.type=body::JointType::Hinge;
    check(!body::applyJointSwingWithComponent(parent,child,joint,component,{0,0,1},0.01f),"hinge mode does not opt into swing probes");
    joint.type=body::JointType::SwingTwist;
    child.body.orientation=math::Quaternion::fromAxisAngle({0,1,0},std::numbers::pi_v<float>);
    const auto singular=body::relativeJointSwingTwist(parent,child,joint);
    check(!singular.twistDefined && body::relativeJointTwistAngle(parent,child,joint)==0,"perpendicular 180-degree swing has explicit undefined twist");
    check(sameRotation(singular.swing*singular.twist,child.body.orientation),"singular fallback retains orientation reconstruction");
    check(!body::applyJointSwingWithComponent(parent,child,joint,component,{0,0,1},0.01f),"undefined twist cannot be silently preserved by a swing probe");
    joint.hingeAxis={};
    bool invalidAxis=false;
    try {body::relativeJointSwingTwist(parent,child,joint);} catch(const std::invalid_argument&) {invalidAxis=true;}
    check(invalidAxis,"zero twist axis is rejected");
    return passed?0:1;
}
