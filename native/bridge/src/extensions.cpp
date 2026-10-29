// SPDX-License-Identifier: MIT
// The host side of the extension interface, and the loading of the mods' extension DLLs.
#include "core.hpp"
#include "agility.hpp"
#include "diagnostics.hpp"
#include <filesystem>

namespace tgb {
namespace {
class HostImpl final : public Host {
public:
    uint32_t abi() const noexcept override { return TGB_EXTENSION_ABI; }
    void log(const char* source, const char* message) noexcept override {
        try { tgb::log(std::string("[")+(source?source:"?")+"] "+(message?message:"")); } catch(...) {}
    }
    bool add_hook(void* target, void* detour, void** original) noexcept override {
        try { return tgb::add_hook(target, detour, original); } catch(...) { return false; }
    }
    bool add_vtable_hook(void* object, size_t slot, void* detour) noexcept override {
        try { return tgb::add_vtable_hook(object, slot, detour); } catch(...) { return false; }
    }
    void* vtable_original(void* object, size_t slot) noexcept override {
        try { return tgb::vtable_original(object, slot); } catch(...) { return nullptr; }
    }
    bool set_internal(bool value) noexcept override { const bool previous=internalCall; internalCall=value; return previous; }
    bool internal() noexcept override { return internalCall; }
    HRESULT create_native_factory(REFIID iid, void** out) noexcept override {
        try { return tgb::create_native_factory(iid, out); } catch(...) { return E_FAIL; }
    }
    bool trusted_signer(const wchar_t* path, const wchar_t* vendor) noexcept override {
        try { return path && vendor && tgb::trusted_signer(path, vendor); } catch(...) { return false; }
    }
    HMODULE load_signed(const wchar_t* path, const wchar_t* vendor) noexcept override {
        try { return path && vendor ? tgb::load_signed(path, vendor) : nullptr; } catch(...) { return nullptr; }
    }
    ID3D11Device5* game_device() noexcept override { return core().gameDevice.load(); }
    ID3D11DeviceContext4* game_context() noexcept override { return core().gameContext.load(); }
    uint32_t game_device_generation() noexcept override { return core().generation.load(); }
    ID3D12Device* d3d12_device() noexcept override { return core().devicePtr.load(); }
    uint32_t gpu_vendor() noexcept override { return core().vendor.load(); }
    const wchar_t* bepinex_root() noexcept override { return core().bepinex.c_str(); }
};

void load_extension(ExtensionRecord& record) {
    const auto file=narrow(std::filesystem::path(record.path).filename().wstring());
    record.module=LoadLibraryExW(record.path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!record.module) {
        log("Could not load extension "+file+": error "+std::to_string(GetLastError())+
            " (126 can mean a missing dependency, not a missing file)");
        return;
    }
    const auto create=reinterpret_cast<CreateExtensionFn>(GetProcAddress(record.module, TGB_CREATE_EXTENSION));
    if(!create) { log("Extension "+file+" exports no " TGB_CREATE_EXTENSION "; it is not a TG Graphics Bridge extension of this version"); return; }
    record.extension=create(&host(), TGB_EXTENSION_ABI);
    if(!record.extension) { log("Extension "+file+" refused TG Graphics Bridge extension ABI "+std::to_string(TGB_EXTENSION_ABI)); return; }
    record.name=record.extension->name() ? record.extension->name() : file;
    record.needs=record.extension->needs();
    // Hooks and runtime state reference the extension for the rest of the process.
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(create), &pinned);
    record.active=record.extension->bootstrap();
    log(std::string("Extension ")+record.name+" ("+file+") "+(record.active ? "active" : "inactive after its bootstrap")+
        "; needs "+(record.needs&NeedPresentation ? "presentation, " : "")+(record.needs&NeedD3D12 ? "the D3D12 device" : "no D3D12 device"));
}
} // namespace

Host& host() { static HostImpl instance; return instance; }

