#include "ControlScreen.hpp"
#include "ScreenCanvas.hpp"
#include <cassert>
#include <clocale>
#include <fstream>
#include <iostream>
#include <regex>

namespace {
std::string plain(const std::string& line) {
    static const std::regex escapes("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])");
    return std::regex_replace(line,escapes,"");
}
}
int main(int argc,char** argv) {
    std::setlocale(LC_CTYPE,"");
    aura::cli::JointControls controls{};
    for (std::size_t i=0;i<aura::sim::actionSpecs.size();++i) {
        const auto &spec=aura::sim::actionSpecs[i];
        controls[i]={spec.label,0.0,spec.minimum,spec.maximum};
    }
    controls[0].value=0.15;
    controls[1].value=-0.10;
    controls[2].value=0.25;
    controls[3].value=0.30;
    aura::cli::ControlObservation observation;
    observation.age_seconds=0.03;
    observation.pelvis_height=3.4;
    observation.pelvis_uprightness=0.98;
    observation.is_standing=true;
    observation.has_motion=true;
    observation.simulation_time=8.5;
    observation.steps=1020;
    observation.joint_angles={0.123,-0.08,0.2,0.28};
    // Static schematic fixture; this does not claim physical stability.
    const std::array<std::array<double,3>,16> positions{{
        {0,3,0},{0,3.6,0},{0,4.1,0},{0,4.5,0},{0,4.9,0},
        {-.7,4,0},{.7,4,0},{-1,3.3,0},{1,3.3,0},{-1,2.8,0},{1,2.8,0},
        {-.3,2.4,0},{.3,2.4,0},{-.3,1.4,0},{.3,1.4,0},{0,.6,0}}};
    for (const auto& position:positions)
        observation.body_pose.push_back({position[0],position[1],position[2],1,0,0,0,.45,.7,.4});
    auto render=[&](int width,int height,int mode,bool editing=false) {
        std::optional<aura::cli::ControlObservation> data=observation;
        if (mode==0) data.reset();
        if (mode==2) data->age_seconds=2.0;
        return aura::cli::control_frame(controls,data,0,.05,editing,"-0.125",
            "Targets applied to viewer.",false,.5,width,height);
    };
    for (int width:{1,20,59,75,76,80,112,120,160,220})
        for (int height:{1,8,16,17,24,25,28,34,40,60})
            for (int mode=0;mode<3;++mode) for (bool editing:{false,true}) {
                const auto frame=render(width,height,mode,editing);
                assert(static_cast<int>(frame.size())==height);
                for (const auto& row:frame) assert(aura::cli::screen::textCells(plain(row))<=width);
            }
    for (int mode=0;mode<3;++mode) {
        std::string frame;
        for (const auto& row:render(120,40,mode)) frame+=plain(row)+'\n';
        assert((frame.find("+0.123")!=std::string::npos)==(mode==1));
        assert(frame.find("+0.150")!=std::string::npos);
        if (mode==2) assert(frame.find("STALE")!=std::string::npos);
    }
    const auto shortFrame=render(80,17,1);
    assert(plain(shortFrame[11]).find("Targets applied")==std::string::npos);
    assert(plain(shortFrame[12]).find("Targets applied")!=std::string::npos);
    std::string lastJointFrame;
    for (const auto &row : aura::cli::control_frame(controls,observation,controls.size()-1,.05,
            false,"","Ready",false,.5,80,17)) lastJointFrame+=plain(row)+'\n';
    assert(lastJointFrame.find("Right wrist")!=std::string::npos);
    assert(lastJointFrame.find("15 / 15")!=std::string::npos);
    assert(lastJointFrame.find("1-9 quick select")!=std::string::npos);
    assert(lastJointFrame.find("arrows reach all 15 joints")!=std::string::npos);
    observation.has_motor_states=true;
    observation.motor_enabled.fill(true);
    std::size_t head=0;
    for (std::size_t i=0;i<aura::sim::actionSpecs.size();++i)
        if (std::string(aura::sim::actionSpecs[i].jointName)=="head") head=i;
    observation.motor_enabled[head]=false;
    std::string motorFrame;
    for (const auto& row : aura::cli::control_frame(controls,observation,head,.05,
            false,"","Ready",false,.5,120,40)) motorFrame+=plain(row)+'\n';
    assert(motorFrame.find("MOTOR OFF")!=std::string::npos);
    assert(motorFrame.find("14/15 motors active")!=std::string::npos);
    if (argc==2) {
        std::ofstream out(argv[1]);
        for (const auto& row:render(120,40,1)) out<<row<<'\n';
    }
    std::cout<<"Control layout, resize, editing, and missing/stale telemetry checks passed.\n";
}
