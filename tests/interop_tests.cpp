// SPDX-License-Identifier: MIT
#include "tgfg_interop_policy.hpp"
// The one DXGI flag these checks use, spelled out so the test stays free of Windows headers.
#define DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT_VALUE 64u
#include <iostream>
#include <cstdlib>
#include <limits>
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"FAILED line "<<__LINE__<<": " #x "\n";return EXIT_FAILURE;}}while(0)
int main() {
    using namespace tgfg;
    CHECK(fits({0,0,1920,1080},1920,1080));CHECK(fits({32,64,128,256},1920,1080));
    CHECK(!fits({0,0,0,1080},1920,1080));CHECK(!fits({0,0,1920,0},1920,1080));
    CHECK(!fits({1,0,1920,1080},1920,1080));CHECK(!fits({0,1,1920,1080},1920,1080));
    CHECK(!fits({UINT32_MAX,0,2,1},UINT32_MAX,UINT32_MAX));
    CHECK(!fits({0,UINT32_MAX,1,2},UINT32_MAX,UINT32_MAX));
    for(auto format:{10u,24u,28u,29u,87u,91u})CHECK(source_format_supported(format));
    for(auto format:{0u,2u,40u,41u,255u})CHECK(!source_format_supported(format));
    CHECK(output_format(10)==24);CHECK(output_format(24)==24);CHECK(output_format(29)==28);CHECK(output_format(91)==87);
    CHECK(default_color_space(10)==1);CHECK(default_color_space(24)==0);
    CHECK(source_color_supported(10,1));CHECK(!source_color_supported(10,0));CHECK(!source_color_supported(10,12));
    CHECK(source_color_supported(24,0));CHECK(source_color_supported(24,12));CHECK(!source_color_supported(24,1));
    CHECK(source_color_supported(28,0));CHECK(!source_color_supported(28,12));CHECK(!source_color_supported(87,1));
    CHECK(output_color_space(10,1)==12);CHECK(output_color_space(24,12)==12);CHECK(output_color_space(87,0)==0);
    for(auto effect:{0u,1u,3u,4u}) {CHECK(virtual_buffer_count(3,effect)==1);CHECK(next_virtual_buffer(0,3,effect)==0);}
    auto p=plan_resize(0,0,0,0,2,10,2560,1440);
    CHECK(p.valid && p.count==2 && p.width==2560 && p.height==1440 && p.format==10);
    p=plan_resize(3,3840,2160,24,2,28,1920,1080);CHECK(p.valid && p.count==3 && p.format==24);
    CHECK(!plan_resize(17,1,1,24,2,28,1,1).valid);CHECK(!plan_resize(0,1,1,24,0,28,1,1).valid);
    CHECK(!plan_resize(2,0,0,24,2,28,0,0).valid);CHECK(!plan_resize(2,32769,2160,24,2,28,1,1).valid);
    CHECK(!plan_resize(2,3840,32769,24,2,28,1,1).valid);CHECK(!plan_resize(2,3840,2160,2,2,28,1,1).valid);
    // A ResizeBuffers to the configuration already in effect, which the bridge may skip once the
    // runtime conditions it cannot see from here also hold.
    const uint32_t live=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT_VALUE;
    auto same=plan_resize(3,3840,2160,10,3,10,3840,2160);
    CHECK(resize_is_redundant(same,live,3,3840,2160,10,live));
    // Zero width and height mean the window's current client area, which here is the same configuration.
    CHECK(resize_is_redundant(plan_resize(0,0,0,0,3,10,3840,2160),live,3,3840,2160,10,live));
    // Any one difference is a real resize: count, width, height, format, flags.
    CHECK(!resize_is_redundant(same,live,2,3840,2160,10,live));
    CHECK(!resize_is_redundant(same,live,3,2560,2160,10,live));
    CHECK(!resize_is_redundant(same,live,3,3840,1440,10,live));
    CHECK(!resize_is_redundant(same,live,3,3840,2160,24,live));
    CHECK(!resize_is_redundant(same,live,3,3840,2160,10,0));
    CHECK(!resize_is_redundant(plan_resize(0,0,0,0,3,10,2560,1440),live,3,3840,2160,10,live));
    // An HDR toggle changes the format, so it is never mistaken for a no-op.
    CHECK(!resize_is_redundant(plan_resize(0,0,0,24,3,10,3840,2160),live,3,3840,2160,10,live));
    // An invalid plan is rejected before anything else.
    CHECK(!resize_is_redundant(plan_resize(17,3840,2160,10,3,10,3840,2160),live,17,3840,2160,10,live));
    // Matching numbers are not sufficient: a resize may still be owed to the presentation swapchain.
    ResyncState resync;
    CHECK(!resync.pending); // nothing is owed on a swapchain the bridge just created
    // DXGI requires a resize to complete a fullscreen or target-mode change, and the client issues it
    // before any Present has had the chance to report a problem.
    resync.display_transition();
    CHECK(resync.pending);
    resync.present_status(true); // a clean occlusion probe presents nothing, so it settles nothing
    CHECK(resync.pending);
    resync.resized();
    CHECK(!resync.pending);
    // A mode change owes one, and occlusion is treated the same way; a later clean probe does not
    // retire either. These checks cover the state machine only: that the bridge consults it before
    // skipping a resize is D3D behaviour, which no portable test here establishes.
    resync.present_status(false);
    CHECK(resync.pending);
    resync.present_status(true);
    CHECK(resync.pending);
    resync.resized();
    CHECK(!resync.pending);
    // Clean frames on their own never owe anything.
    for(int i=0;i<3;++i) resync.present_status(true);
    CHECK(!resync.pending);
    CHECK(std::abs(pq_encode_nits(100)-0.5080784215)<1e-9);
    CHECK(std::abs(pq_encode_nits(1000)-0.7518270962)<1e-9);
    CHECK(std::abs(pq_encode_nits(10000)-1)<1e-10);
    CHECK(pq_encode_nits(0)<1e-6);CHECK(pq_encode_nits(-10)==pq_encode_nits(0));
    CHECK(pq_encode_nits(20000)==pq_encode_nits(10000));
    CHECK(pq_encode_nits(80)<pq_encode_nits(100));
    FenceTimeline timeline;
    const auto produced=timeline.next(),retired=timeline.next(),nextProduced=timeline.next();
    CHECK(produced<retired && retired<nextProduced && produced==1);
    std::cout<<checks<<" interop policy checks passed. No D3D11/D3D12 device or GPU was exercised.\n";
}
