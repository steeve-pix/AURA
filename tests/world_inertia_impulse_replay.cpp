#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>
#include "aura/physics/Inertia.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Impulse.hpp"
#include "aura/physics/MechanicalState.hpp"

int main(int argc, char **argv) {
    using namespace aura;
    if(argc!=2) {std::cerr << "Usage: aura_world_inertia_impulse_replay snapshot.csv\n";return 2;}
    bool passed=true;
    const auto check=[&](bool value,const char *message) {
        if(!value) {std::cerr << "FAIL " << message << '\n';passed=false;}
    };
    physics::RigidBody3D axes;
    axes.momentOfInertia={2,4,5};
    check((physics::applyInverseInertiaWorld(axes,{8,6,10})-math::Vec3{4,1.5f,2}).length()<1e-6f,
          "aligned inverse inertia divides by local principal moments");
    axes.orientation=math::Quaternion::fromAxisAngle({0,0,1},std::numbers::pi_v<float>/2);
    check((physics::applyInverseInertiaWorld(axes,{8,6,10})-math::Vec3{2,3,2}).length()<2e-6f,
          "rotated anisotropic principal axes are mapped into world space");
    axes.orientation={axes.orientation.w*2,axes.orientation.x*2,axes.orientation.y*2,axes.orientation.z*2};
    check((physics::applyInverseInertiaWorld(axes,{8,6,10})-math::Vec3{2,3,2}).length()<2e-6f,
          "inverse inertia normalizes orientation without mutating body state");
    std::ifstream file(argv[1]);std::string line;bool loaded=false;
    physics::RigidBody3D before;math::Vec3 point;
    while(std::getline(file,line)) {
        if(!line.starts_with("worstFloorImpulseInput,") || line.starts_with("worstFloorImpulseInput,mass,")) continue;
        if(loaded) {std::cerr << "Duplicate impulse input\n";return 2;}
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);std::string tag;row>>tag;
        row>>before.mass>>before.restitution>>before.position.x>>before.position.y>>before.position.z
            >>before.orientation.w>>before.orientation.x>>before.orientation.y>>before.orientation.z
            >>before.velocity.x>>before.velocity.y>>before.velocity.z
            >>before.angularVelocity.x>>before.angularVelocity.y>>before.angularVelocity.z
            >>before.momentOfInertia.x>>before.momentOfInertia.y>>before.momentOfInertia.z
            >>point.x>>point.y>>point.z;
        if(!row) {std::cerr << "Incomplete impulse state\n";return 2;} loaded=true;
    }
    if(!loaded) {std::cerr << "Missing impulse input\n";return 2;}
    // MechanicalState and impulse response both use normalized principal axes.
    const auto energy=[](auto body) {
        body.orientation=body.orientation.normalized();
        const auto state=physics::mechanicalState(body);
        return state.linearKinetic+state.rotationalKinetic;
    };
    const math::Vec3 normal{0,1,0};
    const auto r=point-before.position;
    const auto lever=r.cross(normal);
    const float vn=physics::velocityAtWorldPoint(before,point).dot(normal);
    const auto oldInverse=math::Vec3{lever.x/before.momentOfInertia.x,lever.y/before.momentOfInertia.y,lever.z/before.momentOfInertia.z};
    const float oldEffectiveInverseMass=1.0f/before.mass+oldInverse.cross(r).dot(normal);
    const float oldJ=-(1+before.restitution)*vn/oldEffectiveInverseMass;
    auto old=before;
    // Frozen historical response; production now uses the world inverse tensor.
    old.velocity += normal * (oldJ / old.mass);
    const auto oldAngular = r.cross(normal * oldJ);
    old.angularVelocity += math::Vec3{oldAngular.x/old.momentOfInertia.x,
                                    oldAngular.y/old.momentOfInertia.y,
                                    oldAngular.z/old.momentOfInertia.z};
    const auto inverse=physics::applyInverseInertiaWorld(before,lever);
    const float effectiveInverseMass=1.0f/before.mass+inverse.cross(r).dot(normal);
    const float j=-(1+before.restitution)*vn/effectiveInverseMass;
    auto corrected=before;
    const auto impulse=normal*j;
    // Verify the production impulse response against its predicted effective mass.
    physics::applyImpulseAtPoint(corrected, impulse, point);
    const float afterVn=physics::velocityAtWorldPoint(corrected,point).dot(normal);
    const double predictedDeltaVn=double(j)*effectiveInverseMass;
    const double actualDeltaVn=double(afterVn)-vn;
    const double predictedWork=double(j)*vn+0.5*double(j)*j*effectiveInverseMass;
    const double actualWork=energy(corrected)-energy(before);
    const auto sample=[&](const char *name,const auto &state,float magnitude,float k) {
        std::cout << "worldInertiaReplay," << name << ',' << magnitude << ',' << k << ','
            << physics::velocityAtWorldPoint(state,point).dot(normal) << ',' << energy(state)
            << ',' << energy(state)-energy(before) << ',' << state.velocity.length() << ',' << state.angularVelocity.length() << '\n';
    };
    std::cout << std::scientific << std::setprecision(9)
        << "worldInertiaReplay,stage,J,effectiveInverseMass,normalVelocity,KE,deltaKE,linearSpeed,angularSpeed\n";
    sample("before",before,0,0);sample("old",old,oldJ,oldEffectiveInverseMass);sample("corrected",corrected,j,effectiveInverseMass);
    check(before.restitution==0 && vn<0,"replay is the recorded inelastic incoming contact");
    check(energy(old)-energy(before)>9,"old copied response reproduces the energy injection");
    check(std::abs(afterVn)<2e-5f && std::abs(predictedDeltaVn-actualDeltaVn)<2e-5,
          "predicted effective-mass normal response agrees with actual impulse response");
    check(actualWork<=1e-5 && std::abs(actualWork-predictedWork)<2e-5,
          "corrected impulse dissipates energy and agrees with predicted work");
    check((corrected.position-before.position).lengthSquared()==0 && corrected.orientation.w==before.orientation.w &&
          corrected.orientation.x==before.orientation.x && corrected.orientation.y==before.orientation.y && corrected.orientation.z==before.orientation.z,
          "copied velocity experiment does not change geometry");
    std::cout << "worldInertiaChecks,predictedDeltaVn=" << predictedDeltaVn << ",actualDeltaVn=" << actualDeltaVn
        << ",predictedWork=" << predictedWork << ",actualWork=" << actualWork << ",passed=" << passed << '\n';
    return passed?0:1;
}
