// Read-only attribution of full-body translation, including post-fall settling.
#include "../../src/app/SimulationStep.hpp"
#include "aura/sim/AuraSimulation.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Motion.hpp"
#include <stdexcept>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
using namespace aura;
struct Sample {
    std::array<double,3> com{}, momentum{};
    double energy=0, mass=0;
};
Sample sample(sim::AuraSimulation& simulation) {
    Sample s; double mass=0;
    for (const auto* part:simulation.getParts()) {
        const auto& b=part->body;mass+=b.mass;
        const std::array<double,3> p{b.position.x,b.position.y,b.position.z},v{b.velocity.x,b.velocity.y,b.velocity.z};
        for(int i=0;i<3;++i) {s.com[i]+=b.mass*p[i];s.momentum[i]+=b.mass*v[i];}
    }
    for(auto& x:s.com)x/=mass;
    s.mass=mass;
    s.energy=app::bodyEnergy(simulation.getParts());
    return s;
}
struct Change {double x=0,z=0,path=0,px=0,pz=0,max=0,y=0,py=0,energy=0;};
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void gripTests() {
    for (float mass : {1.f, 10.f}) for (math::Vec3 velocity : {math::Vec3{.04f,0,0}, {1,0,0}, {1,0,1}}) {
        physics::RigidBody3D block;
        block.mass=mass; block.position={0,.5f,0}; block.velocity=velocity;
        block.momentOfInertia={mass/6,mass/6,mass/6};
        math::Vec3 settled;
        for(int tick=0;tick<1200;++tick) {
            block.acceleration={0,-9.81f,0};
            physics::integrateLinearMotion(block,1.f/120);
            physics::integrateAngularMotion(block,1.f/120);
            for(int iteration=0;iteration<16;++iteration) {
                physics::FloorCollisionAudit audit;
                physics::resolveFloorCollision(block,{1,1,1},0,1.f/1920,&audit,physics::FloorFrictionModel::Coulomb);
                check(audit.energyAfterFriction <= audit.energyBefore+1e-5*mass, "static floor must not add kinetic energy");
                for(std::size_t i=0;i<audit.contacts.size();++i)
                    check(audit.frictionImpulses[i].length() <= physics::groundStaticFriction*audit.normalImpulses[i].y+1e-5*mass,
                          "ground impulses stay within the physical friction cone");
            }
            if(tick==119) {
                check(std::hypot(block.velocity.x,block.velocity.z)<.001, "dry ground stops a sliding block within one second");
                check(block.velocity.y<.001, "zero restitution ground does not keep bouncing at rest");
                settled=block.position;
                std::cout<<"grip mass="<<mass<<" initial_speed="<<velocity.length()<<" stopped_distance="
                         <<std::hypot(settled.x,settled.z)<<" residual_speed="<<block.velocity.length()<<'\n';
            }
        }
        check(std::hypot(block.position.x-settled.x,block.position.z-settled.z)<.001,
              "grounded block must not develop sustained drift over the next nine seconds");
        if(velocity.x==.04f && velocity.z==0)
            check(std::hypot(block.position.x,block.position.z)<.002, "small initial slip stops within two millimetres");
    }
    physics::RigidBody3D block; block.position={0,.5f,0}; block.velocity={1,-.1f,0};
    const physics::RoomBody entry{&block,{1,1,1}};
    physics::resolveRoomCollisions(std::span<const physics::RoomBody>{&entry,1},{},physics::FloorFrictionModel::Coulomb,1.f/1920);
    check(block.velocity.x<1, "final room support must include ground friction");
    physics::RigidBody3D torso;
    torso.position={0,.725f,0}; torso.orientation=math::Quaternion::fromAxisAngle({0,0,1},.785398f);
    const physics::CollisionShape sphere{physics::CollisionShapeType::Sphere,.725f};
    const physics::RoomBody shaped{&torso,{1.5f,1.45f,.8f},sphere};
    check(physics::projectRoomGeometry(std::span<const physics::RoomBody>{&shaped,1},{},true), "shaped body fits the room");
    check(torso.position.y==.725f, "room floor must not lift a sphere using imaginary box corners");
    check(physics::roomPenetration(torso,shaped.size,{},sphere)<1e-6f, "shaped floor containment matches floor contacts");
    physics::RigidBody3D airborne; airborne.position={0,10,0}; airborne.velocity={1,0,2};
    physics::resolveFloorCollision(airborne,{1,1,1},0,1.f/120,nullptr,physics::FloorFrictionModel::Coulomb);
    check(airborne.velocity.x==1 && airborne.velocity.z==2, "ground friction does not brake airborne movement");
}
void viewerTests() {
    sim::AuraSimulation simulation;
    int count=0;
    while(!simulation.terminationStatus().terminated) {
        simulation.step({});
        check(++count<1200,"neutral viewer reaches confirmed fall");
    }
    const auto steps=simulation.steps();
    sim::AuraAction action; action.leftHipTarget=.2f;
    for(int i=0;i<120;++i) simulation.step(action);
    check(simulation.steps()==steps+120 && simulation.action().leftHipTarget==action.leftHipTarget,
          "normal /run simulation continues stepping and accepting controls after FALLEN");
    check(std::isfinite(sample(simulation).energy),"post-fall viewer state remains finite");
    check(simulation.terminationStatus().terminated,"continued viewing preserves FALLEN classification");
    std::cout<<"viewer continues after FALLEN: "<<steps<<" -> "<<simulation.steps()<<" steps\n";
    simulation.reset(); check(!simulation.terminationStatus().terminated,"viewer reset clears failure latch");
}

