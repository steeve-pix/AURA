#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include "aura/sim/AuraSimulation.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/Motion.hpp"

// Anatomy is checked from vectors, independently of the motor's angle scalar.
// +Z is forward, +Y up. All probes below are in the parent body's local frame.
int main() {
    using namespace aura;
    bool passed=true;
    sim::AuraSimulation simulation;
    const auto check=[&](bool condition,std::string_view name,const char* message) {
        if (!condition) { passed=false; std::cerr<<"FAIL "<<name<<": "<<message<<'\n'; }
    };
    for (const auto& spec:sim::actionSpecs) {
        const std::string_view name=spec.jointName;
        const auto it=std::find_if(simulation.getJoints().begin(),simulation.getJoints().end(),
                                  [&](const auto& entry){return std::string_view(entry.name)==name;});
        if (it==simulation.getJoints().end()) { check(false,name,"missing joint"); continue; }
        const bool elbow=name=="leftElbow" || name=="rightElbow";
        const bool knee=name=="leftKnee" || name=="rightKnee";
        const bool ankle=name=="leftAnkle" || name=="rightAnkle";
        const bool neck=name=="neck" || name=="head";
        const bool waist=name=="waist";
        check(it->constraint.minAngle==(elbow || knee?0.0f:static_cast<float>(spec.minimum)),name,"one-sided joints forbid hyperextension");
        check((it->constraint.hingeAxis-math::Vec3{elbow?-1.f:1.f,0,0}).length()<1e-6f,
              name,"hinge follows anatomical pitch axis");
        for (bool tilted:{false,true}) {
            const auto orientation=tilted?math::Quaternion::fromAxisAngle({1,2,3},0.8f):math::Quaternion{};
            for (int sign:{-1,1}) {
                if (sign<0 && spec.minimum==0) continue;
                auto parent=*it->partA,child=*it->partB;
                auto joint=it->constraint;
                parent.body.orientation=orientation;
                const float angle=0.1f*sign;
                child.body.orientation=orientation*math::Quaternion::fromAxisAngle(joint.hingeAxis,angle);
                child.body.position=body::localToWorldPoint(parent,joint.localAnchorA)-child.body.orientation.rotate(joint.localAnchorB);
                check(std::abs(body::relativeJointAngle(parent,child,joint)-angle)<1e-5f,name,"angle convention survives whole-body rotation");
                const auto relative=parent.body.orientation.conjugate()*child.body.orientation;
                if (ankle) check(sign*relative.rotate({0,0,1}).y<0,name,"positive ankle points toes down; negative points toes up");
                else if (neck) check(sign*relative.rotate({0,1,0}).z>0,name,"positive neck/head nod tilts upper segment forward");
                else if (waist) check(sign*relative.conjugate().rotate({0,1,0}).z<0,name,"positive waist extends torso relative to pelvis; negative flexes forward");
                else check(sign*relative.rotate({0,-1,0}).z*(elbow?1.f:-1.f)>0,name,
                           elbow?"positive elbow flexes forward":"positive hanging limb pitches backward; signed joints also pitch forward");
                const std::array<body::BodyPart3D*,1> component{&child};
                body::correctJointAngleWithComponent(parent,child,joint,component);
                check(std::abs(body::relativeJointAngle(parent,child,joint)-angle)<1e-5f,name,"legal motion is retained");
                if (joint.type==body::JointType::Hinge) {
                    child.body.orientation=orientation*math::Quaternion::fromAxisAngle({0,1,0},.15f)*
                        math::Quaternion::fromAxisAngle(joint.hingeAxis,angle);
                    child.body.position=body::localToWorldPoint(parent,joint.localAnchorA)-child.body.orientation.rotate(joint.localAnchorB);
                    body::correctHingeAxisWithComponent(parent,child,joint,component);
                    check(body::jointSwingRotationVector(body::relativeJointSwing(parent,child,joint)).length()<1e-5f,
                          name,"hinge rejects sideways rotation rather than permitting an extra bend axis");
                    check(std::abs(body::relativeJointAngle(parent,child,joint)-angle)<1e-5f,name,"sideways repair retains legal hinge flexion");
                }

                // Test real torque/inertia integration from zero relative rotation.
                child.body.orientation=orientation;
                parent.body.angularVelocity=child.body.angularVelocity={};
                parent.body.torque=child.body.torque={};
                joint.targetAngle=angle;
                body::applyJointMotor(parent,child,joint);
                const auto worldAxis=orientation.rotate(joint.hingeAxis);
                check(sign*child.body.torque.dot(worldAxis)>0,name,"child torque moves toward requested direction");
                check((parent.body.torque+child.body.torque).length()<1e-6f,name,"parent receives opposite torque");
                for (auto* part:{&parent,&child}) {
                    physics::updateAngularAcceleration(part->body);
                    physics::integrateAngularMotion(part->body,1.f/120);
                }
                check(sign*body::relativeJointAngle(parent,child,joint)>0,name,"integrated motor moves in requested direction");

                // Damping must remove relative rotational energy in either frame.
                parent.body.orientation=child.body.orientation=orientation;
                parent.body.angularVelocity=worldAxis*0.25f;
                child.body.angularVelocity=worldAxis*1.25f;
                parent.body.torque=child.body.torque={};
                joint.motorStiffness=0;
                joint.motorDamping=2;
                body::applyJointMotor(parent,child,joint);
                const float power=parent.body.torque.dot(parent.body.angularVelocity)+child.body.torque.dot(child.body.angularVelocity);
                check(std::abs(power+2.f)<1e-5f,name,"damping opposes relative motion on both bodies");
            }
            for (float requested:{it->constraint.minAngle-.1f,it->constraint.maxAngle+.1f}) {
                auto parent=*it->partA,child=*it->partB;
                const auto& joint=it->constraint;
                parent.body.orientation=orientation;
                child.body.orientation=orientation*math::Quaternion::fromAxisAngle(joint.hingeAxis,requested);
                child.body.position=body::localToWorldPoint(parent,joint.localAnchorA)-child.body.orientation.rotate(joint.localAnchorB);
                const std::array<body::BodyPart3D*,1> component{&child};
                body::correctJointAngleWithComponent(parent,child,joint,component);
                const auto expected=std::clamp(requested,joint.minAngle,joint.maxAngle);
                check(std::abs(body::relativeJointAngle(parent,child,joint)-expected)<1e-5f,name,"both angular bounds are enforced");
                check((body::localToWorldPoint(parent,joint.localAnchorA)-body::localToWorldPoint(child,joint.localAnchorB)).length()<2e-6f,
                      name,"component limit correction preserves the attachment pivot");
            }
        }
        std::cout<<name<<": anatomical direction, rotated frame, motor reaction, damping and limits checked\n";
    }
    return passed?0:1;
}
