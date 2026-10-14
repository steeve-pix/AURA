// Runs the production input handlers against memory-only controls and a fake worker.
#include "Terminal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc,char** argv) {
    using namespace aura::cli;
    if (argc<2) return 2;
    const std::string mode=argv[1];
    if (mode=="evaluation-worker" || mode=="critic-worker") {
        if (mode=="evaluation-worker") std::cout<<"AURA_EVAL\tstart\tshortcut-fixture.pt\t0\t0\t1200"<<std::endl;
        else std::cout<<"AURA_CRITIC\tstart\tshortcut-fixture.pt\t205\t14\t32\t16\t20\t43\t0.001\t0.995\t100"<<std::endl;
        for (std::string command;std::getline(std::cin,command);)
            if (command=="quit") return 0;
        return 0;
    }
    if (mode=="worker") {
        const auto emit=[](const std::string& line) { std::cout<<"AURA_NN\t"<<line<<std::endl; };
        emit("model\tshortcut-fixture.pt\tnew\t0\t0");
        emit("start\t8\t1200\t42\t100");
        emit("tensor\t0\tweight\t2x2\t4\tweight");
        emit("tensor\t1\tbias\t2\t2\tbias");
        emit("phase\tready");
        bool paused=false,visual=false;
        for (std::string command;std::getline(std::cin,command);) {
            if (command=="quit") break;
            if (command=="pause") { paused=!paused; emit(std::string("paused\t")+(paused?"1":"0")); }
            else if (command=="visual") { visual=!visual; emit(std::string("viewer\t")+(visual?"1":"0")); }
            else if (command=="next") { emit("phase\tcollecting"); }
            else if (command.starts_with("inspect ")) {
                // No tensor mutation: only exercise command delivery to the worker.
                emit("viewer_error\tInspection received: "+command);
            }
        }
        return 0;
    }
    bool home=false;
    std::optional<std::string> captured;
    {
        TerminalSession terminal;
        if (mode=="prompt") {
            print_header();
            captured=read_prompt();
        } else if (mode=="control" && argc==3) {
            JointControls controls{};
            for (std::size_t i=0;i<controls.size();++i) {
                const auto& spec=aura::sim::actionSpecs[i];
                controls[i]={spec.label,0,spec.minimum,spec.maximum,false};
            }
            edit_joint_controls(controls,[&](const JointControls& updated) {
                std::ofstream out(argv[2],std::ios::app);
                for (const auto& joint:updated) out<<joint.value<<' ';
                out<<'\n';
                return std::string{};
            },[]{return std::optional<ControlObservation>{};});
        } else if (mode=="training" || mode=="evaluation" || mode=="critic") {
            const auto self=std::filesystem::absolute(argv[0]).string();
            // Test binary paths are controlled by this repository, not user input.
            if (mode=="training") run_nn_training("'"+self+"' worker");
            else if (mode=="evaluation") run_evaluation("'"+self+"' evaluation-worker");
            else run_critic_training("'"+self+"' critic-worker");
        } else if (mode=="setup" && argc==3) {
            select_training(argv[2]);
        } else return 2;
        home=returning_home();
    }
    if (mode=="prompt") std::cout<<"DRIVER_INPUT "<<(captured?captured->empty()?"<EMPTY>":*captured:"<EXIT>")<<std::endl;
    std::cout<<"DRIVER_RETURN "<<(home?"HOME":"BACK")<<std::endl;
}
