// SPDX-License-Identifier: MIT
// Facts about this session that the mods explain to the player in their settings, the performance
// overlay and the troubleshooting report: the Windows build, the Direct3D 12 runtime the game asks for
// and why a D3D12 device failed, which GPU drives the game's display, and the NVIDIA driver settings
// that change frame pacing under frame generation (a frame rate limit, forced VSync, the driver's own
// frame generation, overrides of DLSS Frame Generation). Read as key=value lines through
// TGGraphicsBridge_ReadDiagnostics; each fact is also logged when it is first learned or changes.
//
// The NVAPI declarations below follow NVIDIA's nvapi.h, nvapi_interface.h and NvApiDriverSettings.h
// (github.com/NVIDIA/nvapi), which NVIDIA publishes under the MIT license.
#include "diagnostics.hpp"
#include "core.hpp"
#include <map>
#include <memory>
#include <sstream>
#include <thread>

namespace tgb {
namespace {
std::mutex factMutex;
std::map<std::string, std::string>& facts() { static auto* instance=new std::map<std::string, std::string>; return *instance; }
std::string hex(uint32_t value) { std::ostringstream s; s<<"0x"<<std::hex<<std::uppercase<<value; return s.str(); }
std::string one_line(std::string text) {
    for(auto& c:text) if(c=='\r' || c=='\n') c=' ';
    return text;
}
std::wstring registry_text(const wchar_t* value) {
    wchar_t buffer[256]{}; DWORD bytes=sizeof(buffer);
    if(RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", value, RRF_RT_REG_SZ, nullptr, buffer, &bytes)!=ERROR_SUCCESS) return {};
    return buffer;
}
DWORD registry_number(const wchar_t* value) {
    DWORD number=0, bytes=sizeof(number);
    if(RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", value, RRF_RT_REG_DWORD, nullptr, &number, &bytes)!=ERROR_SUCCESS) return 0;
    return number;
}
// A DLL's file version ("14.44.35211.0"), or "" when it is missing.
std::string dll_version(const std::wstring& path) {
    DWORD ignored=0; const DWORD bytes=GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if(!bytes) return {};
    std::unique_ptr<unsigned char[]> data(new unsigned char[bytes]);
    VS_FIXEDFILEINFO* info=nullptr; UINT size=0;
    if(!GetFileVersionInfoW(path.c_str(), 0, bytes, data.get()) ||
       !VerQueryValueW(data.get(), L"\\", reinterpret_cast<void**>(&info), &size) || size<sizeof(*info)) return {};
    return std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+
        std::to_string(HIWORD(info->dwFileVersionLS))+"."+std::to_string(LOWORD(info->dwFileVersionLS));
}
std::wstring system_path(const wchar_t* name) {
    wchar_t directory[MAX_PATH]{};
    const UINT length=GetSystemDirectoryW(directory, MAX_PATH);
    return length && length<MAX_PATH ? std::wstring(directory)+L"\\"+name : std::wstring();
}
// Next to the game's executable, where Windows looks before System32.
std::wstring game_folder_path(const wchar_t* name) {
    wchar_t exe[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if(!length || length>=MAX_PATH) return {};
    std::wstring path(exe, length);
    const size_t slash=path.find_last_of(L"\\/");
    return slash==std::wstring::npos ? std::wstring() : path.substr(0, slash+1)+name;
}
// The Visual C++ runtime DLL a vendor DLL loaded now would bind to: one already in the process, else
// the game folder's copy, else System32's. "" when there is none.
std::string runtime_version(const wchar_t* name, std::string& where) {
    if(const HMODULE loaded=GetModuleHandleW(name)) {
        wchar_t path[MAX_PATH]{};
        const DWORD length=GetModuleFileNameW(loaded, path, MAX_PATH);
        if(length && length<MAX_PATH) { where="loaded"; return dll_version(std::wstring(path, length)); }
    }
    const std::string local=dll_version(game_folder_path(name));
    if(!local.empty()) { where="game folder"; return local; }
    where="System32";
    return dll_version(system_path(name));
}

// NVAPI's driver settings (DRS). NVDRS_SETTING_V1 as the driver checks it: 12320 bytes. The current
// nvapi.h adds a 64-bit member to the value unions, which pads them to 12328 bytes under its 8-byte
// packing, and the driver (616.92 on an RTX 5080) refuses that size as an incompatible version.
using NvStatus=int32_t;
constexpr NvStatus NvOk=0, NvSettingNotFound=-160;
using NvUnicodeString=uint16_t[2048];
struct NvBinarySetting { uint32_t length; uint8_t data[4096]; };
#pragma pack(push, 4)
struct NvSetting {
    uint32_t version;
    NvUnicodeString name;
    uint32_t id, type, location, isCurrentPredefined, isPredefinedValid;
    union { uint32_t u32; NvBinarySetting binary; NvUnicodeString text; } predefined;
    union { uint32_t u32; NvBinarySetting binary; NvUnicodeString text; } current;
};
#pragma pack(pop)
static_assert(sizeof(NvSetting)==12320, "NVDRS_SETTING_V1 as the driver expects it");
struct NvApplication {                   // NVDRS_APPLICATION_V4
    uint32_t version, isPredefined;
    NvUnicodeString appName, userFriendlyName, launcher, fileInFolder;
    uint32_t flags;
    NvUnicodeString commandLine;
};
constexpr uint32_t nv_version(size_t size, uint32_t v) { return uint32_t(size)|(v<<16); } // MAKE_NVAPI_VERSION
using QueryInterface=void*(__cdecl*)(uint32_t);
using Initialize=NvStatus(__cdecl*)();
using CreateSession=NvStatus(__cdecl*)(void**);
using DestroySession=NvStatus(__cdecl*)(void*);
using LoadSettings=NvStatus(__cdecl*)(void*);
using FindApplication=NvStatus(__cdecl*)(void*, NvUnicodeString, void**, NvApplication*);
using BaseProfile=NvStatus(__cdecl*)(void*, void**);
using GetSetting=NvStatus(__cdecl*)(void*, void*, uint32_t, NvSetting*);
// The settings, by the name the fact carries. The IDs are NvApiDriverSettings.h's, except Smooth
// Motion's: NVIDIA's header doesn't name the driver's own frame generation, and the IDs here are the
// ones NVIDIA Profile Inspector lists for it.
struct DriverSetting { const char* key; uint32_t id; };
constexpr DriverSetting kDriverSettings[]={
    {"frameRateLimit", 0x10835002},          // FRL_FPS: Max Frame Rate, in fps, 0 off
    {"vsync", 0x00A879CF},                   // VSYNCMODE: 0x60925292 the application's choice
    {"maxPrerenderedFrames", 0x007BA09E},    // PRERENDERLIMIT: Low Latency Mode (1 = On)
    {"dlssgMode", 0x10308298},               // NGX_DLSSG_MODE: DLSS Frame Generation override
    {"dlssgMultiFrameCount", 0x104D6667},    // NGX_DLSSG_MULTI_FRAME_COUNT
    {"dlssgFgOverride", 0x10E41E03},         // NGX_DLSS_FG_OVERRIDE
    {"smoothMotion", 0xB0D384C0},            // NVIDIA Smooth Motion (Profile Inspector's ID)
    {"smoothMotionApis", 0xB0CC0875},        // the APIs Smooth Motion applies to (Profile Inspector's ID)
};
// A setting's value in the game's profile, or where that doesn't set it, in the base profile.
bool driver_setting(GetSetting get, void* session, void* app, void* base, uint32_t id, uint32_t& value, const char*& where, bool& readable) {
    auto s=std::make_unique<NvSetting>();
    const std::pair<void*, const char*> profiles[]={{app, "game"}, {base, "global"}};
    for(const auto& profile:profiles) {
        if(!profile.first) continue;
        *s=NvSetting{}; s->version=nv_version(sizeof(NvSetting), 1);
        const NvStatus result=get(session, profile.first, id, s.get());
        if(result==NvOk) { value=s->current.u32; where=profile.second; return true; }
        if(result!=NvSettingNotFound) readable=false;
    }
    return false;
}
void read_driver_settings() {
    const HMODULE nvapi=LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!nvapi) { set_fact("nvidia.drs", "no NVIDIA driver"); return; }
    const auto query=reinterpret_cast<QueryInterface>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    const auto init=query ? reinterpret_cast<Initialize>(query(0x0150E828)) : nullptr;
    const auto create=query ? reinterpret_cast<CreateSession>(query(0x0694D52E)) : nullptr;
    const auto destroy=query ? reinterpret_cast<DestroySession>(query(0xDAD9CFF8)) : nullptr;
    const auto load=query ? reinterpret_cast<LoadSettings>(query(0x375DBD6B)) : nullptr;
    const auto find=query ? reinterpret_cast<FindApplication>(query(0xEEE566B2)) : nullptr;
    const auto baseProfile=query ? reinterpret_cast<BaseProfile>(query(0xDA8466A0)) : nullptr;
    const auto get=query ? reinterpret_cast<GetSetting>(query(0x73BF8338)) : nullptr;
    if(!init || !create || !destroy || !load || !find || !baseProfile || !get || init()!=NvOk) {
        set_fact("nvidia.drs", "unavailable"); return;
    }
    std::wstring exe(MAX_PATH, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size())));
    const std::wstring name=exe.substr(exe.find_last_of(L"\\/")+1);
    std::map<std::string, std::string> last;
    for(;;) {
        std::map<std::string, std::string> now;
        void* session=nullptr;
        bool readable=true;
        if(create(&session)==NvOk && session) {
            if(load(session)==NvOk) {
                auto app=std::make_unique<NvApplication>(); app->version=nv_version(sizeof(NvApplication), 4);
                NvUnicodeString appName{};
                for(size_t i=0;i<name.size() && i<2047;++i) appName[i]=uint16_t(name[i]);
                void* appProfile=nullptr; void* base=nullptr;
                if(find(session, appName, &appProfile, app.get())!=NvOk) appProfile=nullptr;
                if(baseProfile(session, &base)!=NvOk) base=nullptr;
                for(const auto& setting:kDriverSettings) {
                    uint32_t value=0; const char* where="";
                    now[setting.key]=driver_setting(get, session, appProfile, base, setting.id, value, where, readable)
                        ? hex(value)+"@"+where : "unset";
                }
            } else readable=false;
            destroy(session);
        } else readable=false;
        set_fact("nvidia.drs", readable ? "read" : "refused");
        if(readable && now!=last) {
            std::string line="NVIDIA driver settings for this game (its profile, else the global one):";
            for(const auto& entry:now) { set_fact("nvidia."+entry.first, entry.second); line+=" "+entry.first+"="+entry.second; }
            log(line);
            last=now;
        }
        Sleep(60000);
    }
}
std::once_flag driverSettingsStarted;
} // namespace

void set_fact(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(factMutex);
    facts()[key]=one_line(value);
}
std::string fact(const std::string& key) {
    std::lock_guard<std::mutex> lock(factMutex);
    const auto found=facts().find(key);
    return found==facts().end() ? std::string() : found->second;
}
std::string all_facts() {
    std::lock_guard<std::mutex> lock(factMutex);
    std::string text;
    for(const auto& entry:facts()) text+=entry.first+"="+entry.second+"\n";
    return text;
}

void note_windows() {
    using RtlGetVersionFn=LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version{}; version.dwOSVersionInfoSize=sizeof(version);
    const auto rtl=reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if(!rtl || rtl(&version)!=0) return;
    const DWORD ubr=registry_number(L"UBR");
    // Windows 11 still names itself "Windows 10" in ProductName; the build number tells them apart.
    std::string product=narrow(registry_text(L"ProductName"));
    if(version.dwBuildNumber>=22000 && product.rfind("Windows 10", 0)==0) product.replace(0, 10, "Windows 11");
    const std::string build=std::to_string(version.dwBuildNumber)+(ubr ? "."+std::to_string(ubr) : "");
    const std::string display=narrow(registry_text(L"DisplayVersion"));
    set_fact("windows.product", product);
    set_fact("windows.build", build);
    set_fact("windows.version", display);
    log("Windows: "+product+(display.empty() ? "" : " "+display)+", build "+build);
    // The mods' own DLLs carry their C++ runtime; NVIDIA's, AMD's and Intel's need the Visual C++
    // Redistributable, and Intel's XeSS frame generation needs 14.40.33810 or later of it.
    std::string where, where1;
    const std::string runtime=runtime_version(L"msvcp140.dll", where);
    const bool runtime1=!runtime_version(L"vcruntime140_1.dll", where1).empty();
    set_fact("windows.vcruntime", runtime.empty() ? "missing" : runtime+" ("+where+")"+(runtime1 ? "" : ", vcruntime140_1.dll missing"));
    log("Visual C++ runtime: "+(runtime.empty() ? std::string("none") : runtime+" from "+where)+(runtime1 ? "" : "; vcruntime140_1.dll missing"));
}

void start_driver_settings() {
    std::call_once(driverSettingsStarted, []{ std::thread([]{ try { read_driver_settings(); } catch(...) {} }).detach(); });
}

namespace {
// Which GPU the monitor showing the game's window is connected to. cross: "0" the GPU the game renders
// on, "1" another one (named in adapter), "unknown" when no enumerated output shows that monitor (a
// remote session, a window being moved or closed), which says nothing either way.
struct DisplayOwner { std::string cross, adapter; UINT outputs=0; };
DisplayOwner display_owner(IDXGIAdapter* game, const DXGI_ADAPTER_DESC& gameDesc, HWND window) {
    DisplayOwner owner;
    const HMONITOR monitor=MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    const auto drives=[&](IDXGIAdapter* adapter, UINT& outputs) {
        bool found=false; outputs=0;
        ComPtr<IDXGIOutput> output;
        for(UINT i=0; SUCCEEDED(adapter->EnumOutputs(i, &output)); ++i, output.Reset()) {
            ++outputs;
            DXGI_OUTPUT_DESC desc{};
            if(SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor==monitor) found=true;
        }
        return found;
    };
    if(drives(game, owner.outputs)) { owner.cross="0"; owner.adapter=narrow(gameDesc.Description); return owner; }
    ComPtr<IDXGIFactory1> factory;
    InternalGuard own;
    if(SUCCEEDED(create_native_factory(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)); ++i, adapter.Reset()) {
            DXGI_ADAPTER_DESC1 desc{};
            if(FAILED(adapter->GetDesc1(&desc)) || (desc.AdapterLuid.LowPart==gameDesc.AdapterLuid.LowPart && desc.AdapterLuid.HighPart==gameDesc.AdapterLuid.HighPart)) continue;
            UINT ignored=0;
            if(drives(adapter.Get(), ignored)) { owner.cross="1"; owner.adapter=narrow(desc.Description); return owner; }
        }
    }
    owner.cross="unknown";
    return owner;
}
std::mutex displayMutex;
ComPtr<IDXGIAdapter> displayGame;
DXGI_ADAPTER_DESC displayGameDesc{};
HWND displayWindow=nullptr;
std::string displayKnown; // the last definite answer, "0|adapter" or "1|adapter"
std::once_flag displayWatchStarted;
// Records the answer as facts and logs it when it changes. An unknown answer after a definite one is
// kept out: a window being moved between monitors can briefly be on none.
void note_owner(const DisplayOwner& owner, const std::string& game) {
    if(owner.cross=="unknown") {
        if(!displayKnown.empty()) return;
        set_fact("display.crossAdapter", "unknown");
        log("The display the game's window is on belongs to no output of "+game+" or of another enumerated GPU");
        return;
    }
    const std::string key=owner.cross+"|"+owner.adapter;
    if(key==displayKnown) return;
    const bool changed=!displayKnown.empty();
    displayKnown=key;
    set_fact("display.adapter", owner.adapter);
    set_fact("display.crossAdapter", owner.cross);
    if(owner.cross=="1")
        log(std::string(changed ? "The game's window is now" : "The game's window is")+" on a display connected to "+owner.adapter+", not to the "+game+
            " the game renders on: every frame shown is copied from one GPU to the other (hybrid graphics, as on most laptops, or a monitor on the other GPU)");
    else if(changed) log("The game's window is now on a display connected to "+owner.adapter+", the GPU it renders on");
}
// Every 5 s: the window can move to another monitor, and some laptops switch their panel between GPUs.
void watch_display() {
    for(;;) {
        Sleep(5000);
        try {
            ComPtr<IDXGIAdapter> game; DXGI_ADAPTER_DESC desc{}; HWND window=nullptr;
            { std::lock_guard<std::mutex> lock(displayMutex); game=displayGame; desc=displayGameDesc; window=displayWindow; }
            if(!game || !window || !IsWindow(window)) continue;
            const auto owner=display_owner(game.Get(), desc, window);
            std::lock_guard<std::mutex> lock(displayMutex);
            note_owner(owner, narrow(desc.Description));
        } catch(...) {}
    }
}
}
// A new window for the same device (a swapchain the game replaced): the watcher follows it, or it would
// skip every check once the old window is gone.
void note_display_window(HWND window) {
    std::lock_guard<std::mutex> lock(displayMutex);
    if(window) displayWindow=window;
}
// Called under the core mutex from the game's swapchain creation, and again for each new device.
// Which adapter's output shows the game's window: on a laptop with hybrid graphics the internal panel
// hangs off the integrated GPU while the game renders on the other, and every presented frame crosses
// between them. Watched afterwards, since the answer can change during play.
void note_display(IDXGIAdapter* game, const DXGI_ADAPTER_DESC& gameDesc, HWND window) {
    const auto owner=display_owner(game, gameDesc, window);
    set_fact("adapter.game.outputs", std::to_string(owner.outputs));
    {
        std::lock_guard<std::mutex> lock(displayMutex);
        displayGame=game; displayGameDesc=gameDesc; displayWindow=window;
        note_owner(owner, narrow(gameDesc.Description));
    }
    std::call_once(displayWatchStarted, []{ std::thread(watch_display).detach(); });
}

// The game's adapter: name, vendor and device, video memory and the driver version.
void note_adapter(IDXGIAdapter* adapter, const DXGI_ADAPTER_DESC& desc) {
    set_fact("adapter.game", narrow(desc.Description));
    set_fact("adapter.game.vendor", hex(desc.VendorId));
    set_fact("adapter.game.device", hex(desc.DeviceId));
    set_fact("adapter.game.vramMB", std::to_string(desc.DedicatedVideoMemory>>20));
    LARGE_INTEGER umd{};
    if(SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
        const auto part=[&](int shift) { return std::to_string((umd.QuadPart>>shift)&0xFFFF); };
        set_fact("adapter.game.driver", part(48)+"."+part(32)+"."+part(16)+"."+part(0));
    }
    if(desc.VendorId==0x10DE) start_driver_settings();
}
} // namespace tgb
