// SPDX-License-Identifier: MIT
#include "ray_regeneration.hpp"
#include <ffx_api.h>
#include <ffx_api_types.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_denoiser.h>
#include <cstring>
#include <filesystem>

namespace tgpt {
namespace {
struct Runtime {
    HMODULE loader=nullptr, denoiser=nullptr;
    PfnFfxCreateContext create=nullptr; PfnFfxDestroyContext destroy=nullptr;
    PfnFfxConfigure configure=nullptr; PfnFfxQuery query=nullptr; PfnFfxDispatch dispatch=nullptr;
    bool available=false;
} rt;

std::string utf8(const wchar_t* text) {
    if(!text) return {};
    const int bytes=WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(bytes>0 ? size_t(bytes-1) : 0, '\0');
    if(bytes>1) WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], bytes, nullptr, nullptr);
    return out;
}
void message(uint32_t type, const wchar_t* text) {
    log_throttled(std::string("AMD Ray Regeneration ")+(type==FFX_API_MESSAGE_TYPE_ERROR ? "error: " : "warning: ")+utf8(text));
}
bool load_runtime() {
    if(rt.loader) return true;
    const auto folder=std::filesystem::path(native_root())/L"FidelityFX";
    const auto denoiser=folder/L"amd_fidelityfx_denoiser_dx12.dll", loader=folder/L"amd_fidelityfx_loader_dx12.dll";
    std::error_code missing;
    if(!std::filesystem::exists(denoiser, missing) || !std::filesystem::exists(loader, missing)) {
        log("AMD Ray Regeneration's runtime is not installed (Native/FidelityFX); the built-in denoiser serves"); return false;
    }
    // AMD's loader finds its effect DLLs by module name, so the denoiser is loaded first, by full path.
    rt.denoiser=host().load_signed(denoiser.c_str(), L"ADVANCED MICRO DEVICES");
    if(rt.denoiser) rt.loader=host().load_signed(loader.c_str(), L"ADVANCED MICRO DEVICES");
    if(!rt.loader) { log("AMD Ray Regeneration's runtime is not AMD-signed or could not be loaded (details in TGGraphicsBridge.Native.log)"); return false; }
    rt.create=reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(rt.loader, "ffxCreateContext"));
    rt.destroy=reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(rt.loader, "ffxDestroyContext"));
    rt.configure=reinterpret_cast<PfnFfxConfigure>(GetProcAddress(rt.loader, "ffxConfigure"));
    rt.query=reinterpret_cast<PfnFfxQuery>(GetProcAddress(rt.loader, "ffxQuery"));
    rt.dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(rt.loader, "ffxDispatch"));
    if(!rt.create || !rt.destroy || !rt.configure || !rt.query || !rt.dispatch) {
        log("AMD's FidelityFX loader lacks the FSR API exports; Ray Regeneration unavailable"); rt.create=nullptr; return false;
    }
    // Errors and warnings from the denoiser effect go to this log.
    ffxConfigureDescGlobalDebug debug{}; debug.header.type=FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG;
    debug.effectId=FFX_API_EFFECT_ID_DENOISER; debug.fpMessage=&message; debug.debugLevel=FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
    rt.configure(nullptr, &debug.header);
    return true;
}
FfxApiResource resource(ID3D12Resource* r) {
    // Every texture the tracer hands over rests in UNORDERED_ACCESS; AMD's backend transitions from and back to it.
    return ffxApiGetResourceDX12(r, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
}
} // namespace

bool rrg_probe(ID3D12Device* device, uint32_t vendor) {
    rt.available=false;
    if(vendor!=0x1002) { log("AMD Ray Regeneration needs an AMD Radeon RX 9000 or newer; the built-in denoiser (or DLSS Ray Reconstruction) serves"); return false; }
    if(!device || !load_runtime()) return false;
    uint64_t count=0;
    ffxQueryDescGetVersions query{}; query.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_DENOISER; query.device=device; query.outputCount=&count;
    if(rt.query(nullptr, &query.header)!=FFX_API_RETURN_OK || !count) {
        log("AMD Ray Regeneration does not run on this GPU (it needs a Radeon RX 9000 or newer); the built-in denoiser serves"); return false;
    }
    std::vector<uint64_t> ids(static_cast<size_t>(count)); std::vector<const char*> names(static_cast<size_t>(count), nullptr);
    query.versionIds=ids.data(); query.versionNames=names.data();
    if(rt.query(nullptr, &query.header)!=FFX_API_RETURN_OK) { log("AMD Ray Regeneration's version query failed; the built-in denoiser serves"); return false; }
    std::string list;
    for(const char* n:names) { if(!list.empty()) list+=", "; list+=n ? n : "?"; }
    log("AMD Ray Regeneration available on this GPU: "+list);
    rt.available=true;
    return true;
}
bool rrg_available() { return rt.available; }

