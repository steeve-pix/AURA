#include "ControlScreen.hpp"
#include "BodyPreview.hpp"
#include "ScreenCanvas.hpp"

namespace aura::cli {
std::vector<std::string> control_frame(
    const JointControls& controls, const std::optional<ControlObservation>& observation,
    std::size_t selected, double step, bool editing, const std::string& draft,
    const std::string& status, bool failed, double previewYaw, int width, int height) {
    using namespace screen;
    const Palette p;
    constexpr double limitMarginFraction=0.05;
    Canvas canvas(width,height);
    const int w=std::max(1,std::min(156,width-4)), x=(width-w)/2;
    const bool compact=width<76 || height<17;
    const bool live=observation && observation->age_seconds<=control_freshness_seconds;
    const auto unavailable=glyph("—","--");
    const auto connection=!observation?"WAITING":!live?"STALE":"LIVE";
    canvas.heading(x,w,"JOINT CONTROL",p,glyph(live?"●":"○",live?"*":"o")+" "+connection,live?p.green:p.gold);
    if (compact) {
        canvas.put(x,4,"Enlarge terminal: 76 columns / 17 rows",p.dim,w);
        canvas.put(x,height-2,"Q back / Esc home",p.text,w);
        return canvas.lines();
    }
    const int jointCount=static_cast<int>(controls.size());
    selected=std::min(selected,controls.size()-1);
    const bool metrics=height>=jointCount+21, wide=w>=108 && height>=jointCount+24;
    const int tableY=metrics?9:3, tableW=wide?72:w;
    const int visibleCount=std::min(jointCount,std::max(1,height-tableY-(metrics?11:10)));
    const int firstVisible=std::clamp(static_cast<int>(selected)-visibleCount+1,0,jointCount-visibleCount);
    const int tableHeight=visibleCount+(metrics?6:5);
    if (metrics) {
        const int cardW=(w-6)/4;
        const std::array<std::string,4> labels{"PELVIS HEIGHT","UPRIGHTNESS","BODY STATE","SIMULATION"};
        const std::array<std::string,4> values{
            live?number(observation->pelvis_height,2)+" m":unavailable,
            live?number(observation->pelvis_uprightness,3):unavailable,
            live?(observation->is_standing?"Standing":"Fallen"):unavailable,
            live && observation->has_motion?(observation->paused?"Paused":"Running"):unavailable};
        for (int i=0;i<4;++i) {
            const int cx=x+i*(cardW+2);
            canvas.box(cx,4,cardW,4,labels[i],p);
            canvas.put(cx+2,6,values[i],i==2 && live && !observation->is_standing?p.gold:p.bold,cardW-4);
        }
    }
    canvas.box(x,tableY,tableW,tableHeight,"JOINT TARGETS / RADIANS",p);
    const auto& activeJoint=controls[selected];
    const bool motorOff=live && observation->has_motor_states && !observation->motor_enabled[selected];
    canvas.put(x+2,tableY+1,motorOff?activeJoint.name+" / MOTOR OFF / target stored":activeJoint.name+" limits  ["+number(activeJoint.minimum,2)+", "+number(activeJoint.maximum,2)+"] rad",p.dim,tableW-4);
    canvas.put(x+4,tableY+2,"Joint",p.dim);
    canvas.put(x+23,tableY+2,glyph("θ actual","Actual"),p.green,10);
    canvas.put(x+34,tableY+2,glyph("θ* target","Target"),p.purple,10);
    canvas.put(x+45,tableY+2,glyph("Δ error","Error"),p.dim,10);
    canvas.put(x+56,tableY+2,"Joint range",p.dim,tableW-58);
    for (int row=0;row<visibleCount;++row) {
        const int i=firstVisible+row;
        const auto& joint=controls[i];
        const int y=tableY+3+row;
        const auto style=selected==static_cast<std::size_t>(i)?p.selected:p.text;
        if (selected==static_cast<std::size_t>(i)) canvas.put(x+1,y,std::string(tableW-2,' '),p.selected,tableW-2);
        canvas.put(x+2,y,selected==static_cast<std::size_t>(i)?glyph("▸",">"):" ",p.green);
        const double margin=limitMarginFraction*(joint.maximum-joint.minimum);
        const bool warning=joint.clamped || (live &&
            (observation->joint_angles[i]-joint.minimum<=margin || joint.maximum-observation->joint_angles[i]<=margin));
        const bool disabled=live && observation->has_motor_states && !observation->motor_enabled[i];
        canvas.put(x+4,y,std::to_string(i+1)+" "+joint.name+(disabled?" / off":warning?" !":""),disabled?p.gold:warning?p.gold:style,17);
        const auto background=selected==static_cast<std::size_t>(i)?p.caps.shell_style:std::string{};
        canvas.put(x+23,y,live?number(observation->joint_angles[i],3,true):unavailable,background+(live?p.green:p.dim),10);
        canvas.put(x+34,y,number(joint.value,3,true),background+(disabled?p.dim:joint.clamped?p.gold:p.purple),10);
        canvas.put(x+45,y,live?number(joint.value-observation->joint_angles[i],3,true):unavailable,style,10);
        const int length=std::min(19,tableW-60);
        const auto marker=[&](double angle) {
            return static_cast<int>(std::lround(std::clamp((angle-joint.minimum)/(joint.maximum-joint.minimum),0.0,1.0)*(length-1)));
        };
        const int target=marker(joint.value), actual=live?marker(observation->joint_angles[i]):-1;
        canvas.put(x+56,y,"[",p.dim);
        for (int col=0;col<length;++col) {
            const bool both=col==target && col==actual;
            canvas.put(x+57+col,y,both?glyph("◈","@") : col==target?glyph("◆","^") : col==actual?glyph("●","o"):glyph("─","-"),
                       both?p.green:col==target?p.purple:col==actual?p.green:p.dim);
        }
        canvas.put(x+57+length,y,"]",p.dim);
    }
    const auto legend=visibleCount<jointCount ?
        "Joints "+std::to_string(firstVisible+1)+"-"+std::to_string(firstVisible+visibleCount)+" / "+std::to_string(jointCount)+"  /  arrows navigate" :
        glyph("● actual   ◆ target   ◈ same cell","o actual   ^ target   @ same cell");
    canvas.put(x+2,tableY+tableHeight-2,legend,p.dim,tableW-4);
    if (height>=tableY+tableHeight+15) {
        const auto& joint=controls[selected];
        const int y=tableY+tableHeight+1;
        canvas.box(x,y,tableW,8,"SELECTED / "+joint.name,p);
        canvas.put(x+2,y+2,glyph("e = θ* − θ","error = target - actual"),p.bold,tableW-4);
        canvas.put(x+2,y+4,glyph("θ*: commanded target   θ: measured angle","target: commanded   actual: measured"),p.dim,tableW-4);
        canvas.put(x+2,y+3,"Limits  ["+number(joint.minimum,2)+", "+number(joint.maximum,2)+"] rad",p.dim,tableW-4);
        const bool outside=live && (observation->joint_angles[selected]<joint.minimum || observation->joint_angles[selected]>joint.maximum);
        const double margin=limitMarginFraction*(joint.maximum-joint.minimum);
        const bool near=live && (observation->joint_angles[selected]-joint.minimum<=margin || joint.maximum-observation->joint_angles[selected]<=margin);
        canvas.put(x+2,y+5,motorOff?"Motor disabled / this target does not actuate the joint":joint.clamped?"! Target clamped to its limit":outside?"! Actual angle is outside limits":near?"! Actual angle is near a limit":live?"Actual angle is within limits":"Waiting for a fresh joint measurement",joint.clamped || outside || near?p.gold:p.green,tableW-4);
        canvas.put(x+2,y+6,"Arrow step  "+number(step,2)+" rad  /  S changes step size",p.dim,tableW-4);
    }
    if (wide) {
        const int px=x+tableW+2, pw=w-tableW-2, ph=height-tableY-6;
        canvas.box(px,tableY,pw,ph,"BODY POSE",p);
        canvas.put(px+2,tableY+1,live?"Live transforms / orbiting view":"Last pose / awaiting fresh data",p.dim,pw-4);
        if (observation && !observation->body_pose.empty()) {
            const auto pose=body_preview(*observation,pw-4,ph-5,previewYaw,p.caps.utf8);
            for (int row=0;row<static_cast<int>(pose.size());++row)
                canvas.put(px+2,tableY+3+row,pose[row],live?p.purple:p.dim,pw-4);
        } else canvas.put(px+2,tableY+4,"Waiting for body telemetry",p.dim,pw-4);
        canvas.put(px+2,tableY+ph-2,"Read-only body schematic",p.dim,pw-4);
    }
    if (metrics) {
        const std::string details=!observation?"Waiting for viewer telemetry":!live?"Viewer data is stale / age "+number(observation->age_seconds,1)+" s":
            observation->has_motion?"Time "+number(observation->simulation_time,1)+" s  "+glyph("·","/")+"  "+integer(static_cast<int>(observation->steps))+" steps  "+glyph("·","/")+"  Speed "+number(observation->speed,2)+" m/s  "+glyph("·","/")+"  Spin "+number(observation->angular_speed,2)+" rad/s":"Live viewer telemetry";
        const std::string motors=live && observation->has_motor_states ?
            "  /  "+std::to_string(std::count(observation->motor_enabled.begin(),observation->motor_enabled.end(),true))+"/"+std::to_string(controls.size())+" motors active" : "";
        canvas.put(x,3,details+motors,live?p.dim:p.gold,w);
    }
    canvas.put(x,height-5,editing?glyph("↳","->")+" "+controls[selected].name+" target: "+draft+"_":status,failed && !editing?p.caps.error_color:editing?p.purple:p.text,w);
    canvas.put(x,height-4,"1-"+std::to_string(std::min<std::size_t>(9,controls.size()))+" quick select / arrows reach all "+std::to_string(controls.size())+" joints   R neutral",p.dim,w);
    canvas.footer(x,w,editing?glyph("↵ apply   Ctrl+U clear","Enter apply   Ctrl+U clear"):
        glyph("↑↓ select   ←→ adjust","Up/Down select   Left/Right adjust")+" "+number(step,2)+glyph(" rad   ↵ edit   S step"," rad   Enter edit   S step"),
        editing?"Esc cancel edit":"Q back / Esc home","Targets apply immediately in the viewer",p);
    return canvas.lines();
}
}
