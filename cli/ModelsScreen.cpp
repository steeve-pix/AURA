#include "ModelsScreen.hpp"
#include "ScreenCanvas.hpp"
#include <chrono>
#include <ctime>
#include <cctype>

namespace aura::cli {
bool model_is_best(const SavedModel& model) {
    return model.path.stem().string().ends_with("_best");
}
std::string model_family(const SavedModel& model) {
    auto name=model.path.stem().string();
    if (model_is_best(model)) name.resize(name.size()-5);
    return name;
}
std::optional<std::size_t> best_model_index(const std::vector<SavedModel>& models, std::size_t selected) {
    if (selected>=models.size()) return std::nullopt;
    for (std::size_t i=0;i<models.size();++i)
        if (model_is_best(models[i]) && model_family(models[i])==model_family(models[selected])) return i;
    return std::nullopt;
}

std::string model_size(std::uintmax_t bytes) {
    if (bytes<1024) return std::to_string(bytes)+" B";
    double value=static_cast<double>(bytes);
    const char* units[]{"B","KiB","MiB","GiB","TiB"};
    int unit=0;
    while (value>=1024 && unit<4) { value/=1024; ++unit; }
    return screen::number(value,1)+" "+units[unit];
}

std::string model_updated(std::filesystem::file_time_type modified, bool dateOnly) {
    const auto systemTime=std::chrono::file_clock::to_sys(modified);
    const auto time=std::chrono::system_clock::to_time_t(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(systemTime));
    std::tm local{};
    if (!localtime_r(&time,&local)) return "Unavailable";
    std::ostringstream text;
    text<<std::put_time(&local,dateOnly?"%Y-%m-%d":"%Y-%m-%d %H:%M");
    return text.str();
}

std::vector<SavedModel> saved_models(const std::filesystem::path& directory, std::string& error) {
    std::vector<SavedModel> models;
    error.clear();
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) && !ec) return models;
    for (std::filesystem::directory_iterator it(directory,ec), end; !ec && it!=end; it.increment(ec)) {
        if (it->path().extension()==".pt" && it->is_regular_file(ec)) {
            const auto bytes=it->file_size(ec);
            if (ec) break;
            const auto modified=it->last_write_time(ec);
            if (!ec) models.push_back({it->path(),bytes,modified});
        }
    }
    if (ec) error="Cannot read models folder: "+ec.message();
    std::sort(models.begin(),models.end(),[](const auto& a,const auto& b){const auto familyA=model_family(a),familyB=model_family(b);
        if (familyA!=familyB) return familyA<familyB;
        if (model_is_best(a)!=model_is_best(b)) return !model_is_best(a);
        return a.path.filename()<b.path.filename();});
    for (std::size_t i=0;i<models.size();++i) models[i].hasBestCopy=best_model_index(models,i).has_value();
    return models;
}

std::vector<SavedModel> filter_models(const std::vector<SavedModel>& models, const std::string& query) {
    const auto lower=[](std::string text) {
        for (auto& ch:text) ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return text;
    };
    std::istringstream words(lower(query));
    std::vector<std::string> terms;
    for (std::string term;words>>term;) terms.push_back(term);
    std::vector<SavedModel> matches;
    for (const auto& model:models) {
        const auto name=lower(model.path.filename().string());
        if (std::all_of(terms.begin(),terms.end(),[&](const auto& term){return name.find(term)!=std::string::npos;}))
            matches.push_back(model);
    }
    return matches;
}

