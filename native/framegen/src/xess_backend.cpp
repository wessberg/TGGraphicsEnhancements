// SPDX-License-Identifier: MIT
// Intel XeSS Frame Generation (XeSS SDK 3.x) and Intel Xe Low Latency. XeSS-FG presents through its
// own proxy swapchain and must run with XeLL and no other latency technology. On Intel GPUs it can
// generate several frames (the runtime reports the maximum); elsewhere it generates one.
#include "fg_backends.hpp"
#include <xefg_swapchain.h>
#include <xefg_swapchain_d3d12.h>
#include <xell.h>
#include <xell_d3d12.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
namespace tgfg {
namespace {
struct XessRuntime {
    HMODULE fg=nullptr, ll=nullptr;
#define F(n) decltype(&::n) n=nullptr
    F(xefgSwapChainD3D12CreateContext); F(xefgSwapChainD3D12InitFromSwapChainDesc); F(xefgSwapChainD3D12GetSwapChainPtr);
    F(xefgSwapChainD3D12TagFrameResource); F(xefgSwapChainD3D12GetInitializationParameters); F(xefgSwapChainTagFrameConstants);
    F(xefgSwapChainSetEnabled); F(xefgSwapChainSetPresentId); F(xefgSwapChainGetLastPresentStatus);
    F(xefgSwapChainSetLoggingCallback); F(xefgSwapChainDestroy); F(xefgSwapChainSetLatencyReduction);
    F(xefgSwapChainGetProperties); F(xefgSwapChainSetNumInterpolatedFrames); F(xefgSwapChainGetVersion);
    F(xefgSwapChainSetUiCompositionState);
    F(xellD3D12CreateContext); F(xellSetSleepMode); F(xellSleep); F(xellAddMarkerData); F(xellSetLoggingCallback); F(xellGetVersion);
    F(xellGetFramesReports);
#undef F
    ID3D12Device* device=nullptr;
    xell_context_handle_t xell=nullptr;
    bool xellFailed=false, haveMode=false;
    xell_sleep_params_t mode{};
    // The last mode xellSetSleepMode refused: not asked for again (each attempt costs a GPU drain)
    // until a different one is wanted.
    bool haveRefused=false;
    xell_sleep_params_t refused{};
} xs;
void report(const char* what,int code) {
    state().status.backendResult=code;
    log_throttled(std::string("Intel XeSS: ")+what+" returned "+std::to_string(code));
}
void xefg_log(const char* message,xefg_swapchain_logging_level_t level,void*) {
    if(message) log_throttled(std::string("Intel XeSS-FG[")+std::to_string(int(level))+"] "+message);
}
void xell_log(const char* message,xell_logging_level_t level) {
    if(message) log_throttled(std::string("Intel XeLL[")+std::to_string(int(level))+"] "+message);
}
// Depth and motion-vector encoding as described by the observed DLSS SR feature.
uint32_t init_flags(uint32_t ngx) {
    uint32_t flags=XEFG_SWAPCHAIN_INIT_FLAG_NONE;
    if(ngx&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) flags|=XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;
    if(ngx&NVSDK_NGX_DLSS_Feature_Flags_MVJittered) flags|=XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV;
    return flags;
}
// XeLL is created on first use: XeSS-FG requires it, and on Intel GPUs it also works on its own.
bool ensure_xell() {
    if(xs.xell) return true;
    if(xs.xellFailed || !xs.xellD3D12CreateContext || !xs.device) return false;
    const auto result=xs.xellD3D12CreateContext(xs.device,&xs.xell);
    if(result!=XELL_RESULT_SUCCESS || !xs.xell) {
        xs.xell=nullptr; xs.xellFailed=true; report("xellD3D12CreateContext",result); return false;
    }
    xs.xellSetLoggingCallback(xs.xell,XELL_LOGGING_LEVEL_WARNING,&xell_log);
    xell_version_t version{}; xs.xellGetVersion(&version);
    log("Intel XeLL "+std::to_string(version.major)+"."+std::to_string(version.minor)+"."+std::to_string(version.patch)+" context created");
    return true;
}
bool ok(xefg_swapchain_result_t result) { return int(result)>=0; } // positive values are warnings
class XessOutput final : public GeneratorOutput {
    xefg_swapchain_handle_t handle_=nullptr;
    ComPtr<IDXGISwapChain4> chain_;
    uint32_t max_=0, generated_=0, flags_=0;
    bool enabled_=false, composing_=false; // composing: UI composition on, fed by the HUD-less image
    static const char* label(xefg_swapchain_resource_type_t type) {
        return type==XEFG_SWAPCHAIN_RES_DEPTH?"TagFrameResource(depth)":type==XEFG_SWAPCHAIN_RES_MOTION_VECTOR?"TagFrameResource(motion vectors)":"TagFrameResource(HUD-less color)";
    }
    // UI composition follows the HUD-less image: with one tagged, XeSS interpolates it and extracts
    // the UI from the back buffer (AUTO picks BACKBUFFER_HUDLESS); without one, the UI is interpolated.
    void compose(bool want) {
        if(want==composing_) return;
        const auto result=xs.xefgSwapChainSetUiCompositionState(handle_,want?XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED:XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_DISABLED);
        if(ok(result)) { composing_=want; log(std::string("Intel XeSS-FG UI composition ")+(want?"on: interpolating the HUD-less frame, UI from the back buffer":"off: the UI is interpolated")); }
        else report("SetUiCompositionState",result);
    }
    bool tag(ID3D12GraphicsCommandList* list,uint32_t id,xefg_swapchain_resource_type_t type,ID3D12Resource* resource,uint32_t width,uint32_t height) {
        xefg_swapchain_d3d12_resource_data_t data{};
        data.type=type;
        // Copied into XeSS storage on the bridge command list, so the shared input can be
        // returned to D3D11 as soon as that list has run, as with the other backends.
        data.validity=XEFG_SWAPCHAIN_RV_ONLY_NOW;
        data.resourceBase={0,0}; data.resourceSize={width,height};
        data.pResource=resource; data.incomingState=D3D12_RESOURCE_STATE_COMMON;
        const auto result=xs.xefgSwapChainD3D12TagFrameResource(handle_,list,id,&data);
        if(!ok(result)) { report(label(type),result); return false; }
        return true;
    }
public:
    bool init(const OutputParams& p,std::string& error) {
        if(!ensure_xell()) { error="Intel XeLL, which XeSS Frame Generation requires, is unavailable"; return false; }
        auto result=xs.xefgSwapChainD3D12CreateContext(p.device,&handle_);
        if(!ok(result) || !handle_) { handle_=nullptr; error="xefgSwapChainD3D12CreateContext returned "+std::to_string(result); return false; }
        xs.xefgSwapChainSetLoggingCallback(handle_,XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING,&xefg_log,nullptr);
        result=xs.xefgSwapChainSetLatencyReduction(handle_,xs.xell);
        if(!ok(result)) { error="xefgSwapChainSetLatencyReduction returned "+std::to_string(result); return false; }
        xefg_swapchain_d3d12_init_params_t params{};
        params.maxInterpolatedFrames=XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES;
        params.initFlags=flags_=init_flags(p.inputFlags);
        params.uiMode=XEFG_SWAPCHAIN_UI_MODE_AUTO; // composition is switched on per frame when a HUD-less image is tagged (compose)
        DXGI_SWAP_CHAIN_DESC1 desc=p.desc;
        result=xs.xefgSwapChainD3D12InitFromSwapChainDesc(handle_,p.hwnd,&desc,p.fullscreen,p.queue,p.factory,&params);
        if(!ok(result)) { error="xefgSwapChainD3D12InitFromSwapChainDesc returned "+std::to_string(result); return false; }
        if(ok(xs.xefgSwapChainD3D12GetInitializationParameters(handle_,&params))) max_=params.maxInterpolatedFrames;
        if(!max_) max_=1;
        generated_=max_; // XeSS starts at the maximum configured during initialization
        result=xs.xefgSwapChainD3D12GetSwapChainPtr(handle_,IID_PPV_ARGS(&chain_));
        if(!ok(result) || !chain_) { error="xefgSwapChainD3D12GetSwapChainPtr returned "+std::to_string(result); return false; }
        log("Intel XeSS-FG swapchain ready: up to "+std::to_string(max_)+" generated frame(s) per rendered frame, init flags="+std::to_string(flags_));
        return true;
    }
    ~XessOutput() override {
        if(!handle_) return;
        if(enabled_) xs.xefgSwapChainSetEnabled(handle_,0);
        chain_.Reset(); // XeSS refuses to destroy while its proxy is still referenced
        const auto result=xs.xefgSwapChainDestroy(handle_);
        if(!ok(result)) log("xefgSwapChainDestroy returned "+std::to_string(result));
    }
    IDXGISwapChain4* swapchain() override { return chain_.Get(); }
    uint32_t max_generated() const override { return max_; }
    bool record(ID3D12GraphicsCommandList* list,uint32_t id,const GenerationInputs* in,uint32_t generated) override {
        bool want=in!=nullptr, refused=false;
        if(want && (in->depthWidth!=in->motionWidth || in->depthHeight!=in->motionHeight)) {
            log_throttled("Intel XeSS-FG needs depth and motion vectors of the same size; this frame is presented as rendered");
            want=false; refused=true;
        }
        if(want) {
            generated=std::clamp(generated,1u,max_);
            // May rebuild XeSS pipelines, so only on an actual change of the user's multiplier.
            if(generated!=generated_) {
                const auto result=xs.xefgSwapChainSetNumInterpolatedFrames(handle_,generated);
                if(ok(result)) generated_=generated; else report("SetNumInterpolatedFrames",result);
            }
            xefg_swapchain_frame_constant_data_t constants{};
            std::memcpy(constants.viewMatrix,in->camera->worldToView,sizeof constants.viewMatrix);
            std::memcpy(constants.projectionMatrix,in->camera->viewToClip,sizeof constants.projectionMatrix);
            constants.jitterOffsetX=in->jitterX; constants.jitterOffsetY=in->jitterY;
            constants.motionVectorScaleX=in->motionScaleX; constants.motionVectorScaleY=in->motionScaleY; // to pixels, as NGX
            constants.resetHistory=in->reset?1u:0u; constants.frameRenderTime=in->frameTimeMs;
            const auto result=xs.xefgSwapChainTagFrameConstants(handle_,id,&constants);
            if(!ok(result)) { report("TagFrameConstants",result); want=false; refused=true; }
            else if(!tag(list,id,XEFG_SWAPCHAIN_RES_DEPTH,in->depth,in->depthWidth,in->depthHeight) ||
                    !tag(list,id,XEFG_SWAPCHAIN_RES_MOTION_VECTOR,in->motion,in->motionWidth,in->motionHeight)) { want=false; refused=true; }
            else {
                // Same format and size as the back buffer (the bridge made it so). A refused tag only
                // costs this frame's UI composition, never the frame itself.
                const bool hudless=in->hudless && tag(list,id,XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,in->hudless,in->hudlessWidth,in->hudlessHeight);
                compose(hudless);
            }
        }
        if(want!=enabled_) {
            const auto result=xs.xefgSwapChainSetEnabled(handle_,want?1u:0u);
            if(ok(result)) enabled_=want; else { report("SetEnabled",result); if(want) return false; }
        }
        return !refused;
    }
    void before_present(uint32_t id) override {
        const auto result=xs.xefgSwapChainSetPresentId(handle_,id);
        if(!ok(result)) report("SetPresentId",result);
    }
    void after_present() override {
        xefg_swapchain_present_status_t status{};
        if(!ok(xs.xefgSwapChainGetLastPresentStatus(handle_,&status))) return;
        auto& s=state();
        s.diag.sdkPresented+=status.framesPresented; s.status.outputFramesReported+=status.framesPresented;
        // Warnings follow history resets and resolution changes; only errors are reported.
        if(status.isFrameGenEnabled && int(status.frameGenResult)<0) report("frame generation",status.frameGenResult);
    }
    void quiesce() override {
        if(enabled_ && ok(xs.xefgSwapChainSetEnabled(handle_,0))) enabled_=false;
    }
    void resized(UINT,UINT,DXGI_FORMAT) override {} // XeSS follows ResizeBuffers on its proxy
    bool stale(uint32_t inputFlags) const override { return init_flags(inputFlags)!=flags_; }
};
} // namespace
void load_xess_runtime() {
    const auto folder=std::filesystem::path(state().root)/L"XeSS";
    const auto ll=folder/L"libxell.dll", fg=folder/L"libxess_fg.dll";
    if(!std::filesystem::exists(ll) || !std::filesystem::exists(fg)) {
        fact("xess","not-installed"); log("Intel XeSS frame generation runtime is not installed (Native/XeSS); XeSS and XeLL unavailable"); return;
    }
    // Intel's requirements for XeSS-FG and XeLL: Windows 10 21H1 (build 19043) or Windows 11, and the
    // Visual C++ runtime 14.40.33810 or later. Neither is loaded where they aren't met.
    const uint32_t build=windows_build();
    if(build && build<kXessMinimumWindowsBuild) {
        fact("xess","windows-too-old "+std::to_string(build));
        log("Intel XeSS frame generation and XeLL need Windows 10 21H1 (build 19043) or newer; this is build "+std::to_string(build)+". XeSS and XeLL unavailable");
        return;
    }
    // The copy XeSS would bind to: one already loaded, or an application-local one, counts as much as
    // System32's (Intel allows the runtime to be bundled).
    std::wstring where;
    const uint64_t runtime=bound_file_version(L"msvcp140.dll",folder.wstring(),where);
    constexpr uint64_t kNeeded=(uint64_t(14)<<48)|(uint64_t(40)<<32)|(uint64_t(33810)<<16);
    const std::string whereUtf8=std::filesystem::path(where).u8string();
    if(runtime<kNeeded) {
        fact("xess",runtime?"vcruntime-too-old "+version_text(runtime):std::string("vcruntime-missing"));
        log("Intel XeSS frame generation needs the Microsoft Visual C++ runtime 14.40.33810 or newer; it would use "+
            (runtime?version_text(runtime)+" ("+whereUtf8+")":std::string("none"))+". XeSS and XeLL unavailable");
        return;
    }
    log("Intel XeSS will use the Visual C++ runtime "+version_text(runtime)+" ("+whereUtf8+")");
    // libxess_fg.dll loads libxell.dll by module name; loading XeLL first by full path resolves that here.
    xs.ll=load_signed(ll.wstring(),L"INTEL");
    if(xs.ll) xs.fg=load_signed(fg.wstring(),L"INTEL");
    if(!xs.fg) { fact("xess","unsigned-or-unloadable"); return; }
    bool complete=true;
#define F(m,n) xs.n=reinterpret_cast<decltype(xs.n)>(GetProcAddress(xs.m,#n)); complete=complete && xs.n
    F(fg,xefgSwapChainD3D12CreateContext); F(fg,xefgSwapChainD3D12InitFromSwapChainDesc); F(fg,xefgSwapChainD3D12GetSwapChainPtr);
    F(fg,xefgSwapChainD3D12TagFrameResource); F(fg,xefgSwapChainD3D12GetInitializationParameters); F(fg,xefgSwapChainTagFrameConstants);
    F(fg,xefgSwapChainSetEnabled); F(fg,xefgSwapChainSetPresentId); F(fg,xefgSwapChainGetLastPresentStatus);
    F(fg,xefgSwapChainSetLoggingCallback); F(fg,xefgSwapChainDestroy); F(fg,xefgSwapChainSetLatencyReduction);
    F(fg,xefgSwapChainGetProperties); F(fg,xefgSwapChainSetNumInterpolatedFrames); F(fg,xefgSwapChainGetVersion);
    F(fg,xefgSwapChainSetUiCompositionState);
    F(ll,xellD3D12CreateContext); F(ll,xellSetSleepMode); F(ll,xellSleep); F(ll,xellAddMarkerData); F(ll,xellSetLoggingCallback); F(ll,xellGetVersion);
    F(ll,xellGetFramesReports);
#undef F
    if(!complete) { fact("xess","missing-exports"); log("Intel XeSS runtime is missing expected exports; XeSS and XeLL unavailable"); xs.xefgSwapChainD3D12CreateContext=nullptr; xs.xellD3D12CreateContext=nullptr; return; }
    xefg_swapchain_version_t version{}; xs.xefgSwapChainGetVersion(&version);
    log("Intel XeSS-FG "+std::to_string(version.major)+"."+std::to_string(version.minor)+"."+std::to_string(version.patch)+
        " runtime loaded from Native/XeSS (Intel-signed)");
}
void probe_xess(ID3D12Device* device,uint32_t vendor) {
    if(!xs.xefgSwapChainD3D12CreateContext) return;
    xs.device=device;
    auto& s=state();
    // XeLL reduces latency on its own only on Intel GPUs; elsewhere only together with XeSS-FG.
    if(vendor==0x8086 && ensure_xell()) s.status.latencyAvailable|=1u<<uint32_t(Latency::XeLL);
    xefg_swapchain_handle_t probe=nullptr;
    const auto result=xs.xefgSwapChainD3D12CreateContext(device,&probe);
    if(!ok(result) || !probe) { fact("xess","unsupported "+std::to_string(result)); log("Intel XeSS frame generation is not supported on this device: "+std::to_string(result)); return; }
    // Before initialization the properties report the maximum number of interpolated frames.
    xefg_swapchain_properties_t properties{};
    const bool haveProperties=ok(xs.xefgSwapChainGetProperties(probe,&properties));
    xs.xefgSwapChainDestroy(probe);
    if(!haveProperties || !properties.maxSupportedInterpolations) { fact("xess","no-interpolation"); log("Intel XeSS frame generation reported no interpolation support"); return; }
    s.status.backendsAvailable|=1u<<uint32_t(Backend::XeSS);
    s.status.xessMaxGenerated=properties.maxSupportedInterpolations;
    fact("xess","available "+std::to_string(properties.maxSupportedInterpolations));
    log("Intel XeSS frame generation available: up to "+std::to_string(properties.maxSupportedInterpolations)+
        " generated frame(s) per rendered frame on this GPU.");
}
std::unique_ptr<GeneratorOutput> create_xess_output(const OutputParams& params,std::string& error) {
    if(!xs.xefgSwapChainD3D12CreateContext) { error="Intel XeSS runtime not loaded"; return nullptr; }
    auto output=std::make_unique<XessOutput>();
    if(!output->init(params,error)) return nullptr;
    return output;
}
void* xell_context() { return xs.xell; }
// A failed sleep keeps XeSS-FG from generating that frame (record_generation); its code is kept for the
// report and logged at most every 5 s, so a sleep that keeps failing has a cause on record.
bool xell_sleep(uint32_t frameId) {
    if(!xs.xell) return false;
    const auto result=xs.xellSleep(xs.xell,frameId);
    if(result==XELL_RESULT_SUCCESS) return true;
    fact("xell.sleep","failed "+std::to_string(int(result)));
    log_throttled("Intel XeLL: xellSleep returned "+std::to_string(int(result))+"; XeSS frame generation skips frames without it");
    return false;
}
void xell_marker(uint32_t frameId,sl::PCLMarker marker) {
    // The six XeLL markers use the same numbering as Streamline's PCL markers.
    if(!xs.xell || uint32_t(marker)>uint32_t(sl::PCLMarker::ePresentEnd)) return;
    const auto result=xs.xellAddMarkerData(xs.xell,frameId,static_cast<xell_latency_marker_type_t>(uint32_t(marker)));
    if(result!=XELL_RESULT_SUCCESS) log_throttled("Intel XeLL: xellAddMarkerData("+std::to_string(uint32_t(marker))+") returned "+std::to_string(int(result)));
}
// The last 64 XeLL frames, averaged, so the XeSS path can be compared with the Reflex one instead of
// being described only from how it feels. Note what these two measure is not identical: Reflex's
// pcLatency ends at the GPU finishing the frame, while XeLL only timestamps the CPU side, so the
// comparison that means something is how far each moves when frame generation is turned on.
bool xell_latency(uint64_t& simToPresentUs,uint64_t& submitToPresentUs,uint32_t& frames) {
    simToPresentUs=submitToPresentUs=0; frames=0;
    if(!xs.xell || !xs.xellGetFramesReports) return false;
    xell_frame_report_t reports[64]{};
    if(xs.xellGetFramesReports(xs.xell,reports)!=XELL_RESULT_SUCCESS) return false;
    uint64_t sim=0,submit=0; uint32_t submitted=0;
    for(const auto& r:reports) {
        if(!r.m_frame_id || !r.m_sim_start_ts || r.m_present_end_ts<=r.m_sim_start_ts) continue;
        sim+=r.m_present_end_ts-r.m_sim_start_ts; ++frames;
        if(r.m_render_submit_start_ts && r.m_present_end_ts>r.m_render_submit_start_ts) {
            submit+=r.m_present_end_ts-r.m_render_submit_start_ts; ++submitted;
        }
    }
    if(!frames) return false;
    simToPresentUs=sim/frames/1000;                              // the reports are nanoseconds
    submitToPresentUs=submitted?submit/submitted/1000:0;
    return true;
}
bool xell_mode_pending(bool lowLatency,uint32_t minimumIntervalUs) {
    if(lowLatency && !ensure_xell()) return false;
    if(!xs.xell) return false;
    if(xs.haveRefused && bool(xs.refused.bLowLatencyMode)==lowLatency && xs.refused.minimumIntervalUs==minimumIntervalUs) return false;
    return !xs.haveMode || bool(xs.mode.bLowLatencyMode)!=lowLatency || xs.mode.minimumIntervalUs!=minimumIntervalUs;
}
bool xell_low_latency_active() { return xs.xell && xs.haveMode && xs.mode.bLowLatencyMode; }
bool xell_apply(bool lowLatency,uint32_t minimumIntervalUs) {
    if(!xs.xell) return false;
    xell_sleep_params_t mode{}; mode.bLowLatencyMode=lowLatency?1u:0u; mode.minimumIntervalUs=minimumIntervalUs;
    const auto result=xs.xellSetSleepMode(xs.xell,&mode);
    if(result!=XELL_RESULT_SUCCESS) {
        report("xellSetSleepMode",result); fact("xell.mode","refused "+std::to_string(result));
        xs.refused=mode; xs.haveRefused=true; return false;
    }
    xs.haveRefused=false;
    xs.mode=mode; xs.haveMode=true;
    log(std::string("Intel XeLL low latency ")+(lowLatency?"on":"off")+(minimumIntervalUs?", frame limit "+std::to_string(minimumIntervalUs)+" us":""));
    return true;
}
} // namespace tgfg
