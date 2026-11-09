// SPDX-License-Identifier: MIT
// AMD FSR upscaling through the FSR API (FidelityFX SDK 2.x): AMD's signed loader and upscaler DLLs.
// The context takes AMD's default upscaler for this GPU, which is how FSR 4 reaches a Radeon RX 6000 or newer
// and FSR 3.1 every other GPU. AMD's rule for versions: an override only ever with an ID its query
// returned, and the choice either left to that default or made by the player in the options, never by
// the application. The log names the version AMD chose and the newest one it offers.
#include "runtime.hpp"
#include <ffx_api.h>
#include <ffx_api_types.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_upscale.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace tgup {
namespace {
struct FsrRuntime {
    HMODULE loader=nullptr, upscaler=nullptr;
    PfnFfxCreateContext create=nullptr; PfnFfxDestroyContext destroy=nullptr;
    PfnFfxConfigure configure=nullptr; PfnFfxQuery query=nullptr; PfnFfxDispatch dispatch=nullptr;
    std::mutex versionsMutex;
    std::vector<uint64_t> versionIds;
    std::vector<std::string> versionNames; // copied: AMD notes some names live in memory a later query overwrites
    int newest=-1;                         // index of the newest version in AMD's list
    std::vector<TGUP_Mode> modes;          // FSR's quality modes, Native AA first
    bool available=false;
} fsr;

void message(uint32_t type, const wchar_t* text) {
    // Also called from FSR's worker threads; log() serialises.
    if(!text) return;
    const int bytes=WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes>0 ? size_t(bytes-1) : 0, '\0');
    if(bytes>1) WideCharToMultiByte(CP_UTF8, 0, text, -1, &utf8[0], bytes, nullptr, nullptr);
    log_throttled(std::string("AMD FSR ")+(type==FFX_API_MESSAGE_TYPE_ERROR ? "error: " : "warning: ")+utf8);
}

uint32_t create_flags(const TGUP_Frame& f) {
    // HDRP's color before post-processing is linear HDR, pre-exposed by HDRP's own exposure. FSR gets
    // that exposure twice: as its pre-exposure, which it divides out so its history holds while the
    // exposure adapts, and as its exposure texture, which multiplies it back in, so FSR works on the
    // color HDRP tonemaps (unless the player asks FSR to compute its own exposure). Motion vectors are
    // at render resolution and not jittered.
    uint32_t flags=FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
    if(f.flags&TGUP_FLAG_DEPTH_INVERTED) flags|=FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
    if(f.flags&TGUP_FLAG_FSR_AUTO_EXPOSURE) flags|=FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    if(f.flags&TGUP_FLAG_FSR_DEBUG_CHECK) flags|=FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
    if(f.flags&TGUP_FLAG_FSR_DEBUG_VIEW) flags|=FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION;
    return flags;
}

