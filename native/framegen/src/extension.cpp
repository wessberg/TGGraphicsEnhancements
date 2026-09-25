// SPDX-License-Identifier: MIT
// TG Frame Generation as a TG Graphics Bridge extension. The bridge loads this DLL at startup, creates
// the shared D3D12 device through it (so NVIDIA Streamline binds to that device on NVIDIA GPUs), and
// hands it the game's swapchain creation: frame generation presents through its own D3D12 swapchain.
#include "sl_runtime.hpp"
#include "dx11_bridge.hpp"
#include "fg_backends.hpp"
#include <sstream>

namespace tgfg {
bool bootstrap_extension();
namespace {
tgb::Host* bridgeHost=nullptr;
std::string hex(HRESULT hr) { std::ostringstream s; s<<"0x"<<std::hex<<std::uppercase<<uint32_t(hr); return s.str(); }

class FrameGenerationExtension final : public tgb::Extension {
public:
    const char* name() noexcept override { return "TG Frame Generation"; }
    uint32_t needs() noexcept override { return tgb::NeedD3D12|tgb::NeedPresentation; }
    bool bootstrap() noexcept override {
        try { return bootstrap_extension(); } catch(...) { return false; }
    }
    // Streamline hosts DLSS-G and Reflex on NVIDIA GPUs, and only on a device created through its own
    // D3D12CreateDevice. Everywhere else the bridge's plain device serves FSR, XeSS and the other
    // latency technologies.
    bool create_d3d12_device(IDXGIAdapter* adapter,uint32_t vendor,ID3D12Device** out) noexcept override {
        try {
            auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if(!out || !s.initialized || vendor!=0x10DE || s.device) return false;
            ComPtr<ID3D12Device> proxy;
            HRESULT hr;
            { SLGuard inside; hr=s.sl.createDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&proxy)); }
            if(FAILED(hr) || !proxy) {
                s.status.lastInteropHresult=FAILED(hr) ? hr : E_POINTER;
                fact("streamline.device",hex(s.status.lastInteropHresult));
                log("Streamline D3D12CreateDevice failed ("+hex(s.status.lastInteropHresult)+"); the bridge creates a plain device, "
                    "so DLSS Frame Generation and Reflex are unavailable this session");
                return false;
            }
            on_device(proxy.Get()); // binds Streamline and keeps the native device underneath the proxy
            if(!s.device) return false;
            if(s.slReady) s.bridgeDeviceProxy=proxy;
            *out=s.device.Get(); (*out)->AddRef();
            return true;
        } catch(const std::exception& e) { log(std::string("Streamline device creation: ")+e.what()); return false; }
          catch(...) { return false; }
    }
    void d3d12_device_ready(ID3D12Device* device,uint32_t vendor) noexcept override {
        try {
            auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if(!s.device) s.device=device;
            s.status.gpuVendor=vendor;
            s.deviceReady=true;
            // The probes create DXGI factories and devices of their own, which the bridge's hooks must
            // pass through, as for every other call this extension makes into DXGI.
            try { SLGuard inside; probe_vendor_backends(s.device.Get(),vendor); }
            catch(const std::exception& e) { log(std::string("Capability probe: ")+e.what()); }
            catch(...) { log("Capability probe failed"); }
            s.probed=true;
            if(s.status.bridgeFlags&1u) { offer_reflex(true); s.status.flags|=4u; } // capabilities known (normally set when the swapchain is replaced)
        } catch(const std::exception& e) { log(std::string("D3D12 device handover: ")+e.what()); }
          catch(...) {}
    }
    HRESULT create_swapchain(IUnknown* device,IDXGIFactory* factory,HWND window,const DXGI_SWAP_CHAIN_DESC1& desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,IDXGIOutput* restrictTo,IDXGISwapChain1** out) noexcept override {
        try { return try_create_dx11_bridge(device,factory,window,desc,fullscreen,restrictTo,out); }
        catch(...) { return E_FAIL; }
    }
};
} // namespace

tgb::Host& host() { return *bridgeHost; }
} // namespace tgfg

extern "C" __declspec(dllexport) tgb::Extension* __cdecl TGGraphicsBridge_CreateExtension(tgb::Host* host,uint32_t abi) {
    static tgfg::FrameGenerationExtension* extension=nullptr;
    if(!host || abi!=TGB_EXTENSION_ABI || host->abi()!=TGB_EXTENSION_ABI) return nullptr;
    // No exception crosses into the bridge, which has a C runtime of its own.
    try { if(!extension) { tgfg::bridgeHost=host; extension=new tgfg::FrameGenerationExtension(); } }
    catch(...) { return nullptr; }
    return extension;
}
