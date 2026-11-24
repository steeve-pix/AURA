#pragma once
#include "ui/ScreenCanvas.hpp"
#include <chrono>
#include <array>

namespace aura::cli {
    // A compact overview, with the complete read-only evidence on a separate page.
    class ResearchScreen {
        struct Row { std::string kind, label, value, detail; };
        struct Comparison { std::string label; double deterministic, stochastic, reward, falls; };
        std::vector<Row> content_;
        std::vector<Comparison> comparisons_;
        std::string mode_, model_, phase_ = "LOADING", error_, progress_, report_;
        int update_ = 0, done_ = 0, total_ = 0, page_ = 1, maxScroll_ = 0;
        std::array<int, 2> scroll_{};
        std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();

        static bool summaryRow(const Row &row) {
            return row.kind == "row" && (row.label == "Actor" || row.label == "Critic" ||
                row.label == "Adam steps" || row.label == "Environment" || row.label == "History" ||
                row.label == "Recorded balance trend" || row.label == "Controlled balance" ||
                row.label == "Quality / return" || row.label == "Saved updates" || row.label == "Recorded updates" ||
                row.label == "Deterministic" || row.label == "Stochastic" || row.label == "Error");
        }
    public:
        void receive(const std::string &line) {
            if (!line.starts_with("AURA_RESEARCH\t")) {
                if (!line.empty() && line.front() != ' ' && line.find("Error:") != std::string::npos) {
                    error_ = line;
                    content_.push_back({"row", "Error", line, "Check the Python environment or selected checkpoint"});
                    phase_ = "FAILED";
                }
                return;
            }
            std::istringstream input(line.substr(14));
            std::string kind;
            std::getline(input, kind, '\t');
            const auto field = [&] { std::string value; std::getline(input, value, '\t'); return value; };
            if (kind == "start") {
                mode_ = field(); model_ = field(); input >> update_; phase_ = "WORKING";
            } else if (kind == "row") content_.push_back({kind, field(), field(), field()});
            else if (kind == "section") content_.push_back({kind, field(), "", ""});
            else if (kind == "comparison") {
                Comparison result{};
                result.label = field();
                if (input >> result.deterministic >> result.stochastic >> result.reward >> result.falls &&
                    std::isfinite(result.deterministic) && std::isfinite(result.stochastic) &&
                    std::isfinite(result.reward) && std::isfinite(result.falls)) comparisons_.push_back(result);
            } else if (kind == "saved") { report_ = field(); content_.push_back({"row", "Report path", report_, ""}); }
            else if (kind == "progress") { input >> done_ >> total_; std::getline(input >> std::ws, progress_); }
            else if (kind == "complete") { phase_ = "COMPLETE"; done_ = total_; }
            else if (kind == "stopped") phase_ = "STOPPED";
            else if (kind == "error") { error_ = field(); phase_ = "FAILED"; content_.push_back({"row", "Error", error_, ""}); }
        }
        std::string key(const std::string &key) {
            if (key == "1" || key == "2") { page_ = key[0]-'0'; return {}; }
            if (key == "d" || key == "D" || key == "\t") { page_ = page_ == 1 ? 2 : 1; return {}; }
            auto &offset = scroll_[page_-1];
            if (key == "\033[A" || key == "scroll_up") offset -= 3;
            if (key == "\033[B" || key == "scroll_down") offset += 3;
            if (key == "\033[5~") offset -= 12;
            if (key == "\033[6~") offset += 12;
            if (key == "\033[H") offset = 0;
            if (key == "\033[F") offset = maxScroll_;
            offset = std::clamp(offset, 0, maxScroll_);
            return {};
        }
        std::vector<std::string> frame(int width, int rows, bool exited) {
            using namespace screen;
            const Palette p;
            Canvas c(width, rows);
            const int w = std::max(1, std::min(132, width - 6)), x = (width - w) / 2;
            const auto title = mode_ == "compare" ? "POLICY COMPARISON" : mode_ == "diagnose" ? "MODEL DIAGNOSIS" : "TRAINING REPORT";
            c.heading(x, w, title, p, exited && phase_ == "WORKING" ? "INTERRUPTED" : phase_,
                      phase_ == "FAILED" ? p.gold : phase_ == "COMPLETE" ? p.green : p.purple);
            if (rows < 18 || width < 60) {
                c.put(x, 3, "Enlarge terminal: 60 columns / 18 rows", p.dim, w);
                c.shortcuts(x, rows - 2, "Q back   Esc home", p, w);
                return c.lines();
            }
            c.put(x, 2, model_.empty() ? "Loading frozen checkpoint..." : ellipsize(model_,w), p.bold, w);
            c.put(x, 3, "Update " + std::to_string(update_) + " / read-only" +
                  (mode_ == "report" ? " / saved measurements" : " / CPU evaluation"), p.dim, w);
            c.shortcuts(x, 5, "[1] Overview", p, 17, page_ == 1 ? p.selected : p.dim, page_ == 1 ? p.caps.shell_style : "");
            c.shortcuts(x+19, 5, "[2] Details", p, w-19, page_ == 2 ? p.selected : p.dim, page_ == 2 ? p.caps.shell_style : "");
            if (total_ > 0 && phase_ == "WORKING") {
                const int filled = std::clamp(done_ * 18 / total_, 0, 18);
                for (int i = 0; i < 18; ++i) c.put(x+i, 6, glyph(i < filled ? "━" : "─", "-"), i < filled ? p.green : p.dim);
                c.put(x+20, 6, std::to_string(100*done_/total_) + "% / " + progress_, p.purple, w-20);
            } else c.put(x, 6, phase_ == "COMPLETE" ? "Complete / no weight updates" :
                         phase_ == "FAILED" ? ellipsize(error_,w) : "Reading saved state...", phase_ == "FAILED" ? p.gold : p.green, w);

            // Each logical row is drawn in columns. Details wrap; the overview stays compact.
            struct DisplayRow { std::string label, value, detail, color; bool section = false; };
            std::vector<DisplayRow> lines;
            if (page_ == 1 && mode_ == "compare") {
                lines.push_back({"", "BALANCED DURATION / SECONDS", "", p.gold, true});
                if(w>=85) lines.push_back({"Policy", "Deterministic", "Exploration mean / reward", p.purple});
                else lines.push_back({"", "Policy / deterministic / exploration mean / reward", "", p.purple, true});
                for (const auto &result: comparisons_) lines.push_back({result.label, number(result.deterministic,3) + " s",
                    number(result.stochastic,3) + " s / " + number(result.reward,2), p.green});
                if (comparisons_.empty()) lines.push_back({"", "Waiting for the first policy result...", "", p.dim, true});
                lines.push_back({"", "3 matched exploration streams / same saved world", "", p.dim, true});
                for(const auto &result: comparisons_) if(result.label == "Seed baseline")
                    lines.push_back({"", "Seed baseline reconstructed / provenance in Details.", "", p.gold, true});
                lines.push_back({"", "Individual streams, fall rates and provenance: Details", "", p.dim, true});
                for(const auto &row:content_) if(row.label=="Error" || row.value=="Unavailable" || row.value=="Skipped")
                    lines.push_back({row.label,row.value,row.detail,p.gold});
            } else {
                for(const auto &row: content_) {
                    if(page_ == 1 && (!summaryRow(row) || (row.label == "Critic" && row.value == "Weight health only"))) continue;
                    if(row.kind == "section") {
                        if(page_ == 2) lines.push_back({"",row.label,"",p.gold,true});
                        continue;
                    }
                    const auto color = row.label == "Error" || row.value == "Needs attention" || row.value == "Unavailable" || row.detail.starts_with("Different optimizer")
                        ? p.gold : row.value == "Finite" || row.value.starts_with("Compatible") ? p.green : p.purple;
                    lines.push_back({row.label == "Controlled balance" ? "Balanced time" : row.label == "Recorded balance trend" ? "Balance trend" : row.label,page_ == 1 && row.label == "Environment" && row.value.starts_with("Compatible")
                        ? "Compatible" : row.value,row.detail,color});
                    if(page_ == 1 && update_ > 0 && (row.label == "Recorded updates" && row.value == "0" ||
                        row.label == "History" && row.value.starts_with("0 recorded")))
                        lines.push_back({"", "Earlier updates have no saved history.", "", p.gold, true});
                }
            }
            if(page_ == 1 && !report_.empty()) {
                const auto slash=report_.find_last_of('/');
                lines.push_back({"", "EXPORT / reports/" + mode_ + "/", "", p.gold, true});
                lines.push_back({"Files",report_.substr(slash == std::string::npos ? 0 : slash+1),"",p.green});
            }
            int capacity = 1;
            for (const auto &row: lines) capacity += 4 + (textCells(row.value)+textCells(row.detail)) / std::max(1,w-26);
            Canvas body(w, capacity);
            int y=0;
            const int labelWidth = w >= 95 ? 24 : 19;
            for(const auto &row:lines) {
                if(row.section) { body.put(0,y++,ellipsize(row.value,w-2),row.color,w-2); continue; }
                body.put(0,y,ellipsize(row.label,labelWidth-2),page_ == 1 && mode_ == "compare" ? p.bold : p.dim,labelWidth-2);
                if(page_==1 && mode_=="compare" && !row.detail.empty() && w>=85) {
                    body.put(labelWidth,y,ellipsize(row.value,22),row.color,22);
                    body.put(labelWidth+24,y,ellipsize(row.detail,w-labelWidth-26),p.purple,w-labelWidth-26);
                    ++y;
                } else {
                    const int space=w-labelWidth-2;
                    std::string text=row.value;
                    do {
                        if(page_==1) { body.put(labelWidth,y++,ellipsize(text,space),row.color,space); break; }
                        if(textCells(text)<=space) { body.put(labelWidth,y++,text,row.color,space); break; }
                        // Split only at UTF-8 character boundaries; paths may have no spaces.
                        std::size_t cut=0;
                        while(cut<text.size()) {
                            auto next=cut+1;
                            while(next<text.size() && (static_cast<unsigned char>(text[next])&0xc0)==0x80) ++next;
                            if(textCells(text.substr(0,next))>space) break;
                            cut=next;
                        }
                        if(!cut) break;
                        body.put(labelWidth,y++,text.substr(0,cut),row.color,space); text.erase(0,cut);
                    } while(!text.empty());
                    if(!row.detail.empty() && (page_==2 || mode_=="compare")) {
                        std::string text = row.detail;
                        do {
                            if (textCells(text)<=space) { body.put(labelWidth,y++,text,p.dim,space); break; }
                            std::size_t cut=0;
                            while(cut<text.size()) {
                                auto next=cut+1;
                                while(next<text.size() && (static_cast<unsigned char>(text[next])&0xc0)==0x80) ++next;
                                if(textCells(text.substr(0,next))>space) break;
                                cut=next;
                            }
                            if(!cut) break;
                            body.put(labelWidth,y++,text.substr(0,cut),p.dim,space); text.erase(0,cut);
                        } while(!text.empty());
                    }
                }
            }
            const int viewport=std::max(1,rows-11);
            maxScroll_=std::max(0,y-viewport);
            auto &offset=scroll_[page_-1]; offset=std::clamp(offset,0,maxScroll_);
            c.copy(body,x,8,offset,viewport);
            if(maxScroll_>0) {
                const int thumb=std::max(1,viewport*viewport/std::max(1,y));
                const int top=(viewport-thumb)*offset/maxScroll_;
                for(int row=0;row<viewport;++row) c.put(x+w-1,8+row,glyph(row>=top && row<top+thumb ? "┃":"│","|"),row>=top && row<top+thumb?p.purple:p.dim);
            }
            c.rule(x,rows-3,w,p.dim);
            c.shortcuts(x,rows-2,maxScroll_>0 ? "D overview/details   Up/Down scroll" : "D overview/details",p,std::max(0,w-24));
            c.shortcuts(x+w-23,rows-2,"Q back   Esc home",p,23);
            c.put(x,rows-1,phase_=="COMPLETE"?"Saved in reports/" + mode_ + "/ / Markdown, JSON and CSV":
                  phase_=="FAILED"?ellipsize(error_,w):"Read-only / cancel safely at any time",phase_=="FAILED"?p.gold:p.dim,w);
            return c.lines();
        }
        const std::string &error() const { return error_; }
    };
}
