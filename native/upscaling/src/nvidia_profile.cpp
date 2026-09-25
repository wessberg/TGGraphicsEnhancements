// SPDX-License-Identifier: MIT
// The DLSS Super Resolution overrides the NVIDIA App (or NVIDIA Profile Inspector) stores in the
// driver's settings profile for this game: a forced model preset, a forced performance mode or render
// scale, forced DLAA. NVIDIA's runtime applies them inside the driver, so the game's own DLSS
// parameters never show them; the performance overlay reads them here, through NVAPI's driver settings (DRS)
// interface. The driver can't change what the game renders: a forced mode or scale changes the render
// size NGX recommends, which the game's DLSS modes then use.
// Read on a thread of its own from the first request on, and again every 30 seconds.
//
// The NVAPI declarations below follow NVIDIA's nvapi.h, nvapi_interface.h and NvApiDriverSettings.h
// (github.com/NVIDIA/nvapi), which NVIDIA publishes under the MIT license.
#include "runtime.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace tgup {
namespace {
using NvStatus=int32_t;
constexpr NvStatus NvOk=0, NvSettingNotFound=-160;
using NvUnicodeString=uint16_t[2048];
struct NvBinarySetting { uint32_t length; uint8_t data[4096]; };
// NVDRS_SETTING_V1 as the driver checks it: 12320 bytes. The current nvapi.h adds a 64-bit member to the
// value unions, which pads them to 12328 bytes under its 8-byte packing, and the driver (616.92 on an RTX
// 5080, 2026-09-23) refuses that size as an incompatible structure version.
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
constexpr uint32_t version(size_t size, uint32_t v) { return uint32_t(size)|(v<<16); } // MAKE_NVAPI_VERSION
using QueryInterface=void*(__cdecl*)(uint32_t);
using Initialize=NvStatus(__cdecl*)();
using CreateSession=NvStatus(__cdecl*)(void**);
using DestroySession=NvStatus(__cdecl*)(void*);
using LoadSettings=NvStatus(__cdecl*)(void*);
using FindApplication=NvStatus(__cdecl*)(void*, NvUnicodeString, void**, NvApplication*);
using GlobalProfile=NvStatus(__cdecl*)(void*, void**);
using GetSetting=NvStatus(__cdecl*)(void*, void*, uint32_t, NvSetting*);
// NvApiDriverSettings.h
constexpr uint32_t kSrOverride=0x10E41E01, kSrPreset=0x10E41DF3, kSrMode=0x10AFB768, kDlaaOverride=0x10E41DF4, kSrScaling=0x10E41DF5;

std::mutex overrideMutex;
TGUP_DlssOverride latest{};
std::once_flag started;

// A setting's value in the game's profile, or, where the game's profile doesn't set it, in the global
// one. readable turns false on any answer other than a value or "not set".
bool setting(GetSetting get, void* session, void* app, void* global, uint32_t id, uint32_t& value, bool& readable) {
    auto s=std::make_unique<NvSetting>();
    for(void* profile:{app, global}) {
        if(!profile) continue;
        *s=NvSetting{}; s->version=version(sizeof(NvSetting), 1);
        const NvStatus result=get(session, profile, id, s.get());
        if(result==NvOk) { value=s->current.u32; return true; }
        if(result!=NvSettingNotFound) readable=false;
    }
    return false;
}
void read_profile() {
    const HMODULE nvapi=LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!nvapi) { log("NVIDIA driver settings: nvapi64.dll is not installed (no NVIDIA driver); no DLSS overrides to read"); return; }
    const auto query=reinterpret_cast<QueryInterface>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if(!query) return;
    const auto init=reinterpret_cast<Initialize>(query(0x0150E828));
    const auto create=reinterpret_cast<CreateSession>(query(0x0694D52E));
    const auto destroy=reinterpret_cast<DestroySession>(query(0xDAD9CFF8));
    const auto load=reinterpret_cast<LoadSettings>(query(0x375DBD6B));
    const auto find=reinterpret_cast<FindApplication>(query(0xEEE566B2));
    const auto globalProfile=reinterpret_cast<GlobalProfile>(query(0x617BFF9F));
    const auto get=reinterpret_cast<GetSetting>(query(0x73BF8338));
    if(!init || !create || !destroy || !load || !find || !globalProfile || !get || init()!=NvOk) {
        log("NVIDIA driver settings: NVAPI's settings interface is unavailable; no DLSS overrides to read"); return;
    }
    std::wstring exe(MAX_PATH, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size())));
    const std::wstring name=exe.substr(exe.find_last_of(L"\\/")+1);
    uint32_t lastPreset=~0u, lastMode=~0u, lastDlaa=~0u, lastEnabled=~0u, lastScaling=~0u;
    bool reportedUnreadable=false;
    for(;;) {
        TGUP_DlssOverride o{}; o.modeOverride=~0u;
        void* session=nullptr;
        if(create(&session)==NvOk && session) {
            if(load(session)==NvOk) {
                auto app=std::make_unique<NvApplication>(); app->version=version(sizeof(NvApplication), 4);
                NvUnicodeString appName{};
                for(size_t i=0;i<name.size() && i<2047;++i) appName[i]=uint16_t(name[i]);
                void* appProfile=nullptr; void* global=nullptr;
                if(find(session, appName, &appProfile, app.get())!=NvOk) appProfile=nullptr;
                if(globalProfile(session, &global)!=NvOk) global=nullptr;
                uint32_t value=0;
                bool readable=true;
                if(setting(get, session, appProfile, global, kSrPreset, value, readable)) o.presetOverride=value;
                if(setting(get, session, appProfile, global, kSrOverride, value, readable)) o.overrideEnabled=value;
                if(setting(get, session, appProfile, global, kSrMode, value, readable)) o.modeOverride=value;
                if(setting(get, session, appProfile, global, kDlaaOverride, value, readable)) o.dlaaOverride=value;
                if(setting(get, session, appProfile, global, kSrScaling, value, readable)) o.scalingRatio=value;
                o.known=readable ? 1u : 0u;
                if(!readable && !reportedUnreadable) { reportedUnreadable=true; log("NVIDIA driver settings: this driver refuses the settings query; DLSS overrides unknown"); }
            }
            destroy(session);
        }
        if(o.known && (o.presetOverride!=lastPreset || o.modeOverride!=lastMode || o.dlaaOverride!=lastDlaa || o.overrideEnabled!=lastEnabled ||
                       o.scalingRatio!=lastScaling)) {
            lastPreset=o.presetOverride; lastMode=o.modeOverride; lastDlaa=o.dlaaOverride; lastEnabled=o.overrideEnabled; lastScaling=o.scalingRatio;
            std::string shown(name.size(), '?');
            for(size_t i=0;i<name.size();++i) if(name[i]<128) shown[i]=char(name[i]);
            log("NVIDIA driver settings for "+shown+": DLSS-SR preset override "+std::to_string(o.presetOverride)+
                " (0 none, 1..15 A..O, 16777215 latest), override "+std::to_string(o.overrideEnabled)+
                ", performance mode "+(o.modeOverride==~0u ? std::string("not set") : std::to_string(o.modeOverride))+
                " (3 the game's), DLAA override "+std::to_string(o.dlaaOverride)+", scaling ratio "+std::to_string(o.scalingRatio)+" (0 not set)");
        }
        {
            std::lock_guard<std::mutex> lock(overrideMutex);
            latest=o;
        }
        Sleep(30000);
    }
}
} // namespace

void dlss_override(TGUP_DlssOverride& out) {
    std::call_once(started, []{ std::thread([]{ try { read_profile(); } catch(...) {} }).detach(); });
    std::lock_guard<std::mutex> lock(overrideMutex);
    out=latest;
}
} // namespace tgup
