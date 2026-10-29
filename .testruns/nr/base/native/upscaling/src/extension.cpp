// SPDX-License-Identifier: MIT
// TG Better Upscaling as a TG Graphics Bridge extension. It needs the bridge's shared D3D12 device and the
// game's real D3D11 device, and never touches the game's swapchain.
#include "runtime.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace tgup {
namespace {
tgb::Host* bridgeHost=nullptr;
HMODULE self=nullptr;
std::wstring root; // BepInEx/TGBetterUpscaling/Native

class UpscalingExtension final : public tgb::Extension {
public:
    const char* name() noexcept override { return "TG Better Upscaling"; }
    uint32_t needs() noexcept override { return tgb::NeedD3D12; }
    bool bootstrap() noexcept override {
        try {
            log("TG Better Upscaling 1.2.1 extension bootstrap (TG Graphics Bridge); PID="+std::to_string(GetCurrentProcessId()));
            install_dlss_dll(root);
            const bool fsr=load_fsr_runtime(root), xess=load_xess_runtime(root);
            if(!fsr && !xess) { log("Neither AMD FSR nor Intel XeSS could be loaded; upscaling stays off"); return false; }
            return true;
        } catch(const std::exception& e) { log(std::string("Bootstrap exception: ")+e.what()); return false; }
          catch(...) { return false; }
    }
    void d3d12_device_ready(ID3D12Device* device, uint32_t vendor) noexcept override {
        try { upscaler_device_ready(device, vendor); }
        catch(const std::exception& e) { log(std::string("Capability probe: ")+e.what()); }
        catch(...) {}
    }
};
} // namespace

tgb::Host& host() { return *bridgeHost; }
std::wstring native_root() { return root; }
void log(const std::string& message) {
    static std::mutex logMutex;
    // Never throws: it is called from catch handlers, some of them in noexcept functions.
    try {
        std::lock_guard<std::mutex> guard(logMutex);
        const auto line="[TGUP] "+message+"\n";
        OutputDebugStringA(line.c_str());
        if(!root.empty()) {
            std::ofstream file(std::filesystem::path(root)/L"TGBetterUpscaling.Native.log", std::ios::app);
            file<<GetTickCount64()<<" "<<line;
        }
    } catch(...) {}
}
void log_throttled(const std::string& message) {
    static std::mutex throttleMutex; static std::string last; static uint64_t lastTime=0;
    try {
        std::lock_guard<std::mutex> guard(throttleMutex);
        const uint64_t now=GetTickCount64();
        if(message==last && now-lastTime<5000) return;
        last=message; lastTime=now;
    } catch(...) { return; }
    log(message);
}
} // namespace tgup

extern "C" __declspec(dllexport) tgb::Extension* __cdecl TGGraphicsBridge_CreateExtension(tgb::Host* host, uint32_t abi) {
    static tgup::UpscalingExtension* extension=nullptr;
    if(!host || abi!=TGB_EXTENSION_ABI || host->abi()!=TGB_EXTENSION_ABI) return nullptr;
    // No exception crosses into the bridge, which has a C runtime of its own.
    try {
        if(!extension) {
            tgup::bridgeHost=host;
            // Heap-grown module path: never a 64 KB stack buffer.
            std::vector<wchar_t> buffer(MAX_PATH);
            for(;;) {
                const DWORD length=GetModuleFileNameW(tgup::self, buffer.data(), DWORD(buffer.size()));
                if(length && length<buffer.size()) { tgup::root=std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().wstring(); break; }
                if(!length || buffer.size()>=32768) break;
                buffer.resize(buffer.size()*2);
            }
            extension=new tgup::UpscalingExtension();
        }
    } catch(...) { return nullptr; }
    return extension;
}
TGUP_EXPORT uint32_t TGUP_CALL TGUP_GetAbiVersion() { return TGUP_ABI; }
TGUP_EXPORT int32_t TGUP_CALL TGUP_ReadStatus(TGUP_Status* out) {
    if(!out || out->size!=sizeof(TGUP_Status) || out->abi!=TGUP_ABI) return -1;
    try { return tgup::upscaler_read_status(*out); } catch(...) { return -2; }
}
TGUP_EXPORT int32_t TGUP_CALL TGUP_GetFsrVersionName(uint32_t index, char* buffer, uint32_t size) {
    if(!buffer || !size) return -1;
    try {
        if(index>=tgup::fsr_version_count()) return -2;
        const auto name=tgup::fsr_version_name(index);
        const size_t count=std::min<size_t>(name.size(), size-1);
        std::memcpy(buffer, name.data(), count); buffer[count]=0;
        return int32_t(count);
    } catch(...) { return -3; }
}
TGUP_EXPORT int32_t TGUP_CALL TGUP_SubmitFrame(const TGUP_Frame* frame) {
    if(!frame) return -1;
    try { return tgup::upscaler_submit(*frame); } catch(...) { return -10; }
}
TGUP_EXPORT int32_t TGUP_CALL TGUP_SetOutput(uint32_t sequence, uint64_t output) {
    try { return tgup::upscaler_set_output(sequence, output); } catch(...) { return -10; }
}
TGUP_EXPORT void* TGUP_CALL TGUP_GetRenderEvent() { return tgup::upscaler_render_event(); }
TGUP_EXPORT int32_t TGUP_CALL TGUP_GetDlssDll(uint64_t* loaded, uint64_t* requested, uint64_t* bundled) {
    uint64_t l=0, r=0, b=0;
    uint32_t source=0;
    try { source=tgup::dlss_dll(l, r, b); } catch(...) { return -2; }
    if(loaded) *loaded=l;
    if(requested) *requested=r;
    if(bundled) *bundled=b;
    return int32_t(source);
}
TGUP_EXPORT int32_t TGUP_CALL TGUP_GetDlssOverride(TGUP_DlssOverride* out) {
    if(!out) return -1;
    try { tgup::dlss_override(*out); return 0; } catch(...) { return -2; }
}
TGUP_EXPORT int32_t TGUP_CALL TGUP_GetModes(uint32_t backend, TGUP_Mode* modes, uint32_t capacity) {
    if(!modes && capacity) return -1;
    try {
        const auto list=backend==TGUP_BACKEND_FSR ? tgup::fsr_modes() : backend==TGUP_BACKEND_XESS ? tgup::xess_modes() : std::vector<TGUP_Mode>();
        for(size_t i=0;i<list.size() && i<capacity;++i) modes[i]=list[i];
        return int32_t(list.size());
    } catch(...) { return -2; }
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    // No DisableThreadLibraryCalls: the static C runtime needs thread notifications.
    if(reason==DLL_PROCESS_ATTACH) tgup::self=module;
    // Never load runtimes or touch the GPU under loader lock.
    return TRUE;
}