class FsrBackend final : public Backend {
    ID3D12Device* device_;
    ffxContext context_=nullptr;
    uint32_t flags_=0, width_=0, height_=0;
    int32_t version_=-1;          // index in AMD's list of the version the context runs; -1 unknown
    std::string provider_;
    // FSR's exposure input: a 1x1 R32_FLOAT texture, written each frame from a ring of upload slots so
    // that a frame still on the GPU never has its value overwritten.
    static constexpr uint32_t kExposureSlots=8, kSlotBytes=D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    ComPtr<ID3D12Resource> exposure_, exposureUpload_;
    uint8_t* exposureMapped_=nullptr;
    uint32_t exposureSlot_=0;
    bool create_exposure(std::string& error) {
        if(exposure_) return true;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture{}; texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; texture.Width=1; texture.Height=1;
        texture.DepthOrArraySize=1; texture.MipLevels=1; texture.Format=DXGI_FORMAT_R32_FLOAT; texture.SampleDesc.Count=1;
        texture.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        HRESULT hr=device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&exposure_));
        if(FAILED(hr)) { error="FSR exposure texture: "+hexhr(hr); exposure_.Reset(); return false; }
        heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=kExposureSlots*kSlotBytes; buffer.Height=1;
        buffer.DepthOrArraySize=1; buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr=device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&exposureUpload_));
        const D3D12_RANGE none{0, 0};
        if(SUCCEEDED(hr)) hr=exposureUpload_->Map(0, &none, reinterpret_cast<void**>(&exposureMapped_));
        if(FAILED(hr)) { error="FSR exposure upload buffer: "+hexhr(hr); exposure_.Reset(); exposureUpload_.Reset(); exposureMapped_=nullptr; return false; }
        return true;
    }
    // Records the copy of this frame's exposure into the texture, which rests in COMMON like the other inputs.
    void write_exposure(ID3D12GraphicsCommandList* list, float value) {
        exposureSlot_=(exposureSlot_+1)%kExposureSlots;
        std::memcpy(exposureMapped_+size_t(exposureSlot_)*kSlotBytes, &value, sizeof(value));
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource=exposure_.Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource=exposure_.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource=exposureUpload_.Get(); source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Offset=uint64_t(exposureSlot_)*kSlotBytes;
        source.PlacedFootprint.Footprint={DXGI_FORMAT_R32_FLOAT, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
    }
public:
    explicit FsrBackend(ID3D12Device* device) : device_(device) {}
    ~FsrBackend() override { if(context_) fsr.destroy(&context_, nullptr); }
    const char* name() const override { return "AMD FSR"; }
    bool stale(const TGUP_Frame& f) const override {
        return !context_ || f.outputWidth!=width_ || f.outputHeight!=height_ || create_flags(f)!=flags_;
    }
    bool prepare(const TGUP_Frame& f, std::string& error) override {
        if(context_) { fsr.destroy(&context_, nullptr); context_=nullptr; }
        if(!(f.flags&TGUP_FLAG_FSR_AUTO_EXPOSURE) && !create_exposure(error)) return false;
        // The render resolution follows the game's quality slider, anywhere up to the output size, so
        // the context is created for the largest one and each dispatch passes the actual size.
        ffxCreateContextDescUpscale create{}; create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        create.flags=create_flags(f);
        create.maxRenderSize={f.outputWidth, f.outputHeight}; create.maxUpscaleSize={f.outputWidth, f.outputHeight};
        create.fpMessage=&message;
        ffxCreateContextDescUpscaleVersion version{}; version.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
        version.version=FFX_UPSCALER_VERSION;
        ffxCreateBackendDX12Desc backend{}; backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backend.device=device_;
        create.header.pNext=&version.header; version.header.pNext=&backend.header;
        const auto result=fsr.create(&context_, &create.header, nullptr);
        if(result!=FFX_API_RETURN_OK || !context_) {
            context_=nullptr; error="ffxCreateContext(upscale) returned "+std::to_string(result); return false;
        }
        flags_=create.flags; width_=f.outputWidth; height_=f.outputHeight;
        ffxQueryGetProviderVersion provider{}; provider.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        const bool known=fsr.query(&context_, &provider.header)==FFX_API_RETURN_OK;
        provider_=known && provider.versionName ? provider.versionName : "?";
        std::string newest;
        {
            std::lock_guard<std::mutex> lock(fsr.versionsMutex);
            version_=-1;
            for(size_t i=0;i<fsr.versionIds.size();++i) if(known && fsr.versionIds[i]==provider.versionId) version_=int32_t(i);
            if(fsr.newest>=0) newest=fsr.versionNames[size_t(fsr.newest)];
        }
        const bool isNewest=version_>=0 && version_==fsr.newest;
        log("AMD FSR upscaler "+provider_+" (AMD's default"+(isNewest ? ", the newest this GPU offers" : newest.empty() ? "" : "; the newest offered is "+newest)+
            ") for "+std::to_string(width_)+"x"+std::to_string(height_)+", create flags="+std::to_string(flags_));
        return true;
    }
    bool record(ID3D12GraphicsCommandList* list, const Inputs& in, std::string& error) override {
        const auto& f=*in.frame;
        ffxDispatchDescUpscale dispatch{}; dispatch.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        dispatch.commandList=list;
        // COMMON is where every shared texture rests between the two APIs; FSR transitions from and
        // back to the state it is told.
        dispatch.color=ffxApiGetResourceDX12(in.color, FFX_API_RESOURCE_STATE_COMMON);
        dispatch.depth=ffxApiGetResourceDX12(in.depth, FFX_API_RESOURCE_STATE_COMMON);
        dispatch.motionVectors=ffxApiGetResourceDX12(in.motion, FFX_API_RESOURCE_STATE_COMMON);
        dispatch.output=ffxApiGetResourceDX12(in.output, FFX_API_RESOURCE_STATE_COMMON);
        sdk_jitter(f, dispatch.jitterOffset.x, dispatch.jitterOffset.y);
        dispatch.motionVectorScale={f.motionScaleX, f.motionScaleY};
        dispatch.renderSize={f.renderWidth, f.renderHeight};
        dispatch.upscaleSize={f.outputWidth, f.outputHeight};
        dispatch.enableSharpening=f.sharpness>0.f;
        dispatch.sharpness=f.sharpness;
        dispatch.frameTimeDelta=frame_time_ms(f.frameTimeMs);
        dispatch.preExposure=f.preExposure;
        if(!(flags_&FFX_UPSCALE_ENABLE_AUTO_EXPOSURE)) {
            write_exposure(list, f.preExposure);
            dispatch.exposure=ffxApiGetResourceDX12(exposure_.Get(), FFX_API_RESOURCE_STATE_COMMON);
        }
        dispatch.reset=in.reset;
        fsr_near_far(f, dispatch.cameraNear, dispatch.cameraFar);
        dispatch.cameraFovAngleVertical=f.fovY;
        dispatch.viewSpaceToMetersFactor=1.f; // Unity units are metres
        dispatch.flags=(f.flags&TGUP_FLAG_FSR_DEBUG_VIEW) ? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW : 0u;
        const auto result=fsr.dispatch(&context_, &dispatch.header);
        if(result!=FFX_API_RETURN_OK) { error="ffxDispatch(upscale) returned "+std::to_string(result); return false; }
        return true;
    }
    void describe(TGUP_Status& s) const override { s.activeFsrVersion=version_<0 ? 0u : uint32_t(version_); }

};
} // namespace

