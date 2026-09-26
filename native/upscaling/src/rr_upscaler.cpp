// SPDX-License-Identifier: MIT
// NVIDIA DLSS Ray Reconstruction through NVIDIA's NGX SDK (statically linked) and the NVIDIA-signed
// nvngx_dlssd.dll in Native/DLSS, on the bridge's D3D12 device like FSR and XeSS. It denoises and upscales
// in one step, in place of DLSS Super Resolution, for a frame whose lighting TG Path Tracing traced; the
// guide buffers (diffuse albedo, specular albedo, normals with roughness) are TG Path Tracing's, copied to
// shared textures with the frame's color, depth and motion vectors.
//
// D3D12, not the game's D3D11 device: NGX's D3D11 path runs Ray Reconstruction's network as CUDA kernels
// through NvAPI, and in the 310.9.1 runtime its first evaluation dereferences a null kernel launch
// (NGXCubinD3D11::MarkBufferResourceAsAccessWrittenD3D11). NVIDIA ships Ray Reconstruction for D3D12.
//
// NGX is initialized once for the shared device when the bridge reports it; the feature is created on the
// render thread's command list and re-created when the render or output size, the quality mode or the
// creation flags change.
#include "runtime.hpp"
#pragma warning(push, 0)
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssd_d3d.h>
#pragma warning(pop)
#include <cstring>
#include <filesystem>
#include <psapi.h>
#include <mutex>

namespace tgup {
namespace {
// This mod's NGX project id (NVIDIA asks engines' integrations for a project GUID).
constexpr const char* kProjectId="4f0c7b1e-9a2d-4c85-b3e6-6d1f8a2c7e59";

struct Ngx {
    std::mutex mutex;
    ComPtr<ID3D12Device> device;    // the device NGX was initialized with
    bool initialized=false, probed=false, available=false;
    std::string reason;             // why Ray Reconstruction is unavailable
} ngx;

std::string result_text(NVSDK_NGX_Result r) {
    const wchar_t* w=GetNGXResultAsString(r);
    std::string s;
    if(w) for(; *w; ++w) s+=char(*w<128 ? *w : '?');
    char code[16]; std::snprintf(code, sizeof(code), " (0x%08X)", unsigned(r));
    return s+code;
}
std::string utf8(const std::wstring& w) {
    std::string s(size_t(WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr)), '\0');
    if(!s.empty()) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), int(s.size()), nullptr, nullptr);
    return s;
}
// NGX's own errors and warnings about Ray Reconstruction, into this mod's native log.
void NVSDK_CONV ngx_log(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    if(!message) return;
    std::string text(message);
    while(!text.empty() && (text.back()=='\n' || text.back()=='\r')) text.pop_back();
    // Its search for optional files and for NVIDIA's over-the-air model cache fails harmlessly on most systems.
    if(text.find("failed to load NGXCore")!=std::string::npos || text.find("from cache")!=std::string::npos) return;
    if(text.find("error")!=std::string::npos || text.find("warning")!=std::string::npos) log_throttled("NGX: "+text);
}

