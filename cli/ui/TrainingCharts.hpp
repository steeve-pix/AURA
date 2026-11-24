#pragma once
#include "ui/ScreenCanvas.hpp"
#include <limits>

namespace aura::cli::screen {
    // Connected curves use 2x4 pixels per cell; x coordinates retain real sample indices.
    enum class ChartKind { training, evaluation, loss, rewardReturn, rewardRate, balancedTime, evaluationBalanced, criticTraining, criticValidation };

    // Common worker-local episode indices prevent asynchronous histories from
    // shifting against each other. Render raw samples at terminal subcell resolution.
    inline void worker_chart(Canvas &c, int x, int y, int w, int h, const std::string &title,
                             const std::vector<std::vector<double>> &series, int selected,
                             bool selectedOnly, const Palette &p, ChartKind kind = ChartKind::balancedTime) {
        c.box(x,y,w,h,title,p);
        if (w<30 || h<10) return;
        const bool seconds=kind==ChartKind::balancedTime;
        const int first=selectedOnly?selected:(selected/4)*4;
        const int last=std::min(int(series.size()),selectedOnly?first+1:first+4);
        const std::array<std::string,4> colors{p.purple,p.green,p.caps.syntax_command,p.gold};
        int end=0;
        for(int worker=first;worker<last;++worker)end=std::max(end,int(series[worker].size()));
        if (!end) {c.put(x+3,y+4,"Waiting for completed worker episodes",p.dim,w-6);return;}
        const int begin=std::max(0,end-60);
        c.put(x+2,y+1,std::string(seconds?"Seconds balanced":"Survival steps")+" / last "+std::to_string(end-begin)+
            " episodes per worker / "+(selectedOnly?"selected worker":"shared scale / * selected"),p.dim,w-4);
        double low=std::numeric_limits<double>::infinity(),high=0;
        for(int worker=first;worker<last;++worker)
            for(int i=begin;i<int(series[worker].size());++i)if(std::isfinite(series[worker][i])&&series[worker][i]>=0) {
                low=std::min(low,series[worker][i]);high=std::max(high,series[worker][i]);
            }
        if(!std::isfinite(low)) {c.put(x+3,y+4,"No finite completed samples in this window",p.dim,w-6);return;}
        const double pad=std::max((high-low)*.12,seconds?.025:5.);
        const double rawStep=std::max((high-low+2*pad)/2,seconds?.01:1.);
        const double magnitude=std::pow(10.,std::floor(std::log10(rawStep)));
        const double scaled=rawStep/magnitude;
        const double step=(scaled<=1?1:scaled<=2?2:scaled<=2.5?2.5:scaled<=5?5:10)*magnitude;
        low=std::max(0.,std::floor((high-low<1e-9?low-pad:low)/step)*step);
        int intervals=std::max(2,int(std::ceil((high+pad-low)/step)));
        if(intervals%2)++intervals;
        high=low+intervals*step;
        const int left=x+10,top=y+3,cols=w-13,rows=h-8;
        const bool unicode=p.caps.utf8;
        const int sx=unicode?2:1,sy=unicode?4:1,pixelWidth=cols*sx,pixelHeight=rows*sy;
        const auto px=[&](int i){return end-begin==1?(pixelWidth-1)/2:(i-begin)*(pixelWidth-1)/(end-begin-1);};
        const auto py=[&](double value){return std::clamp(int(std::lround((high-value)/(high-low)*(pixelHeight-1))),0,pixelHeight-1);};
        for(int row:{0,rows/2,rows-1}) {
            const double value=high-(high-low)*row/std::max(1,rows-1);
            c.put(x+2,top+row,number(value,seconds?2:0)+(seconds?" s":""),p.dim,7);
            for(int col=0;col<cols;col+=6)c.put(left+col,top+row,glyph("┄","."),p.dim);
        }
        std::array<std::vector<int>,4> dots;
        for(auto &layer:dots)layer.resize(cols*rows);
        static constexpr int bits[4][2]{{1,8},{2,16},{4,32},{64,128}};
        const auto pixel=[&](int worker,int ax,int ay){
            if(ax>=0&&ax<pixelWidth&&ay>=0&&ay<pixelHeight)
                dots[worker-first][(ay/sy)*cols+ax/sx]|=unicode?bits[ay%4][ax%2]:1;
        };
        const auto line=[&](int worker,int ax,int ay,int bx,int by){
            const int dx=std::abs(bx-ax),directionX=ax<bx?1:-1;
            const int dy=-std::abs(by-ay),directionY=ay<by?1:-1;
            int error=dx+dy;
            while(true) {
                pixel(worker,ax,ay);if(ax==bx&&ay==by)break;
                const int twice=2*error;
                if(twice>=dy){error+=dy;ax+=directionX;}
                if(twice<=dx){error+=dx;ay+=directionY;}
            }
        };
        for(int worker=first;worker<last;++worker) {
            bool previousValid=false;int previousX=0,previousY=0;
            for(int i=begin;i<int(series[worker].size());++i) {
                const double value=series[worker][i];
                if(!std::isfinite(value)||value<0){previousValid=false;continue;}
                const int ax=px(i),ay=py(value);
                if(previousValid)line(worker,previousX,previousY,ax,ay);else pixel(worker,ax,ay);
                previousX=ax;previousY=ay;previousValid=true;
            }
        }
        // A terminal cell has one foreground color. Give the selected trace
        // priority where curves overlap; H isolates it without altering data.
        for(int row=0;row<rows;++row)for(int col=0;col<cols;++col) {
            int owner=-1;
            for(int worker=first;worker<last;++worker)if(dots[worker-first][row*cols+col]) {
                owner=worker;if(worker==selected)break;
            }
            if(owner>=0)c.put(left+col,top+row,unicode?utf8(0x2800+dots[owner-first][row*cols+col]):"*",
                colors[owner%4]+(owner==selected?p.caps.strong:""));
        }
        const auto endpoint=[&](int worker) {
            if(worker<first||worker>=last||int(series[worker].size())<=begin)return;
            const int i=int(series[worker].size())-1;const double value=series[worker][i];
            if(std::isfinite(value)&&value>=0)c.put(left+px(i)/sx,top+py(value)/sy,
                worker==selected?glyph("●","o"):glyph("○","o"),colors[worker%4]);
        };
        for(int worker=first;worker<last;++worker)if(worker!=selected)endpoint(worker);
        endpoint(selected);
        c.rule(left,top+rows,cols,p.dim);
        const auto firstTick=std::to_string(begin+1),lastTick=std::to_string(end);
        c.put(end-begin==1?left+cols/2:left,top+rows+1,firstTick,p.dim);
        if(end-begin>1)c.put(left+cols-textCells(lastTick),top+rows+1,lastTick,p.dim,textCells(lastTick));
        if(cols>30)c.put(left+std::max(0,(cols-18)/2),top+rows+1,"Episode per worker",p.dim,18);
        const int shown=std::max(1,last-first),legendWidth=(w-4)/shown;
        for(int worker=first;worker<last;++worker) {
            const auto &values=series[worker];
            const std::string latest=values.empty()||!std::isfinite(values.back())?"pending":number(values.back(),seconds?2:0)+(seconds?"s":" steps");
            const std::string label=(worker==selected?"*":" ")+std::string(legendWidth>=19?"Worker ":"W")+std::to_string(worker+1)+" "+latest;
            c.put(x+2+(worker-first)*legendWidth,y+h-2,label,colors[worker%4],legendWidth-1);
        }
    }

