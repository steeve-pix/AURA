#include "screens/ModelsScreen.hpp"
#include "screens/TrainingSetup.hpp"
#include "app/Commands.hpp"
#include "terminal/TerminalCapabilities.hpp"
#include "ui/ScreenCanvas.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <clocale>
#include <unistd.h>

int main(int argc, char **argv) {
    std::setlocale(LC_ALL, "");
    assert(aura::cli::training_settings("16 3 4", true) == (std::array<int,3>{16,3,4}));
    assert(aura::cli::training_settings("16 3", false) == (std::array<int,3>{16,3,0}));
    assert(aura::cli::training_settings("64|5|2", true) == (std::array<int,3>{64,5,2}));
    assert(!aura::cli::training_settings("64||", true));
    assert(aura::cli::training_settings("7 2 5",true) == (std::array<int,3>{7,2,5}));
    assert(aura::cli::training_settings("32|3|6",false) == (std::array<int,3>{32,3,6}));
    for (const auto *invalid: {"7 2 0", "128 0 4", "8 2 0", "8 2", "8.5 2 4", "8 2 4 extra"})
        assert(!aura::cli::training_settings(invalid, true));
    assert(aura::cli::find_command("/create")->id == aura::cli::CommandId::create);
    assert(aura::cli::find_command("/experiment")->id == aura::cli::CommandId::experiment);
    assert(aura::cli::find_command("/experiments")->id == aura::cli::CommandId::experiment);
    const auto hints = aura::cli::screen::shortcut_runs(
        "Up/Down Choose   Tab Filter   [Enter] Output   Q Back / Esc Home");
    std::string hintText;
    int highlightedKeys = 0;
    for (const auto &[run,key]: hints) {
        hintText += run;
        highlightedKeys += key;
    }
    assert(highlightedKeys == 5);
    assert(hintText == "[Up/Down] Choose   [Tab] Filter   [Enter] Output   [Q] Back / [Esc] Home");
    assert(aura::cli::screen::shortcut_width("Q Back") == 8);
    aura::cli::screen::Canvas narrowHints(4, 1);
    narrowHints.shortcuts(0, 0, "Enter Apply", aura::cli::screen::Palette{}, 4);
    assert(narrowHints.lines()[0].find('[') == std::string::npos); // Don't cut a key label in half.
    for(int width:{60,100,160}) {
        aura::cli::screen::Canvas tabs(width,1);
        aura::cli::center_tabs(tabs,0,width,2);
        const std::string line=std::regex_replace(tabs.lines()[0],std::regex("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])"),"");
        const std::array<std::string,3> labels{"[1] Model library","[2] Experiments","[3] Designs"};
        for(int i=0;i<2;++i)assert(line.find(labels[i+1])-line.find(labels[i])-labels[i].size()==4);
    }
    for (int width : {60,100,160}) {
        aura::cli::screen::Canvas footer(width,24);
        const int divider = footer.actionFooter(2,width-4,{
            {"Enter Inspect"},{"R Refresh"},{"Q Back"},{"Esc Home"},{"[D] Delete",true,true}
        },"Status stays below controls",aura::cli::screen::Palette{});
        const auto &lines = footer.lines();
        assert(lines[22].find("Delete") != std::string::npos);
        assert(lines[23].find("Status stays below controls") != std::string::npos);
        assert(divider < 22 && !lines[divider].empty());
    }
    const auto saved=aura::cli::parse_saved_catalog("AURA_SAVED\t2f6d6f64656c732f6578706572696d656e74732f72756e\texperiment\t7374756479\t6472616674\nAURA_SAVED_DETAIL\t2f6d6f64656c732f6578706572696d656e74732f72756e\t53747564793a20626173656c696e65\n");
    assert(saved.size()==1 && saved[0].name=="study" && saved[0].details[0]=="Study: baseline");
    for(int width:{60,100,160})for(int height:{20,40,60}) {
        std::string frame;
        for(const auto &line:aura::cli::center_saved_frame(saved,0,1,width,height,"",false,0,true))frame+=std::regex_replace(line,std::regex("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])"),"")+'\n';
        assert(frame.find("study")!=std::string::npos);
        assert(frame.find("[D] Delete")!=std::string::npos);
        assert(frame.rfind("[D] Delete")>frame.rfind("[Esc] Home"));
        assert(frame.find("[U]")!=std::string::npos);
    }
    const std::vector<aura::cli::SavedEntry> styledEntries{{"/models/study","experiment","study","draft",{
        "Study: baseline","Status: draft","Actor: [64, 64] / Tanh","Critic: [64, 64] / Tanh",
        "Evaluation seeds: [10042, 10043, 10044]","Location: /project/cli/../models/study"
    }}};
    for (int width : {60,100,160}) for (bool inspect : {false,true}) {
        // At narrow widths details are explicitly opened with Enter.
        if(width<100 && !inspect) continue;
        std::string raw, plain;
        for(const auto &line:aura::cli::center_saved_frame(styledEntries,0,1,width,40,"",inspect,0,false))raw+=line+'\n';
        plain=std::regex_replace(raw,std::regex("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])"),"");
        assert(plain.find("Actor:")!=std::string::npos);
        assert(plain.find("Evaluation seeds:")!=std::string::npos);
        assert(plain.find("cli/..") == std::string::npos);
        const aura::cli::screen::Palette palette;
        assert(raw.find(palette.purple)!=std::string::npos);
        assert(raw.find(palette.gold)!=std::string::npos);
    }
    for(int tab : {1,2}) for(int width : {60,100,160}) {
        const auto frame=aura::cli::center_saved_frame(styledEntries,0,tab,width,40,"",true,0,false);
        assert(frame[6].find(tab==1?"EXPERIMENT DETAILS":"DESIGN DETAILS")!=std::string::npos);
        assert(frame[8].find("study")!=std::string::npos);
        assert(frame[9].find("Study:")!=std::string::npos);
    }
    const auto root = std::filesystem::temp_directory_path() / ("aura-model-tests-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    std::string error;
    assert(aura::cli::saved_models(root / "missing", error).empty() && error.empty());
    std::ofstream(root / "b.pt") << "weights";
    std::ofstream(root / "a.pt") << "model";
    std::ofstream(root / "ignore.tmp") << "partial";
    std::filesystem::create_directory(root / "directory.pt");
    auto models = aura::cli::saved_models(root, error);
    assert(error.empty() && models.size() == 2);
    assert(models[0].path.filename() == "a.pt" && models[0].bytes == 5);
    assert(models[0].modified == std::filesystem::last_write_time(root / "a.pt"));
    assert(aura::cli::model_size(0) == "0 B");
    assert(aura::cli::model_size(1024) == "1.0 KiB");
    assert(aura::cli::model_size(3 * 1024 * 1024) == "3.0 MiB");
    assert(aura::cli::model_updated(models[0].modified).size() == 16);
    assert(aura::cli::model_updated(models[0].modified, true).size() == 10);
    for (int width: {1, 30, 50, 80, 120, 180})
        for (int height: {1, 8, 13, 24, 40}) {
            const auto frame = aura::cli::models_frame(models, 1, "", width, height);
            assert(frame.size() == static_cast<std::size_t>(height));
            const auto center = aura::cli::models_frame(models, 1, "", width, height, "center");
            assert(center.size() == static_cast<std::size_t>(height));
            for (const auto &stage: {"mode", "critic_source", "name", "confirm", "settings", "saved_settings"}) {
                const auto setup = aura::cli::training_setup_frame(0, true, stage, "balance", width, height);
                assert(setup.size() == static_cast<std::size_t>(height));
            }
        }
    std::string criticSetup;
    for (const auto &row: aura::cli::training_setup_frame(3, false, "mode", "", 120, 24)) criticSetup += row;
    assert(criticSetup.find("[4] Critic only") != std::string::npos);
    criticSetup.clear();
    for (const auto &row: aura::cli::training_setup_frame(0, false, "critic_source", "", 120, 24)) criticSetup += row;
    assert(criticSetup.find("FROZEN ACTOR SOURCE") != std::string::npos);
    assert(criticSetup.find("32 training / 16 validation episodes / 20 critic updates") != std::string::npos);
    std::string gpuSetup;
    for (const auto &row: aura::cli::training_setup_frame(0, true, "settings", "32 3 4", 120, 24,
                                                           true, 7, "gpu")) gpuSetup += row;
    assert(gpuSetup.find("Compute device") != std::string::npos);
    assert(gpuSetup.find("GPU / Apple Metal or CUDA") != std::string::npos);
    assert(gpuSetup.find("[G] CPU/GPU") != std::string::npos);
    assert(gpuSetup.find("[Tab] next input") != std::string::npos);
    assert(gpuSetup.find("Left/Right") == std::string::npos);
    aura::cli::apply_model_catalog(models,
                                   "AURA_MODEL\t612e7074\tcompatible\t205\t14\tCurrent contracts\n"
                                   "AURA_MODEL\t622e7074\tincompatible\t205\t15\t15 actions versus 14 actions\n"
                                   "AURA_MODEL_INFO\t612e7074\t32\t1\t377\n");
    std::string center;
    for (const auto &row: aura::cli::center_models_frame(models, 0, "", 120, 24)) center += row + '\n';
    assert(center.find("MODEL LOG") == std::string::npos);
    assert(
        center.find(aura::cli::terminal_capabilities().utf8
                        ? "⚠ incompatible"
                        : "! incompatible") != std::string::npos);
    assert(center.find("[3] Designs") != std::string::npos);
    assert(center.find("377 steps") != std::string::npos);
    assert(aura::cli::find_command("/train")->id == aura::cli::CommandId::train_nn);
    assert(aura::cli::find_command("train")->id == aura::cli::CommandId::train_nn);
    assert(aura::cli::find_command("/train_nn")->id == aura::cli::CommandId::train_nn);
    assert(aura::cli::find_command("/center")->id == aura::cli::CommandId::center);
    assert(aura::cli::find_command("/models")->id == aura::cli::CommandId::center);
    for (const auto *name: {"/exit", "exit", "/quit", "quit"})
        assert(aura::cli::find_command(name) == aura::cli::find_command("/exit"));
    for (const auto *prefix: {"q", "qu", "qui", "quit", "/q", "/qu", "/qui", "/quit"})
        assert(aura::cli::quit_alias_prefix(prefix));
    for (const auto *other: {"", "/", "queue", "/quitter"})
        assert(!aura::cli::quit_alias_prefix(other));
    int exitCommands = 0;
    for (const auto &command: aura::cli::commands) {
        assert(command.name != "/models" && command.name != "/evaluate" && command.name != "/train_nn");
        assert(command.name != "/quit");
        if (command.id == aura::cli::CommandId::exit) ++exitCommands;
    }
    assert(exitCommands == 1);
    for (const auto *name: {"/queue", "queue", "/unqueue", "unqueue"})
        assert(aura::cli::find_command(name) == nullptr);
    assert(center.find("COMMAND CENTER") != std::string::npos);
    assert(center.find("[1] Model library") != std::string::npos);
    assert(center.find("[2] Experiments") != std::string::npos);
    assert(center.find("Latest + Best grouped by model") == std::string::npos);
    assert(models[0].episodes == 32 && models[0].updates == 1 && models[0].bestEvaluation == 377);
    assert(center.find("Saved best survival: 377 steps") != std::string::npos);
    aura::cli::apply_model_catalog(models, "AURA_MODEL_BALANCE\t612e7074\t200\t1.667\n");
    assert(models[0].bestBalanced == 200);
    std::string balancedLibrary;
    for (const auto &row: aura::cli::center_models_frame(models, 0, "", 160, 30)) balancedLibrary += row;
    assert(balancedLibrary.find("Best balance: 200 steps / 1.67 s") != std::string::npos);
    assert(center.find("[Enter] Inspect") != std::string::npos);
    assert(center.find("[W] Watch") != std::string::npos);
    assert(center.find("[N] Network") == std::string::npos);
    assert(center.find("[E] Evaluate") != std::string::npos);
    assert(center.find("[T] Resume") != std::string::npos);
    assert(!aura::cli::model_selectable(models[1], "center"));
    assert(models[0].inputs == 205 && models[0].outputs == 14);
    assert(aura::cli::model_selectable(models[0], "resume"));
    for (const auto &action: {"resume", "watch", "evaluate"})
        assert(!aura::cli::model_selectable(models[1], action));
    assert(!aura::cli::model_selectable(models[1], "restart"));
    // Critic entries remain separate from actor pickers and cannot drive motors.
    std::filesystem::create_directories(root / "critics" / "run");
    std::ofstream(root / "critics" / "run" / "critic.pt") << "critic";
    std::ofstream(root / "critics" / "run" / "validation.pt") << "archive";
    assert(aura::cli::saved_models(root, error).size() == 2);
    auto library = aura::cli::saved_models(root, error, true);
    assert(library.size() == 4);
    aura::cli::apply_model_catalog(library,
                                   "AURA_MODEL\t637269746963732f72756e2f6372697469632e7074\tcompatible\t205\t1\tInspect only\n"
                                   "AURA_MODEL_KIND\t637269746963732f72756e2f6372697469632e7074\t637269746963\t4f6c64657220626f6479\n"
                                   "AURA_MODEL_DETAIL\t637269746963732f72756e2f6372697469632e7074\t4e4554574f524b\n");
    assert(library.size() == 3); // Dataset archive has no model catalog entry.
    const auto critics = aura::cli::filter_models(library, "critic older");
    assert(critics.size() == 1 && critics[0].networkDetails[0] == "NETWORK");
    for (const auto *action: {"watch", "evaluate", "resume", "restart"})
        assert(!aura::cli::model_selectable(critics[0], action));
    std::string criticFrame;
    for (const auto &row: aura::cli::center_models_frame(critics, 0, "", 160, 30)) criticFrame += row;
    assert(criticFrame.find("[Enter] Inspect") != std::string::npos);
    assert(criticFrame.find("[N] Network") == std::string::npos);
    assert(criticFrame.find("[W] Watch") == std::string::npos);
    assert(criticFrame.find("[E] Evaluate") == std::string::npos);
    assert(criticFrame.find("older body") != std::string::npos);
    auto networkModel = critics[0];
    networkModel.networkDetails = {
        "ROLE", "Critic / predicts return; controls no motors", "NETWORK",
        "205 -> 17 -> 9 -> 1", "Tanh hidden layers / linear value output",
        "OBSERVATIONS / 205 inputs", "  1-60   joint relative orientations (60)",
        "PREDICTION / 1 output", "Discounted return / 100.0",
        "Saved validation MSE: 0.09051459282636642", "Constant baseline MSE: 0.20016027987003326"
    };
    const auto sheetText = [&](int width, int height, std::size_t &offset) {
        std::string text;
        const auto frame = aura::cli::model_network_frame(networkModel, width, height, offset);
        assert(frame.size() == static_cast<std::size_t>(height));
        for (const auto &row: frame) text += row + '\n';
        return text;
    };
    std::size_t networkOffset = 0;
    const auto wideSheet = sheetText(150, 42, networkOffset);
    assert(wideSheet.find("17 units") != std::string::npos); // Saved architecture, not today's 64-unit layers.
    assert(wideSheet.find("9 units") != std::string::npos);
    assert(wideSheet.find("1 output") != std::string::npos);
    assert(wideSheet.find(" -> ") == std::string::npos);
    assert(wideSheet.find("0.09051") != std::string::npos);
    assert(wideSheet.find("54.8% lower error") != std::string::npos);
    for (int width: {1, 30, 50, 80, 120, 180})
        for (int height: {1, 8, 24, 40}) {
            networkOffset = 9999;
            sheetText(width, height, networkOffset);
            assert(networkOffset < 9999);
        }
    networkOffset = 9999;
    const auto lastPage = sheetText(80, 24, networkOffset);
    assert(networkOffset > 0 && lastPage.find("0.09051") != std::string::npos);
    sheetText(150, 60, networkOffset);
    assert(networkOffset == 0); // Resizing back to a full sheet resets the stale scroll position.
    networkModel.kind = "actor";
    networkModel.networkDetails = {
        "NETWORK", "205 -> 64 -> Tanh -> 64 -> Tanh -> 32 -> Tanh",
        "OBSERVATIONS / 205 inputs", "  1-60   joint relative orientations (60)", "MOVEMENTS / 32 outputs"
    };
    for (int i = 1; i <= 32; ++i) networkModel.networkDetails.push_back(
        std::to_string(i) + "  Joint target " + std::to_string(i));
    const auto actorSheet = sheetText(150, 42, networkOffset);
    assert(actorSheet.find("Joint target 32") != std::string::npos);
    bool pairedColumns = false;
    for (const auto &row: aura::cli::model_network_frame(networkModel, 150, 42, networkOffset))
        pairedColumns |= row.find("Joint target 1 ") != std::string::npos && row.find("Joint target 17") !=
                std::string::npos;
    assert(pairedColumns && networkOffset == 0);
    // A narrow screen still exposes every output through scrolling.
    networkOffset = 9999;
    assert(sheetText(40, 24, networkOffset).find("target 32") != std::string::npos);
    networkModel.networkDetails = {"NETWORK", "Architecture unavailable in this checkpoint"};
    assert(sheetText(120, 30, networkOffset).find("Architecture unavailable") != std::string::npos);
    if (argc == 2) {
        std::ofstream out(argv[1]);
        for (const auto &line: aura::cli::models_frame(models, 1, "", 120, 24)) out << line << '\n';
        std::ofstream setup(std::string(argv[1]) + "-setup.ansi");
        for (const auto &line: aura::cli::training_setup_frame(4, true, "mode", "", 120, 24, false)) setup << line << '\n';
        std::ofstream compact(std::string(argv[1]) + "-setup-80.ansi");
        for (const auto &line: aura::cli::training_setup_frame(4, true, "mode", "", 80, 24, false)) compact << line << '\n';
    }
    auto parsedStates=models;
    aura::cli::apply_model_catalog(parsedStates,"AURA_MODEL\t612e7074\thistorical\t205\t32\tHistorical replay\n"
        "AURA_MODEL\t622e7074\texperimental\t205\t32\tExperimental objective\n");
    assert(parsedStates[0].contractState=="historical");
    assert(parsedStates[1].contractState=="experimental");
    assert(!aura::cli::model_selectable(parsedStates[1],"resume"));
    assert(aura::cli::model_selectable(parsedStates[1],"evaluate"));
    auto historicalActor = models[0];
    historicalActor.kind = "actor";
    historicalActor.contractState = "historical";
    for (const auto *action: {"watch", "evaluate", "fork", "compare", "diagnose", "report"})
        assert(aura::cli::model_selectable(historicalActor, action));
    assert(!aura::cli::model_selectable(historicalActor, "resume"));
    assert(!aura::cli::model_selectable(historicalActor, "restart"));
    std::string forkSetup;
    for (const auto &row: aura::cli::training_setup_frame(4, true, "mode", "", 100, 24, false))
        forkSetup += row;
    assert(forkSetup.find("[5] Fork actor") != std::string::npos);
    assert(forkSetup.find("fresh critic and Adam") != std::string::npos);
    const auto families = root / "families";
    std::filesystem::create_directory(families);
    for (const auto *name: {"balance_extra.pt", "balance_best.pt", "balance.pt", "orphan_best.pt", "best_effort.pt"})
        std::ofstream(families / name) << "checkpoint";
    auto grouped = aura::cli::saved_models(families, error);
    aura::cli::apply_model_catalog(grouped, "AURA_MODEL_BALANCE\t62616c616e63655f626573742e7074\t72\t0.6\n");
    assert(grouped[0].bestBalanced == 72 && grouped[1].bestBalanced == 72);
    assert(grouped[0].path.filename() == "balance.pt");
    assert(grouped[1].path.filename() == "balance_best.pt");
    assert(grouped[2].path.filename() == "balance_extra.pt");
    assert(aura::cli::best_model_index(grouped, 0) == 1);
    assert(!aura::cli::model_selectable(grouped[1], "resume"));
    assert(!aura::cli::model_selectable(grouped[1], "restart"));
    assert(aura::cli::model_selectable(grouped[1], "watch"));
    assert(aura::cli::model_selectable(grouped[1], "evaluate"));
    assert(aura::cli::model_selectable(grouped[1], "critic")); // Frozen actor, no actor updates.
    assert(aura::cli::model_action_error(grouped[1], "resume").find("read-only") != std::string::npos);
    std::string bestFrame;
    for (const auto &row: aura::cli::center_models_frame(grouped, 1, "", 160, 40)) bestFrame += row;
    assert(bestFrame.find("[T] Resume") != std::string::npos);
    assert(aura::cli::reserved_training_name("BALANCE_BEST"));
    assert(!aura::cli::reserved_training_name("balance"));
    assert(aura::cli::best_model_index(grouped, 1) == 1);
    assert(!aura::cli::best_model_index(grouped, 2));
    assert(!aura::cli::best_model_index(grouped, 99));
    assert(!aura::cli::model_is_best(grouped[3]));
    assert(aura::cli::model_is_best(grouped[4]));
    assert(aura::cli::filter_models(grouped, "").size() == grouped.size());
    assert(aura::cli::filter_models(grouped, "   ").size() == grouped.size());
    const auto bestMatches = aura::cli::filter_models(grouped, "BALANCE best");
    assert(bestMatches.size() == 1 && bestMatches[0].path.filename() == "balance_best.pt");
    const auto latestOnly = aura::cli::filter_models(grouped, "balance.pt");
    assert(latestOnly.size() == 1 && latestOnly[0].hasBestCopy);
    assert(aura::cli::filter_models(grouped, "not-a-model").empty());
    assert(aura::cli::filter_models(grouped, "balance")[1].path.filename() == "balance_best.pt");
    std::string searchFrame;
    for (const auto &row: aura::cli::center_models_frame({}, 0, "", 120, 24, "missing", true, grouped.size()))
        searchFrame += row;
    assert(searchFrame.find("CHECKPOINT LIBRARY") == std::string::npos);
    assert(searchFrame.find("Search  missing") != std::string::npos);
    assert(searchFrame.find("No matching models") != std::string::npos);
    assert(searchFrame.find("0 / 5 checkpoints") != std::string::npos);
    assert(searchFrame.find("[Esc] Cancel search") != std::string::npos);
    std::string groupedFrame;
    for (const auto &row: aura::cli::center_models_frame(grouped, 0, "", 140, 30)) groupedFrame += row;
    assert(groupedFrame.find("Latest") != std::string::npos);
    assert(groupedFrame.find("Best") != std::string::npos);
    assert(groupedFrame.find("[B] Best") != std::string::npos);
    std::filesystem::remove(root / "a.pt");
    models = aura::cli::saved_models(root, error);
    assert(models.size() == 1 && models[0].path.filename() == "b.pt");
    std::filesystem::remove_all(root);
}