void apply_model_catalog(std::vector<SavedModel>& models, const std::string& catalog) {
    const auto filenameHex=[](const std::string& name) {
        constexpr char digits[]="0123456789abcdef";
        std::string result;
        for (unsigned char c : name) { result+=digits[c>>4]; result+=digits[c&15]; }
        return result;
    };
    std::istringstream lines(catalog);
    std::string line;
    while (std::getline(lines,line)) {
        if (line.starts_with("AURA_MODEL_INFO\t")) {
            std::istringstream info(line.substr(16));
            std::string name;
            int episodes=-1,updates=-1,best=-1;
            if (info>>name>>episodes>>updates>>best && episodes>=-1 && updates>=-1 && best>=-1)
                for (auto& model : models) if (filenameHex(model.path.filename().string())==name) {
                    model.episodes=episodes; model.updates=updates; model.bestEvaluation=best;
                }
            continue;
        }
        if (!line.starts_with("AURA_MODEL\t")) continue;
        std::istringstream fields(line.substr(11));
        std::string name,status,detail;
        int inputs=0,outputs=0;
        if (!(fields>>name>>status>>inputs>>outputs) || inputs<0 || outputs<0 ||
            (status!="compatible" && status!="incompatible" && status!="unreadable")) continue;
        std::getline(fields>>std::ws,detail);
        for (auto &model : models) if (filenameHex(model.path.filename().string())==name) {
            model.contractState=status; model.contractDetail=detail;
            model.inputs=inputs; model.outputs=outputs;
        }
    }
}

bool model_selectable(const SavedModel& model, const std::string& action) {
    return action=="restart" || model.contractState=="compatible" || model.contractState=="unknown";
}

