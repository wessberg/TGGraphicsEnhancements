// SPDX-License-Identifier: MIT
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <string>
#include <mutex>
#include <atomic>
#include "tgfg_api.h"
#include "tgfg_policy.hpp"
#include "tgfg_ring.hpp"
namespace tgfg {
using Microsoft::WRL::ComPtr;
struct Streamline {
    HMODULE module=nullptr;
#define CORE(n) decltype(&::n) n=nullptr
    CORE(slInit); CORE(slShutdown); CORE(slSetD3DDevice); CORE(slGetNativeInterface);
    CORE(slIsFeatureSupported); CORE(slGetFeatureRequirements); CORE(slGetFeatureFunction);
    CORE(slSetConstants); CORE(slSetTagForFrame); CORE(slGetNewFrameToken);
#undef CORE
    PFun_slDLSSGSetOptions* setOptions=nullptr;
    PFun_slDLSSGGetState* getState=nullptr;
    PFun_slReflexSetOptions* reflexOptions=nullptr;
    PFun_slReflexSleep* sleep=nullptr;
    PFun_slReflexGetState* reflexState=nullptr; // optional: latency reports for the pipeline summary
    PFun_slPCLSetMarker* marker=nullptr;
    decltype(&::D3D12CreateDevice) createDevice=nullptr;
    decltype(&::CreateDXGIFactory) createFactory=nullptr;
    decltype(&::CreateDXGIFactory1) createFactory1=nullptr;
    decltype(&::CreateDXGIFactory2) createFactory2=nullptr;
};
struct Frame {
    sl::FrameToken* token=nullptr; // Streamline frame, only when Streamline is bound to the device
    TGFG_Camera camera{};
    uint32_t id=0;
    bool cameraReady=false, renderBegun=false, renderEnded=false, inputContract=false, inputs=false, simEnded=false;
    bool xell=false; // XeLL slept for this frame, so it receives this frame's markers
    uint32_t evalCount=0;
    bool inputsSubmitted=false;
    TGFG_HdrpInputs hdrp{};                    // HDRP's own depth and motion vectors for this frame
    bool hdrpPending=false, hdrpCaptured=false; // packet received; copied at render event 3
    uint64_t born=0;
};
// Per-interval pipeline counters. Written under State::mutex and summarised in the native
// log every few seconds, so a single test session shows exactly where frames drop out.
struct Diag {
    uint64_t framesBegun=0, cameraPackets=0, ev0=0, ev1=0, ev2=0, evUnknownFrame=0;
    uint64_t ngxEvals=0, ngxNoFrameId=0, ngxNoCamera=0, ngxNotBegun=0, ngxAfterEnd=0, ngxMulti=0;
    uint64_t ngxDims=0, ngxParams=0, ngxRequestOff=0, captureFailed=0, captureOk=0, evalFailed=0;
    uint64_t presents=0, presentsWithInputs=0, tagged=0, tagFailed=0, readyPresents=0, fgPresents=0;
    uint64_t sdkPresented=0; // frames an FSR/XeSS swapchain reported presenting (rendered + generated)
    uint64_t hdrpPackets=0, hdrpCaptured=0, hdrpUpscaler=0, hdrpRejected=0;
    uint32_t lastWhy=0;
    uint64_t lastSummary=0;
    bool markerFailLogged=false, tagFailLogged=false;
};
struct State {
    std::recursive_mutex mutex;
    Diag diag;
    Streamline sl;
    HMODULE self=nullptr;
    std::wstring root, runtimeDir;
    std::string engineVersionUtf8;
    std::array<const wchar_t*,1> pluginPaths{};
    std::array<sl::Feature,3> features{{sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL}};
    TGFG_Request request{};
    TGFG_Status status{sizeof(TGFG_Status),TGFG_ABI};
    FrameRing<Frame> frames;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Device> bridgeDeviceProxy;
    ComPtr<ID3D12CommandQueue> presentQueue;
    IDXGISwapChain* swapchain=nullptr; // non-owning; process-pinned module, no hot unloading
    sl::ViewportHandle viewport{0};
    uint64_t heartbeat=0;
    std::atomic<long> asyncApiError{S_OK};
    // initialized: Streamline started. deviceReady: the private D3D12 presentation device exists.
    // slReady: Streamline is bound to that device; dlssReady/reflexReady: DLSS-G / Reflex+PCL usable.
    bool initialized=false, deviceReady=false, fatal=false, bootstrapAttempted=false;
    bool slReady=false, dlssReady=false, reflexReady=false, bridgeAllowed=false;
    bool signatureChecked=false;
    ComPtr<ID3D12Fence> lastInputsFence;
    uint64_t lastInputsFenceValue=0;
    bool vsyncSupported=false;
    sl::DLSSGOptions applied{};
    sl::ReflexOptions lastReflex{};
    bool haveOptions=false, haveReflex=false;
    Output output=Output::Standard;  // swapchain currently presenting
    Latency latency=Latency::Off;    // latency technology currently selected
    // Create flags of the observed DLSS SR feature: how depth and motion vectors are encoded.
    // Until one is seen, HDRP's defaults (inverted depth, render-resolution vectors) are assumed.
    uint32_t ngxFlags=NVSDK_NGX_DLSS_Feature_Flags_DepthInverted|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    // Last frame whose inputs came from the upscaler (DLSS SR). While the upscaler runs, its inputs
    // take precedence over HDRP's own.
    uint32_t lastUpscalerFrame=0;
    bool upscalerSeen=false;
};
State& state();
extern thread_local bool insideSL;
extern thread_local bool haveRenderFrame;
extern thread_local uint32_t renderFrameId;
void log(const std::string&);
// For conditions that can repeat every frame: identical messages are written at most every 5 s.
void log_throttled(const std::string&);
std::wstring module_path(HMODULE);
std::wstring system_directory();
void failure(Reason, const std::string&, bool fatal=false);
// WinVerifyTrust plus the leaf signer's display name (upper case) containing vendor.
bool trusted_signer(const std::wstring& path,const wchar_t* vendor);
// A signed vendor DLL from TGFG's own runtime folder, loaded by full path.
HMODULE load_signed(const std::wstring& path,const wchar_t* vendor);
bool load_runtime();
// Logs once if a module named sl.interposer.dll (another mod's Streamline) appears after startup.
void note_foreign_streamline();
bool install_graphics_hooks();
bool install_ngx_discovery();
// Render event 3: copies the frame's HDRP depth and motion vectors unless the upscaler supplies them.
void capture_hdrp_inputs(Frame&);
// DXGI factory straight from the system dxgi.dll, never a Streamline or overlay proxy.
HRESULT create_native_factory(REFIID,void**);
void on_device(IUnknown*);
void before_present(IDXGISwapChain*);
void after_present(HRESULT);
void clear_tags(Frame*);
void options_off();
void update_caps();
// Pipeline bookkeeping shared by the Streamline (DLSS-G) and FSR/XeSS present paths.
Readiness frame_readiness(Frame*,uint32_t format,uint32_t maxGenerated,uint32_t dynamicSupported,bool backendOk);
void note_generation(Frame*,Reason,bool enabled);
void present_markers(Frame*,bool start);
void release_stale_frames(bool generating);
void summarise(uint64_t now);
// Latency technologies: selection (State::mutex held) and the per-present parts.
Latency update_latency();
void latency_marker(sl::PCLMarker,Frame*);
// Hook registry holds actual trampolines, never a fake implementation of rendering calls.
bool add_hook(void* target,void* detour,void** original);
bool add_vtable_hook(void* object,size_t slot,void* detour);
void* vtable_original(void* object,size_t slot);
struct SLGuard { bool previous; SLGuard():previous(insideSL){insideSL=true;} ~SLGuard(){insideSL=previous;} };
} // namespace tgfg
