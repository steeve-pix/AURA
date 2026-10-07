#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include "HipCapsuleRotationReplay.hpp"

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
    return passed?0:1;
}