int32_t add_extension(const wchar_t* path) {
    auto& c=core(); std::lock_guard<std::recursive_mutex> lock(c.mutex);
    if(c.bootstrapAttempted) return -2;
    if(!path || !*path) return -1;
    const std::filesystem::path file(path);
    if(!file.is_absolute()) return -3;
    for(const auto& e:c.extensions) if(_wcsicmp(e.path.c_str(), path)==0) return 0;
    if(c.extensions.size()>=16) return -4;
    ExtensionRecord record; record.path=file.wstring();
    c.extensions.push_back(record);
    c.status.extensionsRegistered=uint32_t(c.extensions.size());
    return 0;
}

uint32_t bootstrap() {
    auto& c=core(); std::lock_guard<std::recursive_mutex> lock(c.mutex);
    c.status.nativeLoadPid=GetCurrentProcessId();
    if(c.bootstrapAttempted) return c.bootstrapResult;
    c.bootstrapAttempted=true;
    c.bootstrapResult=2;
    c.status.presentationProvider=-1; c.status.deviceCreator=-1; // -1: none, and the bridge's own device
    // Each mod bundles the bridge (BepInEx/<mod>/Native/TGGraphicsBridge.Native.dll) and one copy
    // runs; its log goes to BepInEx/TGGraphicsBridge whichever mod's copy that is.
    const auto self=std::filesystem::path(module_path(c.self));
    c.bepinex=self.parent_path().parent_path().parent_path().wstring();
    c.root=(std::filesystem::path(c.bepinex)/L"TGGraphicsBridge").wstring();
    { std::error_code ignored; std::filesystem::create_directories(c.root, ignored); }
    // The BepInEx root is found from where the DLL sits, so a copy anywhere else says so.
    if(_wcsicmp(self.parent_path().filename().c_str(), L"Native")!=0 || _wcsicmp(std::filesystem::path(c.bepinex).filename().c_str(), L"BepInEx")!=0)
        log("TGGraphicsBridge.Native.dll is not in BepInEx/<mod>/Native ("+self.u8string()+"); its log and the BepInEx root it reports assume it is");
    log("TG Graphics Bridge 1.2.3 bootstrap; PID="+std::to_string(GetCurrentProcessId())+"; "+
        std::to_string(c.extensions.size())+" extension(s) registered; running "+self.parent_path().parent_path().filename().u8string()+"'s copy");
    note_windows();
    // Before the extensions load their runtimes, the first to load d3d12.dll.
    {
        std::vector<std::wstring> paths;
        for(const auto& e:c.extensions) paths.push_back(e.path);
        log(align_agility_sdk(paths));
    }
    // Extensions install their own detours during their bootstrap, so the registry comes first.
    if(!initialize_hooks()) { log("MinHook initialization failed; no extension is loaded"); c.status.bootstrapped=2; return c.bootstrapResult; }
    // Before the extensions load their runtimes, which may load dxgi.dll and so let other hooks in first.
    if(!c.extensions.empty() && !prime_swapchain_hooks())
        log("The swapchain methods could not be hooked up front; only factories the game creates from now on are covered");
    for(auto& e:c.extensions) {
        load_extension(e);
        if(e.active) { ++c.status.extensionsActive; c.needs|=e.needs; }
        else ++c.status.extensionsFailed;
    }
    for(size_t i=0;i<c.extensions.size();++i) {
        const auto& e=c.extensions[i];
        if(!e.active || !(e.needs&NeedPresentation)) continue;
        if(c.presentation<0) { c.presentation=int(i); log("Presentation provider: "+e.name); }
        else log("Extension "+e.name+" also wants to replace the game's swapchain; "+c.extensions[size_t(c.presentation)].name+" keeps it");
    }
    c.status.needs=c.needs; c.status.presentationProvider=c.presentation;
    if(!c.needs) {
        log("No active extension needs the graphics hooks; the swapchain hooks stay idle and the game renders untouched");
        c.status.bootstrapped=1; c.bootstrapResult=0;
    } else if(!install_graphics_hooks()) {
        log("Installing the swapchain-creation hooks failed; the extensions cannot reach the game's device");
        c.status.bootstrapped=2; c.bootstrapResult=3; return c.bootstrapResult;
    } else { c.status.bootstrapped=1; c.bootstrapResult=0; }
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&bootstrap), &pinned)) log("Could not pin TGGraphicsBridge.Native.dll");
    return c.bootstrapResult;
}
} // namespace tgb
