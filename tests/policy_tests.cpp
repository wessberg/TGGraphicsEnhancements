// SPDX-License-Identifier: MIT
#include "tgfg_policy.hpp"
#include "tgfg_ring.hpp"
#include <iostream>
#include <limits>
#include <cstddef>
#include <cstdlib>
static unsigned checks=0;
#define CHECK(x) do {++checks; if(!(x)){std::cerr<<"FAILED line "<<__LINE__<<": " #x "\n"; return EXIT_FAILURE;}}while(0)
int main() {
    using namespace tgfg;
    CHECK(sizeof(TGFG_Request)==48); CHECK(sizeof(TGFG_Status)==188); CHECK(sizeof(TGFG_Camera)==416);
    CHECK(offsetof(TGFG_Request,sequence)==8); CHECK(offsetof(TGFG_Status,requestedSequence)==8);
    CHECK(offsetof(TGFG_Status,sourceApi)==88); CHECK(offsetof(TGFG_Status,sourcePresents)==104);
    CHECK(offsetof(TGFG_Status,nativeLoadPid)==144); CHECK(offsetof(TGFG_Status,backendsAvailable)==152);
    CHECK(offsetof(TGFG_Status,backendResult)==184);
    CHECK(offsetof(TGFG_Camera,viewToClip)==32); CHECK(offsetof(TGFG_Camera,nearPlane)==336); CHECK(offsetof(TGFG_Camera,worldToView)==352);
    TGFG_Request request{sizeof(TGFG_Request),TGFG_ABI,1,1,2,0,0,0,11,0,1};
    CHECK(offsetof(TGFG_Request,latencyMode)==40); CHECK(offsetof(TGFG_Request,backend)==44);
    for(unsigned latency=0;latency<=4;++latency) {request.latencyMode=latency;CHECK(valid(request));}
    request.latencyMode=0;
    for(unsigned backend=1;backend<=3;++backend) {request.backend=backend;CHECK(valid(request));}
    request.backend=1;
    CHECK(valid(request));
    Readiness ready; ready.sdk=ready.caps=ready.renderFrame=ready.inputs=ready.inputContract=ready.dx11Bridge=true;
    ready.maxGenerated=5; ready.dynamicSupported=1; ready.format=24; ready.evalCount=1;
    CHECK(eligibility(request,ready)==Reason::Ready);
    for(unsigned multiplier=2;multiplier<=kMaxMultiplier;++multiplier) {request.fixedMultiplier=multiplier;CHECK(eligibility(request,ready)==Reason::Ready);}
    // Requests are clamped to what the runtime reports, never rejected for exceeding it.
    request.fixedMultiplier=16; CHECK(generated_frames(request,5)==5); CHECK(generated_frames(request,1)==1);
    request.fixedMultiplier=2; CHECK(generated_frames(request,5)==1); CHECK(generated_frames(request,0)==1);
    request.fixedMultiplier=4; CHECK(generated_frames(request,3)==3); CHECK(generated_frames(request,6)==3);
    request.fixedMultiplier=2;
    auto altered=request; altered.fixedMultiplier=kMaxMultiplier+1; CHECK(!valid(altered));
    altered=request; altered.fixedMultiplier=1; CHECK(!valid(altered));
    altered=request; altered.mode=3; CHECK(!valid(altered));
    altered=request; altered.latencyMode=5; CHECK(!valid(altered));
    altered=request; altered.backend=4; CHECK(!valid(altered));
    altered=request; altered.backend=0; CHECK(!valid(altered)); // frame generation requested without a technology
    altered.mode=0; CHECK(valid(altered));
    altered=request; altered.abi=0x00040000; CHECK(!valid(altered));
    altered=request; altered.size=0; CHECK(!valid(altered));
    altered=request; altered.dynamicTargetFps=std::numeric_limits<float>::quiet_NaN(); CHECK(!valid(altered));
    altered=request; altered.baseFpsLimit=std::numeric_limits<float>::infinity(); CHECK(!valid(altered));
    altered=request; altered.dynamicTargetFps=-1; CHECK(!valid(altered));
    altered=request; altered.dynamicTargetFps=1001; CHECK(!valid(altered));
    altered=request; altered.mode=0; CHECK(eligibility(altered,ready)==Reason::UserOff);
    altered=request; altered.sourceGraphicsApi=12; CHECK(eligibility(altered,ready)==Reason::UnsupportedAPI);
    altered=request; altered.suspensionMask=1; CHECK(eligibility(altered,ready)==Reason::Suspended);
    auto bad=ready; bad.dx11Bridge=false; CHECK(eligibility(request,bad)==Reason::BridgeNotCreated);
    bad=ready; bad.ageMs=1001; CHECK(eligibility(request,bad)==Reason::ClientTimeout);
    bad=ready; bad.fatal=true; CHECK(eligibility(request,bad)==Reason::NativeError);
    bad=ready; bad.sdk=false; CHECK(eligibility(request,bad)==Reason::WaitingBackend);
    bad=ready; bad.backend=false; CHECK(eligibility(request,bad)==Reason::BackendUnavailable);
    bad=ready; bad.caps=false; CHECK(eligibility(request,bad)==Reason::CapsUnknown);
    bad=ready; bad.maxGenerated=0; CHECK(eligibility(request,bad)==Reason::MultiplierUnsupported);
    bad=ready; bad.maxGenerated=1; altered=request; altered.fixedMultiplier=6; CHECK(eligibility(altered,bad)==Reason::Ready);
    altered=request; altered.mode=2; CHECK(eligibility(altered,ready)==Reason::Ready);
    bad=ready; bad.dynamicSupported=0; CHECK(eligibility(altered,bad)==Reason::DynamicUnsupported);
    bad=ready; bad.format=10; CHECK(eligibility(request,bad)==Reason::UnsupportedFormat);
    bad=ready; bad.format=0; CHECK(eligibility(request,bad)==Reason::UnsupportedFormat);
    bad=ready; bad.renderFrame=false; CHECK(eligibility(request,bad)==Reason::MissingRenderEvent);
    bad=ready; bad.evalCount=2; CHECK(eligibility(request,bad)==Reason::MultipleDlssViews);
    bad=ready; bad.inputContract=false; CHECK(eligibility(request,bad)==Reason::InputContractInvalid);
    bad=ready; bad.inputs=false; CHECK(eligibility(request,bad)==Reason::NoDlssInputs);
    // DLSS and "Off" present through the standard swapchain; FSR and XeSS through their own.
    altered=request; altered.backend=0; altered.mode=0; CHECK(output_for(altered)==Output::Standard);
    altered.backend=1; CHECK(output_for(altered)==Output::Standard);
    altered.backend=2; CHECK(output_for(altered)==Output::FSR);
    altered.backend=3; CHECK(output_for(altered)==Output::XeSS);
    // One latency setting; each frame generation technology dictates what may run with it.
    LatencyCaps nvidia; nvidia.reflex=true; nvidia.xell=true;
    LatencyCaps amd; amd.antiLag2=true;
    LatencyCaps intel; intel.xell=true; intel.xellStandalone=true;
    auto want=request; want.backend=2;
    want.latencyMode=uint32_t(Latency::Reflex); CHECK(select_latency(want,Output::FSR,false,nvidia)==Latency::Reflex);
    want.latencyMode=uint32_t(Latency::ReflexBoost); CHECK(select_latency(want,Output::Standard,false,nvidia)==Latency::ReflexBoost);
    want.latencyMode=uint32_t(Latency::Reflex); CHECK(select_latency(want,Output::XeSS,false,nvidia)==Latency::XeLL); // XeSS-FG runs with XeLL only
    CHECK(select_latency(want,Output::XeSS,false,LatencyCaps{})==Latency::Off);
    want.latencyMode=uint32_t(Latency::XeLL); CHECK(select_latency(want,Output::FSR,false,nvidia)==Latency::Off); // XeLL alone is Intel-only
    CHECK(select_latency(want,Output::Standard,false,intel)==Latency::XeLL);
    want.latencyMode=uint32_t(Latency::AntiLag2); CHECK(select_latency(want,Output::FSR,false,amd)==Latency::AntiLag2);
    CHECK(select_latency(want,Output::FSR,false,nvidia)==Latency::Off);
    want.latencyMode=uint32_t(Latency::Off); CHECK(select_latency(want,Output::Standard,true,nvidia)==Latency::Reflex); // DLSS-G requires Reflex
    CHECK(select_latency(want,Output::Standard,false,nvidia)==Latency::Off);
    CHECK(select_latency(want,Output::Standard,true,amd)==Latency::Off);
    for(auto f:{24u,28u,29u,87u,91u}) CHECK(supported_present_format(f));
    for(auto f:{0u,10u,2u,255u}) CHECK(!supported_present_format(f));
    CHECK(frame_limit_us(0)==0); CHECK(frame_limit_us(100)==10000); CHECK(frame_limit_us(60)==16667);
    CHECK(frame_limit_us(236.0f/4)==16949); CHECK(frame_limit_us(-1)==0);
    TGFG_Camera camera{}; camera.size=sizeof(camera);camera.abi=TGFG_ABI;camera.width=3840;camera.height=2160;
    camera.nearPlane=0.1f;camera.farPlane=10000;camera.fovRadians=1;camera.aspect=16.0f/9;
    for(unsigned i=0;i<4;++i) camera.viewToClip[i*5]=camera.clipToView[i*5]=camera.clipToPrev[i*5]=camera.prevToClip[i*5]=1;
    CHECK(valid(camera)); camera.viewToClip[3]=std::numeric_limits<float>::infinity(); CHECK(!valid(camera));
    camera.viewToClip[3]=0; camera.worldToView[7]=std::numeric_limits<float>::quiet_NaN(); CHECK(!valid(camera)); camera.worldToView[7]=0;
    camera.viewToClip[3]=0; camera.farPlane=0.01f; CHECK(!valid(camera)); camera.farPlane=10000;
    camera.width=0; CHECK(!valid(camera)); camera.width=3840;camera.fovRadians=4;CHECK(!valid(camera));
    FrameRing<unsigned> ring;
    for(unsigned id=0;id<6;++id) {auto* slot=ring.acquire(id);CHECK(slot!=nullptr);*slot=id+100;}
    CHECK(ring.acquire(6)==nullptr); CHECK(*ring.find(0)==100); CHECK(ring.find(6)==nullptr);
    ring.release(6); CHECK(ring.find(0)!=nullptr); // wrong generation cannot free a slot
    ring.release(0); CHECK(ring.find(0)==nullptr); CHECK(ring.acquire(6)!=nullptr);
    CHECK(ring.find(0)==nullptr); CHECK(ring.find(6)!=nullptr);
    FrameRing<unsigned> wrap;
    CHECK(wrap.acquire(UINT32_MAX)!=nullptr); CHECK(wrap.find(UINT32_MAX)!=nullptr);
    wrap.release(UINT32_MAX); CHECK(wrap.find(UINT32_MAX)==nullptr); CHECK(wrap.acquire(0)!=nullptr);
    std::cout<<checks<<" portable ABI/policy/ring checks passed. No Windows/Unity/Streamline rendering test was performed.\n";
    return EXIT_SUCCESS;
}