class RayReconstructionBackend final : public Backend {
public:
    explicit RayReconstructionBackend(ID3D12Device* device) : device_(device) {}
    ~RayReconstructionBackend() override {
        if(feature_) NVSDK_NGX_D3D12_ReleaseFeature(feature_);
        if(params_) NVSDK_NGX_D3D12_DestroyParameters(params_);
    }
    const char* name() const override { return "DLSS Ray Reconstruction"; }
    bool stale(const TGUP_Frame& f) const override {
        return !params_ || f.renderWidth!=width_ || f.renderHeight!=height_ || f.outputWidth!=outWidth_ || f.outputHeight!=outHeight_ ||
            quality(f)!=quality_ || create_flags(f)!=flags_;
    }
    // The feature itself is created on the next command list (NGX records its creation there).
    bool prepare(const TGUP_Frame& f, std::string& error) override {
        if(feature_) { NVSDK_NGX_D3D12_ReleaseFeature(feature_); feature_=nullptr; }
        if(!params_ && NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_AllocateParameters(&params_))) { params_=nullptr; error="NGX parameters could not be allocated"; return false; }
        width_=f.renderWidth; height_=f.renderHeight; outWidth_=f.outputWidth; outHeight_=f.outputHeight;
        quality_=quality(f); flags_=create_flags(f);
        return true;
    }
    // A feature whose creation was recorded into a list that never ran was never initialized on the GPU: it is
    // created again. The next frame also starts the temporal history afresh (a DLSS mod may veto evaluations).
    void discarded() override {
        if(createdInList_ && feature_) { NVSDK_NGX_D3D12_ReleaseFeature(feature_); feature_=nullptr; }
        createdInList_=false; resetNext_=true;
    }
    bool record(ID3D12GraphicsCommandList* list, const Inputs& in, std::string& error) override {
        createdInList_=false;
        if(!in.diffuseAlbedo || !in.specularAlbedo || !in.normalRoughness) { error="Ray Reconstruction has no guides for this frame"; return false; }
        // NGX reads its inputs as shader resources and writes its output as unordered access; the shared
        // textures rest in COMMON between the two APIs and go back there.
        ID3D12Resource* reads[]={in.color, in.depth, in.motion, in.diffuseAlbedo, in.specularAlbedo, in.normalRoughness};
        D3D12_RESOURCE_BARRIER barriers[7]{};
        auto transition=[](D3D12_RESOURCE_BARRIER& b, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
            b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=r; b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore=from; b.Transition.StateAfter=to;
        };
        const auto readable=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        for(int i=0;i<6;++i) transition(barriers[i], reads[i], D3D12_RESOURCE_STATE_COMMON, readable);
        transition(barriers[6], in.output, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(7, barriers);
        bool ok=evaluate(list, in, error);
        for(auto& b:barriers) std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list->ResourceBarrier(7, barriers);
        return ok;
    }
private:
    static uint32_t quality(const TGUP_Frame& f) { return f.mode<=5 ? f.mode : uint32_t(NVSDK_NGX_PerfQuality_Value_MaxQuality); }
    static uint32_t create_flags(const TGUP_Frame& f) {
        return NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|
            ((f.flags&TGUP_FLAG_DEPTH_INVERTED) ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0u);
    }
    bool evaluate(ID3D12GraphicsCommandList* list, const Inputs& in, std::string& error) {
        const auto& f=*in.frame;
        bool reset=in.reset || resetNext_;
        if(!feature_) {
            NVSDK_NGX_DLSSD_Create_Params create{};
            create.InDenoiseMode=NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
            create.InRoughnessMode=NVSDK_NGX_DLSS_Roughness_Mode_Packed;
            create.InUseHWDepth=NVSDK_NGX_DLSS_Depth_Type_HW;
            create.InWidth=width_; create.InHeight=height_; create.InTargetWidth=outWidth_; create.InTargetHeight=outHeight_;
            create.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value(quality_);
            create.InFeatureCreateFlags=int(flags_);
            // Preset F, the SDK's newest model ("Default model RR2", 310.9), in every quality mode, rather than whatever
            // NGX's default is after an update. A nvngx_dlssd.dll without it uses its own default.
            for(const char* mode:{NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
                                  NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
                                  NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality})
                params_->Set(mode, unsigned(NVSDK_NGX_RayReconstruction_Hint_Render_Preset_F));
            const NVSDK_NGX_Result r=NGX_D3D12_CREATE_DLSSD_EXT(list, 1, 1, &feature_, params_, &create);
            if(NVSDK_NGX_FAILED(r) || !feature_) { feature_=nullptr; error="Ray Reconstruction feature creation failed: "+result_text(r); return false; }
            reset=true; createdInList_=true;
            log("DLSS Ray Reconstruction created: render "+std::to_string(width_)+"x"+std::to_string(height_)+" -> "+
                std::to_string(outWidth_)+"x"+std::to_string(outHeight_)+", NGX quality "+std::to_string(quality_)+", preset F; nvngx_dlssd.dll loaded from "+dlssd_modules());
        }
        NVSDK_NGX_D3D12_DLSSD_Eval_Params eval{};
        eval.pInDiffuseAlbedo=in.diffuseAlbedo; eval.pInSpecularAlbedo=in.specularAlbedo;
        eval.pInNormals=in.normalRoughness; eval.pInRoughness=in.normalRoughness; // roughness packed in the normals' alpha
        eval.pInColor=in.color; eval.pInOutput=in.output; eval.pInDepth=in.depth; eval.pInMotionVectors=in.motion;
        // HDRP's jitter as its own DLSS pass hands it over: negated, in render pixels.
        sdk_jitter(f, eval.InJitterOffsetX, eval.InJitterOffsetY);
        eval.InRenderSubrectDimensions={f.renderWidth, f.renderHeight};
        eval.InReset=reset ? 1 : 0;
        eval.InMVScaleX=f.motionScaleX; eval.InMVScaleY=f.motionScaleY;
        // The exposure HDRP baked into the color, as HDRP's own DLSS pass hands it over (its GPU exposure, 0.2..2): Ray
        // Reconstruction divides it out, so its history holds while HDRP's auto exposure adapts. Without it (NGX then
        // assumes 1) every change of exposure, as the view turns between bright and dark, looked like new lighting its
        // history had to catch up with.
        eval.InPreExposure=f.preExposure>0.f ? f.preExposure : 1.f;
        eval.InFrameTimeDeltaInMsec=frame_time_ms(f.frameTimeMs);
        std::memcpy(worldToView_, f.worldToView, sizeof(worldToView_)); std::memcpy(viewToClip_, f.viewToClip, sizeof(viewToClip_));
        eval.pInWorldToViewMatrix=worldToView_; eval.pInViewToClipMatrix=viewToClip_;
        const NVSDK_NGX_Result r=NGX_D3D12_EVALUATE_DLSSD_EXT(list, feature_, params_, &eval);
        if(NVSDK_NGX_FAILED(r)) { error="Ray Reconstruction evaluation failed: "+result_text(r); return false; }
        resetNext_=false;
        return true;
    }
    // Every nvngx_dlssd.dll in the process (a DLSS mod may bring its own): which one NGX runs is not the folder asked for.
    static std::string dlssd_modules() {
        HMODULE modules[512]; DWORD bytes=0; std::string list;
        if(!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) return "(unknown)";
        for(DWORD i=0;i<bytes/sizeof(HMODULE) && i<512;++i) {
            wchar_t path[MAX_PATH]{};
            if(!GetModuleFileNameW(modules[i], path, MAX_PATH)) continue;
            if(_wcsicmp(std::filesystem::path(path).filename().c_str(), L"nvngx_dlssd.dll")!=0) continue;
            list+=(list.empty() ? "" : ", ")+utf8(path);
        }
        return list.empty() ? "(none yet)" : list;
    }
    bool createdInList_=false, resetNext_=false;
    ComPtr<ID3D12Device> device_;
    NVSDK_NGX_Parameter* params_=nullptr;
    NVSDK_NGX_Handle* feature_=nullptr;
    uint32_t width_=0, height_=0, outWidth_=0, outHeight_=0, quality_=0, flags_=0;
    float worldToView_[16]{}, viewToClip_[16]{};
};
} // namespace

