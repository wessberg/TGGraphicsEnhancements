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
#include <algorithm>
#include <map>
#include <mutex>
#include <atomic>
#include "tgfg_api.h"
#include "tgfg_policy.hpp"
#include "tgfg_ring.hpp"
#include "tgb/bridge_api.hpp"
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
    bool xellSlept=false; // and the sleep succeeded: XeSS-FG may generate from this frame
    uint32_t evalCount=0;
    bool inputsSubmitted=false;
    TGFG_HdrpInputs hdrp{};                    // HDRP's own depth and motion vectors for this frame
    bool hdrpPending=false, hdrpCaptured=false; // packet received; copied at render event 3
    TGFG_Hudless hudless{};
    bool hudlessPending=false; // copied at render event 4, before HDRP recycles the color
    uint64_t born=0;
};
// CPU time the bridge's Present spends per frame, by stage, in microseconds: waiting for the D3D12
// queue to free a buffer (GPU backlog), in the presenting SDK's Present (pacing, VSync), and handing
// the frame back to D3D11. Summed over a window; the rest of the total is copying and recording.
struct PresentTiming {
    uint64_t presents=0, totalUs=0, gpuWaitUs=0, sdkPresentUs=0, handoffUs=0, intervalUs=0;
    uint32_t totalMaxUs=0, gpuWaitMaxUs=0, sdkPresentMaxUs=0, intervalMaxUs=0, slowIntervals=0;
    void add(uint32_t total,uint32_t gpuWait,uint32_t sdkPresent,uint32_t handoff,uint32_t interval) {
        ++presents; totalUs+=total; gpuWaitUs+=gpuWait; sdkPresentUs+=sdkPresent; handoffUs+=handoff;
        totalMaxUs=(std::max)(totalMaxUs,total); gpuWaitMaxUs=(std::max)(gpuWaitMaxUs,gpuWait);
        sdkPresentMaxUs=(std::max)(sdkPresentMaxUs,sdkPresent);
        if(interval) { intervalUs+=interval; intervalMaxUs=(std::max)(intervalMaxUs,interval); if(interval>50000) ++slowIntervals; }
    }
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
    // HUD-less frames: copies at render event 2, comparison results and frames handed to a technology.
    uint64_t hudlessCaptured=0, hudlessRejected=0, hudlessChecks=0, hudlessTagged=0, hudlessStill=0;
    uint32_t hudlessDiffPermille=0; int32_t hudlessLastResult=0;
    uint32_t lastWhy=0;
    uint64_t lastSummary=0;
    PresentTiming timing;
    // Times D3D11 had to wait on the GPU for D3D12 before overwriting a shared texture: a back buffer
    // not yet copied, or an input slot whose present had not retired. The back buffer's count is one per
    // present: a D3D11 game renders every frame into the same one, whose copy was queued just before the
    // check. The input slots' count is the one that says D3D12 fell behind.
    uint64_t d11ColorWaits=0, d11SlotWaits=0;
    bool markerFailLogged=false, tagFailLogged=false;
};
struct State {
    std::recursive_mutex mutex;
    Diag diag;
    Streamline sl;
    HMODULE self=nullptr;
    std::wstring root, runtimeDir;
    std::string engineVersionUtf8;
    std::array<const wchar_t*,2> pluginPaths{};
    std::wstring rayReconstructionDir; // TG Better Upscaling's DLSS folder, when NGX here must find Ray Reconstruction
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
    bool reflexOfferDecided=false; // offer_reflex got Streamline's answer, or gave up waiting for it
    // probed: the FSR, XeSS and latency probes ran on the device. The capabilities count as known
    // (status flag 4) only once they have and a game swapchain was replaced: the plugin reads them once.
    bool probed=false;
    bool signatureChecked=false;
    ComPtr<ID3D12Fence> lastInputsFence;
    uint64_t lastInputsFenceValue=0;
    bool vsyncSupported=false;
    // DLSS-G's minimum output side (DLSSGState::minWidthOrHeight), and the output size its runtime last
    // refused as too small: frame generation stays off at that size and resumes when it changes.
    uint32_t dlssgMinimum=0, dlssgRefusedWidth=0, dlssgRefusedHeight=0;
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
    // HUD-less frames: requested by the plugin (it draws the overlay UI after render event 2) and
    // verified once the presented frames differ from the captures only over HUD-sized regions.
    bool hudlessRequested=false, hudlessOk=false;
    uint32_t hudlessGood=0, hudlessBad=0;
    uint32_t debugFlags=0; // TGFG_SetDebugFlags: SDK debug overlays and checks
    // ResizeBuffers calls the bridge carried out against ones it answered without touching anything,
    // because they asked for the configuration already in effect. Session totals, not per-summary
    // counts: a resize is rare, and what matters is that one still happens when something is owed it.
    uint64_t resizes=0, resizesSkipped=0;
    // Since the plugin last read it (TGFG_ReadTimings): one troubleshooting step's frames.
    PresentTiming timingSinceRead;
    // TGFG_SetNoTearing: Present without DXGI_PRESENT_ALLOW_TEARING.
    std::atomic<bool> noTearing{false};
    // TGFG_LogNote: end the current summary window at the next present; hold the 5 s cadence until then.
    std::atomic<bool> summaryRequested{false}, summaryHeld{false};
};
State& state();
// TG Graphics Bridge, which loaded this extension: hooks, signature checks and the shared D3D12 device.
tgb::Host& host();
extern thread_local bool insideSL;
extern thread_local bool haveRenderFrame;
extern thread_local uint32_t renderFrameId;
void log(const std::string&);
// For conditions that can repeat every frame: identical messages are written at most every 5 s.
void log_throttled(const std::string&);
std::wstring module_path(HMODULE);
std::wstring system_directory();
// The Windows build number (RtlGetVersion, which compatibility shims leave alone); 0 if unknown.
uint32_t windows_build();
// A System32 DLL's file version, its four parts packed 16 bits each from the top; 0 when missing.
uint64_t system_file_version(const wchar_t* name);
// The version of the copy of a runtime DLL (such as msvcp140.dll) that a DLL loaded from dllDir binds
// to: one already loaded into the process, else dllDir's, the game folder's, then System32's (the
// order load_signed's LoadLibraryEx flags search). where names it; 0 and "" when there is none.
uint64_t bound_file_version(const wchar_t* name,const std::wstring& dllDir,std::wstring& where);
std::string version_text(uint64_t packed);
void failure(Reason, const std::string&, bool fatal=false);
// A fact about this session for TGFG_ReadDiagnostics (key=value); the plugin explains it to the player.
void fact(const std::string& key,const std::string& value);
std::string facts();
const char* sl_result_name(sl::Result);
// WinVerifyTrust plus the leaf signer's display name (upper case) containing vendor.
bool trusted_signer(const std::wstring& path,const wchar_t* vendor);
// A signed vendor DLL from TGFG's own runtime folder, loaded by full path.
HMODULE load_signed(const std::wstring& path,const wchar_t* vendor);
bool load_runtime();
// Logs once if a module named sl.interposer.dll (another mod's Streamline) appears after startup.
void note_foreign_streamline();
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
// Adds Reflex to the offered latency technologies when Streamline reports it available. last: the
// plugin is about to read the capabilities, so an unanswered query means not offered.
void offer_reflex(bool last);
// Pipeline bookkeeping shared by the Streamline (DLSS-G) and FSR/XeSS present paths.
Readiness frame_readiness(Frame*,uint32_t format,uint32_t maxGenerated,uint32_t dynamicSupported,bool backendOk);
void note_generation(Frame*,Reason,bool enabled);
// One HUD-less comparison result: sample points where the presented frame differs from the capture
// before the overlay UI, where that capture differs from the previously presented frame, and where
// the presented frame itself does. A still image proves nothing and is skipped; a capture that did
// not change while the frame did is stale. Decides whether the captures reach the technologies.
void note_hudless_difference(uint32_t differing,uint32_t changed,uint32_t moving,uint32_t samples);
// Forget that the captures were ever verified. The verification is a statement about one buffer
// configuration - these dimensions, this format, this comparison view - so when the configuration
// changes the evidence stops applying and the captures have to earn it again against the new one.
void reset_hudless_verification(const char* why);
void present_markers(Frame*,bool start);
void release_stale_frames(bool generating);
void summarise(uint64_t now);
// Latency technologies: selection (State::mutex held) and the per-present parts.
Latency update_latency();
void latency_marker(sl::PCLMarker,Frame*);
// The bridge's hook registry: actual trampolines, never a fake implementation of rendering calls.
bool add_hook(void* target,void* detour,void** original);
bool add_vtable_hook(void* object,size_t slot,void* detour);
void* vtable_original(void* object,size_t slot);
// Streamline, FSR and XeSS create swapchains and factories through the same DXGI entry points the
// game uses; while this is set, both this extension's hooks and the bridge's pass those calls through.
struct SLGuard {
    bool previous, hostPrevious;
    SLGuard():previous(insideSL),hostPrevious(host().set_internal(true)){insideSL=true;}
    ~SLGuard(){insideSL=previous;host().set_internal(hostPrevious);}
    SLGuard(const SLGuard&)=delete; SLGuard& operator=(const SLGuard&)=delete;
};
} // namespace tgfg
