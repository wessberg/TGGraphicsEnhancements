// SPDX-License-Identifier: MIT
// AMD Anti-Lag 2 (header-only SDK; the implementation is the AMD D3D12 driver extension). It delays
// the start of the next frame just before input sampling, like Reflex's sleep.
#include "fg_backends.hpp"
#include <ffx_antilag2_dx12.h>
#include <atomic>
#include <cstdio>
namespace tgfg {
namespace {
AMD::AntiLag2DX12::Context context{};
bool ready=false;
std::atomic<uint64_t> failures{0};
// A call the driver refused: counted into the report's facts and logged at most every 5 s, so an
// Anti-Lag 2 that initialised and then stopped working is visible rather than reported as active.
void note(const char* what,HRESULT hr) {
    if(SUCCEEDED(hr)) return;
    char code[16]{}; std::snprintf(code,sizeof code,"0x%08X",static_cast<unsigned>(hr));
    const uint64_t count=++failures;
    fact("antilag2.errors",std::to_string(count)+", last "+what+" "+code);
    log_throttled(std::string("AMD Anti-Lag 2 ")+what+" returned "+code);
}
}
void antilag_probe(ID3D12Device* device,uint32_t vendor) {
    if(vendor!=0x1002) { fact("antilag2","not-amd"); return; }
    const HRESULT result=AMD::AntiLag2DX12::Initialize(&context,device);
    if(result!=S_OK) { fact("antilag2","unavailable "+std::to_string(result)); log("AMD Anti-Lag 2 is not available with this driver; HRESULT="+std::to_string(result)); return; }
    ready=true; state().status.latencyAvailable|=1u<<uint32_t(Latency::AntiLag2);
    fact("antilag2","available"); log("AMD Anti-Lag 2 available");
}
void antilag_update(bool enabled,uint32_t maxFps) {
    // Once per frame before input sampling; disabled calls only record the state change.
    if(ready) note("Update",AMD::AntiLag2DX12::Update(&context,enabled,maxFps));
}
void antilag_end_of_frame() {
    if(ready) note("MarkEndOfFrameRendering",AMD::AntiLag2DX12::MarkEndOfFrameRendering(&context));
}
void antilag_tag_swapchain(IDXGISwapChain* chain,bool enabled) {
    if(!ready || !chain) return;
    // FSR's frame generation swapchain reads this each Present and reports interpolated frames to Anti-Lag 2.
    struct { AMD::AntiLag2DX12::Context* context; bool enabled; } data{&context,enabled};
    static const GUID antiLag2Data={0x5083ae5b,0x8070,0x4fca,{0x8e,0xe5,0x35,0x82,0xdd,0x36,0x7d,0x13}};
    note("swapchain tag",chain->SetPrivateData(antiLag2Data,sizeof(data),&data));
}
} // namespace tgfg
