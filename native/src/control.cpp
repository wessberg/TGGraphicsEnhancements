// SPDX-License-Identifier: MIT
#include "sl_runtime.hpp"
#include "dx11_bridge.hpp"
#include "fg_backends.hpp"
#include "camera_orientation.hpp"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <memory>
namespace tgfg {
HMODULE moduleSelf=nullptr;
namespace {
void api_error(const sl::APIError& error) {
    // Called on NVIDIA's presentation thread: no mutex, logging, or GPU work here.
    state().asyncApiError.store(error.hres,std::memory_order_relaxed);
}
// Streamline PCL marker (Reflex); XeLL markers are sent by latency_marker.
void marker(sl::PCLMarker m,Frame* f) {
    auto& s=state(); if(!f || !f->token || !s.reflexReady) return;
    SLGuard inside; auto result=s.sl.marker(m,*f->token);
    if(result!=sl::Result::eOk) {
        s.status.lastSetOptionsResult=static_cast<int32_t>(result); s.fatal=true;
        if(!s.diag.markerFailLogged) {
            s.diag.markerFailLogged=true;
            log("slPCLSetMarker("+std::to_string(static_cast<int>(m))+") returned "+std::to_string(static_cast<int>(result))+
                "; frame generation is disabled for the rest of this session");
        }
    }
}
const char* reason_name(uint32_t r) {
    static const char* names[]={"Ready","NoClient","WaitingBackend","UnsupportedAPI","Suspended","UserOff",
        "CapsUnknown","MultiplierUnsupported","DynamicUnsupported","UnsupportedFormat","NativeError","OptionsAccepted",
        "InputDispatched","ClientTimeout","MissingRenderEvent","NoDlssInputs","StaleFrame","MultipleDlssViews",
        "UnsupportedQueue","InputContractInvalid","ExistingStreamline","RuntimeNotSupported","CameraInvalid",
        "PresentFailed","BridgeNotCreated","InteropFailure","UnsupportedColorSpace","SourceDeviceMismatch",
        "UnsupportedSourceLayout","BridgeReady","VsyncUnsupported","BackendUnavailable","BackendFailed"};
    return r<sizeof(names)/sizeof(names[0])?names[r]:"Unknown";
}
const char* output_name(Output o) { return o==Output::FSR?"FSR":o==Output::XeSS?"XeSS":"standard"; }
const char* latency_name(Latency l) {
    static const char* names[]={"Off","Reflex","Reflex+Boost","XeLL","Anti-Lag 2"};
    return uint32_t(l)<5?names[uint32_t(l)]:"Unknown";
}
std::string hex32(uint32_t v) { char b[16]{}; std::snprintf(b,sizeof(b),"%X",v); return b; }
std::string n(uint64_t v) { return std::to_string(v); }
} // namespace
// One line every 5 s: where frames drop out between Unity's frame events, the DLSS SR
// evaluation, the Present-time readiness check and the frame generation options.
void summarise(uint64_t now) {
    auto& s=state(); auto& d=s.diag;
    if(!d.lastSummary) { d.lastSummary=now; return; }
    if(now-d.lastSummary<5000) return;
    std::string line="Pipeline "+n((now-d.lastSummary)/1000)+"s: frames="+n(d.framesBegun)+
        " cameraPackets="+n(d.cameraPackets)+" events0/1/2="+n(d.ev0)+"/"+n(d.ev1)+"/"+n(d.ev2);
    if(d.evUnknownFrame) line+=" staleEvents="+n(d.evUnknownFrame);
    line+=" | NGX evals="+n(d.ngxEvals)+" captured="+n(d.captureOk)+
        " rejected[noFrameId="+n(d.ngxNoFrameId)+" noCamera="+n(d.ngxNoCamera)+" beforeBegin="+n(d.ngxNotBegun)+
        " afterEnd="+n(d.ngxAfterEnd)+" multi="+n(d.ngxMulti)+" dims="+n(d.ngxDims)+" params="+n(d.ngxParams)+
        " requestOff="+n(d.ngxRequestOff)+" copyFailed="+n(d.captureFailed)+" evalFailed="+n(d.evalFailed)+"]";
    if(d.hdrpPackets) line+=" | HDRP packets="+n(d.hdrpPackets)+" captured="+n(d.hdrpCaptured)+
        " upscalerFrames="+n(d.hdrpUpscaler)+" rejected="+n(d.hdrpRejected);
    if(s.hudlessRequested || d.hudlessCaptured) {
        line+=" | hudless captured="+n(d.hudlessCaptured)+" rejected="+n(d.hudlessRejected);
        if(d.hudlessLastResult) line+="(last "+std::to_string(d.hudlessLastResult)+")";
        line+=" supplied="+n(d.hudlessTagged)+" checks="+n(d.hudlessChecks)+" still="+n(d.hudlessStill)+
            " diff="+n(d.hudlessDiffPermille/10)+"."+n(d.hudlessDiffPermille%10)+"% "+(s.hudlessOk?"verified":"unverified");
    }
    line+=" | presents="+n(d.presents)+" withInputs="+n(d.presentsWithInputs)+" tagged="+n(d.tagged);
    if(d.tagFailed) line+=" tagFailed="+n(d.tagFailed);
    line+=" ready="+n(d.readyPresents)+" fgOn="+n(d.fgPresents)+" last="+reason_name(d.lastWhy);
    line+=" | request backend="+n(s.request.backend)+" mode="+n(s.request.mode)+" x"+n(s.request.fixedMultiplier)+
        " suspend=0x"+hex32(s.request.suspensionMask)+" latency="+n(s.request.latencyMode)+
        " | output="+output_name(s.output)+" latency="+latency_name(s.latency);
    if(s.output==Output::Standard) line+=" DLSS-G status=0x"+hex32(s.status.dlssgStatus)+" vsyncSupported="+(s.vsyncSupported?"1":"0");
    else if(s.status.backendResult) line+=" lastError="+std::to_string(s.status.backendResult);
    // Rendered plus generated frames as the runtime itself reports them, never inferred from our own
    // counts: DLSS-G through slDLSSGGetState, XeSS through its present status. The FSR API exposes no
    // equivalent, so FSR says so rather than printing a zero that reads as a failure.
    line+=" sdkPresented="+(s.output==Output::FSR?std::string("unreported"):n(d.sdkPresented));
    line+=std::string(" fatal=")+(s.fatal?"1":"0");
    // Session totals. A resize that something was owed must still show up here as a real one.
    line+=" | resizes="+n(s.resizes)+" skipped="+n(s.resizesSkipped);
    if(s.reflexReady && s.sl.reflexState && (s.latency==Latency::Reflex || s.latency==Latency::ReflexBoost)) {
        // Reflex's own per-frame timestamps (microseconds). PC latency here is simulation start to
        // the end of GPU rendering on the presenting queue, which in this bridge follows the D3D11
        // frame, so it covers the game's rendering too. Heap-allocated: the state holds 64 reports.
        static std::unique_ptr<sl::ReflexState> reflex;
        if(!reflex) reflex=std::make_unique<sl::ReflexState>();
        *reflex=sl::ReflexState{};
        SLGuard inside;
        if(s.sl.reflexState(*reflex)==sl::Result::eOk) {
            // Each average divides by the number of frames that contributed to it, not by the number
            // that contributed to the first one: a frame missing a render-submit or present timestamp
            // would otherwise drag that average down by however many such frames there were, which is
            // not a constant between backends and so cannot be compared across them.
            uint64_t pc=0,render=0,gpu=0,present=0; uint32_t count=0,rendered=0,presented=0;
            for(const auto& r:reflex->frameReport) {
                if(!r.frameID || !r.simStartTime || r.gpuRenderEndTime<=r.simStartTime) continue;
                pc+=r.gpuRenderEndTime-r.simStartTime; gpu+=r.gpuActiveRenderTimeUs; ++count;
                if(r.renderSubmitStartTime && r.gpuRenderEndTime>r.renderSubmitStartTime) {
                    render+=r.gpuRenderEndTime-r.renderSubmitStartTime; ++rendered;
                }
                // The same pair of timestamps XeLL reports, so the two latency technologies can be
                // compared on one measurement instead of on two that end in different places.
                if(r.presentEndTime>r.simStartTime) { present+=r.presentEndTime-r.simStartTime; ++presented; }
            }
            auto ms=[](uint64_t us){ char b[32]{}; std::snprintf(b,sizeof(b),"%.1fms",us/1000.0); return std::string(b); };
            line+=" | Reflex mode="+n(static_cast<uint32_t>(s.lastReflex.mode))+" lowLatencyAvailable="+(reflex->lowLatencyAvailable?"1":"0")+
                " reportAvailable="+(reflex->latencyReportAvailable?"1":"0")+" reports="+n(count);
            if(count) line+=" pcLatency="+ms(pc/count)+" gpuActive="+ms(gpu/count);
            // The population behind each average, because they need not be the same frames.
            if(rendered) line+=" renderLatency="+ms(render/rendered)+"/"+n(rendered);
            if(presented) line+=" simToPresent="+ms(present/presented)+"/"+n(presented);
        }
    }
    // The XeLL equivalent, so DLSS-with-Reflex and XeSS-with-XeLL can be compared with numbers.
    if(s.latency==Latency::XeLL) {
        uint64_t sim=0,submit=0; uint32_t frames=0;
        if(xell_latency(sim,submit,frames) && frames) {
            auto ms=[](uint64_t us){ char b[32]{}; std::snprintf(b,sizeof(b),"%.1fms",us/1000.0); return std::string(b); };
            line+=" | XeLL reports="+n(frames)+" simToPresent="+ms(sim)+" submitToPresent="+ms(submit);
        }
    }
    log(line);
    note_foreign_streamline();
    const bool markerLogged=d.markerFailLogged, tagLogged=d.tagFailLogged;
    d=Diag{}; d.lastSummary=now; d.markerFailLogged=markerLogged; d.tagFailLogged=tagLogged;
}
namespace {
// Applied through update_latency(), which selects Reflex when the user chose it or DLSS-G needs it.
void set_reflex(bool active,bool boost,float limit) {
    auto& s=state(); if(!s.reflexReady) return;
    sl::ReflexOptions options{};
    options.mode=!active?sl::ReflexMode::eOff:boost?sl::ReflexMode::eLowLatencyWithBoost:sl::ReflexMode::eLowLatency;
    options.frameLimitUs=active?frame_limit_us(limit):0;
    if(s.haveReflex && s.lastReflex.mode==options.mode && s.lastReflex.frameLimitUs==options.frameLimitUs) return;
    SLGuard inside; auto result=s.sl.reflexOptions(options);
    if(result==sl::Result::eOk) { s.lastReflex=options; s.haveReflex=true; }
    else { s.status.lastSetOptionsResult=static_cast<int32_t>(result); s.fatal=true; log("slReflexSetOptions failed"); }
}
void apply_options(Frame* frame,Reason why,const DXGI_SWAP_CHAIN_DESC& description) {
    auto& s=state();
    bool enabled=why==Reason::Ready;
    if(s.fatal) { enabled=false; why=Reason::NativeError; }
    if(!s.dlssReady) { // standard swapchain without DLSS-G: nothing to switch, only the reason to report
        s.status.reason=static_cast<uint32_t>(why); s.status.lastAcceptedMode=0;
        if(s.request.mode==0) s.status.appliedSequence=s.request.sequence;
        return;
    }
    sl::DLSSGOptions options{};
    options.mode=!enabled?sl::DLSSGMode::eOff:s.request.mode==2?sl::DLSSGMode::eDynamic:sl::DLSSGMode::eOn;
    // Streamline requires a positive frame count even while the mode is eOff. In Dynamic mode the
    // SDK 2.14.1 guide documents numFramesToGenerate as ignored; it is still sent as the user's
    // "up to Nx" bound so a runtime that honours it caps generation there. Either way the request
    // is clamped to the maximum the runtime reports.
    options.numFramesToGenerate=enabled?generated_frames(s.request,s.status.maxGeneratedFrames):1;
    options.dynamicTargetFrameRate=enabled && s.request.mode==2?s.request.dynamicTargetFps:0;
    options.numBackBuffers=description.BufferCount;
    options.colorWidth=description.BufferDesc.Width; options.colorHeight=description.BufferDesc.Height;
    options.colorBufferFormat=static_cast<uint32_t>(description.BufferDesc.Format);
    options.queueParallelismMode=sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
    // DLSS-G only allocates the path that interpolates the HUD-less image separately from the back
    // buffer when this is on, so the HUD-less tag does nothing without it. It follows the verified
    // capture: on while one is supplied, off when the HUD is interpolated with the scene. No HUD
    // separation is fabricated; the tag still carries a real image or nothing.
    options.enableUserInterfaceRecomposition=s.hudlessOk?sl::eTrue:sl::eFalse;
    options.onErrorCallback=api_error;
    bool changed=!s.haveOptions || s.applied.mode!=options.mode ||
        s.applied.numFramesToGenerate!=options.numFramesToGenerate ||
        s.applied.dynamicTargetFrameRate!=options.dynamicTargetFrameRate ||
        s.applied.colorWidth!=options.colorWidth || s.applied.colorHeight!=options.colorHeight ||
        s.applied.numBackBuffers!=options.numBackBuffers || s.applied.colorBufferFormat!=options.colorBufferFormat ||
        s.applied.enableUserInterfaceRecomposition!=options.enableUserInterfaceRecomposition;
    auto result=sl::Result::eOk;
    if(changed) { SLGuard inside; result=s.sl.setOptions(s.viewport,options); }
    s.status.lastSetOptionsResult=static_cast<int32_t>(result);
    s.status.reason=static_cast<uint32_t>(why);
    if(result==sl::Result::eOk) {
        s.applied=options; s.haveOptions=true;
        s.status.lastAcceptedMode=enabled?s.request.mode:0;
        if(enabled || s.request.mode==0) s.status.appliedSequence=s.request.sequence;
        if(enabled) s.status.reason=static_cast<uint32_t>(Reason::OptionsAccepted);
    } else {
        s.status.reason=static_cast<uint32_t>(Reason::NativeError);
        options_off(); s.fatal=true;
    }
    if(!enabled) clear_tags(frame);
}
void __stdcall render_event(int eventId,void* encodedId) {
    auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    uint32_t id=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(encodedId));
    auto* frame=s.frames.find(id);
    if(!s.deviceReady || !frame || GetTickCount64()-frame->born>1000) {
        if(s.deviceReady) ++s.diag.evUnknownFrame;
        haveRenderFrame=false; return;
    }
    if(eventId==0) ++s.diag.ev0; else if(eventId==1) ++s.diag.ev1; else if(eventId==2) ++s.diag.ev2;
    if(eventId==0) { // unconditional frame boundary, also supplied in menus
        haveRenderFrame=true; renderFrameId=id;
        if(!frame->simEnded) { latency_marker(sl::PCLMarker::eSimulationEnd,frame); frame->simEnded=true; }
        latency_marker(sl::PCLMarker::eRenderSubmitStart,frame);
    } else if(eventId==1 && frame->cameraReady) {
        haveRenderFrame=true; renderFrameId=id;
        frame->renderBegun=true;
    } else if(eventId==2 && haveRenderFrame && renderFrameId==id) {
        frame->renderEnded=true;
    } else if(eventId==3 && haveRenderFrame && renderFrameId==id && frame->renderBegun && !frame->renderEnded && frame->hdrpPending) {
        capture_hdrp_inputs(*frame);
    } else if(eventId==4 && haveRenderFrame && renderFrameId==id && frame->renderBegun && !frame->renderEnded && frame->hudlessPending) {
        frame->hudlessPending=false;
        if(frame->inputs && s.hudlessRequested) {
            const int result=capture_dx11_hudless_frame(frame->hudless);
            if(result==0) ++s.diag.hudlessCaptured; else { ++s.diag.hudlessRejected; s.diag.hudlessLastResult=result; }
        }
    }
}
} // namespace
Readiness frame_readiness(Frame* frame,uint32_t format,uint32_t maxGenerated,uint32_t dynamicSupported,bool backendOk) {
    auto& s=state(); const uint64_t now=GetTickCount64();
    s.status.requestAgeMs=static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX,now-s.heartbeat));
    Readiness ready{};
    ready.sdk=s.deviceReady; ready.dx11Bridge=(s.status.bridgeFlags&1u)!=0; ready.caps=(s.status.flags&4u)!=0; ready.backend=backendOk;
    // Present itself proves the camera finished rendering. HDRP raises endCameraRendering only
    // after its final Submit, so the end marker bounds NGX evaluations but is not required here.
    ready.renderFrame=frame && frame->cameraReady && frame->renderBegun && now-frame->born<=1000;
    ready.inputs=frame && frame->inputs && frame->inputsSubmitted;
    ready.inputContract=frame && frame->inputContract;
    ready.ageMs=s.status.requestAgeMs; ready.maxGenerated=maxGenerated; ready.dynamicSupported=dynamicSupported;
    // One input source per frame: the DLSS evaluation, or HDRP's own buffers when DLSS doesn't run.
    ready.format=format; ready.evalCount=!frame?0u:frame->evalCount?frame->evalCount:frame->hdrpCaptured?1u:0u; ready.fatal=s.fatal;
    return ready;
}
void note_generation(Frame*,Reason why,bool enabled) {
    auto& s=state();
    s.status.reason=static_cast<uint32_t>(enabled?Reason::OptionsAccepted:why);
    s.status.lastAcceptedMode=enabled?s.request.mode:0;
    if(enabled || s.request.mode==0) s.status.appliedSequence=s.request.sequence;
    ++s.diag.presents; s.diag.lastWhy=static_cast<uint32_t>(why);
    if(why==Reason::Ready) ++s.diag.readyPresents;
    if(enabled) ++s.diag.fgPresents;
}
void note_hudless_difference(uint32_t differing,uint32_t changed,uint32_t moving,uint32_t samples) {
    auto& s=state(); if(!samples) return;
    // A still image (menu, pause, loading screen) says nothing about either check, so it is skipped
    // rather than counted against the captures.
    if(!moving) { ++s.diag.hudlessStill; return; }
    const uint32_t permille=static_cast<uint32_t>(uint64_t(differing)*1000/samples);
    ++s.diag.hudlessChecks; s.diag.hudlessDiffPermille=permille;
    // A HUD covers part of the frame; more than about a third of the sample points means the presented
    // frame was redrawn after the capture (a post-process injector at Present) and the technologies
    // would treat all of it as UI. A capture that did not change while the presented frame did is
    // stale: the game renders into another buffer (a swapchain proxy such as RenoDX's) and the
    // bridge's back buffer is only written at Present. Only a fresh, HUD-sized difference is trusted.
    const bool stale=changed==0, hudSized=permille<=350;
    const bool good=hudSized && !stale;
    if(good) { s.hudlessBad=0; if(s.hudlessGood<UINT32_MAX) ++s.hudlessGood; } else { s.hudlessGood=0; if(s.hudlessBad<UINT32_MAX) ++s.hudlessBad; }
    const std::string percent=std::to_string(permille/10)+"."+std::to_string(permille%10)+"% of the sample points";
    if(!s.hudlessOk && s.hudlessGood==30) {
        s.hudlessOk=true; s.status.bridgeFlags|=32u;
        log("HUD-less frame verified: the presented frame differs from the capture before the overlay UI over "+percent+
            "; frame generation receives that capture as the HUD-less image");
    } else if(s.hudlessBad==30) {
        if(s.hudlessOk) { s.hudlessOk=false; s.status.bridgeFlags&=~32u; }
        if(stale) log("HUD-less frame withheld: the captured color equals the previously presented frame while the presented frame changes;"
                      " check the color source and render-event ordering; the HUD is interpolated");
        else log("HUD-less frame withheld: the presented frame differs from the capture before the overlay UI over "+percent+
                 ". Something redraws the frame after the gameplay camera (a post-process injector at Present?), so the technologies would treat all of it as UI;"
                 " the HUD is interpolated until the frames match again");
    }
}
void reset_hudless_verification(const char* why) {
    auto& s=state();
    const bool wasVerified=s.hudlessOk;
    s.hudlessOk=false; s.hudlessGood=s.hudlessBad=0; s.status.bridgeFlags&=~32u;
    if(wasVerified)
        log(std::string("HUD-less frame unverified again: ")+why+". The captures are withheld and the HUD is "
            "interpolated until they verify against the new buffers");
}
void present_markers(Frame* frame,bool start) {
    if(!start) { latency_marker(sl::PCLMarker::ePresentEnd,frame); return; }
    latency_marker(sl::PCLMarker::eRenderSubmitEnd,frame);
    if(state().latency==Latency::AntiLag2) antilag_end_of_frame();
    latency_marker(sl::PCLMarker::ePresentStart,frame);
}
void release_stale_frames(bool generating) {
    // Frames without a corresponding presentation must never retain ring slots
    // forever. Expire only old CPU records after disabling generation.
    if(generating) return;
    auto& s=state(); const uint64_t now=GetTickCount64();
    for(auto& slot:s.frames.slots)
        if(slot.occupied && now-slot.value.born>1000) s.frames.release(slot.id);
}
Latency update_latency() {
    auto& s=state();
    LatencyCaps caps;
    caps.reflex=s.reflexReady; caps.xell=xell_context()!=nullptr;
    caps.xellStandalone=(s.status.latencyAvailable&(1u<<uint32_t(Latency::XeLL)))!=0;
    caps.antiLag2=(s.status.latencyAvailable&(1u<<uint32_t(Latency::AntiLag2)))!=0;
    const bool live=valid(s.request) && GetTickCount64()-s.heartbeat<=1000 && !s.fatal;
    const bool dlssGenerating=live && s.request.mode!=0 && s.request.backend==uint32_t(Backend::DLSS) &&
        s.request.sourceGraphicsApi==11 && (s.status.bridgeFlags&1u) && !s.request.suspensionMask && s.output==Output::Standard;
    TGFG_Request request=s.request;
    if(!live) request.latencyMode=0; // without the plugin's heartbeat only mandatory pairings remain
    const auto chosen=select_latency(request,s.output,dlssGenerating,caps);
    set_reflex(chosen==Latency::Reflex || chosen==Latency::ReflexBoost,chosen==Latency::ReflexBoost,s.request.baseFpsLimit);
    s.latency=chosen; s.status.activeLatency=static_cast<uint32_t>(chosen);
    return chosen;
}
void latency_marker(sl::PCLMarker m,Frame* frame) {
    if(!frame) return;
    marker(m,frame);
    if(frame->xell) xell_marker(frame->id,m);
}
void before_present(IDXGISwapChain* chain) {
    auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if(!s.deviceReady) return;
    auto* frame=haveRenderFrame?s.frames.find(renderFrameId):nullptr;
    if(s.output!=Output::Standard) {
        // FSR/XeSS: generation was decided while recording the frame; only the markers remain.
        present_markers(frame,true); release_stale_frames(s.status.lastAcceptedMode!=0); return;
    }
    auto asyncError=s.asyncApiError.exchange(S_OK,std::memory_order_relaxed);
    if(FAILED(asyncError)) failure(Reason::PresentFailed,"Asynchronous NVIDIA Present HRESULT="+std::to_string(asyncError),true);
    DXGI_SWAP_CHAIN_DESC description{};
    if(FAILED(chain->GetDesc(&description))) { options_off(); return; }
    s.status.swapchainFormat=static_cast<uint32_t>(description.BufferDesc.Format);
    const bool dlss=s.request.backend==uint32_t(Backend::DLSS) && s.dlssReady;
    auto ready=frame_readiness(frame,s.status.swapchainFormat,s.status.maxGeneratedFrames,s.status.dynamicSupported,dlss);
    auto why=eligibility(s.request,ready);
    if(!supported_present_format(s.status.swapchainFormat) && s.request.mode) why=Reason::UnsupportedFormat;
    if(frame && frame->inputs && !frame->inputsSubmitted && why==Reason::NoDlssInputs) why=Reason::UnsupportedQueue;
    apply_options(frame,why,description);
    ++s.diag.presents; s.diag.lastWhy=static_cast<uint32_t>(why);
    if(why==Reason::Ready) ++s.diag.readyPresents;
    if(s.status.lastAcceptedMode) ++s.diag.fgPresents;
    present_markers(frame,true);
    release_stale_frames(s.status.lastAcceptedMode!=0);
}
void after_present(HRESULT result) {
    auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    auto* frame=haveRenderFrame?s.frames.find(renderFrameId):nullptr;
    present_markers(frame,false);
    if(s.dlssReady && s.output==Output::Standard) update_caps();
    if(FAILED(result)) { failure(Reason::PresentFailed,"Present failed HRESULT="+std::to_string(result),true); options_off(); }
    else if(s.output==Output::Standard && s.status.lastAcceptedMode && s.status.dlssgStatus) {
        log("DLSS-G runtime reported status flags="+std::to_string(s.status.dlssgStatus));
        options_off(); s.fatal=true; s.status.reason=uint32_t(Reason::NativeError);
    }
    if(haveRenderFrame) s.frames.release(renderFrameId);
    haveRenderFrame=false;
    summarise(GetTickCount64());
}
} // namespace tgfg

