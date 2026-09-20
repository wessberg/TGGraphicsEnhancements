// SPDX-License-Identifier: MIT
#include "sl_runtime.hpp"
#include "fg_backends.hpp"
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cwctype>
namespace tgfg {
thread_local bool insideSL=false, haveRenderFrame=false;
thread_local uint32_t renderFrameId=0;
State& state() { static auto* instance=new State; return *instance; } // process lifetime
void log(const std::string& message) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> guard(logMutex);
    auto line="[TGFG] "+message+"\n";
    OutputDebugStringA(line.c_str());
    auto& s=state();
    if(!s.root.empty()) { std::ofstream f(std::filesystem::path(s.root)/L"TGFG.Native.log",std::ios::app); f<<GetTickCount64()<<" "<<line; }
}
void log_throttled(const std::string& message) {
    static std::mutex throttleMutex; static std::string last; static uint64_t lastTime=0;
    std::lock_guard<std::mutex> guard(throttleMutex);
    const uint64_t now=GetTickCount64();
    if(message==last && now-lastTime<5000) return;
    last=message; lastTime=now; log(message);
}
void failure(Reason reason,const std::string& message,bool fatal) {
    auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.status.reason=static_cast<uint32_t>(reason); s.fatal|=fatal;
    log(message);
}
bool trusted_signer(const std::wstring& path,const wchar_t* expected) {
    WINTRUST_FILE_INFO file{}; file.cbStruct=sizeof(file); file.pcwszFilePath=path.c_str();
    WINTRUST_DATA data{}; data.cbStruct=sizeof(data); data.dwUIChoice=WTD_UI_NONE;
    data.fdwRevocationChecks=WTD_REVOKE_NONE; data.dwUnionChoice=WTD_CHOICE_FILE;
    data.pFile=&file; data.dwStateAction=WTD_STATEACTION_VERIFY;
    data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG result=WinVerifyTrust(nullptr,&policy,&data);
    bool vendor=false;
    if(result==ERROR_SUCCESS) {
        auto* provider=WTHelperProvDataFromStateData(data.hWVTStateData);
        auto* signer=provider ? WTHelperGetProvSignerFromChain(provider,0,FALSE,0) : nullptr;
        if(signer && signer->csCertChain && signer->pasCertChain[0].pCert) {
            wchar_t name[512]{};
            CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,name,512);
            std::wstring n(name); std::transform(n.begin(),n.end(),n.begin(),[](wchar_t c){return std::towupper(c);});
            vendor=n.find(expected)!=std::wstring::npos;
        }
    }
    data.dwStateAction=WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr,&policy,&data);
    if(result!=ERROR_SUCCESS || !vendor)
        log("Signature validation rejected "+std::filesystem::path(path).filename().u8string()+" (expected signer "+
            std::filesystem::path(expected).u8string()+"); WinVerifyTrust="+std::to_string(result));
    return result==ERROR_SUCCESS && vendor;
}
HMODULE load_signed(const std::wstring& path,const wchar_t* vendor) {
    if(!trusted_signer(path,vendor)) return nullptr;
    const HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) log("Could not load "+std::filesystem::path(path).filename().u8string()+": error "+std::to_string(GetLastError()));
    return module;
}
std::wstring module_path(HMODULE module) {
    // Heap-grown: never place 64 KB buffers on the stack of an arbitrary caller thread.
    std::vector<wchar_t> buffer(MAX_PATH);
    for(;;) {
        const DWORD length=GetModuleFileNameW(module,buffer.data(),static_cast<DWORD>(buffer.size()));
        if(!length) return {};
        if(length<buffer.size()) return std::wstring(buffer.data(),length);
        if(buffer.size()>=32768) return {};
        buffer.resize(buffer.size()*2);
    }
}
std::wstring system_directory() {
    const UINT needed=GetSystemDirectoryW(nullptr,0);
    if(!needed) return {};
    std::wstring directory(needed,static_cast<wchar_t>(0));
    const UINT written=GetSystemDirectoryW(&directory[0],needed);
    if(!written || written>=needed) return {};
    directory.resize(written); return directory;
}
static std::string engine_version() {
    auto path=std::filesystem::path(module_path(nullptr)).parent_path()/L"UnityPlayer.dll";
    DWORD ignored=0, bytes=GetFileVersionInfoSizeW(path.c_str(),&ignored);
    if(!bytes) return {};
    std::vector<unsigned char> data(bytes);
    if(!GetFileVersionInfoW(path.c_str(),0,bytes,data.data())) return {};
    VS_FIXEDFILEINFO* info=nullptr; UINT size=0;
    if(!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&size) || size<sizeof(*info)) return {};
    return std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+std::to_string(HIWORD(info->dwFileVersionLS));
}
// Streamline already writes sl.log next to this module; only warnings and errors are copied
// here, so an active DLSS-G session does not pay for a file append per informational line.
static void sl_log(sl::LogType type,const char* text) {
    if(type==sl::LogType::eInfo) return;
    log("Streamline["+std::to_string(static_cast<int>(type))+"] "+(text?text:""));
}
// NVIDIA's sl.interposer.dll, byte-identical and still NVIDIA-signed, installed under a private
// file name. Streamline-aware injectors find a game's Streamline with GetModuleHandle on
// "sl.interposer.dll" and hook its exports and DXGI factories. RenoDX DLSS ("DLSS 5") does so and
// then releases every swapchain ReShade did not see being created, returning E_UNEXPECTED; TGFG's
// private output swapchain is one of those. Streamline itself only uses that module name to look
// for an optional NGX updater next to the interposer.
static const wchar_t kInterposerFile[]=L"TGFG.Streamline.dll";
bool load_runtime() {
    auto& s=state();
    if(GetModuleHandleW(L"sl.interposer.dll")) {
        failure(Reason::ExistingStreamline,"An sl.interposer.dll is already loaded. Refusing a second independent Streamline owner."); return false;
    }
    s.runtimeDir=(std::filesystem::path(s.root)/L"Streamline").wstring();
    auto interposer=(std::filesystem::path(s.runtimeDir)/kInterposerFile).wstring();
    if(!trusted_signer(interposer,L"NVIDIA")) return false;
    s.signatureChecked=true;
    s.sl.module=LoadLibraryExW(interposer.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!s.sl.module) { log("Could not load the signed Streamline interposer (TGFG.Streamline.dll): "+std::to_string(GetLastError())); return false; }
#define LOAD(n) s.sl.n=reinterpret_cast<decltype(s.sl.n)>(GetProcAddress(s.sl.module,#n)); if(!s.sl.n){log("Missing Streamline export " #n);return false;}
    LOAD(slInit); LOAD(slShutdown); LOAD(slSetD3DDevice); LOAD(slGetNativeInterface);
    LOAD(slIsFeatureSupported); LOAD(slGetFeatureRequirements); LOAD(slGetFeatureFunction);
    LOAD(slSetConstants); LOAD(slSetTagForFrame); LOAD(slGetNewFrameToken);
#undef LOAD
#define DX(name,member) s.sl.member=reinterpret_cast<decltype(s.sl.member)>(GetProcAddress(s.sl.module,name)); if(!s.sl.member){log("Missing interposer graphics export " name);return false;}
    DX("D3D12CreateDevice",createDevice); DX("CreateDXGIFactory",createFactory);
    DX("CreateDXGIFactory1",createFactory1); DX("CreateDXGIFactory2",createFactory2);
#undef DX
    s.engineVersionUtf8=engine_version();
    const auto& version=s.engineVersionUtf8;
    if(version.empty()) { log("Cannot read UnityPlayer.dll version. Native integration requires a Unity player beside the target executable."); return false; }
    log("Engine identity: Unity file-version "+version+"; applicationId=0; mod-specific project GUID. No application-ID spoofing.");
    s.pluginPaths[0]=s.runtimeDir.c_str();
    sl::Preferences pref{};
    pref.pathsToPlugins=s.pluginPaths.data(); pref.numPathsToPlugins=1;
    pref.pathToLogsAndData=s.root.c_str(); pref.logMessageCallback=sl_log;
    pref.logLevel=sl::LogLevel::eDefault;
    pref.featuresToLoad=s.features.data(); pref.numFeaturesToLoad=3;
    pref.engine=sl::EngineType::eUnity; pref.engineVersion=version.c_str();
    pref.projectId="5e0e616e-ae10-4312-9f43-286d060e53ec";
    pref.applicationId=0; pref.renderAPI=sl::RenderAPI::eD3D12;
    // Automatic SL proxies, NOT eUseManualHooking. The entire swapchain API stays in SL.
    pref.flags|=sl::PreferenceFlags::eUseDXGIFactoryProxy|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    SLGuard inside;
    auto result=s.sl.slInit(pref,sl::kSDKVersion);
    if(result!=sl::Result::eOk) { failure(Reason::NativeError,"slInit failed: "+std::to_string(static_cast<int>(result))); return false; }
    s.initialized=true;
    log("Direct Streamline initialization succeeded. Only DLSS-G, Reflex and PCL requested; no SR or neural-rendering replacement. "
        "Interposer loaded under its private name TGFG.Streamline.dll, so injectors that hook sl.interposer.dll leave it alone.");
    return true;
}
void note_foreign_streamline() {
    // Reported once: another mod starting its own Streamline after TGFG is legal but worth knowing
    // when reading a log (two instances share the driver's NGX core and any OTA plugin files).
    static bool reported=false;
    if(reported) return;
    const HMODULE other=GetModuleHandleW(L"sl.interposer.dll");
    if(!other) return;
    reported=true;
    log("Another Streamline instance was loaded after TGFG: "+std::filesystem::path(module_path(other)).u8string()+
        ". TGFG keeps using its private instance.");
}
void on_device(IUnknown* object) {
    auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if(!s.initialized || !object || s.device) return;
    SLGuard inside;
    ComPtr<ID3D12Device> candidate;
    if(FAILED(object->QueryInterface(IID_PPV_ARGS(&candidate)))) return;
    void* unwrapped=nullptr;
    if(s.sl.slGetNativeInterface(candidate.Get(),&unwrapped)==sl::Result::eOk && unwrapped) {
        candidate.Reset(); candidate.Attach(static_cast<ID3D12Device*>(unwrapped));
    }
    s.device=candidate; // FSR and XeSS use the native device whether or not Streamline binds it
    LUID luid=candidate->GetAdapterLuid(); sl::AdapterInfo adapter{};
    adapter.deviceLUID=reinterpret_cast<uint8_t*>(&luid); adapter.deviceLUIDSizeInBytes=sizeof(luid);
    const auto dlss=s.sl.slIsFeatureSupported(sl::kFeatureDLSS_G,adapter);
    const auto reflex=s.sl.slIsFeatureSupported(sl::kFeatureReflex,adapter);
    if(dlss!=sl::Result::eOk) log("Device does not support DLSS-G; result="+std::to_string(static_cast<int>(dlss)));
    if(reflex!=sl::Result::eOk) log("Device does not support Reflex; result="+std::to_string(static_cast<int>(reflex)));
    if(dlss!=sl::Result::eOk && reflex!=sl::Result::eOk) return; // nothing Streamline could do on this device
    if(s.sl.slSetD3DDevice(candidate.Get())!=sl::Result::eOk) { log("slSetD3DDevice failed; continuing without Streamline"); return; }
    s.slReady=true; s.status.flags|=1u;
    auto feature=[&](sl::Feature f,const char* name)->void* {
        void* p=nullptr; return s.sl.slGetFeatureFunction(f,name,p)==sl::Result::eOk?p:nullptr;
    };
    if(reflex==sl::Result::eOk) {
        s.sl.reflexOptions=reinterpret_cast<PFun_slReflexSetOptions*>(feature(sl::kFeatureReflex,"slReflexSetOptions"));
        s.sl.sleep=reinterpret_cast<PFun_slReflexSleep*>(feature(sl::kFeatureReflex,"slReflexSleep"));
        s.sl.reflexState=reinterpret_cast<PFun_slReflexGetState*>(feature(sl::kFeatureReflex,"slReflexGetState"));
        s.sl.marker=reinterpret_cast<PFun_slPCLSetMarker*>(feature(sl::kFeaturePCL,"slPCLSetMarker"));
        s.reflexReady=s.sl.reflexOptions && s.sl.sleep && s.sl.marker;
        if(!s.reflexReady) log("Reflex/PCL feature functions are missing; Reflex unavailable");
    }
    if(dlss==sl::Result::eOk) {
        s.sl.setOptions=reinterpret_cast<PFun_slDLSSGSetOptions*>(feature(sl::kFeatureDLSS_G,"slDLSSGSetOptions"));
        s.sl.getState=reinterpret_cast<PFun_slDLSSGGetState*>(feature(sl::kFeatureDLSS_G,"slDLSSGGetState"));
        // DLSS Frame Generation requires Reflex; the presenting path forces it on while generating.
        s.dlssReady=s.sl.setOptions && s.sl.getState && s.reflexReady;
        if(!s.dlssReady) log("DLSS-G feature functions (or the Reflex it requires) are missing; DLSS-G unavailable");
    }
    if(s.reflexReady) s.status.latencyAvailable|=(1u<<uint32_t(Latency::Reflex))|(1u<<uint32_t(Latency::ReflexBoost));
    // No capability query here: slDLSSGGetState must be called on the present thread, and this runs
    // on whichever thread created the bridge. The bridge always starts on the standard swapchain, so
    // after_present fills the capabilities on the very first present instead.
    if(s.dlssReady) { s.status.backendsAvailable|=1u<<uint32_t(Backend::DLSS); options_off(); }
    log(std::string("Streamline bound to the private D3D12 presentation device: DLSS-G ")+(s.dlssReady?"available":"unavailable")+
        ", Reflex "+(s.reflexReady?"available":"unavailable")+". FG remains Off until valid render inputs and a user request.");
}
void load_vendor_runtimes() { load_fsr_runtime(); load_xess_runtime(); }
void probe_vendor_backends(ID3D12Device* device,uint32_t vendor) {
    probe_fsr(device); probe_xess(device,vendor); antilag_probe(device,vendor);
}
void update_caps() {
    auto& s=state(); if(!s.dlssReady) return;
    SLGuard inside;
    sl::DLSSGState g{};
    // No options: slDLSSGGetState applies the ones it is handed. Passing the applied options pushed a
    // full set on every present, which defeated the change check in apply_options and made Streamline
    // re-evaluate its flip metering; it reports the frames where both happen as "Repeated
    // slDLSSGSetOptions() call". A null pointer queries the state of the options already in effect,
    // which is all this reads.
    auto r=s.sl.getState(s.viewport,g,nullptr);
    s.status.lastGetStateResult=static_cast<int32_t>(r);
    // Announce the capability set either way. A runtime that refuses to report leaves DLSS-G out of the
    // choices, which is correct, but it must never leave the plugin waiting for capabilities that will
    // not arrive: it reads them once, the first time they are announced.
    s.status.flags|=4u;
    if(r!=sl::Result::eOk) return;
    s.status.flags|=8u;
    s.status.maxGeneratedFrames=g.numFramesToGenerateMax;
    s.status.dynamicSupported=g.bIsDynamicMFGSupported==sl::Boolean::eTrue;
    s.status.dlssgStatus=static_cast<uint32_t>(g.status);
    s.status.presentedSinceQuery=g.numFramesActuallyPresented;
    s.status.outputFramesReported+=g.numFramesActuallyPresented;
    s.diag.sdkPresented+=g.numFramesActuallyPresented; // same counter the XeSS path reports into
    s.vsyncSupported=g.bIsVsyncSupportAvailable==sl::Boolean::eTrue;
    // NVIDIA owns this object. Acquire a COM reference; do not Attach a borrowed
    // fence or treat an API acceptance result as a displayed-frame measurement.
    if(g.inputsProcessingCompletionFence) {
        s.lastInputsFence=static_cast<ID3D12Fence*>(g.inputsProcessingCompletionFence);
        s.lastInputsFenceValue=g.lastPresentInputsProcessingCompletionFenceValue;
    }
}
void options_off() {
    // DLSS-G lives on the Streamline swapchain only; the FSR/XeSS swapchains never see it.
    auto& s=state(); if(!s.dlssReady || s.output!=Output::Standard) return;
    sl::DLSSGOptions off=s.haveOptions?s.applied:sl::DLSSGOptions{}; off.mode=sl::DLSSGMode::eOff;
    // Streamline rejects zero here even though generation is disabled.
    off.numFramesToGenerate=1;
    off.queueParallelismMode=sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
    SLGuard inside;
    auto r=s.sl.setOptions(s.viewport,off);
    s.status.lastSetOptionsResult=static_cast<int32_t>(r);
    if(r==sl::Result::eOk) { s.applied=off; s.haveOptions=true; s.status.lastAcceptedMode=0; }
    else { s.fatal=true; log("Cannot disable DLSS-G; slDLSSGSetOptions returned "+std::to_string(static_cast<int>(r))); }
}
void clear_tags(Frame* frame) {
    auto& s=state(); if(!s.dlssReady || !frame || !frame->token) return;
    sl::ResourceTag tags[]={
        {nullptr,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eValidUntilPresent}};
    SLGuard inside;
    auto r=s.sl.slSetTagForFrame(*frame->token,s.viewport,tags,4,nullptr);
    if(r!=sl::Result::eOk) log("Null resource tagging returned "+std::to_string(static_cast<int>(r)));
}
} // namespace tgfg
