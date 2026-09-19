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
    CHECK(sizeof(TGFG_Request)==44); CHECK(sizeof(TGFG_Status)==152); CHECK(sizeof(TGFG_Camera)==352);
    CHECK(offsetof(TGFG_Request,sequence)==8); CHECK(offsetof(TGFG_Status,requestedSequence)==8);
    CHECK(offsetof(TGFG_Status,sourceApi)==88); CHECK(offsetof(TGFG_Status,sourcePresents)==104);
    CHECK(offsetof(TGFG_Status,nativeLoadPid)==144); CHECK(offsetof(TGFG_Camera,viewToClip)==32); CHECK(offsetof(TGFG_Camera,nearPlane)==336);
    TGFG_Request request{sizeof(TGFG_Request),TGFG_ABI,1,1,2,0,0,0,11,0};
    CHECK(offsetof(TGFG_Request,reflexMode)==40);
    for(unsigned reflex=0;reflex<=2;++reflex) {request.reflexMode=reflex;CHECK(valid(request));}
    request.reflexMode=0;
    CHECK(valid(request));
    Readiness ready; ready.sdk=ready.caps=ready.renderFrame=ready.inputs=ready.inputContract=ready.dx11Bridge=true;
    ready.maxGenerated=5; ready.dynamicSupported=1; ready.format=24; ready.evalCount=1;
    CHECK(eligibility(request,ready)==Reason::Ready);
    for(unsigned multiplier=2;multiplier<=6;++multiplier) {request.fixedMultiplier=multiplier;CHECK(eligibility(request,ready)==Reason::Ready);}
    auto altered=request; altered.fixedMultiplier=7; CHECK(!valid(altered));
    altered=request; altered.fixedMultiplier=1; CHECK(!valid(altered));
    altered=request; altered.mode=3; CHECK(!valid(altered));
    altered=request; altered.reflexMode=3; CHECK(!valid(altered));
    altered=request; altered.abi=0x00010000; CHECK(!valid(altered));
    altered=request; altered.size=0; CHECK(!valid(altered));
    altered=request; altered.dynamicTargetFps=std::numeric_limits<float>::quiet_NaN(); CHECK(!valid(altered));
    altered=request; altered.reflexBaseFpsLimit=std::numeric_limits<float>::infinity(); CHECK(!valid(altered));
    altered=request; altered.dynamicTargetFps=-1; CHECK(!valid(altered));
    altered=request; altered.dynamicTargetFps=1001; CHECK(!valid(altered));
    altered=request; altered.mode=0; CHECK(eligibility(altered,ready)==Reason::UserOff);
    altered=request; altered.sourceGraphicsApi=12; CHECK(eligibility(altered,ready)==Reason::UnsupportedAPI);
    altered=request; altered.suspensionMask=1; CHECK(eligibility(altered,ready)==Reason::Suspended);
    auto bad=ready; bad.dx11Bridge=false; CHECK(eligibility(request,bad)==Reason::BridgeNotCreated);
    bad=ready; bad.ageMs=1001; CHECK(eligibility(request,bad)==Reason::ClientTimeout);
    bad=ready; bad.fatal=true; CHECK(eligibility(request,bad)==Reason::NativeError);
    bad=ready; bad.sdk=false; CHECK(eligibility(request,bad)==Reason::WaitingBackend);
    bad=ready; bad.caps=false; CHECK(eligibility(request,bad)==Reason::CapsUnknown);
    bad=ready; bad.maxGenerated=1; CHECK(eligibility(request,bad)==Reason::MultiplierUnsupported);
    altered=request; altered.mode=2; CHECK(eligibility(altered,ready)==Reason::Ready);
    bad=ready; bad.dynamicSupported=0; CHECK(eligibility(altered,bad)==Reason::DynamicUnsupported);
    bad=ready; bad.format=10; CHECK(eligibility(request,bad)==Reason::UnsupportedFormat);
    bad=ready; bad.format=0; CHECK(eligibility(request,bad)==Reason::UnsupportedFormat);
    bad=ready; bad.renderFrame=false; CHECK(eligibility(request,bad)==Reason::MissingRenderEvent);
    bad=ready; bad.evalCount=2; CHECK(eligibility(request,bad)==Reason::MultipleDlssViews);
    bad=ready; bad.inputContract=false; CHECK(eligibility(request,bad)==Reason::InputContractInvalid);
    bad=ready; bad.inputs=false; CHECK(eligibility(request,bad)==Reason::NoDlssInputs);
    for(auto f:{24u,28u,29u,87u,91u}) CHECK(supported_present_format(f));
    for(auto f:{0u,10u,2u,255u}) CHECK(!supported_present_format(f));
    CHECK(frame_limit_us(0)==0); CHECK(frame_limit_us(100)==10000); CHECK(frame_limit_us(60)==16667);
    CHECK(frame_limit_us(236.0f/4)==16949); CHECK(frame_limit_us(-1)==0);
    TGFG_Camera camera{}; camera.size=sizeof(camera);camera.abi=TGFG_ABI;camera.width=3840;camera.height=2160;
    camera.nearPlane=0.1f;camera.farPlane=10000;camera.fovRadians=1;camera.aspect=16.0f/9;
    for(unsigned i=0;i<4;++i) camera.viewToClip[i*5]=camera.clipToView[i*5]=camera.clipToPrev[i*5]=camera.prevToClip[i*5]=1;
    CHECK(valid(camera)); camera.viewToClip[3]=std::numeric_limits<float>::infinity(); CHECK(!valid(camera));
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