TGFG_EXPORT uint32_t TGFG_CALL TGFG_GetAbiVersion() { return TGFG_ABI; }
TGFG_EXPORT int32_t TGFG_CALL TGFG_Submit(const TGFG_Request* request) {
    if(!request || !tgfg::valid(*request)) return -1;
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if(request->sequence<s.request.sequence) return -2;
    s.request=*request; s.heartbeat=GetTickCount64();
    s.status.requestedSequence=request->sequence; s.status.requestedMode=request->mode;
    s.status.requestedDynamicTargetFps=request->dynamicTargetFps;
    s.status.requestedBaseFpsLimit=request->baseFpsLimit;
    return 0;
}
TGFG_EXPORT int32_t TGFG_CALL TGFG_ReadStatus(TGFG_Status* out) {
    if(!out || out->size!=sizeof(*out) || out->abi!=TGFG_ABI) return -1;
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex); *out=s.status; return 0;
}
TGFG_EXPORT int32_t TGFG_CALL TGFG_BeginSimulation(uint32_t id) {
    auto& s=tgfg::state(); sl::FrameToken* token=nullptr;
    bool xell=false, antiLag=false; uint32_t fpsLimit=0;
    {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if(!s.deviceReady || !(s.status.bridgeFlags&1u)) return -1;
        const auto latency=tgfg::update_latency();
        if(s.frames.find(id)) return 0;
        auto* frame=s.frames.acquire(id); if(!frame) return -3;
        frame->id=id; frame->born=GetTickCount64();
        if(s.slReady) {
            tgfg::SLGuard inside;
            auto result=s.sl.slGetNewFrameToken(token,&id);
            if(result!=sl::Result::eOk || !token) { s.frames.release(id); return -4; }
            frame->token=token;
        }
        // XeLL must see a frame's sleep before any of its markers, so only frames that slept get them.
        frame->xell=xell=latency==tgfg::Latency::XeLL && tgfg::xell_context();
        antiLag=latency==tgfg::Latency::AntiLag2;
        fpsLimit=s.request.baseFpsLimit>0?uint32_t(s.request.baseFpsLimit+0.5f):0;
        ++s.diag.framesBegun;
    }
    // Sleeps are thread-safe and must not hold the state mutex: the presentation
    // thread must be able to make progress while a latency technology waits.
    if(token && s.reflexReady) {
        tgfg::SLGuard inside;
        auto sleepResult=s.sl.sleep(*token);
        auto markerResult=s.sl.marker(sl::PCLMarker::eSimulationStart,*token);
        if(sleepResult!=sl::Result::eOk || markerResult!=sl::Result::eOk) {
            tgfg::failure(tgfg::Reason::NativeError,"Reflex simulation marker/sleep failed",true); return -5;
        }
    }
    if(xell) { tgfg::xell_sleep(id); tgfg::xell_marker(id,sl::PCLMarker::eSimulationStart); }
    tgfg::antilag_update(antiLag,antiLag?fpsLimit:0); // no-op without Anti-Lag 2
    return 0;
}
TGFG_EXPORT int32_t TGFG_CALL TGFG_EndSimulation(uint32_t id) {
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    auto* f=s.frames.find(id); if(!f) return -1;
    if(!f->simEnded) {
        if(f->token && s.reflexReady) {
            tgfg::SLGuard inside; auto r=s.sl.marker(sl::PCLMarker::eSimulationEnd,*f->token);
            if(r!=sl::Result::eOk) return -2;
        }
        if(f->xell) tgfg::xell_marker(id,sl::PCLMarker::eSimulationEnd);
        f->simEnded=true;
    }
    return 0;
}
TGFG_EXPORT int32_t TGFG_CALL TGFG_SetCamera(const TGFG_Camera* packet) {
    if(!packet || !tgfg::valid(*packet)) return -1;
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    auto* f=s.frames.find(packet->frameId); if(!f || f->renderBegun) return -2;
    if(f->cameraReady) return -3; // no two game cameras into the same viewport
    f->camera=*packet;
    // The plugin describes the camera as HDRP rendered it, into Y-inverted targets. Depth, motion
    // vectors and the HUD-less frame all reach the runtimes in presentation orientation, so the
    // clip-space matrices are turned here, once, for every consumer of this packet.
    tgfg::to_presentation_clip(f->camera);
    f->cameraReady=true; ++s.diag.cameraPackets; return 0;
}
TGFG_EXPORT int32_t TGFG_CALL TGFG_SetHdrpInputs(const TGFG_HdrpInputs* packet) {
    if(!packet || packet->size!=sizeof(TGFG_HdrpInputs) || packet->abi!=TGFG_ABI || !packet->depth || !packet->motion ||
       !packet->renderWidth || !packet->renderHeight || !std::isfinite(packet->jitterX) || !std::isfinite(packet->jitterY)) return -1;
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    auto* f=s.frames.find(packet->frameId); if(!f || !f->cameraReady) return -2;
    if(packet->renderWidth>f->camera.width || packet->renderHeight>f->camera.height) return -3;
    f->hdrp=*packet; f->hdrpPending=true; ++s.diag.hdrpPackets; return 0;
}
TGFG_EXPORT void* TGFG_CALL TGFG_GetRenderEvent() { return reinterpret_cast<void*>(tgfg::render_event); }
TGFG_EXPORT int32_t TGFG_CALL TGFG_SetHudless(const TGFG_Hudless* packet) {
    if(!packet || packet->size!=sizeof(TGFG_Hudless) || packet->abi!=TGFG_ABI || !packet->color ||
       !packet->width || !packet->height || (packet->flags&~15u)) return -1;
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    auto* f=s.frames.find(packet->frameId); if(!f || !f->cameraReady || !s.hudlessRequested) return -2;
    if(packet->width!=f->camera.width || packet->height!=f->camera.height) return -3;
    f->hudless=*packet; f->hudlessPending=true; return 0;
}
TGFG_EXPORT int32_t TGFG_CALL TGFG_CaptureHudlessD3D11(uint32_t id,void* context,void* resource) {
    // Call only from a verified post-effects/pre-HUD stage on the immediate
    // rendering context. This export never invents a HUD-less image.
    return tgfg::capture_dx11_hudless(id,static_cast<ID3D11DeviceContext*>(context),static_cast<ID3D11Resource*>(resource));
}
TGFG_EXPORT int32_t TGFG_CALL TGFG_SetHudlessCapture(uint32_t enabled) {
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    const bool want=enabled!=0;
    if(want!=s.hudlessRequested) {
        s.hudlessRequested=want; s.hudlessOk=false; s.hudlessGood=s.hudlessBad=0; s.status.bridgeFlags&=~(16u|32u);
        tgfg::log(want?"HUD-less capture on: HDRP AfterPostProcess color, before overlay UI; verified against the presented frames before use"
                      :"HUD-less capture off: the HUD is interpolated with the scene");
    }
    return 0;
}
TGFG_EXPORT int32_t TGFG_CALL TGFG_SetDebugFlags(uint32_t flags) {
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if(flags==s.debugFlags) return 0;
    s.debugFlags=flags;
    std::string names;
    const char* bits[]={"tear lines","reset indicators","debug view","pacing lines","input checker","three-frame input capture"};
    for(unsigned i=0;i<6;++i) if(flags&(1u<<i)) names+=(names.empty()?"":", ")+std::string(bits[i]);
    tgfg::log("Frame generation debug flags "+std::to_string(flags)+(names.empty()?" (off)":": "+names));
    return 0;
}
TGFG_EXPORT DWORD WINAPI TGFG_Bootstrap(void*) {
    auto& s=tgfg::state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.self=tgfg::moduleSelf; s.status.nativeLoadPid=GetCurrentProcessId();
    if(s.bootstrapAttempted) return s.bridgeAllowed?0:1;
    s.bootstrapAttempted=true;
    try {
        s.root=std::filesystem::path(tgfg::module_path(s.self)).parent_path().wstring();
        tgfg::log("TGFG 0.5.0 DX11-to-DX12 presentation bridge bootstrap; PID="+std::to_string(GetCurrentProcessId()));
        if(!tgfg::load_runtime()) {
            if(s.status.reason==uint32_t(tgfg::Reason::ExistingStreamline)) return 2; // never a second Streamline owner
            tgfg::log("Continuing without NVIDIA Streamline: DLSS Frame Generation and Reflex are unavailable in this session");
        }
        tgfg::load_vendor_runtimes();
        if(!tgfg::install_graphics_hooks() || !tgfg::install_ngx_discovery()) return 2;
        s.bridgeAllowed=true;
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&TGFG_Bootstrap),&pinned)) return 3;
        return 0;
    } catch(const std::exception& e) { tgfg::log(std::string("Bootstrap exception: ")+e.what()); return 4; }
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) { tgfg::moduleSelf=module; DisableThreadLibraryCalls(module); }
    // Never initialize Streamline, MinHook, or shut down the GPU under loader lock.
    return TRUE;
}
