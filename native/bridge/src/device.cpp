// SPDX-License-Identifier: MIT
// The game's D3D11 device, as the swapchain hooks see it (the real device, underneath any wrapper),
// and the one private D3D12 device every extension shares.
#include "core.hpp"
#include "diagnostics.hpp"
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace tgb {
namespace {
std::string hex(uint32_t value) { std::ostringstream s; s<<std::hex<<std::uppercase<<value; return s.str(); }
constexpr int kDeviceAttempts=3;
// Called under the core mutex.
void observe_game_device(IUnknown* object, HWND window) {
    auto& c=core();
    ComPtr<ID3D11Device> device; if(FAILED(object->QueryInterface(IID_PPV_ARGS(&device)))) return; // not a D3D11 swapchain
    ComPtr<ID3D11Device5> device5;
    if(FAILED(device.As(&device5))) { log_throttled("The game's D3D11 device has no ID3D11Device5; shared fences are unavailable"); return; }
    if(c.gameDevice.load()==device5.Get()) { c.gameWindow=window; note_display_window(window); return; }
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    ComPtr<ID3D11DeviceContext4> context4;
    if(!context || FAILED(context.As(&context4))) { log_throttled("The game's D3D11 immediate context has no ID3D11DeviceContext4"); return; }
    c.gameDevices.push_back(device5); c.gameContexts.push_back(context4);
    c.gameContext.store(context4.Get()); c.gameDevice.store(device5.Get());
    const auto generation=++c.generation;
    c.gameWindow=window; c.status.gameDeviceObserved=1; c.status.gameDeviceGeneration=generation;
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC desc{};
    if(SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&desc))) {
        c.vendor.store(desc.VendorId); c.status.gpuVendor=desc.VendorId;
        log("Game D3D11 device observed (generation "+std::to_string(generation)+"): "+narrow(desc.Description)+
            ", vendor 0x"+hex(desc.VendorId)+", feature level 0x"+hex(uint32_t(device->GetFeatureLevel())));
        set_fact("adapter.game.featureLevel", "0x"+hex(uint32_t(device->GetFeatureLevel())));
        note_adapter(adapter.Get(), desc);
        note_display(adapter.Get(), desc, window);
    } else log("Game D3D11 device observed (generation "+std::to_string(generation)+"); its adapter could not be read");
    if(generation>1) log("The game created a new D3D11 device; extensions rebuild the resources they shared with the previous one");
}
ComPtr<ID3D12Device> create_plain_device(IDXGIAdapter* adapter) {
    const auto path=std::filesystem::path(system_directory())/L"d3d12.dll";
    const HMODULE module=LoadLibraryW(path.c_str());
    if(!module) { log("Could not load the system d3d12.dll: error "+std::to_string(GetLastError())); return nullptr; }
    const auto create=reinterpret_cast<decltype(&::D3D12CreateDevice)>(GetProcAddress(module, "D3D12CreateDevice"));
    if(!create) { log("The system d3d12.dll exports no D3D12CreateDevice"); return nullptr; }
    ComPtr<ID3D12Device> device;
    InternalGuard own;
    const HRESULT hr=create(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if(FAILED(hr)) {
        core().status.lastError=hr; set_fact("d3d12.error", "0x"+hex(uint32_t(hr)));
        log("D3D12CreateDevice on the game's adapter failed: 0x"+hex(uint32_t(hr))); return nullptr;
    }
    return device;
}
// Called under the core mutex, from the game's swapchain creation.
bool create_d3d12_device_locked() {
    auto& c=core();
    if(c.device) return true;
    if(c.deviceRefused || c.deviceAttempts>=kDeviceAttempts) return false;
    if(!(c.needs&(NeedD3D12|NeedPresentation))) return false;
    auto* game=c.gameDevice.load();
    if(!game) return false;
    const int attempt=++c.deviceAttempts;
    const std::string retry=attempt<kDeviceAttempts ? "; the game's next swapchain creation tries again" : "; no further attempt this session";
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter;
    if(FAILED(game->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter))) { log("Cannot read the game's adapter; no D3D12 device"+retry); return false; }
    DXGI_ADAPTER_DESC desc{}; adapter->GetDesc(&desc);
    const uint32_t vendor=desc.VendorId;
    ComPtr<ID3D12Device> device;
    for(size_t i=0;i<c.extensions.size() && !device;++i) {
        auto& e=c.extensions[i];
        if(!e.active) continue;
        ID3D12Device* created=nullptr;
        if(e.extension->create_d3d12_device(adapter.Get(), vendor, &created) && created) {
            device.Attach(created); c.deviceCreator=int(i); c.status.deviceCreator=int32_t(i);
            log("Shared D3D12 device created by "+e.name);
        }
    }
    if(!device) {
        device=create_plain_device(adapter.Get());
        if(device) log("Shared D3D12 device created on the game's adapter");
    }
    if(!device) {
        set_fact("d3d12.device", "failed "+std::to_string(attempt)+" of "+std::to_string(kDeviceAttempts));
        log("No D3D12 device (attempt "+std::to_string(attempt)+" of "+std::to_string(kDeviceAttempts)+")"+retry); return false;
    }
    const auto luid=device->GetAdapterLuid();
    if(luid.HighPart!=desc.AdapterLuid.HighPart || luid.LowPart!=desc.AdapterLuid.LowPart) {
        c.deviceRefused=true; set_fact("d3d12.device", "other adapter");
        log("The D3D12 device is not on the game's adapter; it is not used"); return false;
    }
    set_fact("d3d12.device", "ready");
    c.device=device; c.devicePtr.store(device.Get());
    c.status.d3d12DeviceReady=1;
    for(auto& e:c.extensions)
        if(e.active && (e.needs&(NeedD3D12|NeedPresentation))) e.extension->d3d12_device_ready(device.Get(), vendor);
    return true;
}
} // namespace

