// SPDX-License-Identifier: MIT
#include "sl_runtime.hpp"
#include <tgb/ngx_session.hpp>
#include "fg_backends.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cwctype>
#include <memory>
namespace tgfg {
thread_local bool insideSL=false, haveRenderFrame=false;
thread_local uint32_t renderFrameId=0;
State& state() { static auto* instance=new State; return *instance; } // process lifetime
void log(const std::string& message) {
    static std::mutex logMutex;
    // Never throws: it is called from catch handlers, some of them in noexcept functions.
    try {
        std::lock_guard<std::mutex> guard(logMutex);
        auto line="[TGFG] "+message+"\n";
        OutputDebugStringA(line.c_str());
        auto& s=state();
        if(!s.root.empty()) { std::ofstream f(std::filesystem::path(s.root)/L"TGFG.Native.log",std::ios::app); f<<GetTickCount64()<<" "<<line; }
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
void failure(Reason reason,const std::string& message,bool fatal) {
    auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.status.reason=static_cast<uint32_t>(reason); s.fatal|=fatal;
    fact(fatal?"failure.fatal":"failure",message);
    log(message);
}
namespace {
std::mutex factMutex;
std::map<std::string,std::string>& fact_map() { static auto* instance=new std::map<std::string,std::string>; return *instance; }
}
void fact(const std::string& key,const std::string& value) {
    std::string line=value;
    for(auto& c:line) if(c=='\r' || c=='\n') c=' ';
    std::lock_guard<std::mutex> lock(factMutex);
    fact_map()[key]=line;
}
std::string facts() {
    std::lock_guard<std::mutex> lock(factMutex);
    std::string text;
    for(const auto& entry:fact_map()) text+=entry.first+"="+entry.second+"\n";
    return text;
}
const char* sl_result_name(sl::Result result) {
    static const char* names[]={"eOk","eErrorIO","eErrorDriverOutOfDate","eErrorOSOutOfDate","eErrorOSDisabledHWS",
        "eErrorDeviceNotCreated","eErrorNoSupportedAdapterFound","eErrorAdapterNotSupported","eErrorNoPlugins","eErrorVulkanAPI",
        "eErrorDXGIAPI","eErrorD3DAPI","eErrorNRDAPI","eErrorNVAPI","eErrorReflexAPI","eErrorNGXFailed","eErrorJSONParsing",
        "eErrorMissingProxy","eErrorMissingResourceState","eErrorInvalidIntegration","eErrorMissingInputParameter",
        "eErrorNotInitialized","eErrorComputeFailed","eErrorInitNotCalled","eErrorExceptionHandler","eErrorInvalidParameter",
        "eErrorMissingConstants","eErrorDuplicatedConstants","eErrorMissingOrInvalidAPI","eErrorCommonConstantsMissing",
        "eErrorUnsupportedInterface","eErrorFeatureMissing","eErrorFeatureNotSupported","eErrorFeatureMissingHooks",
        "eErrorFeatureFailedToLoad","eErrorFeatureWrongPriority","eErrorFeatureMissingDependency",
        "eErrorFeatureManagerInvalidState","eErrorInvalidState","eWarnOutOfVRAM"};
    const auto index=static_cast<size_t>(result);
    return index<sizeof(names)/sizeof(names[0])?names[index]:"unknown";
}
// Signature checks, hooks and the native DXGI factory belong to TG Graphics Bridge, which owns them for
// the whole process; these keep the names the rest of the extension has always called.
bool trusted_signer(const std::wstring& path,const wchar_t* vendor) { return host().trusted_signer(path.c_str(),vendor); }
HMODULE load_signed(const std::wstring& path,const wchar_t* vendor) { return host().load_signed(path.c_str(),vendor); }
bool add_hook(void* target,void* detour,void** original) { return host().add_hook(target,detour,original); }
bool add_vtable_hook(void* object,size_t slot,void* detour) { return host().add_vtable_hook(object,slot,detour); }
void* vtable_original(void* object,size_t slot) { return host().vtable_original(object,slot); }
HRESULT create_native_factory(REFIID iid,void** out) { return host().create_native_factory(iid,out); }
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
uint32_t windows_build() {
    static const uint32_t build=[] {
        using RtlGetVersionFn=LONG(WINAPI*)(OSVERSIONINFOW*);
        const HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
        const auto get=ntdll?reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll,"RtlGetVersion")):nullptr;
        OSVERSIONINFOW info{}; info.dwOSVersionInfoSize=sizeof(info);
        return get && get(&info)==0?uint32_t(info.dwBuildNumber):0u;
    }();
    return build;
}
static uint64_t file_version(const std::filesystem::path& path) {
    DWORD ignored=0, bytes=GetFileVersionInfoSizeW(path.c_str(),&ignored);
    if(!bytes) return 0;
    std::vector<unsigned char> data(bytes);
    if(!GetFileVersionInfoW(path.c_str(),0,bytes,data.data())) return 0;
    VS_FIXEDFILEINFO* info=nullptr; UINT size=0;
    if(!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&size) || size<sizeof(*info)) return 0;
    return (uint64_t(info->dwFileVersionMS)<<32)|info->dwFileVersionLS;
}
uint64_t system_file_version(const wchar_t* name) { return file_version(std::filesystem::path(system_directory())/name); }
uint64_t bound_file_version(const wchar_t* name,const std::wstring& dllDir,std::wstring& where) {
    where.clear();
    if(const HMODULE loaded=GetModuleHandleW(name)) {
        where=module_path(loaded);
        return where.empty()?0:file_version(where);
    }
    const std::filesystem::path candidates[]={std::filesystem::path(dllDir)/name,
        std::filesystem::path(module_path(nullptr)).parent_path()/name,std::filesystem::path(system_directory())/name};
    for(const auto& candidate:candidates) {
        std::error_code error;
        if(!std::filesystem::exists(candidate,error)) continue;
        where=candidate.wstring();
        return file_version(candidate);
    }
    return 0;
}
std::string version_text(uint64_t v) {
    return std::to_string((v>>48)&0xFFFF)+"."+std::to_string((v>>32)&0xFFFF)+"."+std::to_string((v>>16)&0xFFFF)+"."+std::to_string(v&0xFFFF);
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
// NGX keeps one session per D3D12 device, and on the bridge's device that is the one Streamline starts here
// (TGFG creates the device through Streamline, before any other extension sees it). NGX looks for feature
// DLLs only in the folders the session started with: Streamline's plugin folder and its pathsToPlugins. TG
// Better Upscaling runs DLSS Ray Reconstruction on the same device for TG Path Tracing, from its own DLSS
// folder, which NGX would never search. With both of those mods installed, that folder comes second: it holds
// NVIDIA's nvngx_dlss.dll and nvngx_dlssd.dll, no Streamline plugin and no nvngx_dlssg.dll, so Streamline and
// DLSS Frame Generation still load everything from this mod's own folder. Without them, nothing changes.
static std::wstring ray_reconstruction_folder() {
    const wchar_t* root=host().bepinex_root();
    if(!root || !*root) return {};
    const std::filesystem::path bepinex(root);
    const auto dlss=bepinex/L"TGBetterUpscaling"/L"Native"/L"DLSS";
    std::error_code error;
    if(!std::filesystem::exists(dlss/L"nvngx_dlssd.dll",error) ||
       !std::filesystem::exists(bepinex/L"TGPathTracing"/L"Native"/L"TGPathTracing.Native.dll",error)) return {};
    return dlss.wstring();
}
bool load_runtime() {
    auto& s=state();
    if(const HMODULE other=GetModuleHandleW(L"sl.interposer.dll")) {
        fact("streamline","foreign "+std::filesystem::path(module_path(other)).u8string());
        failure(Reason::ExistingStreamline,"An sl.interposer.dll is already loaded. Refusing a second independent Streamline owner."); return false;
    }
    s.runtimeDir=(std::filesystem::path(s.root)/L"Streamline").wstring();
    auto interposer=(std::filesystem::path(s.runtimeDir)/kInterposerFile).wstring();
    if(!trusted_signer(interposer,L"NVIDIA")) { fact("streamline",GetFileAttributesW(interposer.c_str())==INVALID_FILE_ATTRIBUTES?"missing":"unsigned"); log("TGFG.Streamline.dll is missing or not NVIDIA-signed (details in TGGraphicsBridge.Native.log)"); return false; }
    s.signatureChecked=true;
    s.sl.module=LoadLibraryExW(interposer.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!s.sl.module) { fact("streamline","load-failed "+std::to_string(GetLastError())); log("Could not load the signed Streamline interposer (TGFG.Streamline.dll): "+std::to_string(GetLastError())); return false; }
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
    if(version.empty()) { fact("streamline","no-engine-version"); log("Cannot read UnityPlayer.dll version. Native integration requires a Unity player beside the target executable."); return false; }
    log("Engine identity: Unity file-version "+version+"; applicationId=0; mod-specific project GUID. No application-ID spoofing.");
    s.pluginPaths[0]=s.runtimeDir.c_str();
    uint32_t pluginPathCount=1;
    s.rayReconstructionDir=ray_reconstruction_folder();
    if(!s.rayReconstructionDir.empty()) {
        s.pluginPaths[1]=s.rayReconstructionDir.c_str(); pluginPathCount=2;
        log("TG Better Upscaling and TG Path Tracing are installed: NGX on this device also looks in "+
            std::filesystem::path(s.rayReconstructionDir).u8string()+" for DLSS Ray Reconstruction");
    }
    s.neuralRenderingDir=tgb::neural_runtime_folder(host());
    if(!s.neuralRenderingDir.empty()) s.pluginPaths[pluginPathCount++]=s.neuralRenderingDir.c_str();
    sl::Preferences pref{};
    pref.pathsToPlugins=s.pluginPaths.data(); pref.numPathsToPlugins=pluginPathCount;
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
    if(result!=sl::Result::eOk) { fact("streamline",std::string("init-failed ")+sl_result_name(result)); failure(Reason::NativeError,"slInit failed: "+std::to_string(static_cast<int>(result))); return false; }
    s.initialized=true; fact("streamline","ready");
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
    fact("dlssg",dlss==sl::Result::eOk?"supported":std::string("unsupported ")+sl_result_name(dlss));
    fact("reflex",reflex==sl::Result::eOk?"supported":std::string("unsupported ")+sl_result_name(reflex));
    if(dlss!=sl::Result::eOk) log("Device does not support DLSS-G; result="+std::to_string(static_cast<int>(dlss))+" ("+sl_result_name(dlss)+")");
    if(reflex!=sl::Result::eOk) log("Device does not support Reflex; result="+std::to_string(static_cast<int>(reflex))+" ("+sl_result_name(reflex)+")");
    if(dlss!=sl::Result::eOk && reflex!=sl::Result::eOk) return; // nothing Streamline could do on this device
    if(s.sl.slSetD3DDevice(candidate.Get())!=sl::Result::eOk) { fact("streamline","device-refused"); log("slSetD3DDevice failed; continuing without Streamline"); return; }
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
    if(s.reflexReady) offer_reflex(false);
    // The capabilities are read here, before any present, because the plugin reads them once and the
    // Video tab's rows depend on them. Streamline warns that slDLSSGGetState should be called on the
    // present thread; that concerns the inputs-processing completion fence, which does not exist yet at
    // this point. Two attempts at deferring this to the first present each broke the Video tab.
    if(s.dlssReady) { s.status.backendsAvailable|=1u<<uint32_t(Backend::DLSS); options_off(); update_caps(); }
    log(std::string("Streamline bound to the private D3D12 presentation device: DLSS-G ")+(s.dlssReady?"available":"unavailable")+
        ", Reflex "+(s.reflexReady?"available":"unavailable")+". FG remains Off until valid render inputs and a user request.");
}
// Whether the player is offered Reflex follows its own report: NVIDIA's Reflex guide says to disable
// the Reflex UI when lowLatencyAvailable is false (a driver before 456, a GPU before Maxwell, or an
// adapter that isn't NVIDIA's) and to do everything else the same, so DLSS-G still runs with it.
// Streamline decides it when its Reflex plugin loads. A query that fails is tried again before the
// plugin reads the capabilities (last); without an answer by then Reflex isn't offered.
void offer_reflex(bool last) {
    auto& s=state();
    if(!s.reflexReady || s.reflexOfferDecided) return;
    bool answered=false, available=false;
    if(s.sl.reflexState) {
        auto current=std::make_unique<sl::ReflexState>(); // 64 frame reports: not for the stack
        SLGuard inside;
        const auto result=s.sl.reflexState(*current);
        answered=result==sl::Result::eOk; available=answered && current->lowLatencyAvailable;
        if(!answered) log(std::string("slReflexGetState returned ")+sl_result_name(result)+(last?"; Reflex isn't offered":"; asking again later"));
    }
    if(!answered && !last) { fact("reflex.lowLatency","unknown"); return; }
    s.reflexOfferDecided=true;
    fact("reflex.lowLatency",available?"available":answered?"unavailable":"unknown");
    if(available) s.status.latencyAvailable|=(1u<<uint32_t(Latency::Reflex))|(1u<<uint32_t(Latency::ReflexBoost));
    else if(answered) log("NVIDIA Reflex reports low latency unavailable on this system, so it isn't offered as a Low Latency choice");
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
    if(r!=sl::Result::eOk) return;
    s.status.flags|=8u; // flag 4 (capabilities known) waits for the other probes and the swapchain
    s.status.maxGeneratedFrames=g.numFramesToGenerateMax;
    s.status.dynamicSupported=g.bIsDynamicMFGSupported==sl::Boolean::eTrue;
    s.status.dlssgStatus=static_cast<uint32_t>(g.status);
    s.status.presentedSinceQuery=g.numFramesActuallyPresented;
    s.status.outputFramesReported+=g.numFramesActuallyPresented;
    s.diag.sdkPresented+=g.numFramesActuallyPresented; // same counter the XeSS path reports into
    s.vsyncSupported=g.bIsVsyncSupportAvailable==sl::Boolean::eTrue;
    if(g.minWidthOrHeight!=s.dlssgMinimum) {
        s.dlssgMinimum=g.minWidthOrHeight; fact("dlssg.minimum",std::to_string(g.minWidthOrHeight));
    }
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
