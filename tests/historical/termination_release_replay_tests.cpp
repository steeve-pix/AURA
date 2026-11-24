// Strict numerical reference: Step 128 neutral Release trace, Apple Clang 21 / arm64.
// Behavior independent of this numerical reference is tested in termination_tests.cpp.
#include "aura/sim/AuraSimulation.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace aura;
namespace {
void check(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::vector<std::string> fields(const std::string& line) {
    std::istringstream input(line);std::vector<std::string> result;
    for (std::string value;std::getline(input,value,',');) result.push_back(value);
    return result;
}
void hashValue(std::uint64_t& hash,double value) {
    check(std::isfinite(value),"nonfinite physical state");
    const auto bits=std::bit_cast<std::uint64_t>(value);
    for (int i=0;i<8;++i) { hash^=(bits>>(i*8))&255;hash*=1099511628211ULL; }
}
void hashState(std::uint64_t& hash,sim::AuraSimulation& simulation) {
    for (const auto* part:simulation.getParts()) {
        const auto& b=part->body;
        for (const auto& v:{b.position,b.velocity,b.angularVelocity,b.acceleration,
                           b.angularAcceleration,b.force,b.torque})
            for (auto x:{v.x,v.y,v.z}) hashValue(hash,x);
        for (auto q:{b.orientation.w,b.orientation.x,b.orientation.y,b.orientation.z}) hashValue(hash,q);
    }
}
}
int main(int argc,char** argv) {
#ifndef AURA_VALIDATED_RELEASE
    std::cerr<<"Historical termination replay requires the validated Release configuration; no replay ran.\n";
    return 77;
#else
    try {
        check(argc==2,"Usage: release replay ARCHIVED_NEUTRAL_CSV");
        std::ifstream archive(argv[1]);check(archive.good(),"Archived neutral trace unavailable");
        std::string line;check(static_cast<bool>(std::getline(archive,line)),"Missing trace header");
        const auto header=fields(line);std::map<std::string,std::size_t> columns;
        for (std::size_t i=0;i<header.size();++i) columns.emplace(header[i],i);
        sim::AuraSimulation simulation({}, sim::TerminationMode::BalanceV2,
                                       physics::FloorFrictionModel::LegacyVelocityDamping);
        int firstLegacy=-1,firstV2=-1;
        std::uint64_t digest=14695981039346656037ULL;
        for (int step=0;step<=642;++step) {
            check(static_cast<bool>(std::getline(archive,line)),"Missing archived step "+std::to_string(step));
            const auto row=fields(line);
            const auto saved=[&](const std::string& key)->const std::string& {
                return row.at(columns.at(key));
            };
            const auto obs=simulation.observation();const auto& assessment=obs.balance;
            const auto part=[&](const std::string& name)->const sim::BodyPartObservation& {
                const auto it=std::find_if(obs.bodyPartStates.begin(),obs.bodyPartStates.end(),
                    [&](const auto& p){return p.name==name;});
                check(it!=obs.bodyPartStates.end(),"Missing body part "+name);return *it;
            };
            const auto& torso=part("torso");const auto& head=part("head");
            const auto end=simulation.terminationStatus();
            if (!simulation.isStanding() && firstLegacy<0) firstLegacy=step;
            if (end.terminated && firstV2<0) firstV2=step;
            const std::map<std::string,double> measurements{
                // The archive emits tick * dt, rather than accumulated clock addition.
                {"step",step},{"time",step*(1./120.)},{"legacy_standing",simulation.isStanding()},
                {"pelvis_height",obs.pelvisPosition.y},{"pelvis_up",obs.pelvisUp.y},
                {"torso_height",torso.position.y},{"torso_up",torso.up.y},
                {"head_height",head.position.y},{"com_height",obs.centerOfMassPosition.y},
                {"com_vx",obs.centerOfMassVelocity.x},{"com_vy",obs.centerOfMassVelocity.y},{"com_vz",obs.centerOfMassVelocity.z},
                {"left_foot",obs.leftFootGrounded},{"right_foot",obs.rightFootGrounded},
                {"support_vertices",obs.supportPolygon.vertices.size()},{"support_margin",obs.supportMargin},
                {"enter_envelope",assessment.enterEnvelope},{"stay_envelope",assessment.stayEnvelope},
                {"deep_collapse",assessment.deepCollapse},{"contact_collapse",assessment.contactCollapse},
                {"dangerous_motion",assessment.dangerousMotion},{"enter_timer",assessment.enteringDuration},
                {"outside_timer",assessment.outsideDuration},{"collapse_timer",assessment.collapseDuration},
                {"torso_contact",!torso.geometricFloorContactPoints.empty()},{"head_contact",!head.geometricFloorContactPoints.empty()},
                {"neutral_pelvis",obs.neutralHeightReferences[0]},{"neutral_torso",obs.neutralHeightReferences[1]},
                {"neutral_com",obs.neutralHeightReferences[2]},{"terminated",end.terminated},{"truncated",end.truncated}};
            for (const auto& [key,value]:measurements) {
                check(value==std::stod(saved(key)),"Archived "+key+" differs at step "+std::to_string(step));
            }
            check(saved("balance_state")==sim::balanceStateName(assessment.state),"Archived classifier state differs");
            check(saved("termination_reason")==end.reason,"Archived termination reason differs");
            hashState(digest,simulation);
            if (step<642) simulation.step();
        }
        check(!std::getline(archive,line),"Unexpected extra archived steps");
        check(firstLegacy==462 && firstV2==516,"Release endpoints must remain exactly 462 / 516");
        check(digest==0xefa666e369d7f1e3ULL,"Release physical-state digest differs from Step 128");
        std::cout<<"Strict Release replay passed: legacy=462 V2=516; 643 archived samples exact; physical digest="
                 <<std::hex<<digest<<'\n';
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
#endif
}