bool is_game_window(HWND window, UINT width, UINT height) {
    DWORD pid=0; if(!window || !GetWindowThreadProcessId(window, &pid) || pid!=GetCurrentProcessId()) return false;
    if(GetWindowLongPtrW(window, GWL_STYLE)&WS_CHILD) return false;
    RECT r{}; if(!GetClientRect(window, &r)) return false;
    if(!width) width=UINT(std::max<LONG>(0, r.right-r.left)); if(!height) height=UINT(std::max<LONG>(0, r.bottom-r.top));
    return width>=320 && height>=200; // skip runtime probe windows; first eligible game HWND only
}
HRESULT on_game_swapchain(IUnknown* device, IDXGIFactory* factory, HWND window, const DXGI_SWAP_CHAIN_DESC1& desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* restrictTo, IDXGISwapChain1** out) {
    if(!out) return E_POINTER;
    if(internalCall || !device || !factory || !is_game_window(window, desc.Width, desc.Height)) return S_FALSE;
    auto& c=core();
    Extension* provider=nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(c.mutex);
        // A bootstrap that failed partway can leave some hooks installed: the game keeps its own swapchain.
        if(c.status.bootstrapped!=1 || !c.needs) return S_FALSE;
        ++c.status.swapchainsSeen;
        observe_game_device(device, window);
        // Here, inside the game's own swapchain creation, a wrapper such as ReShade is already in
        // its own call and passes the D3D12 device creation through untouched.
        create_d3d12_device_locked();
        if(c.presentation>=0) provider=c.extensions[size_t(c.presentation)].extension;
    }
    if(!provider) return S_FALSE;
    // Outside the core lock: the provider takes its own locks, which its render thread also holds
    // while calling back into the host.
    const HRESULT hr=provider->create_swapchain(device, factory, window, desc, fullscreen, restrictTo, out);
    if(hr==S_OK) { std::lock_guard<std::recursive_mutex> lock(c.mutex); ++c.status.swapchainsProvided; }
    else if(FAILED(hr)) { std::lock_guard<std::recursive_mutex> lock(c.mutex); c.status.lastError=hr; }
    return hr;
}
} // namespace tgb
