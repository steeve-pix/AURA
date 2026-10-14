#include "EvaluationScreen.hpp"
#include "ScreenCanvas.hpp"
#include <array>

namespace aura::cli {
void EvaluationScreen::receive(const std::string& line) {
    if (!line.starts_with("AURA_EVAL\t")) { if (!line.empty()) error_=line; return; }
    std::istringstream input(line.substr(10));
    std::string kind; input>>kind;
    if (kind=="start") {
        std::getline(input>>std::ws,model_,'\t');
        if (input>>episodes_>>updates_>>limit_ && episodes_>=0 && updates_>=0 && limit_>0) phase_="running";
    } else if (kind=="interface") {
        int observationSchema,actionSchema;
        input>>inputs_>>outputs_>>observationSchema>>actionSchema;
    } else if (kind=="motors") {
        int active,total;
        if (input>>active>>total && total>0 && active>=0 && active<=total) {
            activeMotors_=active; totalMotors_=total;
            std::getline(input>>std::ws,disabledMotors_);
        }
    } else if (kind=="progress") {
        int steps;
        if (input>>steps && steps>=0 && steps<=limit_) steps_=steps;
    } else if (kind=="result") {
        int steps; std::string previous,outcome; double seconds;
        if (!(input>>steps>>previous>>outcome>>seconds) || steps<0 || steps>limit_ || !std::isfinite(seconds) || seconds<0) return;
        int before=0;
        if (previous!="-") {
            std::istringstream value(previous);
            char extra;
            if (!(value>>before) || value>>extra || before<0 || before>limit_) return;
        }
        steps_=steps; previous_=before; hasPrevious_=previous!="-"; outcome_=outcome; seconds_=seconds; phase_="complete";
    } else if (kind=="stopped") phase_="stopped";
    else if (kind=="error") { std::getline(input>>std::ws,error_); phase_="failed"; }
}

std::vector<std::string> EvaluationScreen::frame(int width,int rows,bool exited) {
    using namespace screen;
    const Palette p;
    Canvas c(width,rows);
    const int w=std::max(1,std::min(120,width-4)),x=(width-w)/2;
    const bool complete=phase_=="complete",failed=phase_=="failed" || (exited && !complete && phase_!="stopped");
    const std::string status=complete?"COMPLETE":failed?"FAILED":phase_=="stopped"?"STOPPED":phase_=="running"?"EVALUATING":"LOADING";
    c.heading(x,w,"POLICY EVALUATION",p,glyph(complete?"●":failed?"!":"↻",complete?"*":failed?"!":"o")+" "+status,
              complete?p.green:failed?p.gold:p.purple);
    c.put(x,3,model_.empty()?"Loading checkpoint...":model_,p.bold,w);
    if (!model_.empty()) c.put(x,4,integer(episodes_)+" training episodes  "+glyph("·","/")+"  "+integer(updates_)+" Adam updates",p.dim,w);
    if (inputs_>0) {
        std::string contract=integer(inputs_)+" inputs "+glyph("→","->")+" "+integer(outputs_)+" actions";
        if (activeMotors_>=0) contract+="  /  "+integer(activeMotors_)+"/"+integer(totalMotors_)+" motors";
        if (!disabledMotors_.empty()) contract+="  /  "+disabledMotors_+" off";
        c.put(x,5,contract,disabledMotors_.empty()?p.dim:p.gold,w);
    }
    if (width<60 || rows<19) {
        c.put(x,6,status+" / "+integer(steps_)+" steps",p.text,w);
        c.put(x,8,failed?error_:"Enlarge terminal: 60 columns / 19 rows",failed?p.gold:p.dim,w);
        c.put(x,rows-2,"Q back / Esc home",p.purple,w);
        return c.lines();
    }
    const int cardW=(w-4)/3;
    const int change=steps_-previous_;
    const std::string delta=!complete?glyph("—","--"):!hasPrevious_?"First run":
        (change>0?glyph("↑ ","+ "):change<0?glyph("↓ ","- "):"= ")+integer(std::abs(change))+" steps";
    const std::array<std::string,3> labels{"SURVIVAL","PREVIOUS","CHANGE"};
    const std::array<std::string,3> values{integer(steps_)+" steps",complete && hasPrevious_?integer(previous_)+" steps":glyph("—","--"),delta};
    for (int i=0;i<3;++i) {
        const int cx=x+i*(cardW+2);
        c.box(cx,7,cardW,5,labels[i],p);
        c.put(cx+2,9,values[i],i==2 && complete && hasPrevious_?(change>0?p.green:change<0?p.gold:p.dim):p.bold,cardW-4);
    }
    if (!complete && !failed && phase_!="stopped") {
        const int span=w-4;
        const int filled=limit_>0?std::clamp(steps_*span/limit_,0,span):0;
        for (int i=0;i<span;++i) c.put(x+2+i,12,glyph(i<filled?"━":"─",i<filled?"=":"-"),i<filled?p.purple:p.dim);
        c.put(x+2,14,"Running one episode / limit "+integer(limit_)+" steps",p.dim,w-4);
    } else if (complete) {
        const int resultY=rows>=24?15:13;
        if (rows>=24) c.box(x,13,w,6,"EVALUATION RESULT",p);
        c.put(x+2,resultY,(outcome_=="fallen"?"Fallen":outcome_=="time_limit"?"Time limit reached":outcome_)+"  "+glyph("·","/")+"  "+number(seconds_,2)+" s simulated",p.text,w-4);
        c.put(x+2,resultY+1,hasPrevious_?"Compared with this model's previous recorded evaluation":"No previous comparable evaluation / baseline recorded",p.dim,w-4);
    } else c.put(x+2,13,failed?error_:"Evaluation cancelled / no result recorded",failed?p.gold:p.dim,w-4);
    c.footer(x,w,"",complete || failed || phase_=="stopped"?"Q back / Esc home":"Q cancel / Esc home",
             "One episode  "+glyph("·","/")+"  No exploration  "+glyph("·","/")+"  No weight updates",p);
    return c.lines();
}
}
