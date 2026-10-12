#pragma once

#include "Terminal.hpp"
#include <algorithm>
#include <cmath>

namespace aura::cli {
// Read-only schematic: ellipsoid surfaces use actual part transforms.
// Yaw orbits the preview camera; it never modifies the simulation.
inline std::vector<std::string> body_preview(const ControlObservation& observation,
                                            int width, int height, double yaw, bool utf8) {
    if (width < 4 || height < 4 || observation.body_pose.size() != 16) return {};
    struct Point { double x, y; };
    std::vector<Point> points;
    double minX=1e30, maxX=-1e30, minY=1e30, maxY=-1e30;
    for (const auto& part : observation.body_pose) {
        const double norm=std::hypot(std::hypot(part[3],part[4]),std::hypot(part[5],part[6]));
        if (norm < 1e-8) return {};
        const double w=part[3]/norm, x=part[4]/norm, y=part[5]/norm, z=part[6]/norm;
        for (int latitude=1; latitude<12; ++latitude) {
            const double a=latitude*3.141592653589793/12;
            for (int longitude=0; longitude<28; ++longitude) {
                const double b=longitude*6.283185307179586/28;
                const double vx=part[7]*0.5*std::sin(a)*std::cos(b);
                const double vy=part[8]*0.5*std::cos(a);
                const double vz=part[9]*0.5*std::sin(a)*std::sin(b);
                // q v q*: AURA's world orientation convention.
                const double tx=2*(y*vz-z*vy), ty=2*(z*vx-x*vz), tz=2*(x*vy-y*vx);
                const double px=part[0]+vx+w*tx+y*tz-z*ty;
                const double py=part[1]+vy+w*ty+z*tx-x*tz;
                const double pz=part[2]+vz+w*tz+x*ty-y*tx;
                Point p{std::cos(yaw)*px-std::sin(yaw)*pz,py};
                points.push_back(p);
                minX=std::min(minX,p.x); maxX=std::max(maxX,p.x);
                minY=std::min(minY,p.y); maxY=std::max(maxY,p.y);
            }
        }
    }
    const int pixelsX=utf8 ? width*2 : width, pixelsY=utf8 ? height*4 : height;
    const double aspect=utf8 ? 1.0 : 2.0; // Terminal cells are roughly twice as tall as wide.
    const double scale=std::min((pixelsX-3)/std::max(0.01,maxX-minX),
                               (pixelsY-3)*aspect/std::max(0.01,maxY-minY));
    std::vector<unsigned char> cells(width*height,0);
    constexpr int bits[4][2]={{0,3},{1,4},{2,5},{6,7}};
    for (auto p : points) {
        const int px=static_cast<int>((p.x-(minX+maxX)/2)*scale+pixelsX/2.0);
        const int py=static_cast<int>(((minY+maxY)/2-p.y)*scale/aspect+pixelsY/2.0);
        if (px<0 || px>=pixelsX || py<0 || py>=pixelsY) continue;
        if (utf8) cells[(py/4)*width+px/2] |= 1<<bits[py%4][px%2];
        else cells[py*width+px]=1;
    }
    std::vector<std::string> rows(height);
    for (int row=0;row<height;++row) for (int col=0;col<width;++col) {
        const auto mask=cells[row*width+col];
        if (!mask) rows[row]+=' ';
        else if (!utf8) rows[row]+='.';
        else {
            const unsigned code=0x2800+mask;
            rows[row]+=static_cast<char>(0xe0|(code>>12));
            rows[row]+=static_cast<char>(0x80|((code>>6)&63));
            rows[row]+=static_cast<char>(0x80|(code&63));
        }
    }
    return rows;
}
}
