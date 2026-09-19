// SPDX-License-Identifier: MIT
// Frame generation SDKs other than DLSS-G (AMD FSR, Intel XeSS) and the latency technologies that
// accompany them (Intel XeLL, AMD Anti-Lag 2). Every runtime is optional: a missing, unsigned or
// unsupported one only removes its entries from the capabilities TGFG reports.
#pragma once
#include "sl_runtime.hpp"
#include <memory>
#include <string>
namespace tgfg {
// One frame's generation inputs in NGX/DLSS conventions: depth and motion vectors copied into
// shareable textures (D3D12 state COMMON) and a motion scale to render-resolution pixels.
struct GenerationInputs {
    ID3D12Resource* depth=nullptr; ID3D12Resource* motion=nullptr;
    uint32_t depthWidth=0, depthHeight=0, motionWidth=0, motionHeight=0;
    float jitterX=0, jitterY=0, motionScaleX=1, motionScaleY=1;
    bool reset=false;
    const TGFG_Camera* camera=nullptr;
    float frameTimeMs=0;
};
struct OutputParams {
    HWND hwnd=nullptr;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen=nullptr;
    ID3D12Device* device=nullptr;
    ID3D12CommandQueue* queue=nullptr; // native presenting queue
    IDXGIFactory2* factory=nullptr;    // native factory
    bool hdr=false;                    // output is HDR10 (PQ, BT.2020)
    uint32_t inputFlags=0;             // NVSDK_NGX_DLSS_Feature_Flags describing depth and motion vectors
};
// A presentation swapchain owned by the FSR or XeSS SDK. The standard swapchain (Streamline's
// proxy, which hosts DLSS-G) keeps its original path in the bridge.
class GeneratorOutput {
public:
    virtual ~GeneratorOutput()=default;
    virtual IDXGISwapChain4* swapchain()=0;
    virtual uint32_t max_generated() const=0;
    // Records the frame's SDK work into the open bridge command list. inputs==nullptr presents the
    // frame without generation. false: the SDK refused the frame, which is presented as rendered.
    virtual bool record(ID3D12GraphicsCommandList* list,uint32_t presentId,const GenerationInputs* inputs,uint32_t generated)=0;
    virtual void before_present(uint32_t presentId)=0;
    virtual void after_present()=0;
    // Stops generation and waits for SDK work on the swapchain buffers (resize, fullscreen change, teardown).
    virtual void quiesce()=0;
    virtual void resized(UINT width,UINT height,DXGI_FORMAT format)=0;
    // The observed input description differs from the one this output was created for.
    virtual bool stale(uint32_t inputFlags) const=0;
};
// Bootstrap: the optional runtimes under Native/FidelityFX and Native/XeSS (vendor-signed only).
void load_vendor_runtimes();
void load_fsr_runtime();
void load_xess_runtime();
// On the private presentation device: fills backendsAvailable, the per-backend maxima and latencyAvailable.
void probe_vendor_backends(ID3D12Device*,uint32_t vendor);
void probe_fsr(ID3D12Device*);
void probe_xess(ID3D12Device*,uint32_t vendor);
std::unique_ptr<GeneratorOutput> create_fsr_output(const OutputParams&,std::string& error);
std::unique_ptr<GeneratorOutput> create_xess_output(const OutputParams&,std::string& error);
// Intel XeLL. The context lives for the process; XeSS-FG contexts must be destroyed before it.
void* xell_context();
bool xell_sleep(uint32_t frameId);
void xell_marker(uint32_t frameId,sl::PCLMarker);
// Changing the sleep mode requires an idle GPU; the bridge drains before calling this.
bool xell_apply(bool lowLatency,uint32_t minimumIntervalUs);
bool xell_mode_pending(bool lowLatency,uint32_t minimumIntervalUs);
// AMD Anti-Lag 2 (AMD GPUs with a supporting driver).
void antilag_probe(ID3D12Device*,uint32_t vendor);
void antilag_update(bool enabled,uint32_t maxFps);
void antilag_end_of_frame();
// FSR's swapchain reports interpolated/real frames to Anti-Lag 2 through this private data.
void antilag_tag_swapchain(IDXGISwapChain*,bool enabled);
} // namespace tgfg
