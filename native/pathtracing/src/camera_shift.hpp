// SPDX-License-Identifier: MIT
// The sub-pixel shift of a frame's camera (PT-84).
//
// HDRP shifts its projection by a fraction of a pixel every frame (HDCamera.taaJitter, render pixels) and tells its upscaler
// that shift. The tracer's camera rays must carry the very same one: a pixel's ray then passes through the point HDRP drew
// its depth at, and the upscaler lays each frame's samples down where they were taken. Until 2026-10-08 they carried none:
// the plugin read HDRP's matrices after HDRP had taken the shift out again, the upscaler laid the same unshifted picture
// down at another place each frame, and every edge came out a stair of render pixels, every thin thing a row of dots.
//
// The plugin now reads the matrices while they hold the shift (HdrpPasses armedView). This is the tracer's own hold on it:
// what TGPT_Frame's matrices hold is measured against what TGPT_Frame.jitter says, and matrices that hold none while the
// frame has one get it put in, so that no reading order on the plugin's side can take the shift out of the rays again.
//
// The shift in matrix terms, with HDRP's projection in its GPU form (GL.GetGPUProjectionMatrix, render into a texture: row 1
// negated) and pixels counted as the tracer counts them (ndc = 2 x / width - 1, 1 - 2 y / height): HDRP moves its frustum's
// planes by +jitter pixels, so a point lands -jitter pixels from where it was, and a pixel's centre shows what lay +jitter
// pixels from it. In clip space that is  x' = x + a w,  y' = y + b w  with  a = -2 jx / width,  b = +2 jy / height.
#pragma once
#include "tgpt_api.h"
#include <cmath>

namespace tgpt {
// None: the frame has no shift. Held: the matrices hold the frame's. Applied: they held none and it was put in here.
// Other: they hold another one than the frame says (left as they are: HDRP drew its depth with them).
enum class CameraShift { None, Held, Applied, Other };
struct CameraShiftCheck { CameraShift state=CameraShift::None; float held[2]{}; };

// The shift a frame's view-projection holds (render pixels, counted as TGPT_Frame.jitter): what its camera rays carry,
// whichever way the matrices came. False when no camera can be read from them (held is then 0, 0).
// Matrices in Unity's memory order (column-major: element (row, column) at [column * 4 + row]).
inline bool held_camera_shift(const TGPT_Frame& f, float held[2]) {
    held[0]=held[1]=0.f;
    const double w=double(f.renderWidth), h=double(f.renderHeight);
    if(!(w>0.0 && h>0.0)) return false;
    // The view-projection without the shift, from HDRP's projection without it and its view matrix.
    double plain[4][4];
    for(int row=0;row<4;++row) for(int col=0;col<4;++col) {
        double v=0.0; for(int i=0;i<4;++i) v+=double(f.nonJitteredProj[i*4+row])*double(f.view[col*4+i]);
        plain[row][col]=v;
    }
    // What the handed view-projection adds to its rows 0 and 1, as multiples of row 3 (w): a and b above.
    double ww=0.0, a=0.0, b=0.0;
    for(int col=0;col<4;++col) {
        ww+=plain[3][col]*plain[3][col];
        a+=(double(f.viewProj[col*4+0])-plain[0][col])*plain[3][col];
        b+=(double(f.viewProj[col*4+1])-plain[1][col])*plain[3][col];
    }
    if(!(ww>1e-12) || !std::isfinite(a) || !std::isfinite(b)) return false; // no camera handed over that this can be read from
    a/=ww; b/=ww;
    held[0]=float(-a*w*0.5); held[1]=float(b*h*0.5);
    return true;
}
inline CameraShiftCheck ensure_camera_shift(TGPT_Frame& f) {
    CameraShiftCheck r;
    const double w=double(f.renderWidth), h=double(f.renderHeight);
    if(!held_camera_shift(f, r.held)) return r;
    const double told[2]={std::isfinite(f.jitter[0]) ? double(f.jitter[0]) : 0.0, std::isfinite(f.jitter[1]) ? double(f.jitter[1]) : 0.0};
    const bool any=std::fabs(told[0])>1e-4 || std::fabs(told[1])>1e-4;
    if(std::fabs(double(r.held[0])-told[0])<=0.02 && std::fabs(double(r.held[1])-told[1])<=0.02) { r.state=any ? CameraShift::Held : CameraShift::None; return r; }
    if(std::fabs(r.held[0])>0.01f || std::fabs(r.held[1])>0.01f) { r.state=CameraShift::Other; return r; }
    // The matrices hold no shift and the frame has one: clip' = T clip, so viewProj' = T viewProj (rows 0 and 1 take a and b
    // times row 3) and invViewProj' = invViewProj T^-1 (column 3 gives up a times column 0 and b times column 1).
    const float ta=float(-2.0*told[0]/w), tb=float(2.0*told[1]/h);
    for(int col=0;col<4;++col) { f.viewProj[col*4+0]+=ta*f.viewProj[col*4+3]; f.viewProj[col*4+1]+=tb*f.viewProj[col*4+3]; }
    for(int row=0;row<4;++row) f.invViewProj[3*4+row]-=ta*f.invViewProj[0*4+row]+tb*f.invViewProj[1*4+row];
    r.state=CameraShift::Applied;
    return r;
}
} // namespace tgpt
