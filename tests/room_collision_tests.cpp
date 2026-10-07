#include <array>
#include <cmath>
#include <iostream>
#include "aura/physics/RoomCollision.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Inertia.hpp"
#include "aura/physics/MechanicalState.hpp"

int main() {
    using namespace aura;
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; passed = false; }
    };
    const physics::RoomBounds room{{-10,0,-10},{10,12,10}};
    const math::Vec3 size{0.6f, 1.4f, 0.8f};
    const auto makeBody = [&] {
        physics::RigidBody3D b;
        b.mass = 2;
        b.momentOfInertia = physics::boxMomentOfInertia(b.mass, size);
        b.position = {0,6,0};
        b.orientation = math::Quaternion::fromAxisAngle(math::Vec3{1,2,3}.normalized(),0.7f);
        return b;
    };
    const auto energy = [](const auto &b) {
        const auto state = physics::mechanicalState(b);
        return state.linearKinetic + state.rotationalKinetic;
    };
    const std::array<math::Vec3,6> inward{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    const std::array<math::Vec3,6> outside{{{-15,6,0},{15,6,0},{0,-5,0},{0,18,0},{0,6,-15},{0,6,15}}};
    for (std::size_t i=0;i<outside.size();++i) {
        auto b=makeBody(); b.position=outside[i]; b.velocity=-inward[i]*40;
        const double before=energy(b);
        physics::resolveRoomCollision(b,size,room);
        check(physics::roomPenetration(b,size,room)<2e-6f,"rotated box remains inside each of six room planes after overshoot");
        check(energy(b)<=before+1e-3,"normal impulses dissipate energy for zero restitution");
        // A static wall changes momentum only along its normal.
        const auto dv=b.velocity+inward[i]*40;
        check((dv-inward[i]*dv.dot(inward[i])).length()<1e-5f,"wall impulse leaves tangential linear velocity unchanged");

        auto projection=makeBody(); projection.position=outside[i]; projection.velocity=inward[i]*2;
        physics::resolveRoomCollision(projection,size,room);
        check((projection.velocity-inward[i]*2).length()==0 && projection.angularVelocity.length()==0,
              "pose projection with separating velocity does not manufacture velocity");
    }
    auto interior=makeBody(); interior.velocity={1,2,3}; interior.angularVelocity={0.1f,0.2f,0.3f};
    const auto initial=interior;
    physics::resolveRoomCollision(interior,size,room);
    check((interior.position-initial.position).length()==0 && (interior.velocity-initial.velocity).length()==0 &&
          (interior.angularVelocity-initial.angularVelocity).length()==0,"interior free motion remains unchanged");

    auto corner=makeBody(); corner.position={30,30,-30}; corner.velocity={50,50,-50};
    physics::resolveRoomCollision(corner,size,room);
    check(physics::roomPenetration(corner,size,room)<2e-6f,"three-plane corner collision contains every rotated corner");
    // Repeated large discrete steps can overshoot a wall, but the room projection
    // must recover containment rather than miss a thin finite wall.
    for (int step=0;step<1200;++step) {
        corner.position += corner.velocity*(1.0f/120);
        physics::resolveRoomCollision(corner,size,room);
        check(physics::roomPenetration(corner,size,room)<2e-6f,"repeated room contacts remain contained");
    }
    auto parent=makeBody(), child=makeBody();
    parent.position={15,6,0}; child.position={13,6,0};
    parent.velocity=child.velocity={-2,0,0};
    const auto separation=child.position-parent.position;
    const std::array<physics::RoomBody,2> connected{{{&parent,size},{&child,size}}};
    check(physics::resolveRoomCollisions(connected,room),"connected group fits room");
    check((child.position-parent.position-separation).length()<1e-6f,"common room translation preserves connected geometry");
    check(parent.velocity.x==-2 && child.velocity.x==-2 && parent.angularVelocity.length()==0 && child.angularVelocity.length()==0,
          "common projection adds no velocity to a separating group");
    check(physics::roomPenetration(parent,size,room)<2e-6f && physics::roomPenetration(child,size,room)<2e-6f,
          "common projection contains every group member");

    parent=makeBody(); child=makeBody(); parent.position={15,6,0}; child.position={13,6,0};
    parent.velocity={40,0,0};
    physics::resolveRoomCollisions(connected,room);
    check(child.velocity.length()==0 && child.angularVelocity.length()==0,"room impulse affects only the contacting body");
    child.position.x=-30;
    const auto parentBefore=parent, childBefore=child;
    check(!physics::resolveRoomCollisions(connected,room),"oversized group is reported without breaking it apart");
    check((parent.position-parentBefore.position).length()==0 && (child.position-childBefore.position).length()==0 &&
          (parent.velocity-parentBefore.velocity).length()==0 && (child.velocity-childBefore.velocity).length()==0,
          "failed group fit leaves state untouched");
    return passed ? 0 : 1;
}
