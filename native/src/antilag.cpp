// SPDX-License-Identifier: MIT
// AMD Anti-Lag 2 (header-only SDK; the implementation is the AMD D3D12 driver extension). It delays
// the start of the next frame just before input sampling, like Reflex's sleep.
#include "fg_backends.hpp"
#include <ffx_antilag2_dx12.h>
namespace tgfg {
namespace {
AMD::AntiLag2DX12::Context context{};
bool ready=false;
}
void antilag_probe(ID3D12Device* device,uint32_t vendor) {
    if(vendor!=0x1002) return;
    const HRESULT result=AMD::AntiLag2DX12::Initialize(&context,device);
    if(result!=S_OK) { log("AMD Anti-Lag 2 is not available with this driver; HRESULT="+std::to_string(result)); return; }
    ready=true; state().status.latencyAvailable|=1u<<uint32_t(Latency::AntiLag2);
    log("AMD Anti-Lag 2 available");
}
void antilag_update(bool enabled,uint32_t maxFps) {
    // Once per frame before input sampling; disabled calls only record the state change.
    if(ready) AMD::AntiLag2DX12::Update(&context,enabled,maxFps);
}
void antilag_end_of_frame() {
    if(ready) AMD::AntiLag2DX12::MarkEndOfFrameRendering(&context);
}
void antilag_tag_swapchain(IDXGISwapChain* chain,bool enabled) {
    if(!ready || !chain) return;
    // FSR's frame generation swapchain reads this each Present and reports interpolated frames to Anti-Lag 2.
    struct { AMD::AntiLag2DX12::Context* context; bool enabled; } data{&context,enabled};
    static const GUID antiLag2Data={0x5083ae5b,0x8070,0x4fca,{0x8e,0xe5,0x35,0x82,0xdd,0x36,0x7d,0x13}};
    chain->SetPrivateData(antiLag2Data,sizeof(data),&data);
}
} // namespace tgfg
