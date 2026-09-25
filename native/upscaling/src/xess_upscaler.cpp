// SPDX-License-Identifier: MIT
// Intel XeSS Super Resolution (XeSS SDK 3.x): Intel's signed libxess.dll. Since XeSS 1.3 every quality
// preset accepts any input resolution between native and its own ratio, so the game's continuous
// render scale is served by the narrowest preset that still contains it, and a change of preset is a
// cheap re-initialization.
#include "runtime.hpp"
#include <xess.h>
#include <xess_d3d12.h>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace tgup {
namespace {
struct XessRuntime {
    HMODULE module=nullptr;
#define F(n) decltype(&::n) n=nullptr
    F(xessGetVersion); F(xessD3D12CreateContext); F(xessD3D12Init); F(xessD3D12Execute); F(xessDestroyContext);
    F(xessGetOptimalInputResolution); F(xessSetVelocityScale); F(xessSetJitterScale); F(xessSetExposureMultiplier);
    F(xessSetLoggingCallback); F(xessIsOptimalDriver);
#undef F
    bool available=false;
    uint32_t version=0;
    std::mutex modesMutex;
    std::vector<TGUP_Mode> modes; // XeSS's presets, Native Anti-Aliasing first
} xs;

void xess_log(const char* message, xess_logging_level_t level) {
    if(message) log_throttled(std::string("Intel XeSS[")+std::to_string(int(level))+"] "+message);
}
bool ok(xess_result_t r) { return int(r)>=0; } // positive values are warnings
uint32_t init_flags(const TGUP_Frame& f) {
    // Render-resolution motion vectors (so XeSS needs depth), not jittered, HDR linear color.
    uint32_t flags=XESS_INIT_FLAG_NONE;
    if(f.flags&TGUP_FLAG_DEPTH_INVERTED) flags|=XESS_INIT_FLAG_INVERTED_DEPTH;
    return flags;
}

class XessBackend final : public Backend {
    ID3D12Device* device_;
    xess_context_handle_t context_=nullptr;
    uint32_t flags_=0, width_=0, height_=0;
    uint32_t renderWidth_=0, renderHeight_=0; // the render size preset_ was chosen for
    uint32_t mode_=0;                          // the preset the plugin asked for, 0 for none
    int preset_=-1;                 // index into kXessRanges
    float velocityX_=0, velocityY_=0;
    bool initialized_=false;
    void destroy() {
        if(context_) xs.xessDestroyContext(context_);
        context_=nullptr; initialized_=false; preset_=-1; renderWidth_=renderHeight_=0;
    }
public:
    explicit XessBackend(ID3D12Device* device) : device_(device) {}
    ~XessBackend() override { destroy(); }
    const char* name() const override { return "Intel XeSS"; }
    // The preset is chosen again only when the render size changes: the narrowest preset by ratio can
    // be one the runtime refuses for that size, and comparing against it would re-initialize every frame.
    bool stale(const TGUP_Frame& f) const override {
        return !initialized_ || f.outputWidth!=width_ || f.outputHeight!=height_ || init_flags(f)!=flags_ ||
            f.renderWidth!=renderWidth_ || f.renderHeight!=renderHeight_ || f.mode!=mode_;
    }
    bool prepare(const TGUP_Frame& f, std::string& error) override {
        // A new output size or flags need a new context; a preset change only a re-initialization.
        if(context_ && (f.outputWidth!=width_ || f.outputHeight!=height_ || init_flags(f)!=flags_)) destroy();
        if(!context_) {
            const auto r=xs.xessD3D12CreateContext(device_, &context_);
            if(!ok(r) || !context_) { context_=nullptr; error="xessD3D12CreateContext returned "+std::to_string(int(r)); return false; }
            xs.xessSetLoggingCallback(context_, XESS_LOGGING_LEVEL_WARNING, &xess_log);
            if(xs.xessIsOptimalDriver(context_)==XESS_RESULT_WARNING_OLD_DRIVER)
                log("Intel XeSS reports an older graphics driver than it prefers; it still runs");
        }
        const xess_2d_t output{f.outputWidth, f.outputHeight};
        const auto accepts=[&](int candidate) {
            xess_2d_t optimal{}, minimum{}, maximum{};
            const auto preset=static_cast<xess_quality_settings_t>(kXessRanges[candidate].preset);
            if(!ok(xs.xessGetOptimalInputResolution(context_, &output, preset, &optimal, &minimum, &maximum))) return false;
            // NativeAA reports a single size; any render size smaller than the output needs a real preset.
            return f.renderWidth>=minimum.x && f.renderHeight>=minimum.y && f.renderWidth<=maximum.x && f.renderHeight<=maximum.y;
        };
        // The preset the player chose, when its input range contains the render size; otherwise the
        // narrowest preset whose range, as this runtime reports it, does.
        int index=-1;
        for(int i=0; f.mode && i<int(sizeof(kXessRanges)/sizeof(kXessRanges[0])); ++i)
            if(uint32_t(kXessRanges[i].preset)==f.mode && accepts(i)) index=i;
        const float ratio=upscale_ratio(f.renderWidth, f.renderHeight, f.outputWidth, f.outputHeight);
        if(index<0) for(index=xess_preset_index(ratio); index>=0 && !accepts(index); index=xess_preset_index(ratio, index+1)) {}
        if(index<0) { error="no XeSS preset accepts "+std::to_string(f.renderWidth)+"x"+std::to_string(f.renderHeight)+
            " for "+std::to_string(f.outputWidth)+"x"+std::to_string(f.outputHeight); return false; }
        // The same preset still serves the new render size: no re-initialization, and its history stays.
        if(initialized_ && index==preset_ && f.outputWidth==width_ && f.outputHeight==height_ && init_flags(f)==flags_) {
            renderWidth_=f.renderWidth; renderHeight_=f.renderHeight; mode_=f.mode;
            return true;
        }
        xess_d3d12_init_params_t params{};
        params.outputResolution=output;
        params.qualitySetting=static_cast<xess_quality_settings_t>(kXessRanges[index].preset);
        params.initFlags=init_flags(f);
        // Blocking: allocates XeSS's resources and compiles its kernels, once per preset change.
        const auto r=xs.xessD3D12Init(context_, &params);
        if(!ok(r)) { initialized_=false; error="xessD3D12Init returned "+std::to_string(int(r)); return false; }
        initialized_=true; preset_=index; flags_=params.initFlags; width_=f.outputWidth; height_=f.outputHeight;
        renderWidth_=f.renderWidth; renderHeight_=f.renderHeight; mode_=f.mode;
        velocityX_=velocityY_=0;
        xs.xessSetJitterScale(context_, 1.f, 1.f);
        xs.xessSetExposureMultiplier(context_, 1.f);
        log(std::string("Intel XeSS ")+xess_preset_name(kXessRanges[index].preset)+" for "+std::to_string(width_)+"x"+
            std::to_string(height_)+" (render "+std::to_string(f.renderWidth)+"x"+std::to_string(f.renderHeight)+"), init flags="+std::to_string(flags_));
        return true;
    }
    bool record(ID3D12GraphicsCommandList* list, const Inputs& in, std::string& error) override {
        const auto& f=*in.frame;
        // HDRP's motion vectors to render pixels, current to previous; it changes with the render size.
        if(f.motionScaleX!=velocityX_ || f.motionScaleY!=velocityY_) {
            if(!ok(xs.xessSetVelocityScale(context_, f.motionScaleX, f.motionScaleY))) { error="xessSetVelocityScale failed"; return false; }
            velocityX_=f.motionScaleX; velocityY_=f.motionScaleY;
        }
        // XeSS works on the pre-exposed color as it is, the color HDRP tonemaps; it has no pre-exposure
        // input, so HDRP's exposure (which FSR takes as its pre-exposure) is not applied here.
        xs.xessSetExposureMultiplier(context_, 1.f);
        // XeSS takes its inputs in NON_PIXEL_SHADER_RESOURCE and writes its output as a UAV; the
        // shared textures rest in COMMON between the two APIs and go back there afterwards.
        D3D12_RESOURCE_BARRIER barriers[4]{};
        ID3D12Resource* resources[4]={in.color, in.depth, in.motion, in.output};
        for(int i=0;i<4;++i) {
            barriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[i].Transition.pResource=resources[i];
            barriers[i].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[i].Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;
            barriers[i].Transition.StateAfter=i==3 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        list->ResourceBarrier(4, barriers);
        xess_d3d12_execute_params_t exec{};
        exec.pColorTexture=in.color; exec.pVelocityTexture=in.motion; exec.pDepthTexture=in.depth; exec.pOutputTexture=in.output;
        sdk_jitter(f, exec.jitterOffsetX, exec.jitterOffsetY);
        exec.exposureScale=1.f;
        exec.resetHistory=in.reset ? 1u : 0u;
        exec.inputWidth=f.renderWidth; exec.inputHeight=f.renderHeight;
        const auto r=xs.xessD3D12Execute(context_, list, &exec);
        for(auto& b:barriers) std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list->ResourceBarrier(4, barriers);
        if(!ok(r)) { error="xessD3D12Execute returned "+std::to_string(int(r)); return false; }
        return true;
    }
    void describe(TGUP_Status& s) const override { s.xessPreset=preset_>=0 ? uint32_t(kXessRanges[preset_].preset) : 0u; }
};
} // namespace

bool load_xess_runtime(const std::wstring& root) {
    const auto path=std::filesystem::path(root)/L"XeSS"/L"libxess.dll";
    if(!std::filesystem::exists(path)) { log("Intel XeSS runtime is not installed (Native/XeSS); XeSS unavailable"); return false; }
    xs.module=host().load_signed(path.c_str(), L"INTEL");
    if(!xs.module) { log("libxess.dll is not Intel-signed or could not be loaded; XeSS unavailable (details in TGGraphicsBridge.Native.log)"); return false; }
    bool complete=true;
#define F(n) xs.n=reinterpret_cast<decltype(xs.n)>(GetProcAddress(xs.module, #n)); complete=complete && xs.n
    F(xessGetVersion); F(xessD3D12CreateContext); F(xessD3D12Init); F(xessD3D12Execute); F(xessDestroyContext);
    F(xessGetOptimalInputResolution); F(xessSetVelocityScale); F(xessSetJitterScale); F(xessSetExposureMultiplier);
    F(xessSetLoggingCallback); F(xessIsOptimalDriver);
#undef F
    if(!complete) { log("Intel XeSS runtime is missing expected exports; XeSS unavailable"); xs.xessD3D12CreateContext=nullptr; return false; }
    xess_version_t version{}; xs.xessGetVersion(&version);
    xs.version=(uint32_t(version.major)<<16)|(uint32_t(version.minor)<<8)|uint32_t(version.patch);
    log("Intel XeSS "+std::to_string(version.major)+"."+std::to_string(version.minor)+"."+std::to_string(version.patch)+
        " loaded from Native/XeSS (Intel-signed)");
    return true;
}
void probe_xess(ID3D12Device* device) {
    if(!xs.xessD3D12CreateContext) return;
    // Creating a context is the test: it fails on GPUs without Shader Model 6.4 (the DP4a path) or
    // an unsupported driver. Initialization, which compiles XeSS's kernels, waits for first use.
    xess_context_handle_t probe=nullptr;
    const auto r=xs.xessD3D12CreateContext(device, &probe);
    if(!ok(r) || !probe) { log("Intel XeSS is not supported on this GPU or driver: "+std::to_string(int(r))); return; }
    // Each preset's ratio as this runtime reports it: the optimal input size for a 4K output.
    const xess_2d_t output{3840, 2160};
    std::vector<TGUP_Mode> modes; std::string reported;
    for(const auto& range:kXessRanges) {
        xess_2d_t optimal{}, minimum{}, maximum{};
        if(!ok(xs.xessGetOptimalInputResolution(probe, &output, static_cast<xess_quality_settings_t>(range.preset), &optimal, &minimum, &maximum)) || !optimal.x)
            continue;
        const float ratio=float(output.x)/float(optimal.x);
        modes.push_back(TGUP_Mode{uint32_t(range.preset), ratio});
        char text[32]; std::snprintf(text, sizeof(text), " %.2fx", ratio);
        reported+=(reported.empty() ? "" : ", ")+std::string(xess_preset_name(range.preset))+text;
    }
    {
        std::lock_guard<std::mutex> lock(xs.modesMutex);
        xs.modes=modes;
    }
    xs.xessDestroyContext(probe);
    xs.available=true;
    log("Intel XeSS upscaling available; quality presets: "+(reported.empty() ? std::string("none reported, the game's render-scale slider serves XeSS") : reported));
}
bool xess_available() { return xs.available; }
uint32_t xess_version() { return xs.version; }
std::vector<TGUP_Mode> xess_modes() { std::lock_guard<std::mutex> lock(xs.modesMutex); return xs.available ? xs.modes : std::vector<TGUP_Mode>(); }
std::unique_ptr<Backend> create_xess_backend(ID3D12Device* device) {
    if(!xs.available) return nullptr;
    return std::make_unique<XessBackend>(device);
}
} // namespace tgup
