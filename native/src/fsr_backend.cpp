// SPDX-License-Identifier: MIT
// AMD FSR Frame Generation through the FSR API (FidelityFX SDK 2.x): AMD's signed loader and
// frame generation DLLs, the frame generation swapchain for presentation and pacing, and the
// frame generation context fed from the same inputs the Streamline path uses, including the
// bridge's HUD-less frame when it has a verified one (FSR then keeps the UI it finds in the back
// buffer out of the generated frames instead of interpolating it).
#include "fg_backends.hpp"
#include <ffx_api.h>
#include <ffx_api_types.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>
#include <filesystem>
#include <vector>
#include <cstring>
#include <cstdio>
namespace tgfg {
namespace {
struct FsrRuntime {
    HMODULE loader=nullptr, generation=nullptr;
    PfnFfxCreateContext create=nullptr; PfnFfxDestroyContext destroy=nullptr;
    PfnFfxConfigure configure=nullptr; PfnFfxQuery query=nullptr; PfnFfxDispatch dispatch=nullptr;
} fsr;
void report(const char* what,ffxReturnCode_t code) {
    state().status.backendResult=int32_t(code);
    log_throttled(std::string("AMD FSR: ")+what+" returned "+std::to_string(code));
}
void message(uint32_t type,const wchar_t* text) {
    // Called from FSR's worker threads as well; log() serialises.
    if(!text) return;
    const int bytes=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    std::string utf8(bytes>0?size_t(bytes-1):0,'\0');
    if(bytes>1) WideCharToMultiByte(CP_UTF8,0,text,-1,&utf8[0],bytes,nullptr,nullptr);
    log_throttled(std::string("AMD FSR ")+(type==FFX_API_MESSAGE_TYPE_ERROR?"error: ":"warning: ")+utf8);
}
std::string versions(ID3D12Device* device,uint64_t type,uint64_t& count) {
    count=0;
    ffxQueryDescGetVersions query{}; query.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType=type; query.device=device; query.outputCount=&count;
    if(fsr.query(nullptr,&query.header)!=FFX_API_RETURN_OK || !count) { count=0; return {}; }
    std::vector<uint64_t> ids(static_cast<size_t>(count)); std::vector<const char*> names(static_cast<size_t>(count),nullptr);
    query.versionIds=ids.data(); query.versionNames=names.data();
    if(fsr.query(nullptr,&query.header)!=FFX_API_RETURN_OK) return "?";
    std::string list;
    for(uint64_t i=0;i<count && i<names.size();++i) { if(!list.empty()) list+=", "; list+=names[size_t(i)]?names[size_t(i)]:"?"; }
    return list;
}
// Depth, motion vectors and color as described by the observed DLSS SR feature. Bit 16 of the
// plugin's debug flags turns on FSR's own input checker, which reports through the message callback.
uint32_t generation_flags(uint32_t ngx,bool hdr,uint32_t debug) {
    uint32_t flags=0;
    if(ngx&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) flags|=FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
    if(ngx&NVSDK_NGX_DLSS_Feature_Flags_MVJittered) flags|=FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    if(!(ngx&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)) flags|=FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
    if(hdr) flags|=FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
    if(debug&16u) flags|=FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
    return flags;
}
// FSR's debug overlays on generated frames: tear lines, reset indicators, the debug view (motion
// vectors, depth, masks; only generated frames are presented) and pacing lines.
uint32_t dispatch_flags(uint32_t debug) {
    uint32_t flags=0;
    if(debug&1u) flags|=FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;
    if(debug&2u) flags|=FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS;
    if(debug&4u) flags|=FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;
    if(debug&8u) flags|=FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;
    return flags;
}
std::string f2(float v) { char b[32]{}; std::snprintf(b,sizeof b,"%.3f",v); return b; }
class FsrOutput final : public GeneratorOutput {
    ffxContext swapchainContext_=nullptr, generation_=nullptr;
    ComPtr<IDXGISwapChain4> chain_;
    ID3D12Device* device_=nullptr;
    UINT width_=0, height_=0; DXGI_FORMAT format_=DXGI_FORMAT_UNKNOWN;
    // The resolution the game renders at, which FSR allocates its render-resolution buffers for and
    // uses to scale its lookups into them. It is only known once a frame's inputs arrive, so the
    // context starts at the display size and is rebuilt on the first frame and on every change.
    UINT maxRenderWidth_=0, maxRenderHeight_=0;
    bool hdr_=false;
    bool historyHudless_=false, haveHistory_=false;
    uint32_t flags_=0;
    uint64_t frameId_=0; // FSR requires +1 per presented frame; any other step resets generation
    uint32_t loggedWidth_=0, loggedHeight_=0; bool loggedHudless_=false; // the inputs are logged when they change
    // Called by FSR's swapchain inside Present, on the presenting thread, to record generation.
    static ffxReturnCode_t dispatch(ffxDispatchDescFrameGeneration* params,void* context) {
        return fsr.dispatch(static_cast<ffxContext*>(context),&params->header);
    }
    ffxReturnCode_t configure(bool enabled,ID3D12Resource* hudless=nullptr) {
        ffxConfigureDescFrameGeneration config{}; config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        config.swapChain=chain_.Get();
        config.frameGenerationCallback=&FsrOutput::dispatch; config.frameGenerationCallbackUserContext=&generation_;
        config.frameGenerationEnabled=enabled; config.allowAsyncWorkloads=false;
        // The HUD-less image has the back buffer's format and size (the context was created without a
        // separate HUD-less format). Interpolation runs on the game queue, so the bridge may rewrite the
        // texture once this present's work has retired, which its fence hand-off guarantees. No UI
        // callback: the game draws its UI into the back buffer, from which FSR extracts it.
        if(hudless) config.HUDLessColor=ffxApiGetResourceDX12(hudless,FFX_API_RESOURCE_STATE_COMMON);
        config.flags=dispatch_flags(state().debugFlags);
        config.generationRect={0,0,int32_t(width_),int32_t(height_)}; config.frameID=frameId_;
        return fsr.configure(&generation_,&config.header);
    }
    bool create_generation(std::string& error) {
        ffxCreateContextDescFrameGeneration create{}; create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        create.flags=flags_; create.displaySize={width_,height_}; create.maxRenderSize={maxRenderWidth_,maxRenderHeight_};
        create.backBufferFormat=ffxApiGetSurfaceFormatDX12(format_);
        ffxCreateContextDescFrameGenerationVersion version{}; version.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
        version.version=FFX_FRAMEGENERATION_VERSION;
        ffxCreateBackendDX12Desc backend{}; backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backend.device=device_;
        create.header.pNext=&version.header; version.header.pNext=&backend.header;
        const auto result=fsr.create(&generation_,&create.header,nullptr);
        if(result!=FFX_API_RETURN_OK || !generation_) {
            generation_=nullptr; error="ffxCreateContext(frame generation) returned "+std::to_string(result); return false;
        }
        ffxConfigureDescGlobalDebug1 debug{}; debug.header.type=FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
        debug.fpMessage=&message; debug.debugLevel=FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
        fsr.configure(&generation_,&debug.header);
        ffxQueryGetProviderVersion provider{}; provider.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if(fsr.query(&generation_,&provider.header)==FFX_API_RETURN_OK && provider.versionName)
            log(std::string("AMD FSR frame generation context: ")+provider.versionName+" at "+std::to_string(width_)+"x"+std::to_string(height_)+
                ", render resolution up to "+std::to_string(maxRenderWidth_)+"x"+std::to_string(maxRenderHeight_)+
                ", create flags="+std::to_string(flags_));
        return true;
    }
    void destroy_generation() {
        if(!generation_) return;
        // Disabling waits for FSR's interpolation and composition work before the context goes.
        const auto result=configure(false);
        if(result!=FFX_API_RETURN_OK) report("ffxConfigure(disable before destroy)",result);
        fsr.destroy(&generation_,nullptr); generation_=nullptr;
    }
public:
    bool init(const OutputParams& p,std::string& error) {
        device_=p.device; width_=p.desc.Width; height_=p.desc.Height; format_=p.desc.Format; hdr_=p.hdr;
        maxRenderWidth_=width_; maxRenderHeight_=height_;
        flags_=generation_flags(p.inputFlags,p.hdr,state().debugFlags);
        DXGI_SWAP_CHAIN_DESC1 desc=p.desc;
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{}; if(p.fullscreen) fullscreen=*p.fullscreen;
        IDXGISwapChain4* created=nullptr;
        ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 create{};
        create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
        create.swapchain=&created; create.hwnd=p.hwnd; create.desc=&desc; create.fullscreenDesc=p.fullscreen?&fullscreen:nullptr;
        create.dxgiFactory=p.factory; create.gameQueue=p.queue;
        ffxCreateContextDescFrameGenerationSwapChainVersionDX12 version{};
        version.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
        version.version=FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
        create.header.pNext=&version.header;
        const auto result=fsr.create(&swapchainContext_,&create.header,nullptr);
        if(result!=FFX_API_RETURN_OK || !created) {
            swapchainContext_=nullptr; if(created) created->Release();
            error="ffxCreateContext(frame generation swapchain) returned "+std::to_string(result); return false;
        }
        chain_.Attach(created); // FSR hands the caller one reference to its proxy swapchain
        return create_generation(error);
    }
    ~FsrOutput() override {
        destroy_generation();
        if(swapchainContext_) fsr.destroy(&swapchainContext_,nullptr);
        chain_.Reset(); // the last reference destroys FSR's proxy and the swapchain it created
    }
    IDXGISwapChain4* swapchain() override { return chain_.Get(); }
    // The FSR API exposes no multi-frame control: its swapchain generates one frame per rendered frame.
    uint32_t max_generated() const override { return generation_?1u:0u; }
    bool record(ID3D12GraphicsCommandList* list,uint32_t,const GenerationInputs* in,uint32_t) override {
        ++frameId_;
        // The FSR API asks for the render resolution at context creation; it is only known from the
        // frame's inputs. Rebuilding the context for it keeps FSR's render-resolution buffers and its
        // lookups into them at the resolution the game actually renders.
        const uint32_t wantedFlags=generation_flags(state().ngxFlags,hdr_,state().debugFlags);
        if(in && generation_ && (in->depthWidth!=maxRenderWidth_ || in->depthHeight!=maxRenderHeight_ || flags_!=wantedFlags)) {
            destroy_generation();
            maxRenderWidth_=in->depthWidth; maxRenderHeight_=in->depthHeight;
            flags_=wantedFlags; haveHistory_=false;
            std::string error;
            if(!create_generation(error)) { log("AMD FSR: "+error+"; frame generation stays off"); return false; }
        }
        if(!generation_) return in==nullptr;
        const bool enabled=in!=nullptr;
        auto result=configure(enabled,enabled?in->hudless:nullptr);
        if(result!=FFX_API_RETURN_OK) { report("ffxConfigure(frame generation)",result); return false; }
        if(!enabled) { haveHistory_=false; return true; }
        ffxDispatchDescFrameGenerationPrepareV2 prepare{}; prepare.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
        prepare.frameID=frameId_; prepare.commandList=list; prepare.flags=dispatch_flags(state().debugFlags);
        prepare.renderSize={in->depthWidth,in->depthHeight};
        prepare.jitterOffset={in->jitterX,in->jitterY};
        prepare.motionVectorScale={in->motionScaleX,in->motionScaleY}; // pixels, with HDRP's final Y flip accounted for
        prepare.frameTimeDelta=in->frameTimeMs;
        // A verified HUD-less image can appear/disappear mid-session. Its history must never be
        // blended with the old backbuffer (with UI), nor reused after a disabled frame.
        prepare.reset=in->reset || !haveHistory_ || historyHudless_!=(in->hudless!=nullptr);
        const auto& camera=*in->camera;
        // FSR sorts near/far by the inverted-depth flag itself, but its integrations (and its input
        // checker) pass them swapped for inverted depth, so they are passed that way here too.
        const bool inverted=(flags_&FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED)!=0;
        prepare.cameraNear=inverted?camera.farPlane:camera.nearPlane; prepare.cameraFar=inverted?camera.nearPlane:camera.farPlane;
        prepare.cameraFovAngleVertical=camera.fovRadians; prepare.viewSpaceToMetersFactor=1.f; // Unity units are metres
        if(loggedWidth_!=in->depthWidth || loggedHeight_!=in->depthHeight || loggedHudless_!=(in->hudless!=nullptr)) {
            loggedWidth_=in->depthWidth; loggedHeight_=in->depthHeight; loggedHudless_=in->hudless!=nullptr;
            log("AMD FSR inputs: render "+std::to_string(in->depthWidth)+"x"+std::to_string(in->depthHeight)+", motion vectors "+
                std::to_string(in->motionWidth)+"x"+std::to_string(in->motionHeight)+", display "+std::to_string(width_)+"x"+std::to_string(height_)+
                ", motion scale ("+f2(in->motionScaleX)+", "+f2(in->motionScaleY)+"), jitter ("+f2(in->jitterX)+", "+f2(in->jitterY)+
                "), near/far "+f2(prepare.cameraNear)+"/"+f2(prepare.cameraFar)+(inverted?" (inverted depth)":"")+", vertical FOV "+
                f2(camera.fovRadians*57.29578f)+" deg, frame time "+f2(in->frameTimeMs)+" ms, reset "+(prepare.reset?"1":"0")+
                ", HUD-less "+(in->hudless?"yes":"no")+", inputs Y-flipped to presentation, create flags "+std::to_string(flags_)+", debug flags "+std::to_string(state().debugFlags));
        }
        prepare.depth=ffxApiGetResourceDX12(in->depth,FFX_API_RESOURCE_STATE_COMMON);
        prepare.motionVectors=ffxApiGetResourceDX12(in->motion,FFX_API_RESOURCE_STATE_COMMON);
        std::memcpy(prepare.cameraPosition,camera.position,sizeof prepare.cameraPosition);
        std::memcpy(prepare.cameraUp,camera.up,sizeof prepare.cameraUp);
        std::memcpy(prepare.cameraRight,camera.right,sizeof prepare.cameraRight);
        std::memcpy(prepare.cameraForward,camera.forward,sizeof prepare.cameraForward);
        result=fsr.dispatch(&generation_,&prepare.header);
        if(result!=FFX_API_RETURN_OK) {
            report("ffxDispatch(frame generation prepare)",result);
            configure(false); haveHistory_=false; return false;
        }
        historyHudless_=in->hudless!=nullptr; haveHistory_=true;
        return true;
    }
    void before_present(uint32_t) override { antilag_tag_swapchain(chain_.Get(),state().latency==Latency::AntiLag2); }
    void after_present() override {}
    void quiesce() override {
        if(generation_) { const auto result=configure(false); if(result!=FFX_API_RETURN_OK) report("ffxConfigure(disable)",result); }
        if(swapchainContext_) {
            ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait{};
            wait.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
            fsr.dispatch(&swapchainContext_,&wait.header);
        }
    }
    void resized(UINT width,UINT height,DXGI_FORMAT format) override {
        if(width==width_ && height==height_ && format==format_ && generation_) return;
        // The frame generation context is created for one display size and format.
        destroy_generation(); width_=width; height_=height; format_=format;
        maxRenderWidth_=width_; maxRenderHeight_=height_;
        std::string error; if(!create_generation(error)) log("AMD FSR: "+error+"; frame generation stays off");
    }
    bool stale(uint32_t inputFlags) const override { return generation_flags(inputFlags,hdr_,state().debugFlags)!=flags_; }
};
} // namespace
void load_fsr_runtime() {
    const auto folder=std::filesystem::path(state().root)/L"FidelityFX";
    const auto generation=folder/L"amd_fidelityfx_framegeneration_dx12.dll", loader=folder/L"amd_fidelityfx_loader_dx12.dll";
    if(!std::filesystem::exists(generation) || !std::filesystem::exists(loader)) {
        log("AMD FSR frame generation runtime is not installed (Native/FidelityFX); FSR unavailable"); return;
    }
    // AMD's loader finds its effect DLLs by module name, so the effect DLL is loaded first by full path.
    fsr.generation=load_signed(generation.wstring(),L"ADVANCED MICRO DEVICES");
    if(fsr.generation) fsr.loader=load_signed(loader.wstring(),L"ADVANCED MICRO DEVICES");
    if(!fsr.loader) return;
    fsr.create=reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(fsr.loader,"ffxCreateContext"));
    fsr.destroy=reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(fsr.loader,"ffxDestroyContext"));
    fsr.configure=reinterpret_cast<PfnFfxConfigure>(GetProcAddress(fsr.loader,"ffxConfigure"));
    fsr.query=reinterpret_cast<PfnFfxQuery>(GetProcAddress(fsr.loader,"ffxQuery"));
    fsr.dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(fsr.loader,"ffxDispatch"));
    if(!fsr.create || !fsr.destroy || !fsr.configure || !fsr.query || !fsr.dispatch) {
        log("AMD FSR loader is missing FSR API exports; FSR unavailable"); fsr.create=nullptr; return;
    }
    log("AMD FSR runtime loaded from Native/FidelityFX (AMD-signed)");
}
void probe_fsr(ID3D12Device* device) {
    if(!fsr.create) return;
    uint64_t generators=0, swapchains=0;
    const auto generation=versions(device,FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,generators);
    const auto swapchain=versions(device,FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12,swapchains);
    if(!generators || !swapchains) { log("AMD FSR frame generation has no provider for this device; FSR unavailable"); return; }
    auto& s=state();
    s.status.backendsAvailable|=1u<<uint32_t(Backend::FSR); s.status.fsrMaxGenerated=1;
    log("AMD FSR frame generation available: providers ["+generation+"], swapchain ["+swapchain+
        "]. The FSR API generates one frame per rendered frame (no multi-frame control).");
}
std::unique_ptr<GeneratorOutput> create_fsr_output(const OutputParams& params,std::string& error) {
    if(!fsr.create) { error="AMD FSR runtime not loaded"; return nullptr; }
    auto output=std::make_unique<FsrOutput>();
    if(!output->init(params,error)) return nullptr;
    return output;
}
} // namespace tgfg