    inline void chart(Canvas &canvas, int x, int y, int w, int h, const std::string &title,
                      const std::vector<double> &series, const std::vector<double> &averages,
                      const std::string &color, const Palette &p, ChartKind kind, int batchSize, int indexOffset = 0,
                      const std::vector<int> &indices = {},
                      double reference = std::numeric_limits<double>::quiet_NaN()) {
        const bool critic = kind == ChartKind::criticTraining || kind == ChartKind::criticValidation;
        const bool training = kind == ChartKind::training;
        const bool reward = kind == ChartKind::rewardReturn || kind == ChartKind::rewardRate || kind == ChartKind::balancedTime || kind == ChartKind::evaluationBalanced;
        const bool survival = training || kind == ChartKind::evaluation;
        canvas.box(x, y, w, h, title, p);
        const int retained = training || reward ? 120 : 60;
        const int start = std::max(0, static_cast<int>(series.size()) - retained);
        const int count = static_cast<int>(series.size()) - start;
        canvas.put(x + 2, y + 1,
                   critic
                       ? "Mean squared error / critic update"
                       : reward
                             ? (kind == ChartKind::evaluationBalanced ? "No exploration / balanced seconds by update" : kind == ChartKind::balancedTime ? "Controlled balance / seconds by episode" : kind == ChartKind::rewardReturn
                                    ? "Total reward / completed episode"
                                    : "Average reward / survival step")
                             : training
                                   ? "Exploration / steps by episode"
                                   : survival
                                         ? "No exploration / steps by update"
                                         : "Objective / Adam update", p.dim, w - 4);
        if (series.empty()) {
            canvas.put(x + 3, y + h / 2,
                       reward
                           ? "Waiting for the first completed episode..."
                           : critic
                                 ? "Collecting independent episode datasets..."
                                 : training
                                       ? "Collecting the first episode..."
                                       : survival
                                             ? "Waiting for the initial evaluation..."
                                             : "One loss value per completed batch.", p.dim, w - 6);
            return;
        }
        double low = *std::min_element(series.begin() + start, series.end());
        double high = *std::max_element(series.begin() + start, series.end());
        if (kind == ChartKind::evaluation || kind == ChartKind::evaluationBalanced) {
            low = std::min(low, series.front());
            high = std::max(high, series.front());
        }
        // Center the visible samples, then expand equally in both directions to
        // include every point. Rounding the two bounds independently can leave
        // most of the curve at the bottom after one high episode.
        double center = 0.0;
        for (int i = start; i < static_cast<int>(series.size()); ++i)
            center += (series[i] - center) / (i - start + 1);
        if (kind == ChartKind::criticValidation && std::isfinite(reference)) {
            low = std::min(low, reference);
            high = std::max(high, reference);
        }
        const double padding = std::max((high - low) * .12, survival ? 5.0 : .001);
        const double radius = std::max(high - center, center - low) + padding;
        const double quantum = std::pow(10.0, std::floor(std::log10(radius))) / 5;
        center = std::round(center / quantum) * quantum;
        const double extent = std::ceil((std::max(high - center, center - low) + padding) / quantum) * quantum;
        low = center - extent;
        high = center + extent;
        // Survival cannot be negative; near zero this takes precedence over centering.
        if (survival || critic || kind == ChartKind::balancedTime || kind == ChartKind::evaluationBalanced) low = std::max(0.0, low);
        const int left = x + 11, top = y + 3, cols = std::max(2, w - 14), rows = std::max(2, h - 7);
        const bool unicode = terminal_capabilities().utf8;
        const int pixelWidth = cols * (unicode ? 2 : 1), pixelHeight = rows * (unicode ? 4 : 1);
        struct Dot {
            int bits = 0, averageBits = 0;
        };
        std::vector<Dot> dots(cols * rows);
        const auto pixel = [&](int px, int py, bool average) {
            if (px < 0 || px >= pixelWidth || py < 0 || py >= pixelHeight) return;
            static constexpr int bits[4][2]{{1, 8}, {2, 16}, {4, 32}, {64, 128}};
            auto &dot = dots[(py / (unicode ? 4 : 1)) * cols + px / (unicode ? 2 : 1)];
            (average ? dot.averageBits : dot.bits) |= unicode ? bits[py % 4][px % 2] : 1;
        };
        const auto line = [&](int ax, int ay, int bx, int by, bool average) {
            const int dx = std::abs(bx - ax), sx = ax < bx ? 1 : -1;
            const int dy = -std::abs(by - ay), sy = ay < by ? 1 : -1;
            int error = dx + dy;
            while (true) {
                pixel(ax, ay, average);
                if (ax == bx && ay == by) break;
                const int twice = 2 * error;
                if (twice >= dy) {
                    error += dy;
                    ax += sx;
                }
                if (twice <= dx) {
                    error += dx;
                    ay += sy;
                }
            }
        };
        const auto indexAt = [&](int i) { return indices.empty() ? indexOffset + i + 1 : indices[i]; };
        const auto px = [&](int index) {
            return count == 1
                       ? (pixelWidth - 1) / 2
                       : (indexAt(index) - indexAt(start)) * (pixelWidth - 1) / std::max(
                             1, indexAt(static_cast<int>(series.size()) - 1) - indexAt(start));
        };
        const auto py = [&](double value) {
            return std::clamp(static_cast<int>(std::lround((high - value) / (high - low) * (pixelHeight - 1))), 0,
                              pixelHeight - 1);
        };
        for (int i = start; i < static_cast<int>(series.size()); ++i) {
            pixel(px(i), py(series[i]), false);
            if (i > start) line(px(i - 1), py(series[i - 1]), px(i), py(series[i]), false);
        }
        // Each batch mean spans the episodes from its completed batch.
        if (training && batchSize > 0)
            for (int batch = 0; batch < static_cast<int>(averages.size()); ++batch) {
                const int first = std::max(start, batch * batchSize), last = std::min(
                    static_cast<int>(series.size()) - 1, (batch + 1) * batchSize - 1);
                if (last >= first) line(px(first), py(averages[batch]), px(last), py(averages[batch]), true);
            }
        for (int row: {0, rows / 2, rows - 1}) {
            // Use exact endpoints: cancellation at zero can otherwise produce a
            // tiny negative scientific label on a nonnegative duration axis.
            const double value = row == 0 ? high : row == rows - 1 ? low :
                high - (high - low) * row / (rows - 1);
            auto formatted = number(value, survival ? 0 : kind == ChartKind::rewardReturn ? 1 : 3);
            if (formatted.size() > 8) {
                std::ostringstream compact;
                compact << std::scientific << std::setprecision(1) << value;
                formatted = compact.str();
            }
            std::ostringstream label;
            label << std::setw(8) << formatted;
            canvas.put(x + 2, top + row, label.str(), p.dim, 8);
            canvas.rule(left, top + row, cols, p.dim);
        }
        // The starting evaluation remains a visible reference even after the window advances.
        if (kind == ChartKind::evaluation || kind == ChartKind::evaluationBalanced || (kind == ChartKind::criticValidation && std::isfinite(reference))) {
            const int referenceY = py(kind == ChartKind::evaluation || kind == ChartKind::evaluationBalanced ? series.front() : reference);
            static constexpr int horizontalBits[]{9, 18, 36, 192};
            const auto referenceDot = unicode ? utf8(0x2800 + horizontalBits[referenceY % 4]) : ".";
            for (int col = 0; col < cols; ++col)
                canvas.put(left + col, top + referenceY / (unicode ? 4 : 1), referenceDot, p.gold);
        }
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < cols; ++col) {
                const auto dot = dots[row * cols + col];
                if (dot.bits || dot.averageBits)
                    canvas.put(left + col, top + row,
                               unicode
                                   ? utf8(0x2800 + (dot.averageBits ? dot.averageBits : dot.bits))
                                   : dot.averageBits
                                         ? "="
                                         : "*",
                               dot.averageBits ? p.caps.strong + p.gold : color);
            }
        const int last = static_cast<int>(series.size()) - 1;
        canvas.put(left + px(last) / (unicode ? 2 : 1), top + py(series[last]) / (unicode ? 4 : 1), glyph("●", "o"),
                   color);
        canvas.rule(left, top + rows, cols, p.dim);
        if (count > 1) {
            canvas.put(left, top + rows + 1, std::to_string(indexAt(start)), p.dim);
            const auto last = std::to_string(indexAt(static_cast<int>(series.size()) - 1));
            canvas.put(left + cols - static_cast<int>(last.size()), top + rows + 1, last, p.dim);
            if (cols > 24 && count > 2) canvas.put(left + cols / 2 - 1, top + rows + 1,
                                                   std::to_string(
                                                       (indexAt(start) + indexAt(static_cast<int>(series.size()) - 1)) /
                                                       2), p.dim);
        } else canvas.put(left + cols / 2, top + rows + 1, std::to_string(indexAt(start)), p.dim);
        if (training) {
            canvas.put(x + 2, y + h - 2, glyph("━", "-") + " episode", p.green, 12);
            canvas.put(x + 15, y + h - 2, glyph("━", "=") + " batch mean", p.gold, w - 17);
            if (!averages.empty() && w >= 70) {
                const auto summary = "Mean " + number(averages.back(), 1);
                canvas.put(x + w - textCells(summary) - 2, y + h - 2, summary, p.gold, textCells(summary));
            }
        } else if (survival) {
            const auto summary = "Latest " + integer(static_cast<int>(series.back())) + " / update " + std::to_string(
                                     indexAt(static_cast<int>(series.size()) - 1));
            canvas.put(x + 2, y + h - 2, summary, p.purple, w - 4);
            const auto reference = "Start " + integer(static_cast<int>(series.front()));
            if (w >= textCells(summary) + textCells(reference) + 7)
                canvas.put(x + w - textCells(reference) - 2, y + h - 2, reference, p.gold, textCells(reference));
        } else if (reward) canvas.put(x + 2, y + h - 2,
                                      "Latest " + number(series.back(), kind == ChartKind::rewardReturn ? 2 : 3) +
                                      (kind == ChartKind::evaluationBalanced ? " s / update " : " / episode ") + std::to_string(indexAt(last)), color, w - 4);
        else canvas.put(x + 2, y + h - 2,
                        "Latest " + number(series.back(), 5) + " / update " + std::to_string(
                            indexAt(static_cast<int>(series.size()) - 1)), color, w - 4);
    }
}
