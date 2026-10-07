#include <algorithm>
#include "LegacyFloorResponse.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/LocalJointVelocity.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/MechanicalState.hpp"

int main(int argc, char **argv) {
    using namespace aura;
    if (argc!=2) { std::cerr << "Usage: aura_shin_contact_velocity_replay snapshot.csv\n"; return 2; }
    auto b=body::createAuraBody3D();
    // Anchors are configured from the assembled neutral pose, before loading
    // the simulated state. Rebuilding them from that state changes the joints.
    const auto skeleton=body::createAuraSkeleton3D(b);
    const std::array parts{&b.head,&b.neck,&b.torso,&b.pelvis,
        &b.leftUpperArm,&b.leftForearm,&b.leftHand,&b.rightUpperArm,&b.rightForearm,&b.rightHand,
        &b.leftThigh,&b.leftShin,&b.leftFoot,&b.rightThigh,&b.rightShin,&b.rightFoot};
    std::ifstream input(argv[1]);
    std::array<bool,16> loaded{};
    std::string line;
    while(std::getline(input,line)) {
        if(!line.starts_with("floorContactInput,") || line.starts_with("floorContactInput,part,")) continue;
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line); std::string tag,name; row>>tag>>name;
        const auto it=std::find_if(parts.begin(),parts.end(),[&](const auto *p){return p->name==name;});
        if(it==parts.end() || loaded[it-parts.begin()]) { std::cerr << "Invalid snapshot part\n";return 2; }
        auto &p=**it;auto &r=p.body;
        row>>p.size.x>>p.size.y>>p.size.z>>r.mass>>r.position.x>>r.position.y>>r.position.z
            >>r.orientation.w>>r.orientation.x>>r.orientation.y>>r.orientation.z
            >>r.velocity.x>>r.velocity.y>>r.velocity.z>>r.angularVelocity.x>>r.angularVelocity.y>>r.angularVelocity.z
            >>r.momentOfInertia.x>>r.momentOfInertia.y>>r.momentOfInertia.z>>r.restitution;
        if(!row) {std::cerr << "Incomplete snapshot\n";return 2;}
        loaded[it-parts.begin()]=true;
    }
    if(!std::all_of(loaded.begin(),loaded.end(),[](bool v){return v;})) {std::cerr << "Need all 16 saved bodies\n";return 2;}
    const auto momentum=[&] {
        std::array<double,3> p{};
        for(const auto *part:parts) {
            p[0]+=double(part->body.mass)*part->body.velocity.x;
            p[1]+=double(part->body.mass)*part->body.velocity.y;
            p[2]+=double(part->body.mass)*part->body.velocity.z;
        }
        return p;
    };
    const auto energy=[&] {double e=0;for(const auto *p:parts) {const auto state=physics::mechanicalState(p->body);e+=state.linearKinetic+state.rotationalKinetic;}return e;};
    const auto anchorSpeed=[](const auto &a,const auto &b,const auto &j) {
        return (physics::velocityAtWorldPoint(b.body,body::localToWorldPoint(b,j.localAnchorB))-
                physics::velocityAtWorldPoint(a.body,body::localToWorldPoint(a,j.localAnchorA))).length();
    };
    const auto gap=[](const auto &a,const auto &b,const auto &j) {
        return (body::localToWorldPoint(b,j.localAnchorB)-body::localToWorldPoint(a,j.localAnchorA)).length();
    };
    const auto sample=[&](const std::string &stage) {
        const auto p=momentum();
        std::cout << "shinContactReplay," << stage << ',' << p[0] << ',' << p[1] << ',' << p[2]
            << ',' << b.rightShin.body.velocity.length() << ',' << b.rightFoot.body.velocity.length()
            << ',' << anchorSpeed(b.rightShin,b.rightFoot,skeleton.rightAnkle)
            << ',' << anchorSpeed(b.rightThigh,b.rightShin,skeleton.rightKnee) << ',' << energy()
            << ',' << physics::lowestPoint(b.rightFoot.body,b.rightFoot.size).y
            << ',' << gap(b.rightShin,b.rightFoot,skeleton.rightAnkle)
            << ',' << gap(b.rightThigh,b.rightShin,skeleton.rightKnee) << '\n';
    };
    std::cout << std::scientific << std::setprecision(9)
        << "shinContactReplay,stage,Px,Py,Pz,shinSpeed,footSpeed,ankleAnchorSpeed,kneeAnchorSpeed,KE,footLowestY,ankleGap,kneeGap\n";
    const auto beforeMomentum=momentum();const auto oldFoot=b.rightFoot.body;
    const auto oldShin=b.rightShin.body;
    auto reference=b;
    test::resolveRightShinFloorWithLegacyFootMotion(reference,1.0f/120.0f/16.0f);
    sample("before");
    // Existing shin/foot geometric translation is retained. No velocity copying.
    body::resolveRightShinFloorWithFootTranslation(b,1.0f/120.0f/16.0f);
    sample("afterFloor");
    auto auditBody=oldShin; physics::FloorCollisionAudit audit;
    physics::resolveFloorCollision(auditBody,b.rightShin.size,0,1.0f/120.0f/16.0f,&audit);
    const auto contactMomentum=momentum();
    const auto expected=(audit.dampingDeltaVelocity+audit.normalDeltaVelocity)*oldShin.mass;
    const double budgetResidual=std::max({std::abs(contactMomentum[0]-beforeMomentum[0]-expected.x),
        std::abs(contactMomentum[1]-beforeMomentum[1]-expected.y),std::abs(contactMomentum[2]-beforeMomentum[2]-expected.z)});
    const bool footUntouched=(b.rightFoot.body.velocity-oldFoot.velocity).lengthSquared()==0 &&
        (b.rightFoot.body.angularVelocity-oldFoot.angularVelocity).lengthSquared()==0;
    const bool sameGeometry=(reference.rightShin.body.position-b.rightShin.body.position).lengthSquared()==0 &&
        (reference.rightFoot.body.position-b.rightFoot.body.position).lengthSquared()==0;
    std::array<physics::RigidBody3D,16> poses;
    for(std::size_t i=0;i<parts.size();++i) poses[i]=parts[i]->body;
    double momentumDrift=0,energyRise=0;
    const auto solve=[&](auto &a,auto &child,const auto &joint) {
        for(int i=0;i<body::jointVelocityIterations;++i) for(bool angular:{false,true}) {
            const auto ke=energy();
            if(angular) body::correctLocalJointAngularLimitVelocity(a,child,joint);
            else body::correctLocalJointVelocity(a,child,joint);
            energyRise=std::max(energyRise,energy()-ke);
            const auto p=momentum();for(int k=0;k<3;++k) momentumDrift=std::max(momentumDrift,std::abs(p[k]-contactMomentum[k]));
        }
    };
    for(int sweep=1;sweep<=16;++sweep) {
        solve(b.rightThigh,b.rightShin,skeleton.rightKnee);
        solve(b.rightShin,b.rightFoot,skeleton.rightAnkle);
        solve(b.rightShin,b.rightFoot,skeleton.rightAnkle);
        solve(b.rightThigh,b.rightShin,skeleton.rightKnee);
        if(sweep<=8 || sweep==16) sample("sweep"+std::to_string(sweep));
    }
    bool posesUnchanged=true;
    for(std::size_t i=0;i<parts.size();++i) {
        const auto &a=poses[i];const auto &c=parts[i]->body;
        posesUnchanged &= (a.position-c.position).lengthSquared()==0 && a.orientation.w==c.orientation.w &&
            a.orientation.x==c.orientation.x && a.orientation.y==c.orientation.y && a.orientation.z==c.orientation.z;
    }
    const bool converged=anchorSpeed(b.rightShin,b.rightFoot,skeleton.rightAnkle)<1e-3 &&
        anchorSpeed(b.rightThigh,b.rightShin,skeleton.rightKnee)<1e-3;
    const bool passed=footUntouched && sameGeometry && posesUnchanged && converged && budgetResidual<1e-5 && momentumDrift<1e-5 && energyRise<1e-5;
    std::cout << "shinContactChecks,footVelocityExactlyUnchanged=" << footUntouched << ",sameGeometry=" << sameGeometry
        << ",floorMomentumResidual=" << budgetResidual << ",localMomentumDrift=" << momentumDrift
        << ",maxLocalImpulseEnergyRise=" << energyRise << ",posesFrozen=" << posesUnchanged
        << ",converged=" << converged << ",passed=" << passed << '\n';
    return passed ? 0 : 1;
}
