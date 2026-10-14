#include "NNTraining.hpp"
#include "../include/aura/sim/ActionSpec.hpp"

#include <cassert>
#include <algorithm>
#include <clocale>
#include <cmath>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace {
std::string plain(const std::string& line) {
    static const std::regex escapes("\033(?:\\[[0-9;?]*[ -/]*[@-~]|\\([A-Z0-9])");
    return std::regex_replace(line,escapes,"");
}
int cells(const std::string& line) {
    const auto text=plain(line);
    std::mbstate_t state{};
    int result=0;
    for (std::size_t at=0;at<text.size();) {
        wchar_t decoded=0;
        const auto length=std::mbrtowc(&decoded,text.data()+at,text.size()-at,&state);
        assert(length!=static_cast<std::size_t>(-1) && length!=static_cast<std::size_t>(-2));
        assert(length>0);
        // Some IDE terminals advance for combining accents despite wcwidth=0.
        // Dashboard notation uses standalone symbols to keep borders aligned.
        assert(decoded<0x0300 || decoded>0x036f);
        result+=std::max(0,wcwidth(decoded)); at+=length;
    }
    return result;
}
std::string text(aura::cli::NNTrainingScreen& screen,int width=120,int rows=60) {
    std::string result;
    for (const auto& line:screen.frame(width,rows,false)) result+=plain(line)+'\n';
    return result;
}
void fixture(aura::cli::NNTrainingScreen& screen) {
    // Fixed presentation fixture, not a performance claim or a training result.
    screen.receive("AURA_NN\tstart\t8\t1200\t42\t18254");
    screen.receive("AURA_NN\toptimizer\t0.001\t0.9\t0.999\t1e-8");
    screen.receive("AURA_NN\tdevice\tcpu");
    screen.receive("AURA_NN\tinterface\t205\t14\t2\t3");
    screen.receive("AURA_NN\tmotors\t14\t15\tHead");
    screen.receive("AURA_NN\tevaluation\t0\t377");
    screen.receive("AURA_NN\tbest\t377\taura_balance_best.pt");
    screen.receive("AURA_NN\tlayer\t205\t64\tTanh");
    screen.receive("AURA_NN\tlayer\t64\t64\tTanh");
    screen.receive("AURA_NN\tlayer\t64\t14\tTanh");
    screen.receive("AURA_NN\tsampling\t0.25\t-1\t1");
    screen.receive("AURA_NN\tcredit\t0.995");
    for (std::size_t i=0;i<aura::sim::actionSpecs.size();++i) {
        const auto &spec=aura::sim::actionSpecs[i];
        if (std::string(spec.key)=="head_target") continue;
        const double scale=i<6 ? (i==2 || i==3 ? 0.3 : 0.2) : spec.maximum*0.2;
        screen.receive("AURA_NN\taction\t"+std::string(spec.key)+"\t"+std::to_string(scale)+
                       "\t"+(spec.minimum>=0?"1":"0")+"\t"+spec.label);
    }
    const char* names[]{"network.0.weight","network.0.bias","network.2.weight","network.2.bias","network.4.weight","network.4.bias"};
    const char* shapes[]{"64x205","64","64x64","64","14x64","14"};
    const int counts[]{13120,64,4096,64,896,14};
    const char* labels[]{"Hidden 1 / weights","Hidden 1 / biases","Hidden 2 / weights","Hidden 2 / biases","Output / weights","Output / biases"};
    for (int i=0;i<6;++i) {
        screen.receive("AURA_NN\ttensor\t"+std::to_string(i)+"\t"+names[i]+"\t"+shapes[i]+"\t"+std::to_string(counts[i])+"\t"+labels[i]);
        screen.receive("AURA_NN\tstats\t"+std::to_string(i)+"\t-0.0999234\t0.100171\t4.61812\t0.810305\t0.00100353");
    }
    const int scores[]{234,271,267,240,248,282,226,257,270,250,240,260,235,250,230,245,263,258,256,253,221,248,245,258};
    for (int batch=1;batch<=3;++batch) {
        screen.receive("AURA_NN\tphase\tcollecting");
        for (int i=0;i<8;++i) {
            const int steps=scores[(batch-1)*8+i];
            screen.receive("AURA_NN\treward\t"+std::to_string(batch)+"\t"+std::to_string(i+1)+"\t"+std::to_string(steps)+"\t0\t"+std::to_string(steps*.95)+"\t1");
            screen.receive("AURA_NN\tepisode\t"+std::to_string(batch)+"\t"+std::to_string(i+1)+"\t"+std::to_string(steps)+"\tfallen");
        }
        screen.receive("AURA_NN\tupdate\t"+std::to_string(batch)+"\t"+(batch==1?"-0.0376061":batch==2?"-0.00341794":"-0.0136858")+"\t250.25\t263\t1.39539\t0.00100353");
    }
    screen.receive("AURA_NN\tprogress\t3\t8\t258\t3.03236\t0.495659\t0.0434403\t-0.112093\t0.138943\t0.0507737\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0");
    for (int i=0;i<8;++i) screen.receive("AURA_NN\tobjective\t"+std::to_string(i+1)+"\t"+std::to_string(scores[16+i])+"\t0.0121429\t-21.3747");
    screen.receive("AURA_NN\tinspect\t0\t0\t0,0\t3\t0.0738121\t0.0741759\t-0.00692974\t-0.00115667\t4.12443e-7");
    screen.receive("AURA_NN\tphase\tready");
    assert(text(screen,45,12).find("Enlarge terminal")!=std::string::npos);
}
}
int main(int argc,char** argv) {
    std::setlocale(LC_CTYPE,"");
    // One high episode must not push the ordinary samples to the bottom.
    aura::cli::NNTrainingScreen centered;
    centered.receive("AURA_NN\tstart\t8\t1200\t42\t100");
    const int centeredScores[]{240,260,270,280,290,300,250,402};
    double average=0;
    for (int i=0;i<8;++i) {
        average+=centeredScores[i]/8.0;
        centered.receive("AURA_NN\tepisode\t1\t"+std::to_string(i+1)+"\t"+std::to_string(centeredScores[i])+"\tfallen");
    }
    bool inChart=false;
    std::vector<double> ticks;
    for (const auto& row:centered.frame(160,40,false)) {
        const auto line=plain(row);
        if (line.find("TRAINING SURVIVAL")!=std::string::npos) inChart=true;
        if (!inChart || (line.find("────") == std::string::npos && line.find("----") == std::string::npos)) continue;
        std::smatch label;
        if (ticks.size()<3 && std::regex_search(line,label,std::regex("^ *(?:│|\\|) +(-?[0-9]+(?:\\.[0-9]+)?) ")))
            ticks.push_back(std::stod(label[1].str()));
    }
    assert(ticks.size()==3);
    assert(ticks.front()>402 && ticks.back()<240);
    assert(std::abs((ticks.front()+ticks.back())/2-average)<20);
    aura::cli::NNTrainingScreen screen;
    fixture(screen);
    aura::cli::NNTrainingScreen rewardView;
    fixture(rewardView);
    rewardView.key("3");
    rewardView.receive("AURA_NN\tphase\tcollecting");
    rewardView.receive("AURA_NN\treward\t4\t1\t100\t0.9\t80\t0");
    auto liveReward=text(rewardView,160,40);
    assert(liveReward.find("80.00")!=std::string::npos);
    assert(liveReward.find("0.800")!=std::string::npos);
    assert(liveReward.find("Last step 0.900")!=std::string::npos);
    rewardView.receive("AURA_NN\treward\t4\t1\t100\t0\t80\t1");
    const auto completedReward=text(rewardView,160,40);
    rewardView.receive("AURA_NN\treward\t4\t1\t100\t0\t80\t1");
    assert(text(rewardView,160,40)==completedReward); // No duplicate history or mean.
    rewardView.receive("AURA_NN\tepisode\t4\t1\t100\tfallen");
    rewardView.receive("AURA_NN\treward\t4\t2\t200\t0\t180\t1");
    const auto batchReward=text(rewardView,160,40);
    assert(batchReward.find("130.00")!=std::string::npos);
    assert(batchReward.find("2 completed episodes")!=std::string::npos);
    rewardView.receive("AURA_NN\treward\t4\t2\t200\t0\tnan\t1");
    rewardView.receive("AURA_NN\treward\t3\t8\t200\t0\t100\t1");
    rewardView.receive("AURA_NN\treward\t4\t2\t0\t0\t100\t1");
    assert(text(rewardView,160,40)==batchReward);
    rewardView.key("r");
    assert(text(rewardView,160,40).find("EPISODE DIAGNOSTICS")!=std::string::npos);
    rewardView.key("r");
    assert(text(rewardView,160,40)==batchReward);
    aura::cli::NNTrainingScreen resumed;
    fixture(resumed);
    resumed.receive("AURA_NN\tmodel\tbalance_2.pt\tresume\t24\t3");
    assert(text(resumed).find("balance_2.pt")!=std::string::npos);
    assert(text(resumed).find("24 episodes saved")!=std::string::npos);
    assert(resumed.updates()==3);
    const auto resumedOverview=text(resumed);
    assert(resumedOverview.find("TRAINING / LAST EPISODE")!=std::string::npos);
    assert(resumedOverview.find("BEST EVALUATION")!=std::string::npos);
    resumed.key("g");
    assert(text(resumed).find("/ update 3")!=std::string::npos);
    resumed.key("g");
    assert(resumedOverview.find("25")!=std::string::npos); // Episode axis continues after resume.
    assert(screen.updates()==3);
    const auto overview=text(screen);
    assert(overview.find("14/15 motors")!=std::string::npos);
    assert(overview.find("Initial policy")!=std::string::npos);
    assert(overview.find("Head off")!=std::string::npos);
    assert(overview.find("T inspect joint targets")!=std::string::npos);
    const auto compact=text(screen,160,40);
    assert(compact.find("wheel scroll")==std::string::npos);
    assert(compact.find("TRAINING SURVIVAL")!=std::string::npos);
    assert(compact.find("POLICY LOSS")!=std::string::npos);
    assert(compact.find("EVALUATION SURVIVAL")!=std::string::npos);
    assert(compact.find("G loss chart")!=std::string::npos);
    screen.key("g");
    assert(text(screen,160,40).find("EVALUATION SURVIVAL")==std::string::npos);
    assert(text(screen,160,40).find("G evaluation chart")!=std::string::npos);
    screen.key("g");
    assert(screen.key("t").empty());
    assert(text(screen,160,60).find("Right wrist")!=std::string::npos);
    assert(text(screen,160,60).find("head_target")==std::string::npos);
    screen.key("t");
    assert(text(screen,160,40)==compact);
    assert(overview.find("282")!=std::string::npos);
    assert(overview.find("EVAL SURVIVAL")!=std::string::npos);
    screen.receive("AURA_NN\tphase\tevaluating");
    assert(text(screen).find("EVALUATING")!=std::string::npos);
    screen.receive("AURA_NN\tevaluation\t3\t267");
    screen.receive("AURA_NN\tphase\tready");
    assert(text(screen).find("267 steps")!=std::string::npos);
    screen.receive("AURA_NN\tevaluation\t3\t999"); // Duplicate results cannot replace a measurement.
    screen.receive("AURA_NN\tevaluation\t2\t999"); // Nor can stale updates.
    screen.receive("AURA_NN\tevaluation\t4\t999"); // Future updates have no matching trained policy.
    assert(text(screen).find("267 steps")!=std::string::npos);
    auto evaluated=screen;
    evaluated.receive("AURA_NN\tupdate\t4\t-0.1\t250\t280\t1\t0.001");
    evaluated.receive("AURA_NN\tevaluation\t4\t1201"); // Respect the configured episode limit.
    assert(text(evaluated).find("267 steps")!=std::string::npos);
    evaluated.receive("AURA_NN\tevaluation\t4\t259");
    assert(text(evaluated).find("259 steps")!=std::string::npos);
    assert(text(evaluated).find("Update 4 / -8")!=std::string::npos);
    evaluated.receive("AURA_NN\tbest\t310\tbalance_best.pt");
    assert(text(evaluated).find("BEST EVALUATION")!=std::string::npos);
    assert(text(evaluated).find("310 steps")!=std::string::npos);
    assert(text(evaluated).find("balance_best.pt")!=std::string::npos);
    screen.receive("AURA_NN\tupdate\t4\tnan\t250\t280\t1\t0.001");
    assert(screen.updates()==3);
    screen.receive("AURA_NN\tstats\tbad");
    screen.key("2");
    assert(screen.key("v")=="visual");
    assert(screen.key("V")=="visual");
    screen.receive("AURA_NN\tviewer\t1");
    assert(text(screen,120,40).find("V headless")!=std::string::npos);
    screen.receive("AURA_NN\tviewer\t0");
    assert(text(screen,120,40).find("V visual")!=std::string::npos);
    screen.receive("AURA_NN\tviewer_error\tNo display available");
    assert(text(screen,120,40).find("No display available")!=std::string::npos);
    screen.receive("AURA_NN\tviewer\t0");
    assert(screen.key("\033[B")=="inspect 1 0");
    assert(screen.key("\033[F")=="inspect 1 63");
    assert(text(screen).find("Loading selected value")!=std::string::npos);
    screen.key("\033[A");
    assert(text(screen).find("0.074176")!=std::string::npos);
    screen.receive("AURA_NN\tpaused\t1");
    assert(text(screen,160,40).find("P resume")!=std::string::npos);
    assert(text(screen,160,40).find("N next")==std::string::npos);
    assert(screen.key("n").empty());
    assert(screen.key("p")=="pause");
    screen.receive("AURA_NN\tpaused\t0");
    assert(text(screen,160,40).find("N next")!=std::string::npos);
    assert(screen.key("n")=="next");
    assert(screen.key("n").empty());
    screen.receive("AURA_NN\tphase\tcollecting");
    assert(text(screen).find("Batch 4")!=std::string::npos);
    screen.key("3");
    const auto rewards=text(screen,160,40);
    assert(rewards.find("EPISODE RETURN")!=std::string::npos);
    assert(rewards.find("REWARD / STEP")!=std::string::npos);
    assert(rewards.find("EPISODE REWARD")!=std::string::npos);
    assert(rewards.find("REWARD QUALITY")!=std::string::npos);
    assert(rewards.find("READING THE REWARD")!=std::string::npos);
    assert(rewards.find("R learning details")!=std::string::npos);
    assert(rewards.find("0.950")!=std::string::npos);
    assert(rewards.find("FORWARD PASS")==std::string::npos);
    screen.key("r");
    screen.receive("AURA_NN\tobjective\t1\t249\t-0.01735\t-21.3747\t237.86");
    assert(text(screen).find("Collected before update 3")!=std::string::npos);
    const auto learning=text(screen,120,60);
    assert(learning.find("head_target")==std::string::npos);
    assert(learning.find("FORWARD PASS")!=std::string::npos);
    assert(learning.find("The loss uses raw samples u")!=std::string::npos);
    assert(learning.find("body receives scaled actions a")!=std::string::npos);
    assert(learning.find("Return")!=std::string::npos);
    assert(learning.find("237.86")!=std::string::npos);
    assert(learning.find("Compare step returns")!=std::string::npos);
    assert(learning.find("REWARD-TO-GO")!=std::string::npos);
    assert(learning.find("0.995")!=std::string::npos);
    assert(learning.find("zero if no peers")!=std::string::npos);
    assert(learning.find("Reward is unused")==std::string::npos);
    screen.receive("AURA_NN\tobjective\t1\t249\t-0.01735\t-21.3747\tinvalid");
    assert(text(screen,120,60).find("237.86")!=std::string::npos);
    screen.receive("AURA_NN\tphase\tready");
    for (int page=1;page<=3;++page) for (int width:{1,20,59,60,65,80,100,112,120,160,220}) for (int rows:{1,8,17,18,20,24,32,40,60}) {
        screen.key(std::to_string(page));
        for (const bool expanded:{false,true}) {
            const auto frame=screen.frame(width,rows,false);
            assert(static_cast<int>(frame.size())==rows);
            for (const auto& line:frame) {
                if (cells(line)>width) std::cerr<<"Overflow: page "<<page<<" "<<width<<"x"<<rows<<" cells="<<cells(line)<<" line="<<plain(line)<<'\n';
                assert(cells(line)<=width);
            }
            if (page==3) screen.key("r"); else screen.key("f");
        }
    }
    screen.key("2");
    screen.frame(65,20,false);
    screen.key("\033[6~"); screen.key("\033[6~"); screen.key("\033[6~");
    const auto scrolled=text(screen,65,20);
    assert(scrolled.find("ADAM")!=std::string::npos || scrolled.find("Calculated")!=std::string::npos);
    // Page scrolling leaves tensor selection and the selected scalar unchanged.
    aura::cli::NNTrainingScreen scrolling;
    fixture(scrolling);
    scrolling.key("2"); scrolling.key("f");
    const auto beforeScroll=text(scrolling,120,24);
    for (const auto& key:{"\033[6~","scroll_down"}) assert(scrolling.key(key).empty());
    // Bring the heading back into view after testing mouse scrolling past it.
    assert(scrolling.key("scroll_up").empty());
    const auto afterScroll=text(scrolling,120,24);
    assert(afterScroll!=beforeScroll);
    assert(scrolling.key("l")=="inspect 0 1");
    const auto rowOf=[](const std::string& frame,const std::string& label) {
        const auto position=frame.find(label);
        assert(position!=std::string::npos);
        return std::count(frame.begin(),frame.begin()+position,'\n');
    };
    assert(rowOf(text(scrolling,120,24),"ADAM / UPDATE RULE")==rowOf(afterScroll,"ADAM / UPDATE RULE"));
    for (const auto& key:{"\033[5~","scroll_up"}) assert(scrolling.key(key).empty());
    assert(text(scrolling,120,24)!=afterScroll);
    assert(scrolling.key("\033[B")=="inspect 1 0");
    // More episodes and a deeper/wider model must remain fully inspectable.
    aura::cli::NNTrainingScreen variant;
    variant.receive("AURA_NN\tstart\t16\t777\t9\t999");
    variant.receive("AURA_NN\tlayer\t17\t128\tTanh");
    variant.receive("AURA_NN\tlayer\t128\t96\tReLU");
    variant.receive("AURA_NN\tlayer\t96\t32\tTanh");
    variant.receive("AURA_NN\tlayer\t32\t4\tTanh");
    variant.receive("AURA_NN\tdevice\ttest-device");
    for (int i=0;i<8;++i)
        variant.receive("AURA_NN\ttensor\t"+std::to_string(i)+"\tnetwork."+std::to_string(i)+".weight\t4x32\t128");
    variant.receive("AURA_NN\tupdate\t1\t-0.1\t11\t16\t1\t.001");
    for (int i=1;i<=16;++i)
        variant.receive("AURA_NN\tobjective\t"+std::to_string(i)+"\t"+std::to_string(1000+i)+"\t0.1\t-2");
    variant.key("2");
    const auto parameterText=text(variant,160,90);
    assert(parameterText.find("128")!=std::string::npos);
    assert(parameterText.find("network.7 / weight")!=std::string::npos);
    for (int i=0;i<7;++i) variant.key("\033[B");
    assert(variant.key("l")=="inspect 7 1");
    variant.key("3");
    variant.key("r");
    const auto variantText=text(variant,160,90);
    assert(variantText.find("1016")!=std::string::npos);
    assert(variantText.find("777")!=std::string::npos);
    assert(variantText.find("Collect 16 episodes")!=std::string::npos);
    assert(variantText.find("other 15 episodes")!=std::string::npos);
    assert(variantText.find("relu")!=std::string::npos);
    for (int width:{60,80,120,160}) for (int rows:{18,32,90})
        for (int page=1;page<=3;++page) {
            variant.key(std::to_string(page));
            for (const auto& line:variant.frame(width,rows,false)) assert(cells(line)<=width);
        }
    if (argc==3 && std::string(argv[1])=="--preview") {
        aura::cli::NNTrainingScreen preview;
        preview.receive("AURA_NN\tmodel\tbalance.pt\tresume\t0\t0");
        fixture(preview);
        preview.receive("AURA_NN\tcheckpoint\tbalance.pt\t24");
        preview.receive("AURA_NN\tevaluation\t3\t267");
        preview.receive("AURA_NN\tbest\t310\tbalance_best.pt");
        preview.receive("AURA_NN\tobjective\t1\t249\t-0.01735\t-21.3747\t237.86");
        for (int page=1;page<=3;++page) {
            preview.key(std::to_string(page));
            std::ofstream file(std::string(argv[2])+"-"+std::to_string(page)+".ansi");
            for (const auto& line:preview.frame(120,40,false)) file<<line<<'\n';
        }
        preview.key("2"); preview.key("f");
        std::ofstream file(std::string(argv[2])+"-math.ansi");
        for (const auto& line:preview.frame(120,40,false)) file<<line<<'\n';
    }
    std::cout<<"Screen checks: Unicode cells, compact sizes, scroll, stale selection, batch state and malformed values passed.\n";
}
