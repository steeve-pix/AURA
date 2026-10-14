#include "CriticScreen.hpp"
#include "TrainingCharts.hpp"

namespace aura::cli {
void CriticScreen::receive(const std::string& line) {
    if (!line.starts_with("AURA_CRITIC\t")) { if (!line.empty()) error_=line; return; }
    std::istringstream input(line.substr(12));
    std::string kind; input>>kind;
    if (kind=="start") {
        std::getline(input>>std::ws,source_,'\t');
        if (input>>inputs_>>actions_>>trainingEpisodes_>>validationEpisodes_>>budget_>>seed_>>rate_>>discount_>>scale_
            && inputs_>0 && actions_>0 && trainingEpisodes_>0 && validationEpisodes_>0 && budget_>0
            && std::isfinite(rate_) && rate_>0 && std::isfinite(discount_) && discount_>=0 && discount_<=1 && std::isfinite(scale_) && scale_>0) { phase_="training"; progressAt_=elapsed_; }
    } else if (kind=="phase") {
        std::string phase; input>>phase;
        if (phase=="training" || phase=="validation" || phase=="optimizing") { phase_=phase; progressAt_=elapsed_; }
    } else if (kind=="progress") {
        std::string phase; int episode,total,steps;
        if (input>>phase>>episode>>total>>steps && episode>0 && episode<=total && steps>=0) {
            if (phase=="training" && total==trainingEpisodes_) collectedTraining_=episode;
            if (phase=="validation" && total==validationEpisodes_) collectedValidation_=episode;
            steps_=steps; progressAt_=elapsed_;
        }
    } else if (kind=="baseline") {
        double baseline,constant; int training,validation;
        if (input>>baseline>>constant>>training>>validation && baseline>=0 && std::isfinite(baseline)
            && std::isfinite(constant) && training>0 && validation>0) {
            baseline_=baseline; constant_=constant; trainingCount_=training; validationCount_=validation;
        }
    } else if (kind=="metric") {
        int update; double training,validation;
        if (input>>update>>training>>validation && update>=0 && update<=budget_
            && (updates_.empty() || update>updates_.back()) && std::isfinite(training) && training>=0
            && std::isfinite(validation) && validation>=0) {
            progressAt_=elapsed_;
            updates_.push_back(update); training_.push_back(training); validation_.push_back(validation);
        }
    } else if (kind=="saved") {
        std::getline(input>>std::ws,report_,'\t'); std::getline(input>>std::ws,checkpoint_);
    } else if (kind=="result") {
        double actor,critic; int noGradients;
        if (input>>actor>>noGradients>>critic && std::isfinite(actor) && actor>=0 && std::isfinite(critic) && critic>=0
            && !updates_.empty() && updates_.back()==budget_) {
            actorChange_=actor; criticChange_=critic; unchanged_=actor==0 && noGradients==1;
            phase_=unchanged_?"complete":"failed";
            if (!unchanged_) error_="Frozen actor integrity check failed";
        }
    } else if (kind=="stopped") phase_="stopped";
    else if (kind=="error") { std::getline(input>>std::ws,error_); phase_="failed"; }
}
std::string CriticScreen::key(const std::string& key) {
    if (key=="\033[A" || key=="\033OA" || key=="scroll_up") scroll_=std::max(0,scroll_-1);
    else if (key=="\033[B" || key=="\033OB" || key=="scroll_down") scroll_=std::min(scrollMax_,scroll_+1);
    else if (key=="\033[5~") scroll_=std::max(0,scroll_-std::max(1,viewport_-2));
    else if (key=="\033[6~") scroll_=std::min(scrollMax_,scroll_+std::max(1,viewport_-2));
    else if (key=="\033[H" || key=="\033OH") scroll_=0;
    else if (key=="\033[F" || key=="\033OF") scroll_=scrollMax_;
    return "";
}
std::vector<std::string> CriticScreen::frame(int width,int rows,bool exited,double elapsedSeconds) {
    using namespace screen;
    const Palette p;
    Canvas c(width,rows);
    const int w=std::max(1,std::min(156,width-4)),x=(width-w)/2;
    const bool complete=phase_=="complete",failed=phase_=="failed" || (exited && !complete && phase_!="stopped");
    const bool busy=!complete && !failed && phase_!="stopped";
    if (busy && std::isfinite(elapsedSeconds)) elapsed_=std::max(elapsed_,std::max(0.,elapsedSeconds));
    const std::array<std::string,4> spinnerFrames{"\\","|","/","-"};
    const auto spinner=spinnerFrames[static_cast<std::size_t>(std::fmod(elapsed_,.8)*5)];
    const auto elapsedText=number(elapsed_,0)+"s elapsed";
    const auto quiet=std::max(0.,elapsed_-progressAt_);
    const auto status=complete?"COMPLETE":failed?"FAILED":phase_=="stopped"?"STOPPED":phase_=="optimizing"?"FITTING CRITIC":phase_=="validation"?"VALIDATION EPISODES":phase_=="training"?"TRAINING EPISODES":"LOADING";
    c.heading(x,w,"CRITIC TRAINING LAB",p,(busy?spinner+" ":"")+status,complete?p.green:failed?p.gold:p.purple);
    c.put(x,3,"Actor: "+(source_.empty()?std::string("loading..."):source_)+"  "+glyph("·","/")+"  FROZEN",p.bold,w);
    c.put(x,4,std::to_string(trainingEpisodes_)+" training episodes  "+glyph("·","/")+"  "+std::to_string(validationEpisodes_)+" held out  "+glyph("·","/")+"  "+std::to_string(budget_)+" critic updates",p.dim,w);
    c.put(x,5,"Fresh critic / seed "+std::to_string(seed_)+"  "+glyph("·","/")+"  Adam "+number(rate_,3)+"  "+glyph("·","/")+"  Return scale "+number(scale_,0),p.dim,w);
    if (width<60 || rows<20) {
        c.put(x,7,"Enlarge terminal: 60 columns / 20 rows",p.dim,w);
        c.put(x,9,failed?error_:"Actor optimizer updates: 0",failed?p.gold:p.green,w);
        c.put(x,rows-2,"Q back / Esc home",p.purple,w);
        return c.lines();
    }
    viewport_=rows-10;
    const bool wide=w>=108;
    const int columns=wide?4:2, cardW=(w-(columns-1)*2)/columns;
    const int progressY=wide?6:12, graphY=progressY+6;
    const int chartH=wide?std::max(11,viewport_-graphY):11;
    const int contentHeight=graphY+(wide?chartH:chartH*2+1);
    Canvas body(w,contentHeight);
    const bool measured=!updates_.empty();
    const double improvement=baseline_>0 && measured?100*(baseline_-validation_.back())/baseline_:0;
    const std::array<std::string,4> labels{"TRAINING MSE","VALIDATION MSE","BASELINE MSE","VS BASELINE"};
    const auto pendingValue=busy?spinner+" Waiting":glyph("—","--");
    const std::array<std::string,4> values{
        measured?number(training_.back(),5):pendingValue, measured?number(validation_.back(),5):pendingValue,
        baseline_>=0?number(baseline_,5):pendingValue, baseline_>0 && measured?number(std::abs(improvement),1)+"% "+(improvement>=0?"lower":"higher"):pendingValue};
    const std::array<std::string,4> notes{"Optimizer dataset only","Unseen episodes only","Training mean predictor","Validation error"};
    for (int i=0;i<4;++i) {
        const int cx=(i%columns)*(cardW+2),cy=(i/columns)*6;
        body.box(cx,cy,cardW,5,labels[i],p);
        body.put(cx+2,cy+2,values[i],i==0?p.green:i==2?p.gold:i==3?(improvement<0?p.gold:p.green):p.purple,cardW-4);
        body.put(cx+2,cy+3,notes[i],p.dim,cardW-4);
    }
    body.box(0,progressY,w,5,complete?"RUN COMPLETE":failed?"RUN FAILED":"EXPERIMENT PROGRESS",p);
    std::string progress;
    if (complete) progress="Actor change "+number(actorChange_,8)+" / no actor gradients / 0 actor updates";
    else if (failed) progress=error_.empty()?"Worker exited before completion":error_;
    else if (phase_=="optimizing") progress="Critic update "+std::to_string(measured?updates_.back():0)+" / "+std::to_string(budget_)+"  /  validation never enters optimization";
    else if (phase_=="stopped") progress="Cancelled / actor checkpoint untouched";
    else progress="Training "+std::to_string(collectedTraining_)+" / "+std::to_string(trainingEpisodes_)+"  "+glyph("·","/")+"  Validation "+std::to_string(collectedValidation_)+" / "+std::to_string(validationEpisodes_)+"  "+glyph("·","/")+"  Last episode "+integer(steps_)+" steps";
    body.put(2,progressY+2,progress,failed?p.gold:p.text,w-4);
    const std::string detail=complete?(report_.empty()?"No artifact report received":"Critic and report saved separately / models/critics/"):inputs_>0?std::to_string(inputs_)+" critic inputs "+glyph("→","->")+" 1 value  /  "+std::to_string(actions_)+" frozen actor actions  /  discount "+number(discount_,3):"Preparing frozen actor and fresh critic...";
    body.put(2,progressY+3,phase_=="optimizing" && trainingCount_>0?"Training "+integer(trainingCount_)+" / held out "+integer(validationCount_)+" timesteps  /  constant prediction "+number(constant_,5):detail,p.dim,w-4);
    if (busy && !measured) body.put(2,progressY+3,elapsedText+" / last progress "+number(quiet,0)+"s ago / "+
        (phase_=="optimizing"?"Preparing first error measurement":"Collecting datasets before critic updates"),p.dim,w-4);
    const int left=wide?(w-2)*3/5:w;
    chart(body,0,graphY,left,chartH,"TRAINING ERROR",training_,{},p.green,p,ChartKind::criticTraining,0,0,updates_);
    chart(body,wide?left+2:0,wide?graphY:graphY+chartH+1,wide?w-left-2:w,chartH,"VALIDATION ERROR",validation_,{},p.purple,p,ChartKind::criticValidation,0,0,updates_,baseline_>=0?baseline_:std::numeric_limits<double>::quiet_NaN());
    if (!measured) {
        const auto waiting=failed?std::string("No measurements / experiment failed"):phase_=="stopped"?std::string("Stopped before error measurements"):
            phase_=="loading"?spinner+" Preparing actor and critic...":phase_=="training"?
            spinner+" Collecting training episodes "+integer(collectedTraining_)+" / "+integer(trainingEpisodes_):phase_=="validation"?
            spinner+" Collecting validation episodes "+integer(collectedValidation_)+" / "+integer(validationEpisodes_):spinner+" Calculating initial prediction errors...";
        const auto placeholder=[&](int cx,int cy,int cw) {
            const int middle=cy+chartH/2;
            body.put(cx+3,middle,std::string(std::max(0,cw-6),' '),p.dim,cw-6);
            body.put(cx+3,middle,waiting,failed?p.gold:busy?p.purple:p.dim,cw-6);
            body.put(cx+3,middle+1,busy?"Curves appear after both datasets are collected.":"Actor checkpoint remains unchanged.",p.dim,cw-6);
            body.put(cx+3,middle+2,elapsedText+(busy?" / last progress "+number(quiet,0)+"s ago":""),p.dim,cw-6);
            if (busy && quiet>=30) body.put(cx+3,middle+3,"No recent progress. Q back / Esc home stops safely.",p.gold,cw-6);
        };
        placeholder(0,graphY,left);
        placeholder(wide?left+2:0,wide?graphY:graphY+chartH+1,wide?w-left-2:w);
    }
    scrollMax_=std::max(0,contentHeight-viewport_); scroll_=std::clamp(scroll_,0,scrollMax_);
    c.copy(body,x,7,scroll_,viewport_);
    c.footer(x,w,scrollMax_?glyph("↑↓ / PgUp / PgDn scroll","Up/Down / PgUp / PgDn scroll"):"Actor frozen / critic only","Q back / Esc home",
        "MSE uses returns / 100. Gold dots: constant validation baseline. Each run starts a fresh critic.",p);
    return c.lines();
}
}
