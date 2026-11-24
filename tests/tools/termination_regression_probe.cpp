// Step 132 read-only diagnostic: same default setup and paired stepping as
// termination_tests.cpp. Continue raw stepping past termination to compare modes.
#include "aura/sim/AuraSimulation.hpp"
#include "../../src/app/SimulationStep.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace aura;
namespace {
void hashValue(std::uint64_t& hash, double value) {
    if (!std::isfinite(value)) throw std::runtime_error("Nonfinite physical state");
    const auto bits=std::bit_cast<std::uint64_t>(value);
    for (int i=0;i<8;++i) { hash^=(bits>>(i*8))&255; hash*=1099511628211ULL; }
}
void hashState(std::uint64_t& hash, sim::AuraSimulation& s) {
    for (auto* part:s.getParts()) {
        const auto& b=part->body;
        for (const auto& v:{b.position,b.velocity,b.angularVelocity,b.acceleration,
                           b.angularAcceleration,b.force,b.torque})
            for (auto x:{v.x,v.y,v.z}) hashValue(hash,x);
        for (auto q:{b.orientation.w,b.orientation.x,b.orientation.y,b.orientation.z}) hashValue(hash,q);
    }
}
}
int main(int argc,char** argv) {
    try {
        if (argc<2 || argc>3) throw std::runtime_error("Usage: probe NEW_OUTPUT_DIRECTORY [REPEATS]");
        const int repeats=argc==3?std::stoi(argv[2]):3;
        if (repeats<1) throw std::runtime_error("REPEATS must be positive");
        const std::filesystem::path directory=argv[1];
        if (!std::filesystem::create_directory(directory)) throw std::runtime_error("Output already exists");
        for (int repeat=0;repeat<repeats;++repeat) {
            sim::AuraSimulation legacy({}, sim::TerminationMode::Legacy,
                                       physics::FloorFrictionModel::LegacyVelocityDamping),
                    v2({}, sim::TerminationMode::BalanceV2,
                       physics::FloorFrictionModel::LegacyVelocityDamping);
            if (!repeat) {
                std::ofstream config(directory/"configuration.txt");
                config<<std::setprecision(17)<<"dt "<<app::FIXED_DT<<"\niterations "<<app::jointIterations
                      <<"\npairs "<<legacy.getBodyPairs().size()<<'\n';
                for (const auto& j:legacy.getJoints()) config<<j.name<<' '<<j.motor<<' '
                    <<j.constraint.motorStiffness<<' '<<j.constraint.motorDamping<<' '
                    <<j.constraint.targetAngle<<' '<<j.constraint.targetSwingY<<' '
                    <<j.constraint.targetSwingZ<<'\n';
            }
            std::ofstream csv(directory/("repeat-"+std::to_string(repeat)+".csv"));
            csv<<std::setprecision(17)
               <<"step,balance_state,legacy_terminated,v2_terminated,pelvis_height,pelvis_up,torso_height,torso_up,com_height,com_vy,left_foot,right_foot,support_vertices,support_margin,deep_collapse,contact_collapse,collapse_timer,physical_digest,cumulative_digest\n";
            std::uint64_t cumulative=14695981039346656037ULL;
            for (int step=0;step<=642;++step) {
                std::uint64_t a=14695981039346656037ULL,b=a;
                hashState(a,legacy);hashState(b,v2);hashState(cumulative,legacy);
                if (a!=b) throw std::runtime_error("Termination modes changed physical trajectory");
                const auto obs=legacy.observation();
                const auto& torso=*std::find_if(obs.bodyPartStates.begin(),obs.bodyPartStates.end(),
                    [](const auto& p){return p.name=="torso";});
                csv<<step<<','<<sim::balanceStateName(obs.balance.state)<<','
                   <<legacy.terminationStatus().terminated<<','<<v2.terminationStatus().terminated<<','
                   <<obs.pelvisPosition.y<<','<<obs.pelvisUp.y<<','<<torso.position.y<<','<<torso.up.y<<','
                   <<obs.centerOfMassPosition.y<<','<<obs.centerOfMassVelocity.y<<','
                   <<obs.leftFootGrounded<<','<<obs.rightFootGrounded<<','<<obs.supportPolygon.vertices.size()<<','
                   <<obs.supportMargin<<','<<obs.balance.deepCollapse<<','<<obs.balance.contactCollapse<<','
                   <<obs.balance.collapseDuration<<','<<std::hex<<a<<','<<cumulative<<std::dec<<'\n';
                if (step<642) { legacy.step();v2.step(); }
            }
        }
        std::cout<<repeats<<" paired neutral replays complete; both modes physically identical at every tick.\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