// Once per shared D3D12 device: NGX, and whether this GPU and driver run Ray Reconstruction.
bool rr_probe(ID3D12Device* device) {
    std::lock_guard<std::mutex> lock(ngx.mutex);
    if(!device) return false;
    if(ngx.probed && tgb::same_object(ngx.device.Get(), device)) return ngx.available;
    if(ngx.initialized) { NVSDK_NGX_D3D12_Shutdown1(ngx.device.Get()); ngx.initialized=false; }
    ngx.device=device; ngx.probed=true; ngx.available=false; ngx.reason.clear();
    const std::wstring folder=native_root()+L"\\DLSS";
    const wchar_t* paths[]={folder.c_str()};
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path=paths; info.PathListInfo.Length=1;
    info.LoggingInfo.LoggingCallback=ngx_log;
    info.LoggingInfo.MinimumLoggingLevel=NVSDK_NGX_LOGGING_LEVEL_ON;
    info.LoggingInfo.DisableOtherLoggingSinks=true;
    const std::wstring data=native_root();
    NVSDK_NGX_Result r=NVSDK_NGX_D3D12_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_UNITY, "6000.0", data.c_str(), device, &info);
    if(NVSDK_NGX_FAILED(r)) { ngx.reason="NGX did not initialize: "+result_text(r); log("DLSS Ray Reconstruction unavailable: "+ngx.reason); return false; }
    ngx.initialized=true;
    NVSDK_NGX_Parameter* caps=nullptr;
    r=NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    if(NVSDK_NGX_FAILED(r) || !caps) { ngx.reason="NGX reported no capabilities: "+result_text(r); log("DLSS Ray Reconstruction unavailable: "+ngx.reason); return false; }
    int supported=0, needsDriver=0; unsigned int major=0, minor=0;
    caps->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &supported);
    caps->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, &needsDriver);
    caps->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMajor, &major);
    caps->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMinor, &minor);
    NVSDK_NGX_D3D12_DestroyParameters(caps);
    if(needsDriver) ngx.reason="the NVIDIA driver is too old (Ray Reconstruction needs "+std::to_string(major)+"."+std::to_string(minor)+" or newer)";
    else if(!supported && GetModuleHandleW(L"TGFG.Streamline.dll"))
        ngx.reason="NGX on this device is TG Frame Generation's (Streamline's), which does not look in this mod's DLSS folder "
                   "(TG Frame Generation 1.2.0 or newer adds it when TG Path Tracing is installed)";
    else if(!supported) ngx.reason="this GPU or the bundled nvngx_dlssd.dll does not support it";
    ngx.available=supported && !needsDriver;
    log(ngx.available ? "DLSS Ray Reconstruction available (nvngx_dlssd.dll from "+utf8(folder)+")" : "DLSS Ray Reconstruction unavailable: "+ngx.reason);
    return ngx.available;
}
bool rr_available() { std::lock_guard<std::mutex> lock(ngx.mutex); return ngx.available; }
std::string rr_unavailable_reason() { std::lock_guard<std::mutex> lock(ngx.mutex); return ngx.reason; }
std::unique_ptr<Backend> create_rr_backend(ID3D12Device* device) { return std::make_unique<RayReconstructionBackend>(device); }
} // namespace tgup
