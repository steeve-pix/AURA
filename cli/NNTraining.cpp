#include "NNTraining.hpp"
#include "ScreenCanvas.hpp"
#include "TrainingCharts.hpp"
#include "TerminalCapabilities.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>

namespace aura::cli {
namespace {
using namespace screen;

std::string tensorLabel(const std::string& name) {
    const auto dot=name.rfind('.');
    return dot==std::string::npos?name:name.substr(0,dot)+" / "+name.substr(dot+1);
}
std::string subscript(int index) {
    const char* digits[]{"₀","₁","₂","₃","₄","₅","₆","₇","₈","₉"};
    std::string result;
    for (char digit:std::to_string(index)) result+=terminal_capabilities().utf8?digits[digit-'0']:std::string(1,digit);
    return result;
}
}

void NNTrainingScreen::receive(const std::string& line) {
    if (!line.starts_with("AURA_NN\t")) { if (!line.empty()) error_ = line; return; }
    std::istringstream input(line.substr(8));
    std::string kind; input >> kind;
    if (kind == "start") {
        int batchSize,limit,seed,count;
        if (input >> batchSize >> limit >> seed >> count && batchSize>=2 && limit>0 && count>0) {
            batchSize_=batchSize; episodeLimit_=limit; seed_=seed; count_=count;
            objectives_.assign(batchSize_,{});
        }
    }
    else if (kind == "interface") {
        int inputs,outputs,observationSchema,actionSchema;
        if (input>>inputs>>outputs>>observationSchema>>actionSchema && inputs>0 && outputs>0) {
            inputs_=inputs; outputs_=outputs;
        }
    } else if (kind == "motors") {
        int active,total;
        if (input>>active>>total && total>0 && active>=0 && active<=total) {
            activeMotors_=active; totalMotors_=total;
            std::getline(input>>std::ws,disabledMotors_);
        }
    }
    else if (kind == "model") {
        std::getline(input >> std::ws,model_,'\t');
        input >> trainingMode_ >> completedEpisodes_ >> updates_;
        episodeOffset_=completedEpisodes_;
        batch_=updates_+1;
    }
    else if (kind == "viewer") { int active; if (input >> active) { visual_=active!=0; viewerError_.clear(); } }
    else if (kind == "viewer_error") std::getline(input >> std::ws,viewerError_);
    else if (kind == "checkpoint") {
        std::getline(input >> std::ws,checkpoint_,'\t');
        input >> completedEpisodes_;
        model_=checkpoint_;
    }
    else if (kind == "optimizer") input >> lr_ >> beta1_ >> beta2_ >> epsilon_;
    else if (kind == "device") input >> device_;
    else if (kind == "layer") {
        Layer layer;
        if (input >> layer.inputs >> layer.outputs >> layer.activation && layer.inputs>0 && layer.outputs>0) layers_.push_back(layer);
    } else if (kind == "credit") {
        double discount;
        if (input >> discount && std::isfinite(discount) && discount >= 0 && discount <= 1) discount_=discount;
    } else if (kind == "sampling") input >> sigma_ >> sampleLow_ >> sampleHigh_;
    else if (kind == "action") {
        Action action; int nonnegative;
        if (input >> action.key >> action.scale >> nonnegative && std::isfinite(action.scale)) {
            action.nonnegative=nonnegative;
            std::getline(input >> std::ws,action.label);
            actions_.push_back(action);
        }
    } else if (kind == "motor") {
        std::string key; int enabled;
        if (input>>key>>enabled && (enabled==0 || enabled==1))
            for (auto &action : actions_) if (action.key==key) { action.motorKnown=true; action.enabled=enabled!=0; }
    } else if (kind == "tensor") {
        int index; Tensor tensor;
        if (input >> index >> tensor.name >> tensor.shape >> tensor.count && index >= 0 && index <= static_cast<int>(tensors_.size()) && tensor.count > 0) {
            std::getline(input >> std::ws,tensor.label);
            if (index >= static_cast<int>(tensors_.size())) tensors_.resize(index+1);
            tensors_[index] = tensor;
        }
    } else if (kind == "phase") {
        input >> phase_;
        if (phase_ == "collecting") { episode_ = steps_ = 0; batch_ = updates_+1; rewardSeen_=false; }
    } else if (kind == "paused") { int value = 0; if (input >> value) paused_ = value; }
    else if (kind == "progress") {
        int batch,episode,steps; double height,upright;
        if (input >> batch >> episode >> steps >> height >> upright
            && batch > 0 && episode > 0 && steps >= 0 && std::isfinite(height) && std::isfinite(upright)) {
            std::vector<double> values; double value;
            while (input >> value) values.push_back(value);
            if (values.size()==actions_.size() && std::all_of(values.begin(),values.end(),[](double v){return std::isfinite(v);})) {
                batch_=batch; episode_=episode; steps_=steps; height_=height; upright_=upright;
                for (std::size_t i=0;i<values.size();++i) actions_[i].value=values[i];
            }
        }
    } else if (kind == "reward") {
        int batch,episode,steps,finished; double reward,total;
        if (input>>batch>>episode>>steps>>reward>>total>>finished && batch>0 && episode>0 && episode<=batchSize_
            && steps>0 && steps<=episodeLimit_ && (finished==0 || finished==1) && std::isfinite(reward) && std::isfinite(total)
            && (batch>rewardBatch_ || (batch==rewardBatch_ && (episode>rewardEpisode_ || (episode==rewardEpisode_ && steps>=rewardSteps_))))) {
            rewardSeen_=true; rewardBatch_=batch; rewardEpisode_=episode; rewardSteps_=steps;
            stepReward_=reward; episodeReward_=total;
            if (finished && (batch>completedRewardBatch_ || (batch==completedRewardBatch_ && episode>completedRewardEpisode_))) {
                if (batch!=completedRewardBatch_) { rewardBatchReturn_=0; rewardBatchEpisodes_=0; }
                rewardBatchReturn_+=total; ++rewardBatchEpisodes_;
                completedRewardBatch_=batch; completedRewardEpisode_=episode;
                rewardReturns_.push_back(total); rewardRates_.push_back(total/steps);
                rewardIndices_.push_back(episodeOffset_+static_cast<int>(scores_.size())+1);
                if (rewardReturns_.size()>120) {
                    rewardReturns_.erase(rewardReturns_.begin()); rewardRates_.erase(rewardRates_.begin());
                    rewardIndices_.erase(rewardIndices_.begin());
                }
            }
        }
    } else if (kind == "episode") {
        int batch,episode; double score; std::string outcome;
        if (input >> batch >> episode >> score >> outcome && std::isfinite(score) && score >= 0) { scores_.push_back(score); outcome_=outcome; }
    } else if (kind == "update") {
        int updates; double loss,mean,best,gradient,change;
        if (input >> updates >> loss >> mean >> best >> gradient >> change && updates > updates_
            && std::isfinite(loss) && std::isfinite(mean) && std::isfinite(gradient) && std::isfinite(change)) {
            updates_=updates; loss_=loss; gradient_=gradient; change_=change; losses_.push_back(loss); means_.push_back(mean);
        }
    } else if (kind == "evaluation") {
        int update, steps;
        if (input >> update >> steps && update == updates_
            && (update > evaluationUpdate_ || (!evaluationSeen_ && update == 0))
            && steps >= 0 && steps <= episodeLimit_) {
            previousEvaluationSteps_ = evaluationSeen_ ? evaluationSteps_ : -1;
            evaluationUpdate_ = update;
            evaluationSteps_ = steps;
            evaluationSeen_ = true;
            evaluations_.push_back(steps);
            evaluationUpdates_.push_back(update);
        }
    } else if (kind == "best") {
        int steps; std::string checkpoint;
        if (input >> steps >> checkpoint && steps >= 0 && steps <= episodeLimit_) {
            bestEvaluationSteps_ = steps;
            bestCheckpoint_ = checkpoint;
        }
    } else if (kind == "stats") {
        int index=-1; input >> index;
        if (index >= 0 && index < static_cast<int>(tensors_.size())) {
            auto& tensor=tensors_[index]; input >> tensor.minimum >> tensor.maximum >> tensor.norm >> tensor.gradient >> tensor.change;
        }
    } else if (kind == "inspect") {
        input >> inspectedTensor_ >> inspectedElement_ >> coordinates_ >> inspectedUpdate_;
        for (auto& value : scalar_) input >> value;
    } else if (kind == "objective") {
        int episode; Objective objective;
        if (input >> episode >> objective.steps >> objective.advantage >> objective.logProbability
            && episode >= 1 && episode <= batchSize_ && std::isfinite(objective.advantage) && std::isfinite(objective.logProbability)) {
            input >> std::ws;
            if (!input.eof()) {
                if (!(input >> objective.totalReturn) || !std::isfinite(objective.totalReturn)) return;
                objective.hasReturn=true;
            }
            if (objectiveUpdate_ != updates_) { std::fill(objectives_.begin(),objectives_.end(),Objective{}); objectiveUpdate_=updates_; }
            objective.valid=true; objectives_[episode-1]=objective;
        }
    } else if (kind == "stopped") phase_="stopped";
}
std::string NNTrainingScreen::key(const std::string& key) {
    if (key == "1" || key == "2" || key == "3") { page_=key[0]-'0'; return ""; }
    if ((key == "f" || key == "F") && page_ == 2) { expandedMath_=!expandedMath_; return ""; }
    if ((key == "t" || key == "T") && page_ == 1) { expandedTargets_=!expandedTargets_; scroll_[0]=0; return ""; }
    if ((key == "g" || key == "G") && page_ == 1) { lossChart_=!lossChart_; return ""; }
    if ((key == "r" || key == "R") && page_ == 3) { learningDetails_=!learningDetails_; scroll_[2]=0; return ""; }
    if (key == "p" || key == "P" || key == " ") return "pause";
    if (key == "v" || key == "V") return "visual";
    if ((key == "n" || key == "N") && phase_ == "ready" && !paused_) { phase_="queued"; return "next"; }
    if (key == "\033[5~" || key == "scroll_up") scroll_[page_-1]=std::max(0,scroll_[page_-1]-(key=="scroll_up"?3:std::max(1,viewport_-2)));
    else if (key == "\033[6~" || key == "scroll_down") scroll_[page_-1]=std::min(100000,scroll_[page_-1]+(key=="scroll_down"?3:std::max(1,viewport_-2)));
    else if (page_ == 2 && !tensors_.empty() &&
             (key == "\033[A" || key == "\033OA" || key == "\033[B" || key == "\033OB")) {
        if (key == "\033[A" || key == "\033OA") { selected_=std::max(0,selected_-1); element_=0; }
        else if (key == "\033[B" || key == "\033OB") { selected_=std::min(static_cast<int>(tensors_.size())-1,selected_+1); element_=0; }
        // Keep a selected tensor visible even when the model has many layers.
        const int row=4+selected_;
        if (row<scroll_[1]) scroll_[1]=row;
        else if (row>=scroll_[1]+viewport_) scroll_[1]=row-viewport_+1;
        return "inspect " + std::to_string(selected_) + " " + std::to_string(element_);
    } else if (page_ == 2 && !tensors_.empty()) {
        if (key == "h" || key == "\033[D" || key == "\033OD") element_=std::max(0,element_-1);
        else if (key == "l" || key == "\033[C" || key == "\033OC") element_=std::min(tensors_[selected_].count-1,element_+1);
        else if (key == "[") element_=std::max(0,element_-64);
        else if (key == "]") element_=std::min(tensors_[selected_].count-1,element_+64);
        else if (key == "\033[H" || key == "\033OH" || key == "\033[1~") element_=0;
        else if (key == "\033[F" || key == "\033OF" || key == "\033[4~") element_=tensors_[selected_].count-1;
        else return "";
        return "inspect " + std::to_string(selected_) + " " + std::to_string(element_);
    } else if (key == "\033[A" || key == "\033OA") scroll_[page_-1]=std::max(0,scroll_[page_-1]-1);
    else if (key == "\033[B" || key == "\033OB") scroll_[page_-1]=std::min(100000,scroll_[page_-1]+1);
    else if (key == "\033[H" || key == "\033OH") scroll_[page_-1]=0;
    else if (key == "\033[F" || key == "\033OF") scroll_[page_-1]=scrollMax_;
    return "";
}

std::vector<std::string> NNTrainingScreen::frame(int width,int rows,bool exited) {
    width=std::max(1,width); rows=std::max(1,rows);
    const Palette p;
    Canvas screen(width,rows);
    const int margin=width>=65?2:1, w=std::max(1,std::min(156,width-margin*2)), x=(width-w)/2;
    const bool spacious=width>=60 && rows>=26;
    const int bodyTop=spacious?9:6;
    const std::string state=exited?"EXITED":paused_?"PAUSED":phase_=="ready"?"READY":phase_=="evaluating"?"EVALUATING":phase_=="collecting"?"TRAINING":phase_=="optimizing"?"UPDATING":phase_=="queued"?"STARTING":phase_=="stopped"?"STOPPED":"STARTING";
    const auto statusColor=exited?p.caps.error_color:paused_?p.gold:phase_=="ready"?p.green:p.purple;
    const std::string status=glyph(paused_?"Ⅱ":phase_=="ready"?"●":"↻",paused_?"||":phase_=="ready"?"*":"o")+" "+state;
    screen.heading(x,w,"NEURAL TRAINING LAB",p,status,statusColor);
    if (!model_.empty()) {
        const auto mode=trainingMode_=="resume"?"Resumed session":trainingMode_=="restart"?"Restarted session":trainingMode_=="new"?"New model":trainingMode_;
        const auto note=mode.empty()?std::string{}:"  "+glyph("·","/")+"  "+mode;
        const int room=w>=108?w-35:w;
        const auto modelLabel=ellipsize(model_,std::max(1,room-textCells(note)));
        screen.put(x,spacious?3:2,modelLabel,p.bold,room);
        screen.put(x+textCells(modelLabel),spacious?3:2,note,p.dim,room-textCells(modelLabel));
    }
    if (spacious && w>=108) {
        const std::string runtime=device_+"  "+glyph("·","/")+"  Seed "+std::to_string(seed_);
        screen.put(x+w-textCells(runtime),3,runtime,p.dim,textCells(runtime));
    }
    const std::array<std::string,3> tabs{"1  Overview","2  Parameters","3  Learning"};
    int tabX=x;
    for (int tab=0;tab<3;++tab) {
        screen.put(tabX,spacious?5:3," "+tabs[tab]+" ",page_==tab+1?p.selected:p.dim);
        tabX+=static_cast<int>(tabs[tab].size())+4;
    }
    const auto batchSummary="Batch "+std::to_string(batch_)+"  "+glyph("·","/")+"  "+std::to_string(completedEpisodes_)+" episodes saved"+(w<108?"  "+glyph("·","/")+"  "+device_+" / seed "+std::to_string(seed_):"");
    screen.put(x,spacious?7:4,batchSummary,p.dim,w);
    if (inputs_>0) {
        const auto divider="  "+glyph("·","/")+"  ";
        std::string contract=std::to_string(inputs_)+" inputs "+glyph("→","->")+" "+std::to_string(outputs_)+" actions";
        if (activeMotors_>=0) contract+=divider+std::to_string(activeMotors_)+"/"+std::to_string(totalMotors_)+" motors";
        const std::string motorNote=disabledMotors_.empty()?"":divider+disabledMotors_+" off";
        const int contractWidth=textCells(contract)+textCells(motorNote);
        const bool inlineSummary=spacious && w>=108 && textCells(batchSummary)+contractWidth+4<=w;
        const int contractX=inlineSummary?x+textCells(batchSummary)+4:x;
        const int contractY=inlineSummary?7:spacious?6:5;
        screen.put(contractX,contractY,contract,p.dim,w-(contractX-x));
        screen.put(contractX+textCells(contract),contractY,motorNote,p.gold,w-(contractX-x)-textCells(contract));
    }
    if (width<60 || rows<18) {
        screen.put(x,6,"Episode "+std::to_string(episode_)+"/"+std::to_string(batchSize_)+"  /  "+integer(steps_)+" steps",p.text,w);
        screen.put(x,8,updates_?"Loss "+number(loss_,5):"Collecting the first batch...",p.text,w);
        screen.put(x,std::min(10,rows-3),"Enlarge terminal: 60 columns / 18 rows",p.dim,w);
        screen.put(x,rows-2,std::string(visual_?"V headless":"V visual")+"  P pause  N next  Q back",p.dim,w);
        screen.put(x,rows-1,checkpoint_.empty()?"Checkpoint pending":"Saved / "+checkpoint_,p.dim,w);
        scrollMax_=0;
        return screen.lines();
    }
    if (count_==0) {
        screen.put(x,6,"Loading training configuration...",p.dim,w);
        screen.put(x,rows-2,"Q back / Esc home",p.text,w);
        return screen.lines();
    }
    viewport_=rows-bodyTop-3;
    const bool wide=w>=108;
    const int tensorHeight=std::max(12,static_cast<int>(tensors_.size())+6);
    const int mathTop=wide?tensorHeight+1:tensorHeight+14;
    const int episodeHeight=std::max(14,batchSize_+6);
    const int formulaTop=wide?episodeHeight+1:episodeHeight+16;
    const int formulaHeight=19+static_cast<int>(layers_.size()+actions_.size());
    int contentHeight=0;
    const int overviewColumns=wide?5:2;
    const int overviewCardW=(w-(overviewColumns-1)*2)/overviewColumns;
    const int episodeWidth=wide?3*(overviewCardW+2)-2:w;
    const int targetColumns=std::max(1,(episodeWidth-4)/28);
    const int targetRows=(static_cast<int>(actions_.size())+targetColumns-1)/targetColumns;
    const int episodePanelHeight=expandedTargets_?std::max(7,(wide?7:6)+targetRows):7;
    if (page_==1) contentHeight=std::max(viewport_,((5+(wide?5:2)-1)/(wide?5:2))*6+(wide?1:0)+episodePanelHeight+1+(wide?11:23));
    else if (page_==2) contentHeight=mathTop+(expandedMath_?20:9);
    else if (!learningDetails_) contentHeight=std::max(viewport_,24);
    else contentHeight=formulaTop+formulaHeight;
    Canvas body(w,contentHeight);
    if (page_==1) {
        const int columns=wide?5:2, cardW=(w-(columns-1)*2)/columns;
        const double best=scores_.empty()?0:*std::max_element(scores_.begin(),scores_.end());
        const double mean=means_.empty()?0:means_.back();
        const std::array<std::string,5> titles{"BATCH MEAN","BEST EPISODE","EVAL SURVIVAL","OPTIMIZER","POLICY LOSS"};
        const std::array<std::string,5> values{means_.empty()?glyph("—","--"):number(mean,1),scores_.empty()?glyph("—","--"):number(best,0),evaluationSeen_?integer(evaluationSteps_)+" steps":glyph("—","--"),std::to_string(updates_)+" updates",updates_?number(loss_,5):glyph("—","--")};
        const std::string evaluationNote=phase_=="evaluating"?"Testing policy":evaluationSeen_?(evaluationUpdate_?"Update "+std::to_string(evaluationUpdate_):"Initial policy")+(previousEvaluationSteps_>=0?" / "+number(evaluationSteps_-previousEvaluationSteps_,0,true):""):"Awaiting update";
        const std::array<std::string,5> notes{"training / steps",std::to_string(scores_.size())+" explored",evaluationNote,"Adam / "+number(lr_,3),"per update"};
        for (int i=0;i<5;++i) {
            const int cx=(i%columns)*(cardW+2), cy=(i/columns)*6;
            body.box(cx,cy,cardW,5,titles[i],p);
            body.put(cx+2,cy+2,values[i],i==0?p.caps.strong+p.green:i==2||i==4?p.caps.strong+p.purple:p.bold,cardW-4);
            body.put(cx+2,cy+3,notes[i],p.dim,cardW-4);
        }
        const int liveY=((5+columns-1)/columns)*6+(wide?1:0);
        const int liveW=wide?3*(cardW+2)-2:w;
        const int innerX=wide?2:0, innerY=liveY+(wide?2:0);
        if (wide) body.box(0,liveY,liveW,episodePanelHeight,"TRAINING / LAST EPISODE",p);
        const int barW=std::clamp(liveW-48,6,24);
        const double fraction=phase_=="ready"?1.0:std::clamp((std::max(0,episode_-1)+static_cast<double>(steps_)/std::max(1,episodeLimit_))/std::max(1,batchSize_),0.0,1.0);
        const std::string live=phase_=="evaluating"?"Evaluating policy":phase_=="ready"?"Batch complete":paused_?"Episode paused":"Episode "+std::to_string(episode_)+" / "+std::to_string(batchSize_);
        body.put(innerX,innerY,glyph("▸",">")+" "+live,p.bold,22);
        for (int i=0;i<barW;++i) body.put(innerX+23+i,innerY,glyph(i<static_cast<int>(fraction*barW)?"━":"─",i<static_cast<int>(fraction*barW)?"=":"-"),i<static_cast<int>(fraction*barW)?p.green:p.dim);
        body.put(innerX+25+barW,innerY,integer(steps_)+" / "+integer(episodeLimit_),p.text,liveW-innerX-27-barW);
        body.put(innerX,innerY+1,"Height "+number(height_,2)+" m   Upright "+number(upright_,3)+"   "+(outcome_=="fallen"?"Fallen":outcome_=="time_limit"?"Time limit":""),p.dim,liveW-innerX-2);
        body.put(innerX,innerY+2,std::to_string(actions_.size())+" motor targets / rad",p.dim,liveW-innerX-2);
        body.put(innerX,innerY+3,expandedTargets_?"T collapse targets":"T inspect joint targets",p.purple,liveW-innerX-2);
        if (expandedTargets_) {
            const int targetWidth=(liveW-innerX-2)/targetColumns;
            for (int i=0;i<static_cast<int>(actions_.size());++i) {
                const auto& action=actions_[i];
                const bool disabled=action.motorKnown && !action.enabled;
                const std::string target=action.label+(disabled?" / off":" "+number(action.value,3,true));
                body.put(innerX+(i%targetColumns)*targetWidth,innerY+4+i/targetColumns,
                         target,disabled?p.gold:p.dim,targetWidth-1);
            }
        }
        if (wide) {
            const int bestX=3*(cardW+2), bestW=w-bestX;
            body.box(bestX,liveY,bestW,7,"BEST EVALUATION",p);
            body.put(bestX+2,liveY+2,bestEvaluationSteps_>=0?integer(bestEvaluationSteps_)+" steps":glyph("—","--"),p.caps.strong+p.green,bestW-4);
            body.put(bestX+2,liveY+3,bestEvaluationSteps_>=0?bestCheckpoint_:"Available after evaluation",p.text,bestW-4);
            if (bestEvaluationSteps_>=0 && evaluationSeen_)
                body.put(bestX+2,liveY+4,"Current "+integer(evaluationSteps_)+"  /  "+number(evaluationSteps_-bestEvaluationSteps_,0,true)+" from best",
                    evaluationSteps_<bestEvaluationSteps_?p.gold:p.green,bestW-4);
            body.put(bestX+2,liveY+5,"No noise / saved best",p.dim,bestW-4);
        } else if (bestEvaluationSteps_ >= 0)
            body.put(0,liveY+episodePanelHeight-1,"Best evaluation "+integer(bestEvaluationSteps_)+" steps  "+glyph("·","/")+"  "+bestCheckpoint_,p.green,w);
        const int chartY=liveY+episodePanelHeight+1;
        const auto rightChart = [&](int cx,int cy,int cw,int ch) {
            chart(body,cx,cy,cw,ch,lossChart_?"POLICY LOSS":"EVALUATION SURVIVAL",
                  lossChart_?losses_:evaluations_,{},p.purple,p,lossChart_?ChartKind::loss:ChartKind::evaluation,
                  batchSize_,updates_-static_cast<int>(losses_.size()),lossChart_?std::vector<int>{}:evaluationUpdates_);
        };
        if (wide) {
            const int left=(w-2)*3/5, chartH=contentHeight-chartY;
            chart(body,0,chartY,left,chartH,"TRAINING SURVIVAL",scores_,means_,p.green,p,ChartKind::training,batchSize_,episodeOffset_);
            rightChart(left+2,chartY,w-left-2,chartH);
        } else {
            chart(body,0,chartY,w,11,"TRAINING SURVIVAL",scores_,means_,p.green,p,ChartKind::training,batchSize_,episodeOffset_);
            rightChart(0,chartY+12,w,11);
        }
    } else if (page_==2) {
        const int tableW=wide?(w-2)/2:w, inspectorX=wide?tableW+2:0, inspectorY=wide?0:tensorHeight+1, inspectorW=wide?w-tableW-2:w;
        body.box(0,0,tableW,tensorHeight,"NETWORK PARAMETERS",p);
        std::string architecture;
        for (const auto& layer:layers_) {
            if (architecture.empty()) architecture=std::to_string(layer.inputs);
            architecture+=" "+glyph("→","->")+" "+std::to_string(layer.outputs);
        }
        body.put(2,1,architecture+" / "+integer(count_)+" values",p.dim,tableW-4);
        const bool tableDetail=tableW>=56;
        const int shapeX=tableW>=68?25:22, normX=tableW-(tableDetail?27:23), gradX=tableW-(tableDetail?19:12);
        body.put(3,3,"Layer / tensor",p.dim,20);
        if (tableDetail) body.put(shapeX,3,"Shape",p.dim,10);
        body.put(normX,3,"L2 norm",p.dim,10); body.put(gradX,3,"Grad L2",p.dim,7);
        if (tableDetail) body.put(tableW-11,3,glyph("Max |Δ|","Max delta"),p.dim,9);
        for (int i=0;i<static_cast<int>(tensors_.size());++i) {
            const auto& tensor=tensors_[i];
            const auto style=i==selected_?p.selected:p.text;
            body.put(1,4+i,std::string(tableW-2,' '),style);
            body.put(2,4+i,i==selected_?glyph("▸",">"):" ",i==selected_?p.green:p.dim);
            body.put(4,4+i,(tensor.label.empty()?tensorLabel(tensor.name):tensor.label),style,tableDetail && tableW<68?17:20);
            if (tableDetail) body.put(shapeX,4+i,shapeLabel(tensor.shape),style,10);
            body.put(normX,4+i,number(tensor.norm,3),style,10);
            body.put(gradX,4+i,updates_?number(tensor.gradient,3):glyph("—","--"),style,10);
            if (tableDetail) body.put(tableW-11,4+i,updates_?number(tensor.change,5):glyph("—","--"),style,9);
        }
        body.put(2,tensorHeight-2,"L2 = magnitude / gradient = loss sensitivity",p.dim,tableW-4);
        body.box(inspectorX,inspectorY,inspectorW,12,"PARAMETER INSPECTOR",p);
        if (!tensors_.empty()) {
            const auto& tensor=tensors_[selected_];
            body.put(inspectorX+2,inspectorY+1,tensor.name,p.purple,inspectorW-4);
            body.put(inspectorX+2,inspectorY+2,"Element "+integer(element_+1)+" / "+integer(tensor.count)+"  "+glyph("·","/")+"  shape "+shapeLabel(tensor.shape),p.dim,inspectorW-4);
            const bool valid=selected_==inspectedTensor_ && element_==inspectedElement_ && inspectedUpdate_==updates_;
            if (valid) {
                const auto [before,after,gradient,moment,variance]=scalar_;
                body.put(inspectorX+2,inspectorY+4,"["+coordinates_+"]   "+number(before,6)+"  "+glyph("→","->")+"  "+number(after,6),p.bold,inspectorW-4);
                body.put(inspectorX+2,inspectorY+5,glyph("Δ","Change")+" "+number(after-before,6,true),p.green,inspectorW-4);
                body.put(inspectorX+2,inspectorY+7,glyph("∂L/∂θ","dL/dtheta")+"  "+(updates_?number(gradient,6):glyph("—","--")),p.text,inspectorW-4);
                body.put(inspectorX+2,inspectorY+8,"Range "+number(tensor.minimum,4)+" to "+number(tensor.maximum,4),p.dim,inspectorW-4);
                body.put(inspectorX+2,inspectorY+10,updates_?"Snapshot from Adam update "+std::to_string(inspectedUpdate_):"Initial value / no update yet",p.dim,inspectorW-4);
            } else body.put(inspectorX+2,inspectorY+5,"Loading selected value...",p.dim,inspectorW-4);
        }
        const int mathY=mathTop;
        body.box(0,mathY,w,expandedMath_?20:9,"ADAM / UPDATE RULE",p);
        body.put(w-25,mathY,expandedMath_?" F  collapse math ":" F  expand math ",p.purple,22);
        const auto [before,after,gradient,moment,variance]=scalar_;
        const bool valid=inspectedUpdate_>0 && inspectedUpdate_==updates_ && selected_==inspectedTensor_ && element_==inspectedElement_;
        const double mhat=valid?moment/(1-std::pow(beta1_,inspectedUpdate_)):0;
        const double vhat=valid?variance/(1-std::pow(beta2_,inspectedUpdate_)):0;
        if (p.caps.utf8) {
            body.put(2,mathY+2,"θₜ = θₜ₋₁ − η ·",p.bold);
            body.fraction(20,mathY+1,"mᶜₜ","√(vᶜₜ) + ε",p.purple);
            if (w>=90) {
                body.put(38,mathY+2,"previous value − adaptive step",p.dim,w-40);
                body.put(38,mathY+3,"c = bias corrected",p.dim,w-40);
            }
        } else body.put(2,mathY+2,"theta_new = theta_old - rate * m_hat / (sqrt(v_hat) + eps)",p.bold,w-4);
        body.put(2,mathY+5,glyph("η","Rate")+" = "+number(lr_,3)+"   "+glyph("β₁","beta1")+" = "+number(beta1_,3)+"   "+glyph("β₂","beta2")+" = "+number(beta2_,3)+"   "+glyph("ε","eps")+" = "+number(epsilon_),p.text,w-4);
        body.put(2,mathY+6,valid?"Calculated "+glyph("Δθ","change")+" "+number(-lr_*mhat/(std::sqrt(vhat)+epsilon_),7,true)+"  /  stored "+number(after-before,7,true):(updates_?"Loading the selected calculation...":"The calculation appears after the first update."),p.green,w-4);
        if (expandedMath_) {
            body.rule(2,mathY+8,w-4,p.dim);
            body.put(2,mathY+9,glyph("mₜ = β₁mₜ₋₁ + (1 − β₁)gₜ","m = beta1 * previous_m + (1-beta1) * gradient"),p.text,w-4);
            body.put(2,mathY+10,glyph("vₜ = β₂vₜ₋₁ + (1 − β₂)gₜ²","v = beta2 * previous_v + (1-beta2) * gradient^2"),p.text,w-4);
            if (p.caps.utf8) {
                body.put(2,mathY+13,"mᶜₜ =",p.bold);
                body.fraction(9,mathY+12,"mₜ","1 − β₁ᵗ",p.purple);
                body.put(28,mathY+13,"vᶜₜ =",p.bold);
                body.fraction(35,mathY+12,"vₜ","1 − β₂ᵗ",p.purple);
            } else body.put(2,mathY+13,"m_hat = m / (1-beta1^t); v_hat = v / (1-beta2^t)",p.text,w-4);
            body.put(2,mathY+16,valid?"t = "+std::to_string(inspectedUpdate_)+"   m = "+number(moment,6)+"   v = "+number(variance):"m: gradient momentum   v: squared-gradient momentum",p.dim,w-4);
            body.put(2,mathY+17,valid?glyph("mᶜ","m_hat")+" = "+number(mhat,6)+"   "+glyph("vᶜ","v_hat")+" = "+number(vhat,7):"Bias correction accounts for moments starting at zero.",p.dim,w-4);
            body.put(2,mathY+18,glyph("ᶜ: bias corrected   gₜ = ∂L/∂θₜ₋₁   η: rate   ε: stabilizer","g = dL/dtheta; rate: learning rate; eps: numerical stabilizer"),p.dim,w-4);
        }
    } else if (!learningDetails_) {
        const int cardW=(w-4)/3;
        const bool current=rewardSeen_ && rewardBatch_==batch_;
        const int batchEpisodes=completedRewardBatch_==batch_?rewardBatchEpisodes_:0;
        const std::array<std::string,3> titles{"EPISODE RETURN","REWARD / STEP","BATCH MEAN RETURN"};
        const std::array<std::string,3> values{
            current?number(episodeReward_,2):glyph("—","--"),
            current?number(episodeReward_/rewardSteps_,3):glyph("—","--"),
            batchEpisodes?number(rewardBatchReturn_/batchEpisodes,2):glyph("—","--")};
        const std::array<std::string,3> notes{
            current?"Episode "+std::to_string(rewardEpisode_)+" / "+integer(rewardSteps_)+" steps":"Waiting for rewards",
            current?"Last step "+number(stepReward_,3):"Return / steps",
            std::to_string(batchEpisodes)+" completed episodes"};
        for (int i=0;i<3;++i) {
            const int cx=i*(cardW+2),cw=i==2?w-cx:cardW;
            body.box(cx,0,cw,5,titles[i],p);
            body.put(cx+2,2,values[i],i==1?p.caps.strong+p.purple:p.caps.strong+p.green,cw-4);
            body.put(cx+2,3,notes[i],p.dim,cw-4);
        }
        const int chartH=contentHeight-13;
        if (wide) {
            const int left=(w-2)*3/5;
            chart(body,0,6,left,chartH,"EPISODE REWARD",rewardReturns_,{},p.green,p,ChartKind::rewardReturn,batchSize_,0,rewardIndices_);
            chart(body,left+2,6,w-left-2,chartH,"REWARD QUALITY",rewardRates_,{},p.purple,p,ChartKind::rewardRate,batchSize_,0,rewardIndices_);
        } else chart(body,0,6,w,chartH,"EPISODE REWARD",rewardReturns_,{},p.green,p,ChartKind::rewardReturn,batchSize_,0,rewardIndices_);
        const int noteY=contentHeight-6;
        body.box(0,noteY,w,6,"READING THE REWARD",p);
        body.put(2,noteY+1,"Alive: 0.5 + 0.5 x uprightness per step.",p.text,w-4);
        body.put(2,noteY+2,"Terminal step: 0. Return is the sum of step rewards.",p.dim,w-4);
        body.put(2,noteY+3,"Return depends on survival; compare reward / step.",p.dim,w-4);
        body.put(2,noteY+4,"Training rewards / no critic predictions yet / R learning details",p.purple,w-4);
    } else {
        const int tableW=wide?(w-2)*3/5:w, notesX=wide?tableW+2:0, notesY=wide?0:episodeHeight+1, notesW=wide?w-tableW-2:w;
        body.box(0,0,tableW,episodeHeight,"EPISODE DIAGNOSTICS",p);
        body.put(2,1,objectiveUpdate_?"Collected before update "+std::to_string(objectiveUpdate_):"Results appear after "+std::to_string(batchSize_)+" episodes",p.dim,tableW-4);
        const int stepX=8,returnX=16,advX=27,logX=40;
        body.put(2,3,"Ep.",p.dim); body.put(stepX,3,"Steps",p.dim); body.put(returnX,3,"Return",p.dim); body.put(advX,3,"Advantage",p.dim); body.put(logX,3,"Sum log p",p.dim,tableW-logX-2);
        for (int i=0;i<batchSize_;++i) {
            const auto& objective=objectives_[i];
            body.put(4,4+i,std::to_string(i+1),p.text);
            if (!objective.valid) { body.put(stepX,4+i,glyph("—","--"),p.dim); continue; }
            std::ostringstream steps,advantage,log;
            steps<<std::setw(5)<<objective.steps;
            advantage<<std::showpos<<std::fixed<<std::setprecision(5)<<std::setw(9)<<objective.advantage;
            log<<std::fixed<<std::setprecision(3)<<std::setw(10)<<objective.logProbability;
            body.put(stepX,4+i,steps.str(),p.text);
            body.put(returnX,4+i,objective.hasReturn?number(objective.totalReturn,2):glyph("—","--"),p.purple,10);
            body.put(advX,4+i,advantage.str(),objective.advantage>=0?p.green:p.gold);
            body.put(logX,4+i,log.str(),p.text);
        }
        body.put(2,episodeHeight-2,"Episode advantage is diagnostic; loss uses step advantages.",p.dim,tableW-4);
        body.box(notesX,notesY,notesW,14,"LEARNING CYCLE",p);
        const std::array<std::string,4> labels{"Collect "+std::to_string(batchSize_)+" episodes","Compare step returns","Differentiate the loss","Apply one Adam update"};
        const std::array<std::string,4> icons{glyph("○","o"),glyph("≠","!="),glyph("∇","d"),glyph("↻","~")};
        for (int i=0;i<4;++i) {
            body.put(notesX+2,notesY+2+i*3,icons[i],p.purple);
            body.put(notesX+5,notesY+2+i*3,labels[i],p.text,notesW-7);
            if (i<3) {
                body.put(notesX+2,notesY+3+i*3,glyph("│","|"),p.dim);
                body.put(notesX+2,notesY+4+i*3,glyph("↓","v"),p.dim);
            }
        }
        const int formulaY=formulaTop;
        body.box(0,formulaY,w,formulaHeight,"POLICY GRADIENT / REWARD-TO-GO",p);
        if (p.caps.utf8) {
            body.put(2,formulaY+2,"Aᵢₜ =",p.bold);
            body.fraction(10,formulaY+1,"Gᵢₜ − bᵢₜ",integer(episodeLimit_),p.green);
        } else body.put(2,formulaY+2,"A_it = (G_it - b_it) / "+std::to_string(episodeLimit_),p.bold,w-4);
        body.put(2,formulaY+4,glyph("Gᵢₜ = rᵢₜ + γ Gᵢ,ₜ₊₁   γ = ","G_it = r_it + gamma * G_i,t+1   gamma = ")+number(discount_,3),p.purple,w-4);
        if (p.caps.utf8) {
            body.put(2,formulaY+6,"L = −(1/"+std::to_string(batchSize_)+") ∑ᵢ ∑ₜ Aᵢₜ ℓᵢₜ",p.bold,w-4);
            body.put(2,formulaY+7,"ℓᵢₜ = ∑ₖ log πθ(uᵢₜₖ | oᵢₜ)",p.purple,w-4);
        } else {
            body.put(2,formulaY+6,"L = -sum over episodes and steps(A_it * ell_it) / "+std::to_string(batchSize_),p.bold,w-4);
            body.put(2,formulaY+7,"ell_it = sum over outputs: log p(raw sample | obs)",p.purple,w-4);
        }
        body.put(2,formulaY+8,"i: episode   t: simulation step   k: one of "+std::to_string(actions_.size())+" outputs",p.dim,w-4);
        body.put(2,formulaY+9,"b: mean G of other episodes reaching this step; zero if no peers.",p.dim,w-4);
        body.put(2,formulaY+10,"Episode diagnostics compare total returns with the other "+std::to_string(std::max(0,batchSize_-1))+" episodes.",p.dim,w-4);
        body.rule(2,formulaY+11,w-4,p.dim);
        body.put(2,formulaY+12,"FORWARD PASS",p.bold,w-4);
        for (int i=0;i<static_cast<int>(layers_.size());++i) {
            const auto& layer=layers_[i];
            const auto output=i+1==static_cast<int>(layers_.size())?glyph("μ","mu"):"h"+subscript(i+1);
            const auto previous=i==0?"o":"h"+subscript(i);
            std::string activation=layer.activation;
            if (activation=="Tanh") activation="tanh";
            if (activation=="ReLU") activation="relu";
            if (activation=="Sigmoid") activation="sigmoid";
            const auto affine="W"+subscript(i+1)+previous+" + b"+subscript(i+1);
            body.put(2,formulaY+13+i,output+" = "+(activation=="Identity"?affine:activation+"("+affine+")")+"   "+std::to_string(layer.inputs)+" "+glyph("→","->")+" "+std::to_string(layer.outputs),p.text,w-4);
        }
        const int sampleY=formulaY+14+static_cast<int>(layers_.size());
        body.put(2,sampleY,glyph("u ∼ N(μ, σ²)   σ = ","u ~ Normal(mu, sigma)   sigma = ")+number(sigma_,3)+"  "+glyph("→","->")+"  a = clip(u, "+number(sampleLow_,1)+", "+number(sampleHigh_,1)+")",p.purple,w-4);
        body.put(2,sampleY+1,"The loss uses raw samples u; the body receives scaled actions a.",p.dim,w-4);
        for (int i=0;i<static_cast<int>(actions_.size());++i) {
            const auto& action=actions_[i];
            body.put(2,sampleY+2+i,action.motorKnown && !action.enabled ? action.label+" / motor off / target is not applied" : action.label+" = "+number(action.scale,3)+(action.nonnegative?" max(0, a)":" a")+" rad",action.motorKnown && !action.enabled?p.gold:p.text,w-4);
        }
        body.put(2,sampleY+3+static_cast<int>(actions_.size()),"Training uses balance return; evaluation and best selection use survival.",p.dim,w-4);
    }
    scrollMax_=std::max(0,contentHeight-viewport_);
    scroll_[page_-1]=std::clamp(scroll_[page_-1],0,scrollMax_);
    screen.copy(body,x,bodyTop,scroll_[page_-1],viewport_);
    if (scrollMax_>0) {
        const int track=viewport_, thumb=std::max(1,viewport_*viewport_/contentHeight);
        const int top=(track-thumb)*scroll_[page_-1]/std::max(1,scrollMax_);
        for (int row=0;row<track;++row) screen.put(x+w,bodyTop+row,glyph(row>=top && row<top+thumb?"┃":"│",row>=top && row<top+thumb?"#":"|"),row>=top && row<top+thumb?p.purple:p.dim);
    }
    screen.rule(x,rows-3,w,p.dim);
    const std::string navigation=page_==2?glyph("↑↓ tensor  ←→ value","Up/Down tensor  Left/Right value")+(w>=100?"  [ ] jump  F math":"  F math"):
        page_==3?std::string(learningDetails_?"R reward viewer":"R learning details")+(scrollMax_>0?"   "+glyph("↑↓ scroll","Up/Down scroll"):""):
        page_==1?std::string(lossChart_?"G evaluation chart":"G loss chart")+"   T joint targets":glyph("↑↓ scroll  1 Overview  2 Parameters","Up/Down scroll  1 Overview  2 Parameters");
    const int controlsWidth=w>=90?49:33;
    screen.put(x,rows-2,navigation,p.text,w-controlsWidth-1);
    screen.put(x+w-controlsWidth,rows-2,(visual_?"V headless  ":"V visual  ")+std::string(paused_?"P resume  Q back / Esc home":phase_=="ready"?"N next  Q back / Esc home":"P pause  Q back / Esc home"),p.purple,controlsWidth);
    const std::string mode=visual_?"Visual training":"Headless training";
    const std::string bottom=exited?"Worker exited / "+error_:!viewerError_.empty()?"Viewer unavailable / "+viewerError_:phase_=="ready"?glyph("●","*")+" Ready  "+glyph("·","/")+"  "+mode+"  "+glyph("·","/")+"  "+(checkpoint_.empty()?"Checkpoint pending":"Saved / "+checkpoint_):paused_?glyph("Ⅱ","||")+" Paused / "+mode:mode+"  "+glyph("·","/")+"  "+(updates_?"Latest gradient norm "+number(gradient_,3):"Collecting experience before the first update");
    screen.put(x,rows-1,bottom,exited?p.caps.error_color:!viewerError_.empty()?p.gold:p.dim,w-(scrollMax_>0?23:0));
    if (scrollMax_>0) screen.put(x+w-23,rows-1,"PgUp/PgDn / wheel scroll",p.dim,23);
    return screen.lines();
}
}