bool load_fsr_runtime(const std::wstring& root) {
    const auto folder=std::filesystem::path(root)/L"FidelityFX";
    const auto upscaler=folder/L"amd_fidelityfx_upscaler_dx12.dll", loader=folder/L"amd_fidelityfx_loader_dx12.dll";
    if(!std::filesystem::exists(upscaler) || !std::filesystem::exists(loader)) {
        log("AMD FSR upscaler runtime is not installed (Native/FidelityFX); FSR unavailable"); return false;
    }
    // AMD's loader finds its effect DLLs by module name, so the upscaler is loaded first by full path.
    // Frame generation loads its own copy of the loader from its own folder; each copy finds the
    // effect its mod loaded.
    fsr.upscaler=host().load_signed(upscaler.c_str(), L"ADVANCED MICRO DEVICES");
    if(fsr.upscaler) fsr.loader=host().load_signed(loader.c_str(), L"ADVANCED MICRO DEVICES");
    if(!fsr.loader) { log("AMD FSR runtime is not AMD-signed or could not be loaded; FSR unavailable (details in TGGraphicsBridge.Native.log)"); return false; }
    fsr.create=reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(fsr.loader, "ffxCreateContext"));
    fsr.destroy=reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(fsr.loader, "ffxDestroyContext"));
    fsr.configure=reinterpret_cast<PfnFfxConfigure>(GetProcAddress(fsr.loader, "ffxConfigure"));
    fsr.query=reinterpret_cast<PfnFfxQuery>(GetProcAddress(fsr.loader, "ffxQuery"));
    fsr.dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(fsr.loader, "ffxDispatch"));
    if(!fsr.create || !fsr.destroy || !fsr.configure || !fsr.query || !fsr.dispatch) {
        log("AMD FSR loader is missing FSR API exports; FSR unavailable"); fsr.create=nullptr; return false;
    }
    log("AMD FSR runtime loaded from Native/FidelityFX (AMD-signed)");
    return true;
}
void probe_fsr(ID3D12Device* device) {
    if(!fsr.create) return;
    uint64_t count=0;
    ffxQueryDescGetVersions query{}; query.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; query.device=device; query.outputCount=&count;
    if(fsr.query(nullptr, &query.header)!=FFX_API_RETURN_OK || !count) {
        log("AMD FSR has no upscaler for this GPU; FSR unavailable"); return;
    }
    std::vector<uint64_t> ids(static_cast<size_t>(count)); std::vector<const char*> names(static_cast<size_t>(count), nullptr);
    query.versionIds=ids.data(); query.versionNames=names.data();
    if(fsr.query(nullptr, &query.header)!=FFX_API_RETURN_OK) { log("AMD FSR version query failed; FSR unavailable"); return; }
    std::string list, newest;
    {
        std::lock_guard<std::mutex> lock(fsr.versionsMutex);
        fsr.versionIds.clear(); fsr.versionNames.clear();
        for(uint64_t i=0;i<count && i<ids.size();++i) {
            fsr.versionIds.push_back(ids[size_t(i)]);
            fsr.versionNames.push_back(names[size_t(i)] ? names[size_t(i)] : "?");
            if(!list.empty()) list+=", ";
            list+=fsr.versionNames.back();
        }
        fsr.newest=newest_version(fsr.versionNames);
        if(fsr.newest>=0) newest=fsr.versionNames[size_t(fsr.newest)];
        fsr.available=!fsr.versionIds.empty();
    }
    log("AMD FSR upscaling available: "+list+(newest.empty() ? std::string() : "; newest "+newest)+". FSR runs AMD's default, named when the context is created");
    // The ratio of each quality mode, from AMD's default upscaler for this device (a null-context
    // query routed by the chained backend description).
    static const char* const modeNames[]={"Native AA", "Quality", "Balanced", "Performance", "Ultra Performance"};
    std::vector<TGUP_Mode> modes; std::string reported;
    for(uint32_t mode=FFX_UPSCALE_QUALITY_MODE_NATIVEAA; mode<=FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE; ++mode) {
        float ratio=0.f;
        ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode ratioQuery{}; ratioQuery.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETUPSCALERATIOFROMQUALITYMODE;
        ratioQuery.qualityMode=mode; ratioQuery.pOutUpscaleRatio=&ratio;
        ffxCreateBackendDX12Desc backend{}; backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backend.device=device;
        ratioQuery.header.pNext=&backend.header;
        if(fsr.query(nullptr, &ratioQuery.header)!=FFX_API_RETURN_OK || !(ratio>=1.f) || !(ratio<=8.f)) continue;
        modes.push_back(TGUP_Mode{mode, ratio});
        char text[32]; std::snprintf(text, sizeof(text), " %.2fx", ratio);
        reported+=(reported.empty() ? "" : ", ")+std::string(modeNames[mode])+text;
    }
    {
        std::lock_guard<std::mutex> lock(fsr.versionsMutex);
        fsr.modes=modes;
    }
    log(modes.empty() ? std::string("AMD FSR reports no quality modes; the game's render-scale slider serves FSR") : "AMD FSR quality modes: "+reported);
}
bool fsr_available() { return fsr.available; }
size_t fsr_version_count() { std::lock_guard<std::mutex> lock(fsr.versionsMutex); return fsr.versionNames.size(); }
std::string fsr_version_name(size_t index) {
    std::lock_guard<std::mutex> lock(fsr.versionsMutex);
    return index<fsr.versionNames.size() ? fsr.versionNames[index] : std::string();
}
std::vector<TGUP_Mode> fsr_modes() { std::lock_guard<std::mutex> lock(fsr.versionsMutex); return fsr.modes; }
std::unique_ptr<Backend> create_fsr_backend(ID3D12Device* device) {
    if(!fsr.available) return nullptr;
    return std::make_unique<FsrBackend>(device);
}
} // namespace tgup
