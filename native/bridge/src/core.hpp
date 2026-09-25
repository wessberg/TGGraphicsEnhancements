// SPDX-License-Identifier: MIT
#pragma once
#include "tgb/bridge_api.hpp"
#include "tgb/bridge_c_api.h"
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace tgb {
using Microsoft::WRL::ComPtr;

struct ExtensionRecord {
    std::wstring path;
    HMODULE module=nullptr;
    Extension* extension=nullptr;
    std::string name;
    uint32_t needs=0;
    bool active=false;
};

struct Core {
    std::recursive_mutex mutex;
    HMODULE self=nullptr;
    std::wstring root;    // BepInEx/TGGraphicsBridge: the bridge's log, whichever mod's copy runs
    std::wstring bepinex; // BepInEx
    std::vector<ExtensionRecord> extensions;
    bool bootstrapAttempted=false;
    uint32_t bootstrapResult=0;
    uint32_t needs=0;
    int presentation=-1, deviceCreator=-1;
    // Every game device the hooks saw stays referenced: extensions borrow the current one, and a
    // generation change must never free a device another thread is still recording on.
    std::vector<ComPtr<ID3D11Device5>> gameDevices;
    std::vector<ComPtr<ID3D11DeviceContext4>> gameContexts;
    std::atomic<ID3D11Device5*> gameDevice{nullptr};
    std::atomic<ID3D11DeviceContext4*> gameContext{nullptr};
    std::atomic<uint32_t> generation{0};
    HWND gameWindow=nullptr;
    ComPtr<ID3D12Device> device;
    std::atomic<ID3D12Device*> devicePtr{nullptr};
    std::atomic<uint32_t> vendor{0};
    // D3D12 device creation is tried at up to kDeviceAttempts game swapchain creations; never again
    // after a device on another adapter.
    int deviceAttempts=0; bool deviceRefused=false;
    TGB_Status status{sizeof(TGB_Status), TGB_ABI};
};

Core& core();
Host& host();
extern thread_local bool internalCall;
struct InternalGuard {
    bool previous;
    InternalGuard() : previous(internalCall) { internalCall=true; }
    ~InternalGuard() { internalCall=previous; }
};

void log(const std::string&);
// For conditions that can repeat every frame: an identical message at most every 5 s.
void log_throttled(const std::string&);
std::wstring module_path(HMODULE);
std::wstring system_directory();
std::string narrow(const std::wstring&);
bool trusted_signer(const std::wstring& path, const wchar_t* vendor);
HMODULE load_signed(const std::wstring& path, const wchar_t* vendor);

bool initialize_hooks();
bool add_hook(void* target, void* detour, void** original);
bool add_vtable_hook(void* object, size_t slot, void* detour);
void* vtable_original(void* object, size_t slot);
HRESULT create_native_factory(REFIID iid, void** out);
bool prime_swapchain_hooks();
bool install_graphics_hooks();

bool is_game_window(HWND window, UINT width, UINT height);
// The game creates a swapchain for window with device. S_OK: a presentation provider replaced it;
// S_FALSE: create the game's own; failure: logged, create the game's own.
HRESULT on_game_swapchain(IUnknown* device, IDXGIFactory* factory, HWND window, const DXGI_SWAP_CHAIN_DESC1& desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* restrictTo, IDXGISwapChain1** out);
} // namespace tgb
