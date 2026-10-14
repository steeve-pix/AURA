#include "ModelsScreen.hpp"
#include "TrainingSetup.hpp"
#include "Commands.hpp"
#include "TerminalCapabilities.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <clocale>
#include <unistd.h>

int main(int argc,char** argv) {
    std::setlocale(LC_ALL,"");
    const auto root=std::filesystem::temp_directory_path()/ ("aura-model-tests-"+std::to_string(getpid()));
    std::filesystem::create_directories(root);
    std::string error;
    assert(aura::cli::saved_models(root / "missing",error).empty() && error.empty());
    std::ofstream(root / "b.pt") << "weights";
    std::ofstream(root / "a.pt") << "model";
    std::ofstream(root / "ignore.tmp") << "partial";
    std::filesystem::create_directory(root / "directory.pt");
    auto models=aura::cli::saved_models(root,error);
    assert(error.empty() && models.size()==2);
    assert(models[0].path.filename()=="a.pt" && models[0].bytes==5);
    assert(models[0].modified==std::filesystem::last_write_time(root / "a.pt"));
    assert(aura::cli::model_size(0)=="0 B");
    assert(aura::cli::model_size(1024)=="1.0 KiB");
    assert(aura::cli::model_size(3*1024*1024)=="3.0 MiB");
    assert(aura::cli::model_updated(models[0].modified).size()==16);
    assert(aura::cli::model_updated(models[0].modified,true).size()==10);
    for (int width : {1,30,50,80,120,180}) for (int height : {1,8,13,24,40}) {
        const auto frame=aura::cli::models_frame(models,1,"",width,height);
        assert(frame.size()==static_cast<std::size_t>(height));
        const auto center=aura::cli::models_frame(models,1,"",width,height,"center");
        assert(center.size()==static_cast<std::size_t>(height));
        for (const auto& stage : {"mode","critic_source","name","confirm"}) {
            const auto setup=aura::cli::training_setup_frame(0,true,stage,"balance",width,height);
            assert(setup.size()==static_cast<std::size_t>(height));
        }
    }
    std::string criticSetup;
    for (const auto& row:aura::cli::training_setup_frame(3,false,"mode","",120,24)) criticSetup+=row;
    assert(criticSetup.find("4  Critic only")!=std::string::npos);
    criticSetup.clear();
    for (const auto& row:aura::cli::training_setup_frame(0,false,"critic_source","",120,24)) criticSetup+=row;
    assert(criticSetup.find("FROZEN ACTOR SOURCE")!=std::string::npos);
    assert(criticSetup.find("32 training / 16 validation episodes / 20 critic updates")!=std::string::npos);
    aura::cli::apply_model_catalog(models,
        "AURA_MODEL\t612e7074\tcompatible\t205\t14\tCurrent contracts\n"
        "AURA_MODEL\t622e7074\tincompatible\t205\t15\t15 actions versus 14 actions\n"
        "AURA_MODEL_INFO\t612e7074\t32\t1\t377\n");
    std::string center;
    for (const auto& row : aura::cli::center_models_frame(models,0,"",120,24,{"Finished / Evaluation / a.pt", "377 steps"})) center += row + '\n';
    assert(center.find("MODEL LOG / current session")!=std::string::npos);
    assert(center.find(aura::cli::terminal_capabilities().utf8?"⚠ incompatible":"! incompatible")!=std::string::npos);
    assert(center.find("Finished / Evaluation / a.pt")!=std::string::npos);
    assert(center.find("377 steps")!=std::string::npos);
    assert(aura::cli::find_command("/train")->id==aura::cli::CommandId::train_nn);
    assert(aura::cli::find_command("train")->id==aura::cli::CommandId::train_nn);
    assert(aura::cli::find_command("/train_nn")->id==aura::cli::CommandId::train_nn);
    assert(aura::cli::find_command("/center")->id==aura::cli::CommandId::center);
    assert(aura::cli::find_command("/models")->id==aura::cli::CommandId::center);
    for (const auto* name : {"/exit", "exit", "/quit", "quit"})
        assert(aura::cli::find_command(name)==aura::cli::find_command("/exit"));
    for (const auto* prefix : {"q", "qu", "qui", "quit", "/q", "/qu", "/qui", "/quit"})
        assert(aura::cli::quit_alias_prefix(prefix));
    for (const auto* other : {"", "/", "queue", "/quitter"})
        assert(!aura::cli::quit_alias_prefix(other));
    int exitCommands=0;
    for (const auto& command:aura::cli::commands) {
        assert(command.name!="/models" && command.name!="/evaluate" && command.name!="/train_nn");
        assert(command.name!="/quit");
        if (command.id==aura::cli::CommandId::exit) ++exitCommands;
    }
    assert(exitCommands==1);
    for (const auto* name : {"/queue", "queue", "/unqueue", "unqueue"})
        assert(aura::cli::find_command(name)==nullptr);
    assert(center.find("COMMAND CENTER")!=std::string::npos);
    assert(center.find("1 Model library")!=std::string::npos);
    assert(center.find("2 Model logs")!=std::string::npos);
    assert(center.find("Latest + Best grouped by model")==std::string::npos);
    assert(models[0].episodes==32 && models[0].updates==1 && models[0].bestEvaluation==377);
    assert(center.find("Saved best: 377 steps")!=std::string::npos);
    assert(center.find("[E] Evaluate")!=std::string::npos);
    assert(center.find("[T] Resume")!=std::string::npos);
    assert(!aura::cli::model_selectable(models[1],"center"));
    assert(models[0].inputs==205 && models[0].outputs==14);
    assert(aura::cli::model_selectable(models[0],"resume"));
    for (const auto& action : {"resume","watch","evaluate"})
        assert(!aura::cli::model_selectable(models[1],action));
    assert(aura::cli::model_selectable(models[1],"restart"));
    if (argc==2) {
        std::ofstream out(argv[1]);
        for (const auto& line : aura::cli::models_frame(models,1,"",120,24)) out << line << '\n';
        std::ofstream setup(std::string(argv[1])+"-setup.ansi");
        for (const auto& line : aura::cli::training_setup_frame(0,true,"mode","",120,24)) setup << line << '\n';
    }
    const auto families=root / "families";
    std::filesystem::create_directory(families);
    for (const auto* name:{"balance_extra.pt","balance_best.pt","balance.pt","orphan_best.pt","best_effort.pt"})
        std::ofstream(families / name)<<"checkpoint";
    const auto grouped=aura::cli::saved_models(families,error);
    assert(grouped[0].path.filename()=="balance.pt");
    assert(grouped[1].path.filename()=="balance_best.pt");
    assert(grouped[2].path.filename()=="balance_extra.pt");
    assert(aura::cli::best_model_index(grouped,0)==1);
    assert(aura::cli::best_model_index(grouped,1)==1);
    assert(!aura::cli::best_model_index(grouped,2));
    assert(!aura::cli::best_model_index(grouped,99));
    assert(!aura::cli::model_is_best(grouped[3]));
    assert(aura::cli::model_is_best(grouped[4]));
    assert(aura::cli::filter_models(grouped,"").size()==grouped.size());
    assert(aura::cli::filter_models(grouped,"   ").size()==grouped.size());
    const auto bestMatches=aura::cli::filter_models(grouped,"BALANCE best");
    assert(bestMatches.size()==1 && bestMatches[0].path.filename()=="balance_best.pt");
    const auto latestOnly=aura::cli::filter_models(grouped,"balance.pt");
    assert(latestOnly.size()==1 && latestOnly[0].hasBestCopy);
    assert(aura::cli::filter_models(grouped,"not-a-model").empty());
    assert(aura::cli::filter_models(grouped,"balance")[1].path.filename()=="balance_best.pt");
    std::string searchFrame;
    for (const auto& row:aura::cli::center_models_frame({},0,"",120,24,{},"missing",true,grouped.size())) searchFrame+=row;
    assert(searchFrame.find("CHECKPOINT LIBRARY")==std::string::npos);
    assert(searchFrame.find("Search  missing")!=std::string::npos);
    assert(searchFrame.find("No matching models")!=std::string::npos);
    assert(searchFrame.find("0 / 5 checkpoints")!=std::string::npos);
    assert(searchFrame.find("[Esc] Cancel search")!=std::string::npos);
    std::string groupedFrame;
    for (const auto& row:aura::cli::center_models_frame(grouped,0,"",140,30,{})) groupedFrame+=row;
    assert(groupedFrame.find("Latest")!=std::string::npos);
    assert(groupedFrame.find("Best")!=std::string::npos);
    assert(groupedFrame.find("[B] Best")!=std::string::npos);
    std::filesystem::remove(root / "a.pt");
    models=aura::cli::saved_models(root,error);
    assert(models.size()==1 && models[0].path.filename()=="b.pt");
    std::filesystem::remove_all(root);
}
