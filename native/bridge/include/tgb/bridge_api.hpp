// SPDX-License-Identifier: MIT
// TG Graphics Bridge extension interface.
//
// The bridge (TGGraphicsBridge.Native.dll) owns what every graphics mod built on it shares and what
// may exist only once in the process: the MinHook detours, the DXGI and D3D11 swapchain-creation
// hooks, the game's real D3D11 device (never a wrapper's proxy), and one private D3D12 device on the
// game's adapter. It creates that device inside the game's own swapchain creation, where ReShade does
// not attach, so injectors never wrap it or see the resources made on it.
//
// An extension is a mod's native DLL, listed by that mod's graphics-bridge-extension.json. The
// bridge loads it at bootstrap and calls TGGraphicsBridge_CreateExtension. Extensions and the bridge
// are built by the same toolchain from this repository; the interface is C++ virtual calls and never
// lets an exception cross it. Extensions live for the process and are never unloaded.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstddef>
#include <cstdint>

#define TGB_EXTENSION_ABI 1u
#define TGB_CREATE_EXTENSION "TGGraphicsBridge_CreateExtension"

namespace tgb {
// What an extension needs from the bridge. The bridge creates the D3D12 device only when some
// extension asks for it, and hands the game's swapchain creation to at most one presentation provider.
enum Need : uint32_t {
    NeedD3D12 = 1u,        // the shared private D3D12 device on the game's adapter
    NeedPresentation = 2u  // replaces the game's swapchain (frame generation); the first such extension wins
};

class Host {
public:
    virtual uint32_t abi() const noexcept = 0;
    // One line in TGGraphicsBridge.Native.log, prefixed with the source. Thread-safe.
    virtual void log(const char* source, const char* message) noexcept = 0;
    // MinHook detours, one registry for the process. Idempotent per target and detour; a second,
    // different detour on the same target is refused rather than chained.
    virtual bool add_hook(void* target, void* detour, void** original) noexcept = 0;
    virtual bool add_vtable_hook(void* object, size_t slot, void* detour) noexcept = 0;
    virtual void* vtable_original(void* object, size_t slot) noexcept = 0;
    // Per thread: while set, the bridge's swapchain and factory hooks pass every call through. An
    // extension sets it around its own DXGI and D3D12 work (Streamline's proxies create swapchains
    // through the same factories the game uses). Returns the previous value.
    virtual bool set_internal(bool internal) noexcept = 0;
    virtual bool internal() noexcept = 0;
    // A factory straight from the system dxgi.dll, never a Streamline or overlay proxy.
    virtual HRESULT create_native_factory(REFIID iid, void** out) noexcept = 0;
    // WinVerifyTrust plus the leaf signer's display name (upper case) containing vendor.
    virtual bool trusted_signer(const wchar_t* path, const wchar_t* vendor) noexcept = 0;
    // A vendor-signed DLL loaded by full path; null when unsigned, missing or failing to load.
    virtual HMODULE load_signed(const wchar_t* path, const wchar_t* vendor) noexcept = 0;
    // The game's real D3D11 device and immediate context, from the first swapchain it created for its
    // window; null before that. Borrowed: they stay alive while the generation number is unchanged.
    virtual ID3D11Device5* game_device() noexcept = 0;
    virtual ID3D11DeviceContext4* game_context() noexcept = 0;
    virtual uint32_t game_device_generation() noexcept = 0;
    // The shared D3D12 device (native, never a Streamline proxy), or null. Borrowed, process lifetime.
    virtual ID3D12Device* d3d12_device() noexcept = 0;
    virtual uint32_t gpu_vendor() noexcept = 0; // PCI vendor ID of the game's adapter, 0 until known
    // The BepInEx folder, for extensions that report paths.
    virtual const wchar_t* bepinex_root() noexcept = 0;
};

class Extension {
public:
    virtual const char* name() noexcept = 0;
    virtual uint32_t needs() noexcept = 0;
    // Once, before the bridge installs its hooks and before the game creates a device. Loads runtimes
    // and installs the extension's own hooks. false leaves the extension inactive for the session.
    virtual bool bootstrap() noexcept = 0;
    // The first extension that returns true creates the shared D3D12 device (frame generation creates
    // it through NVIDIA Streamline so DLSS-G and Reflex bind to it). Return the native device.
    virtual bool create_d3d12_device(IDXGIAdapter*, uint32_t /*vendor*/, ID3D12Device** /*device*/) noexcept { return false; }
    // The shared device exists, whoever created it. Called on every extension that needs it.
    virtual void d3d12_device_ready(ID3D12Device*, uint32_t /*vendor*/) noexcept {}
    // Presentation provider only: S_OK replaced the swapchain, S_FALSE declined, a failure is logged
    // and the game's own swapchain is created.
    virtual HRESULT create_swapchain(IUnknown* /*device*/, IDXGIFactory* /*factory*/, HWND /*window*/,
        const DXGI_SWAP_CHAIN_DESC1& /*desc*/, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* /*fullscreen*/,
        IDXGIOutput* /*restrictTo*/, IDXGISwapChain1** /*out*/) noexcept { return S_FALSE; }
protected:
    ~Extension() = default; // never deleted through the interface
};

using CreateExtensionFn = Extension* (__cdecl*)(Host* host, uint32_t abi);

// Marks the calling thread as doing internal graphics work for the scope's lifetime.
class InternalScope {
    Host* host_; bool previous_;
public:
    explicit InternalScope(Host* host) : host_(host), previous_(host ? host->set_internal(true) : false) {}
    ~InternalScope() { if(host_) host_->set_internal(previous_); }
    InternalScope(const InternalScope&) = delete; InternalScope& operator=(const InternalScope&) = delete;
};
} // namespace tgb