std::vector<std::string> center_models_frame(const std::vector<SavedModel>& models, std::size_t selected,
    const std::string& error, int width, int height, const std::vector<std::string>& logs, const std::string& query, bool searching, std::size_t total) {
    using namespace screen;
    const Palette p;
    Canvas c(width,height);
    const int x=2,w=std::max(1,width-4);
    c.put(x,0,"AURA / COMMAND CENTER",p.text,w);
    c.put(x,2,"1 Model library",p.purple,w);
    c.put(x+19,2,"2 Model logs",p.dim,w-19);
    if (width<60 || height<20) {
        c.put(x,4,"Enlarge terminal: 60 columns / 20 rows",p.dim,w);
        c.put(x,height-1,"Q back / Esc home",p.text,w);
        return c.lines();
    }
    const auto count=query.empty()?std::to_string(models.size())+" checkpoints":
        std::to_string(models.size())+" / "+std::to_string(total?total:models.size())+" checkpoints";
    const int countWidth=textCells(count);
    c.put(x,4,"Search  "+(query.empty() && !searching?std::string("[/] Filter models"):query)+(searching?glyph("▏","_"):""),searching?p.text:p.dim,std::max(1,w-countWidth-3));
    c.put(x+w-countWidth,4,count,p.dim,countWidth);
    const bool split=width>=100 && height>=24;
    const int libraryWidth=split?w*3/5:w;
    const int detailX=x+libraryWidth+3,detailWidth=width-detailX-2;
    const int logY=height-8;
    const int rows=std::max(1,logY-9);
    c.put(x,6,"VERSION / MODEL",p.dim,libraryWidth-26);
    c.put(x+libraryWidth-25,6,"STATE",p.dim,14);
    c.put(x+libraryWidth-10,6,"SIZE",p.dim,10);
    if (models.empty()) c.put(x,8,query.empty()?"No saved models. Use /train to create one.":"No matching models. Press / to edit your search.",p.dim,libraryWidth);
    else {
        selected=std::min(selected,models.size()-1);
        const auto first=selected>=static_cast<std::size_t>(rows)?selected-rows+1:0;
        for (std::size_t i=first;i<models.size() && i<first+rows;++i) {
            const int y=8+static_cast<int>(i-first);
            const auto style=i==selected?p.selected:p.text;
            if (i==selected) c.put(x,y,std::string(libraryWidth,' '),p.selected,libraryWidth);
            const bool best=model_is_best(models[i]);
            const bool paired=models[i].hasBestCopy || best_model_index(models,i).has_value();
            c.put(x,y,i==selected?glyph("▸ ","> "):"  ",style,2);
            c.put(x+2,y,best?"Best   ":paired?"Latest ":"Saved  ",i==selected?p.selected:best?p.green:p.dim,7);
            c.put(x+9,y,ellipsize(models[i].path.filename().string(),libraryWidth-35),style,libraryWidth-35);
            const bool incompatible=models[i].contractState=="incompatible";
            c.put(x+libraryWidth-25,y,(incompatible?glyph("⚠ ","! "):std::string{})+models[i].contractState,
                incompatible?style+p.gold+p.caps.strong:style,14);
            c.put(x+libraryWidth-10,y,model_size(models[i].bytes),style,10);
        }
        const auto& model=models[selected];
        if (split) {
            c.put(detailX,6,"MODEL DETAILS",p.dim,detailWidth);
            c.put(detailX,8,ellipsize(model.path.filename().string(),detailWidth),p.text,detailWidth);
            c.put(detailX,9,model_is_best(model)?"Best checkpoint / saved evaluation winner":(model.hasBestCopy || best_model_index(models,selected))?"Latest checkpoint / resume training":"Saved checkpoint / no best copy",model_is_best(model)?p.green:p.dim,detailWidth);
            c.put(detailX,10,integer(model.inputs)+" inputs / "+integer(model.outputs)+" actions",p.dim,detailWidth);
            c.put(detailX,11,(model.contractState=="incompatible"?glyph("⚠ ","! "):std::string{})+model.contractState,
                model_selectable(model,"watch")?p.green:p.gold+p.caps.strong,detailWidth);
            c.put(detailX,12,model.episodes>=0?integer(model.episodes)+" saved episodes / "+integer(model.updates)+" updates":"Training history unavailable",p.dim,detailWidth);
            c.put(detailX,13,model.bestEvaluation>=0?"Saved best: "+integer(model.bestEvaluation)+" steps":"No best score stored",p.green,detailWidth);
            c.put(detailX,15,"Updated "+model_updated(model.modified),p.dim,detailWidth);
        }
    }
    c.put(x,logY,"MODEL LOG / current session",p.dim,w);
    if (logs.empty()) c.put(x,logY+2,models.empty()?"Choose a matching model to view its activity.":"No runs for this model in this session. E evaluates; T resumes training.",p.dim,w);
    else for (std::size_t i=0;i<logs.size() && i<4;++i) c.put(x,logY+2+static_cast<int>(i),logs[i],i==0?p.text:p.dim,w);
    if (!error.empty()) c.put(x,height-2,error,p.gold,w);
    int hintX=x;
    const auto hint=[&](const std::string& key,const std::string& action) {
        const auto label="["+key+"]";
        c.put(hintX,height-1,label,p.purple+p.caps.strong,std::max(0,x+w-hintX));
        hintX+=textCells(label);
        c.put(hintX,height-1," "+action+"  ",p.dim,std::max(0,x+w-hintX));
        hintX+=textCells(action)+3;
    };
    if (searching) {
        c.put(hintX,height-1,"Type to filter  ",p.dim,w);
        hintX+=16;
        hint("Enter","Finish"); hint("Ctrl+U","Clear"); hint("Esc","Cancel search");
    } else {
        hint("/","Search"); hint("Up/Down","Choose"); hint("Enter","Watch");
        hint("E","Evaluate"); hint("T","Resume"); hint("B","Best");
        hint("2","Logs"); hint("Q","Back"); hint("Esc","Home");
    }
    return c.lines();
}