void RayRegeneration::destroy() {
    if(context_ && rt.destroy) { ffxContext c=context_; rt.destroy(&c, nullptr); }
    context_=nullptr; width_=height_=0;
}

bool RayRegeneration::ensure(ID3D12Device* device, uint32_t width, uint32_t height, std::string& error) {
    if(!needs_context(width, height)) return true;
    destroy();
    if(!rt.available) { error="not available"; return false; }
    // The bounced light, the direct light and the reflections.
    ffxCreateContextDescDenoiser create{}; create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_DENOISER;
    create.version=FFX_DENOISER_VERSION;
    create.maxRenderSize={width, height};
    create.signalFlags=FFX_DENOISER_SIGNAL_INDIRECT_DIFFUSE|FFX_DENOISER_SIGNAL_DIRECT_DIFFUSE|FFX_DENOISER_SIGNAL_INDIRECT_SPECULAR;
    create.checkerboardSignalFlags=FFX_DENOISER_SIGNAL_NONE;
    ffxCreateBackendDX12Desc backend{}; backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backend.device=device;
    create.header.pNext=&backend.header;
    ffxContext context=nullptr;
    const auto result=rt.create(&context, &create.header, nullptr);
    if(result!=FFX_API_RETURN_OK || !context) { error="ffxCreateContext(denoiser) returned "+std::to_string(result); return false; }
    context_=context; width_=width; height_=height;
    log("AMD Ray Regeneration context for "+std::to_string(width)+"x"+std::to_string(height)+": bounced light, direct light and reflections");
    return true;
}

bool RayRegeneration::dispatch(ID3D12GraphicsCommandList* list, const RrgFrame& f, std::string& error) {
    if(!context_) { error="no context"; return false; }
    ffxDispatchDescDenoiser d{}; d.header.type=FFX_API_DISPATCH_DESC_TYPE_DENOISER;
    d.commandList=list;
    d.linearDepth=resource(f.linearDepth); d.motionVectors=resource(f.motion); d.normals=resource(f.normals);
    d.specularAlbedo=resource(f.specularAlbedo); d.diffuseAlbedo=resource(f.diffuseAlbedo);
    d.motionVectorScale={1.f, 1.f, 1.f};  // written as the previous texture coordinate minus this one, and the depth delta
    d.jitterOffsets={f.jitter[0], f.jitter[1]};
    d.cameraPositionDelta={f.cameraDelta[0], f.cameraDelta[1], f.cameraDelta[2]};
    std::memcpy(&d.view, f.view, sizeof(d.view));
    std::memcpy(&d.projection, f.projection, sizeof(d.projection));
    d.linearDepthBounds={0.f, 1e5f};      // the sky's texels carry a depth beyond it and pass through
    d.renderSize={f.width, f.height};
    d.frameIndex=f.frameIndex;
    d.flags=f.reset ? FFX_DENOISER_DISPATCH_RESET : 0u; // albedos sqrt-encoded, as the SDK assumes by default
    ffxDispatchDescDenoiserIndirectDiffuse gi{}; gi.header.type=FFX_API_DISPATCH_DESC_TYPE_DENOISER_INDIRECT_DIFFUSE;
    gi.signal.input=resource(f.indirectDiffuse); gi.signal.output=gi.signal.input;
    d.header.pNext=&gi.header;
    ffxDispatchDescDenoiserDirectDiffuse sun{}; sun.header.type=FFX_API_DISPATCH_DESC_TYPE_DENOISER_DIRECT_DIFFUSE;
    ffxDispatchDescDenoiserIndirectSpecular reflections{}; reflections.header.type=FFX_API_DISPATCH_DESC_TYPE_DENOISER_INDIRECT_SPECULAR;
    sun.signal.input=resource(f.directDiffuse); sun.signal.output=sun.signal.input;
    reflections.signal.input=resource(f.indirectSpecular); reflections.signal.output=reflections.signal.input;
    gi.header.pNext=&sun.header; sun.header.pNext=&reflections.header;
    ffxContext c=context_;
    const auto result=rt.dispatch(&c, &d.header);
    if(result!=FFX_API_RETURN_OK) { error="ffxDispatch(denoiser) returned "+std::to_string(result); return false; }
    return true;
}
} // namespace tgpt