int main(int argc,char** argv) {
    try {
    const bool report=argc>1 && std::string(argv[1])=="--report";
    gripTests(); viewerTests();
    for(bool motors:{true,false}) {
        sim::AuraSimulation simulation;
        if(!motors) for(auto& joint:simulation.getJoints())joint.motor=false;
        auto previous=sample(simulation);const auto initial=previous;
        std::map<std::string,Change> changes;
        const app::SimulationTrace trace=[&](std::string_view stage,std::string_view joint) {
            const auto current=sample(simulation);
            if(stage!="begin") {
                const auto dx=current.com[0]-previous.com[0],dz=current.com[2]-previous.com[2];
                auto& c=changes[std::string(stage)+(joint.empty()?"":"/"+std::string(joint))];
                c.energy+=current.energy-previous.energy;
                c.y+=current.com[1]-previous.com[1];c.py+=current.momentum[1]-previous.momentum[1];
                c.x+=dx;c.z+=dz;c.path+=std::hypot(dx,dz);c.max=std::max(c.max,std::hypot(dx,dz));
                c.px+=current.momentum[0]-previous.momentum[0];c.pz+=current.momentum[2]-previous.momentum[2];
            }
            if (stage=="joint" || stage=="self_collision") {
                check(std::hypot(current.com[0]-previous.com[0],current.com[2]-previous.com[2])<2e-6,
                      "internal projection cannot manufacture whole-body horizontal translation");
                check(std::hypot(current.momentum[0]-previous.momentum[0],current.momentum[2]-previous.momentum[2])<1e-4,
                      "internal projection/impulses preserve horizontal momentum");
            }
            check(std::isfinite(current.energy),"all sampled physical states remain finite");
            previous=current;
        };
        for(int tick=0;tick<1200;++tick) {
            app::stepSimulation(simulation.body(),simulation.getSkeleton(),simulation.getJoints(),simulation.getParts(),
                simulation.getBodyPairs(),simulation.getRoomBodies(),simulation.getRoom(),simulation.getDiagnosticHistory(),
                physics::FloorFrictionModel::Coulomb,trace);
            if(tick==119||tick==479||tick==1199) {
                const auto s=sample(simulation);
                std::cout<<"endpoint motors="<<motors<<" tick="<<tick+1<<" dx="<<s.com[0]-initial.com[0]
                         <<" dz="<<s.com[2]-initial.com[2]<<" com_y="<<s.com[1]<<" px="<<s.momentum[0]<<" pz="<<s.momentum[2]<<" vx="<<s.momentum[0]/s.mass<<" vz="<<s.momentum[2]/s.mass
                         <<" motor_work="<<simulation.getDiagnosticHistory().totalMotorWork
                         <<" left_support="<<simulation.getDiagnosticHistory().leftFootNormalImpulse
                         <<" right_support="<<simulation.getDiagnosticHistory().rightFootNormalImpulse<<'\n';
            }
        }
        if(report) for(const auto& [stage,c]:changes)
            std::cout<<"stage motors="<<motors<<' '<<stage<<" dx="<<c.x<<" dz="<<c.z<<" path="<<c.path
                     <<" largest="<<c.max<<" dpx="<<c.px<<" dpz="<<c.pz<<" dy="<<c.y<<" dpy="<<c.py<<" energy_change="<<c.energy<<'\n';
    }
    std::cout<<"Ground grip, COM conservation, finite full-body runs, and continuous viewer behavior passed.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n";return 1; }
}
