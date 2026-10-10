// Copied-state experiment only: production geometry policies are unchanged.
#include "JointCOMProjectionReplay.hpp"
#include <fstream>
#include <sstream>
#include <set>
#include "aura/body/AuraBody3D.hpp"

using namespace aura;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto input=body::createAuraBody3D();
    // Anchors must be configured in the assembled neutral pose, not the fixture.
    const auto skeleton=body::createAuraSkeleton3D(input);
    const auto originalParts=joint_com_replay::parts(input);
    std::ifstream file(argv[1]);std::string line;std::set<std::string> loaded;
    while(std::getline(file,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
        std::string tag,name;row>>tag>>name;
        auto it=std::find_if(originalParts.begin(),originalParts.end(),[&](auto* p){return p->name==name;});
        if(tag!="jointCOMInput"||it==originalParts.end()||!loaded.insert(name).second)return 2;
        auto& p=**it;auto& b=p.body;
        if(!(row>>p.size.x>>p.size.y>>p.size.z>>b.mass>>b.position.x>>b.position.y>>b.position.z
            >>b.orientation.w>>b.orientation.x>>b.orientation.y>>b.orientation.z
            >>b.velocity.x>>b.velocity.y>>b.velocity.z>>b.angularVelocity.x>>b.angularVelocity.y>>b.angularVelocity.z
            >>b.momentOfInertia.x>>b.momentOfInertia.y>>b.momentOfInertia.z))return 2;
    }
    if(loaded.size()!=16)return 2;
    const auto& joint=skeleton.rightAnkle;
    const auto error=body::localToWorldPoint(input.rightFoot,joint.localAnchorB)-body::localToWorldPoint(input.rightShin,joint.localAnchorA);
    auto current=input,balanced=input,directional=input;
    body::correctJointPositionWithComponents(current,skeleton,current.rightShin,current.rightFoot,joint);
    auto a=skeleton.collectComponent(directional,directional.rightShin,joint);
    auto b=skeleton.collectComponent(directional,directional.rightFoot,joint);
    double massA=0,massB=0;for(auto* p:a)massA+=p->body.mass;for(auto* p:b)massB+=p->body.mass;
    const auto da=error*static_cast<float>(massB/(massA+massB));
    const auto db=-error*static_cast<float>(massA/(massA+massB));
    auto ba=skeleton.collectComponent(balanced,balanced.rightShin,joint);
    auto bb=skeleton.collectComponent(balanced,balanced.rightFoot,joint);
    body::translateSubtree(ba,da);body::translateSubtree(bb,db);
    constexpr float floorAllowance=1e-6f;
    const auto lowerBound=[&](const auto& component) {
        float lower=-std::numeric_limits<float>::infinity();
        for(const auto* p:component) {
            const float y=physics::lowestPoint(p->body,p->size).y;
            lower=std::max(lower,std::min(0.0f,y)-floorAllowance-y);
        }
        return lower;
    };
    auto directionA=da,directionB=db;
    // Each component's floor inequality constrains only its Y translation.
    directionB.y=std::max({db.y,lowerBound(b),lowerBound(a)-error.y});
    directionA.y=error.y+directionB.y;
    body::translateSubtree(a,directionA);body::translateSubtree(b,directionB);
    // Float position addition can round the limiting point below the allowance.
    // Move to the next representable safe height, retaining the relative repair.
    for(int i=0;i<8 && physics::lowestPoint(directional.rightFoot.body,directional.rightFoot.size).y<-floorAllowance;++i) {
        const float oldY=directional.rightFoot.body.position.y;
        const float extra=std::nextafter(oldY,std::numeric_limits<float>::infinity())-oldY;
        body::translateSubtree(a,{0,extra,0});body::translateSubtree(b,{0,extra,0});
        directionA.y+=extra;directionB.y+=extra;
    }
    auto production=input;
    const auto productionResult=body::correctJointPositionDirectionally(production,skeleton,production.rightShin,production.rightFoot,joint);
    const auto productionParts=joint_com_replay::parts(production),referenceParts=joint_com_replay::parts(directional);
    double referenceDifference=0;
    for(size_t i=0;i<productionParts.size();++i)referenceDifference=std::max(referenceDifference,
        static_cast<double>((productionParts[i]->body.position-referenceParts[i]->body.position).length()));
    directional=production; // Audit the live implementation below, against the independent copied reference.
    const auto start=joint_com_replay::center(input);
    std::cout<<std::scientific<<std::setprecision(12)
        <<"directionalInput,time=4.775,iteration=1,joint=rightAnkle,massA="<<massA<<",massB="<<massB
        <<",error="<<error.x<<','<<error.y<<','<<error.z<<'\n';
    // Guard the saved event's identity as well as the proposed repair. This is
    // not a live stability test and must not silently pass on a neutral fixture.
    bool passed=productionResult.applied && referenceDifference<1e-6 && std::abs(error.length()-.4833904504776f)<1e-6f && massA==15 && massB==1 &&
        std::abs(physics::lowestPoint(input.rightFoot.body,input.rightFoot.size).y-.0118619799614f)<1e-6f;
    for(auto [label,pose]:std::array{std::pair{"current",&current},std::pair{"balanced",&balanced},std::pair{"directional",&directional}}) {
        const auto com=joint_com_replay::center(*pose);
        const auto ps=joint_com_replay::parts(*pose);
        const float gap=(body::localToWorldPoint(pose->rightFoot,joint.localAnchorB)-body::localToWorldPoint(pose->rightShin,joint.localAnchorA)).length();
        bool floor=true,untouched=true;double increase=0,anchorDrift=0;std::string worstPair;
        for(size_t i=0;i<ps.size();++i) {
            const auto& old=originalParts[i]->body;const auto& now=ps[i]->body;
            const auto same=[](auto x,auto y){return x.x==y.x&&x.y==y.y&&x.z==y.z;};
            untouched &= same(old.velocity,now.velocity)&&same(old.angularVelocity,now.angularVelocity)&&
                old.orientation.w==now.orientation.w&&old.orientation.x==now.orientation.x&&old.orientation.y==now.orientation.y&&old.orientation.z==now.orientation.z;
            const float before=physics::lowestPoint(old,originalParts[i]->size).y,after=physics::lowestPoint(now,ps[i]->size).y;
            floor &= after>=std::min(0.0f,before)-floorAllowance;
            for(size_t j=i+1;j<ps.size();++j) {
                if(!physics::shouldSelfCollide(*originalParts[i],*originalParts[j],input,skeleton))continue;
                const auto penetration=[](auto* x,auto* y){return std::max(0.0f,physics::collisionSphere(*x).radius+
                    physics::collisionSphere(*y).radius-(x->body.position-y->body.position).length());};
                const double d=penetration(ps[i],ps[j])-penetration(originalParts[i],originalParts[j]);
                if(d>increase){increase=d;worstPair=ps[i]->name+std::string("/")+ps[j]->name;}
            }
        }
        // All other joints are inside A. Their anchors must preserve attachment.
        const std::array<const body::Joint3D*,14> js{&skeleton.waist,&skeleton.neck,&skeleton.head,&skeleton.leftShoulder,&skeleton.leftElbow,&skeleton.leftWrist,
            &skeleton.rightShoulder,&skeleton.rightElbow,&skeleton.rightWrist,&skeleton.leftHip,&skeleton.leftKnee,&skeleton.leftAnkle,&skeleton.rightHip,&skeleton.rightKnee};
        const std::array<size_t,14> ia{2,2,1,2,4,5,2,7,8,3,10,11,3,13},ib{3,1,0,4,5,6,7,8,9,10,11,12,13,14};
        for(size_t i=0;i<js.size();++i) {
            const auto gapAt=[&](auto parts){return (body::localToWorldPoint(*parts[ib[i]],js[i]->localAnchorB)-body::localToWorldPoint(*parts[ia[i]],js[i]->localAnchorA)).length();};
            anchorDrift=std::max(anchorDrift,std::abs(static_cast<double>(gapAt(ps)-gapAt(originalParts))));
        }
        const double horizontal=std::hypot(com[0]-start[0],com[2]-start[2]);
        const float footY=physics::lowestPoint(pose->rightFoot.body,pose->rightFoot.size).y;
        std::cout<<"directionalCandidate,"<<label<<",gap="<<gap<<",deltaCOM="<<com[0]-start[0]<<','<<com[1]-start[1]<<','<<com[2]-start[2]
            <<",horizontalCOM="<<horizontal<<",footY="<<footY<<",floorSafe="<<floor<<",thighPenetration="<<joint_com_replay::thighPenetration(*pose)
            <<",maximumExperimentalSphereIncrease="<<increase<<",spherePair="<<worstPair<<",velocitiesAndOrientationsUnchanged="<<untouched<<",otherAnchorGapDrift="<<anchorDrift<<'\n';
        if(pose==&current)passed &= floor&&horizontal>.38&&gap<1e-6;
        if(pose==&balanced)passed &= !floor&&horizontal<1e-6&&gap<1e-6;
        if(pose==&directional)passed &= gap<1e-6&&horizontal<1e-6&&floor&&untouched&&anchorDrift<1e-6&&
            increase<.001&&joint_com_replay::thighPenetration(*pose)<=joint_com_replay::thighPenetration(input)+.001f;
    }
    std::cout<<"directionalTranslation,A,"<<directionA.x<<','<<directionA.y<<','<<directionA.z
        <<"\ndirectionalTranslation,B,"<<directionB.x<<','<<directionB.y<<','<<directionB.z
        <<"\ndirectionalProduction,referenceDifference="<<referenceDifference<<",applied="<<productionResult.applied
        <<"\ndirectionalResult,passed="<<passed<<'\n';
    return passed?0:1;
}