std::vector<std::string> models_frame(const std::vector<SavedModel>& models, std::size_t selected,
                                    const std::string& error, int width, int height, const std::string& action) {
    if (action=="center") return center_models_frame(models,selected,error,width,height,{});
    using namespace screen;
    const Palette p;
    Canvas c(width,height);
    const int w=std::max(1,std::min(120,width-4)),x=(width-w)/2;
    c.heading(x,w,action=="critic"?"FROZEN ACTOR":action=="center"?"COMMAND CENTER":action=="watch"?"SAVED MODELS":action=="evaluate"?"EVALUATE MODEL":action=="resume"?"RESUME TRAINING":"RESTART TRAINING",p);
    if (width<50 || height<14) {
        c.put(x,3,"Enlarge terminal: 50 columns / 14 rows",p.dim,w);
        c.put(x,height-2,"Q back / Esc home",p.text,w);
        return c.lines();
    }
    c.put(x,3,action=="center"?"1 Models   2 Activity  /  "+std::to_string(models.size())+" saved":""+std::to_string(models.size())+" models  /  models/",p.dim,w);
    const int libraryHeight=std::min(height-10,models.empty()?8:static_cast<int>(models.size())+4);
    c.box(x,5,w,libraryHeight,"CHECKPOINT LIBRARY",p);
    if (models.empty()) {
        c.put(x+2,7,"No saved models yet.",p.text,w-4);
        if (height>15) c.put(x+2,9,"Run /train to save the first checkpoint.",p.dim,w-4);
    } else {
        selected=std::min(selected,models.size()-1);
        const bool dateOnly=width<76;
        const int dateWidth=dateOnly?10:16;
        const int dateX=x+w-dateWidth-3,sizeX=dateX-12;
        c.put(x+4,6,"Model",p.dim,sizeX-x-6);
        c.put(sizeX,6,"Size",p.dim,10);
        c.put(dateX,6,dateOnly?"Updated":"Updated (local)",p.dim,dateWidth);
        const auto visible=static_cast<std::size_t>(std::max(1,height-14));
        const auto start=selected>=visible?selected-visible+1:0;
        for (std::size_t i=start;i<models.size() && i<start+visible;++i) {
            const int y=7+static_cast<int>(i-start);
            const bool usable=model_selectable(models[i],action);
            const auto style=i==selected?p.selected:usable?p.text:p.dim;
            if (i==selected) c.put(x+1,y,std::string(w-2,' '),p.selected,w-2);
            c.put(x+2,y,i==selected?glyph("▸",">"):" ",p.green);
            c.put(x+4,y,ellipsize(models[i].path.filename().string(),sizeX-x-6),style,sizeX-x-6);
            c.put(sizeX,y,model_size(models[i].bytes),i==selected?p.selected:p.dim,10);
            c.put(dateX,y,model_updated(models[i].modified,dateOnly),i==selected?p.selected:p.dim,dateWidth);
        }
        const auto &model=models[selected];
        const std::string contract=model.contractState=="unknown" ? "Compatibility will be checked on open" :
            integer(model.inputs)+" inputs "+glyph("→","->")+" "+integer(model.outputs)+" actions  "+glyph("·","/")+"  "+
            (model.contractState=="compatible"?"Compatible":model.contractState=="incompatible"?"Incompatible":"Unreadable");
        c.put(x,std::min(height-5,6+libraryHeight),contract,model_selectable(model,action)?p.dim:p.gold,w);
        const auto position=integer(static_cast<int>(selected+1))+" / "+integer(static_cast<int>(models.size()));
        c.put(x+w-textCells(position),3,position,p.dim,textCells(position));
    }
    const bool blocked=!models.empty() && !model_selectable(models[std::min(selected,models.size()-1)],action);
    const auto* current=models.empty()?nullptr:&models[std::min(selected,models.size()-1)];
    const std::string progress=current && current->episodes>=0?integer(current->episodes)+" saved episodes / "+(current->updates>=0?integer(current->updates)+" updates":"updates unknown")+(current->bestEvaluation>=0?" / Saved best: "+integer(current->bestEvaluation)+" steps":" / No best score stored in this file"):"Training history unavailable";
    const std::string note=!error.empty()?error:blocked?models[std::min(selected,models.size()-1)].contractDetail:
        action=="center"?progress:action=="restart"?"Restart replaces the selected model and its best with fresh state":
        "Compatibility checks the interface / evaluate separately to measure performance";
    c.footer(x,w,action=="center"?"Up/Down select   Enter watch   E evaluate   T resume   R refresh":glyph("↑↓ select   ↵ ","Up/Down select   Enter ")+(action=="critic"?"use actor":action)+"   R refresh","Q back / Esc home",note,p);
    if (!error.empty() || blocked) c.put(x,height-1,note,p.gold,w);
    return c.lines();
}
}
