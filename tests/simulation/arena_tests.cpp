#include "aura/sim/AuraArena.hpp"
#include "aura/body/BodyPartTransform.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>

using namespace aura;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void same(sim::AuraSimulation &a, sim::AuraSimulation &b) {
    for (std::size_t i=0; i<16; ++i) {
        const auto &x=a.getParts()[i]->body, &y=b.getParts()[i]->body;
        check(x.position.x==y.position.x && x.position.y==y.position.y && x.position.z==y.position.z, "parallel position determinism");
        check(x.velocity.x==y.velocity.x && x.velocity.y==y.velocity.y && x.velocity.z==y.velocity.z, "parallel velocity determinism");
        check(x.orientation.w==y.orientation.w && x.orientation.x==y.orientation.x && x.orientation.y==y.orientation.y && x.orientation.z==y.orientation.z, "parallel orientation determinism");
        check(std::isfinite(x.position.x+x.position.y+x.position.z+x.velocity.x+x.velocity.y+x.velocity.z), "finite arena state");
    }
}
float penetration(sim::AuraArena &arena) {
    float result=0;
    for (auto *a:arena.agent(0).getParts()) for (auto *b:arena.agent(1).getParts())
        result=std::max(result, physics::detectBodyContact(a->body,a->size,a->collisionShape,b->body,b->size,b->collisionShape).penetration);
    return result;
}
std::vector<float> gaps(sim::AuraSimulation &agent) {
    std::vector<float> result;
    for(const auto &joint:agent.getJoints()) result.push_back((body::localToWorldPoint(*joint.partA,joint.constraint.localAnchorA)-
        body::localToWorldPoint(*joint.partB,joint.constraint.localAnchorB)).length());
    return result;
}
math::Vec3 momentum(sim::AuraArena &arena) {
    math::Vec3 result{};
    for(std::size_t i=0;i<arena.size();++i) for(auto *part:arena.agent(i).getParts()) result+=part->body.velocity*part->body.mass;
    return result;
}
int main() {
    try {
        sim::AuraArena alone(1, false); sim::AuraSimulation reference;
        for(int tick=0;tick<200;++tick) { alone.step({{}}); reference.step({}); same(alone.agent(0),reference); }
        sim::AuraArena parallel(4,true), serial(4,false);
        check(std::abs(parallel.agent(1).pelvisPosition().x-parallel.agent(0).pelvisPosition().x-12.f)<1e-5f, "default spacing doubled");
        sim::AuraArena historical(4, false, 6.f);
        check(std::abs(historical.agent(1).pelvisPosition().x-historical.agent(0).pelvisPosition().x-6.f)<1e-5f, "historical spacing retained");
        for(int tick=0;tick<100;++tick) {
            std::vector<sim::AuraAction> actions(4);
            for(std::size_t i=0;i<4;++i) { actions[i].leftHipTarget=.02f*float(i); actions[i].rightHipTarget=-.01f*float(i); }
            parallel.step(actions); serial.step(actions);
            for(std::size_t i=0;i<4;++i) same(parallel.agent(i),serial.agent(i));
        }
        sim::AuraArena collision(2,false);
        for(std::size_t i=0;i<2;++i) {
            auto &agent=collision.agent(i);
            const math::Vec3 delta{(i==0?-.25f:.25f)-agent.pelvisPosition().x,0,-agent.pelvisPosition().z};
            for(auto *part:agent.getParts()) { part->body.position+=delta; part->body.velocity={i==0?1.f:-1.f,0,0}; }
        }
        const auto before=penetration(collision); const auto oldMomentum=momentum(collision);
        const auto gapsA=gaps(collision.agent(0)), gapsB=gaps(collision.agent(1));
        collision.resolveContacts();
        const auto after=penetration(collision);
        check(before>.1f && after<before*.01f, "inter-agent shapes separate");
        check(collision.contacts()>0, "real inter-agent contacts counted");
        check((momentum(collision)-oldMomentum).length()<.001f, "contact impulses conserve total momentum");
        for(std::size_t i=0;i<15;++i) {
            check(std::abs(gapsA[i]-gaps(collision.agent(0))[i])<1e-5f, "whole-agent projection preserves first anchors");
            check(std::abs(gapsB[i]-gaps(collision.agent(1))[i])<1e-5f, "whole-agent projection preserves second anchors");
        }
        collision.reset(1);
        check(collision.active(0) && !collision.active(1) && collision.contacts()==0, "wave reset and inactive slots");
        std::cout<<"Arena passed: exact solo replay, deterministic parallel stepping, contacts "<<before<<" -> "<<after<<", anchor preservation and momentum.\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
