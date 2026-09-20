// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
namespace tgfg {
struct RectU { uint32_t x=0,y=0,width=0,height=0; };
inline bool fits(RectU r,uint32_t w,uint32_t h) {
    return r.width && r.height && uint64_t(r.x)+r.width<=w && uint64_t(r.y)+r.height<=h;
}
// Integer values are the public DXGI enum values, kept independent of Windows
// headers so these boundary decisions can be exercised on every build host.
inline uint32_t unorm_format(uint32_t f) { return f==29?28:f==91?87:f; }
inline bool source_format_supported(uint32_t f) {
    f=unorm_format(f); return f==10 || f==24 || f==28 || f==87;
}
inline uint32_t output_format(uint32_t f) { return f==10?24:unorm_format(f); }
inline uint32_t default_color_space(uint32_t f) { return f==10?1u:0u; }
// DXGI: full-range RGB G22 P709=0; linear P709=1; PQ P2020=12.
inline bool source_color_supported(uint32_t f,uint32_t cs) {
    return (f==10 && cs==1) || (unorm_format(f)==24 && (cs==0 || cs==12)) ||
        ((unorm_format(f)==28 || unorm_format(f)==87) && cs==0);
}
inline uint32_t output_color_space(uint32_t f,uint32_t cs) { return f==10?12:cs; }
inline uint32_t virtual_buffer_count(uint32_t, uint32_t) {
    // This D3D11 adapter exposes one stable logical GetBuffer(0), including when
    // the native output is flip-model. ReShade's D3D11 backend uses this convention.
    // It is NOT a D3D12 explicit-index resource ring. Indexed D3D11 clients are
    // outside this prototype's contract and GetBuffer(index>0) fails explicitly.
    return 1u;
}
inline uint32_t next_virtual_buffer(uint32_t, uint32_t, uint32_t) { return 0u; }
struct ResizePlan { uint32_t count,width,height,format; bool valid; };
inline ResizePlan plan_resize(uint32_t count,uint32_t width,uint32_t height,uint32_t format,
    uint32_t oldCount,uint32_t oldFormat,uint32_t clientWidth,uint32_t clientHeight) {
    ResizePlan p{count?count:oldCount,width?width:clientWidth,height?height:clientHeight,format?format:oldFormat,false};
    p.valid=p.count>=1 && p.count<=16 && p.width>0 && p.height>0 && p.width<=32768 && p.height<=32768 && source_format_supported(p.format);
    return p;
}
// Whether a ResizeBuffers is still needed to finish something the presentation swapchain has been
// through. DXGI requires one to complete a fullscreen or target-mode transition, and a client issues it
// then without having seen any Present error first, so Present results alone cannot decide this. Only a
// resize that actually ran clears the flag; a status probe may set it but never clears it.
struct ResyncState {
    // False to begin with: a swapchain the bridge just created already matches what it was created for.
    bool pending=false;
    void display_transition() { pending=true; }  // SetFullscreenState or ResizeTarget, success or not
    void present_status(bool clean) { if(!clean) pending=true; } // any Present, including a TEST probe
    void resized() { pending=false; }            // a ResizeBuffers that reached the swapchain
};
// A ResizeBuffers that asks for the configuration already in effect. The caller adds the conditions
// that numbers alone cannot decide: a healthy bridge, no pending re-synchronisation, and a presentation
// swapchain whose descriptor still reports this geometry.
inline bool resize_is_redundant(const ResizePlan& p,uint32_t flags,uint32_t currentCount,
    uint32_t currentWidth,uint32_t currentHeight,uint32_t currentFormat,uint32_t currentFlags) {
    return p.valid && p.count==currentCount && p.width==currentWidth && p.height==currentHeight &&
        p.format==currentFormat && flags==currentFlags;
}
inline double pq_encode_nits(double nits) {
    const double y=std::pow(std::clamp(nits,0.0,10000.0)/10000.0,2610.0/16384.0);
    return std::pow((3424.0/4096.0+(2413.0/128.0)*y)/(1.0+(2392.0/128.0)*y),2523.0/32.0);
}
// Both APIs signal the same shared timeline, but only under the ordering:
// D3D11 Signal(produced) -> D3D12 Wait -> work -> Signal(retired) -> D3D11 Wait.
// A larger value must never bypass work associated with a smaller value.
struct FenceTimeline {
    uint64_t serial=0;
    uint64_t next() { return ++serial; }
};
} // namespace tgfg
