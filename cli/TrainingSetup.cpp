#include "TrainingSetup.hpp"
#include "ScreenCanvas.hpp"
#include <array>

namespace aura::cli {
std::vector<std::string> training_setup_frame(int selected, bool hasModels, const std::string& stage,
    const std::string& value, int width, int height) {
    using namespace screen;
    const Palette p;
    Canvas c(width,height);
    const int w=std::max(1,std::min(100,width-4)),x=(width-w)/2;
    c.heading(x,w,"TRAINING SETUP",p);
    if (width<60 || height<18) {
        c.put(x,4,"Enlarge terminal: 60 columns / 18 rows",p.dim,w);
        c.put(x,height-2,"Q back / Esc home",p.text,w);
        return c.lines();
    }
    const int spacing=height>=22?3:2;
    c.box(x,4,w,stage=="mode"?3*spacing+5:stage=="critic_source"?8:11,stage=="critic_source"?"FROZEN ACTOR SOURCE":stage=="name"?"NEW MODEL":stage=="confirm"?"RESTART MODEL":"CHOOSE HOW TO TRAIN",p);
    if (stage=="mode") {
        const std::array<std::string,4> names{"1  Resume model","2  New model","3  Restart model","4  Critic only"};
        const std::array<std::string,4> hints{"Continue saved weights, Adam state and episode count",
            "Fresh network / a unique filename / keep existing models",
            "Fresh network / replace a selected model after confirmation",
            "Freeze an actor / train a fresh critic / validate predictions"};
        for (int i=0;i<4;++i) {
            const bool enabled=hasModels || i==1 || i==3;
            if (selected==i) for (int row=0;row<2;++row)
                c.put(x+1,6+i*spacing+row,std::string(w-2,' '),p.selected,w-2);
            c.put(x+2,6+i*spacing,selected==i?glyph("▸",">"):" ",p.green);
            c.put(x+4,6+i*spacing,names[i]+(!enabled?" / no saved models":""),!enabled?p.dim:selected==i?p.selected:p.text,w-6);
            c.put(x+4,7+i*spacing,hints[i],selected==i?p.caps.shell_style+p.dim:p.dim,w-6);
        }
    } else if (stage=="critic_source") {
        const std::array<std::string,2> names{"1  Fresh actor / seed 42","2  Freeze a saved actor"};
        const std::array<std::string,2> hints{"Reproduce the initial critic experiment","Read a checkpoint snapshot / leave its weights and Adam state untouched"};
        for (int i=0;i<2;++i) {
            if (i==selected) for (int row=0;row<2;++row) c.put(x+1,6+i*3+row,std::string(w-2,' '),p.selected,w-2);
            c.put(x+2,6+i*3,i==selected?glyph("▸",">"):" ",p.green);
            c.put(x+4,6+i*3,names[i]+(i==1 && !hasModels?" / no saved models":""),i==selected?p.selected:p.text,w-6);
            c.put(x+4,7+i*3,hints[i],i==selected?p.caps.shell_style+p.dim:p.dim,w-6);
        }
        c.put(x,13,"32 training / 16 validation episodes / 20 critic updates",p.text,w);
        if (height>=20) c.put(x,14,"Adam 0.001 / seed 43 / return scale 100 / fresh critic each run",p.dim,w);
    } else if (stage=="name") {
        c.put(x+3,6,"Model name",p.dim,w-6);
        c.put(x+3,8,value+glyph("▏","_"),p.selected,w-6);
        c.put(x+3,11,"Letters, numbers, underscores and hyphens",p.dim,w-6);
        c.put(x+3,12,"Existing names receive a unique numeric suffix.",p.dim,w-6);
    } else {
        c.put(x+3,6,value,p.bold,w-6);
        c.put(x+3,9,"Reset weights, Adam state and episode count?",p.gold,w-6);
        c.put(x+3,11,"Starting replaces this checkpoint and its saved best.",p.dim,w-6);
        c.put(x+3,12,"Other models are preserved.",p.dim,w-6);
    }
    c.footer(x,w,stage=="confirm"?"Y restart   N cancel":stage=="name"?"Enter create   Ctrl+U clear":stage=="critic_source"?glyph("↑↓ choose   Enter start","Up/Down choose   Enter start"):glyph("↑↓ choose   Enter continue","Up/Down choose   Enter continue"),
             stage=="name"?"Esc cancel edit":"Q back / Esc home",stage=="critic_source"?"Critic artifacts are saved separately / actor optimizer updates: 0":"Choose the model lifecycle before starting training",p);
    return c.lines();
}
}
