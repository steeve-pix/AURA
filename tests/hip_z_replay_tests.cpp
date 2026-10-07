#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include "HipZReplay.hpp"

int main(int argc, char** argv) {
    using namespace aura;
    if (argc != 2) return 2;
    auto input = body::createAuraBody3D();
    const auto skeleton = body::createAuraSkeleton3D(input);
    auto parts = skeleton.collectComponent(input, input.torso, skeleton.head);
    parts.push_back(&input.head);
    std::ifstream file(argv[1]);
    std::string line;
    std::set<std::string> loaded;
    while (std::getline(file, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::string tag, name;
        row >> tag >> name;
        if (tag != "sharedHipInput") continue;
        const auto it = std::find_if(parts.begin(), parts.end(), [&](const auto* p) { return p->name == name; });
        if (it == parts.end() || !loaded.insert(name).second) return 2;
        auto& p = **it;
        auto& b = p.body;
        row >> p.size.x >> p.size.y >> p.size.z >> b.mass >> b.position.x >> b.position.y >> b.position.z
            >> b.orientation.w >> b.orientation.x >> b.orientation.y >> b.orientation.z
            >> b.velocity.x >> b.velocity.y >> b.velocity.z
            >> b.angularVelocity.x >> b.angularVelocity.y >> b.angularVelocity.z
            >> b.momentOfInertia.x >> b.momentOfInertia.y >> b.momentOfInertia.z;
        if (!row) return 2;
    }
    if (loaded.size() != 16) return 2;
    bool passed=true;
    const auto check=[&](bool condition,const char* message) {
        if (!condition) {std::cerr << "FAIL " << message << '\n';passed=false;}
    };
    check(skeleton.leftHip.type==body::JointType::SwingTwist && skeleton.rightHip.type==body::JointType::SwingTwist &&
          skeleton.leftKnee.type==body::JointType::Hinge && skeleton.leftElbow.type==body::JointType::Hinge,
          "hips advertise swing/twist while knees and elbows retain hinge mode");
    auto apiPose=input;
    const auto apiComponent=skeleton.collectComponent(apiPose,apiPose.rightThigh,skeleton.rightHip);
    check(body::applyJointSwingWithComponent(apiPose.pelvis,apiPose.rightThigh,skeleton.rightHip,
                                           apiComponent,{0,0,1},0.160193f),"saved correction accepted through bounded joint API");
    check(physics::capsulePenetration(physics::bodyPartCollisionCapsule(apiPose.leftThigh),
                                    physics::bodyPartCollisionCapsule(apiPose.rightThigh))<1e-6f,
          "saved Class-B collision is separated through reusable joint math");
    check(std::abs(body::relativeJointTwistAngle(apiPose.pelvis,apiPose.rightThigh,skeleton.rightHip)-0.8f)<1e-6f,
          "saved swing correction retains the saturated hip twist");
    const float apiSwing=body::relativeJointSwingZ(apiPose.pelvis,apiPose.rightThigh,skeleton.rightHip);
    check(apiSwing>=skeleton.rightHip.minSwingZ && apiSwing<=skeleton.rightHip.maxSwingZ,
          "corrected saved hip swing respects explicit absolute bounds");
    auto result = test::replayHipZ(input, skeleton, std::cout);
    check(result.negativeLeft.metrics.penetration<result.before.metrics.penetration,
          "negative left and positive right Z separate this saved pose");
    check(!result.negativeLeft.xFixed,"pure pelvis-local Z does not preserve extracted X twist");
    check(result.fixedNegativeLeft.xFixed && result.fixedNegativeLeft.metrics.penetration<result.before.metrics.penetration,
          "constrained Z swing improves collision while retaining X twist");
    check(!result.fixedNegativeLeft.floorSafe,"symmetric outward swing worsens the saved left-foot penetration");
    check(result.separated && result.best.metrics.penetration==0,"biased secondary swing removes the capsule overlap");
    check(result.best.xFixed && result.best.metrics.maximumLimitError<=1e-5f,
          "all six leg limits are retained");
    check(result.best.floorSafe && result.best.gapsSafe && result.best.velocitiesUnchanged,
          "separation preserves floor feasibility, leg attachments and velocities");
    check(std::max(std::abs(result.best.leftZ),std::abs(result.best.rightZ))<=0.25f,
          "secondary swing stays inside the stated experiment budget");
    auto after=skeleton.collectComponent(result.best.pose, result.best.pose.torso,skeleton.head);
    after.push_back(&result.best.pose.head);
    for(size_t i=0;i<parts.size();++i) {
        const auto& a=parts[i]->body;const auto& b=after[i]->body;
        check((a.velocity-b.velocity).lengthSquared()==0 && (a.angularVelocity-b.angularVelocity).lengthSquared()==0,
              "all sixteen bodies retain their exact velocities");
        const bool leg=parts[i]==&input.leftThigh || parts[i]==&input.leftShin || parts[i]==&input.leftFoot ||
                       parts[i]==&input.rightThigh || parts[i]==&input.rightShin || parts[i]==&input.rightFoot;
        if(!leg) check((a.position-b.position).lengthSquared()==0 && a.orientation.w==b.orientation.w &&
                      a.orientation.x==b.orientation.x && a.orientation.y==b.orientation.y && a.orientation.z==b.orientation.z,
                      "pelvis and upper body remain exactly fixed");
    }
    const std::array<const body::BodyPart3D*,4> parents{&input.leftThigh,&input.leftShin,&input.rightThigh,&input.rightShin};
    const std::array<const body::BodyPart3D*,4> children{&input.leftShin,&input.leftFoot,&input.rightShin,&input.rightFoot};
    const auto& pose=result.best.pose;
    const std::array<const body::BodyPart3D*,4> newParents{&pose.leftThigh,&pose.leftShin,&pose.rightThigh,&pose.rightShin};
    const std::array<const body::BodyPart3D*,4> newChildren{&pose.leftShin,&pose.leftFoot,&pose.rightShin,&pose.rightFoot};
    for(size_t i=0;i<4;++i) {
        const auto q=(parents[i]->body.orientation.normalized().conjugate()*children[i]->body.orientation.normalized()).normalized();
        const auto r=(newParents[i]->body.orientation.normalized().conjugate()*newChildren[i]->body.orientation.normalized()).normalized();
        const float sign=q.w*r.w+q.x*r.x+q.y*r.y+q.z*r.z>=0?1:-1;
        check(std::pow(q.w-sign*r.w,2)+std::pow(q.x-sign*r.x,2)+std::pow(q.y-sign*r.y,2)+std::pow(q.z-sign*r.z,2)<9e-12,
              "complete knee and ankle relative orientations survive component rotation");
    }
    return passed?0:1;
}
