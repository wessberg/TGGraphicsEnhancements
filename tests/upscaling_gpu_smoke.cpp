// SPDX-License-Identifier: MIT
// End-to-end check on this machine's GPU, outside the game: TG Graphics Bridge boots with the upscaling
// extension (and, with --with-fg, frame generation as presentation provider), sees a D3D11 swapchain
// being created, creates its D3D12 device there, and every available upscaler then upscales a moving
// test image through the real render event. With DLSS staged, an older nvngx_dlss.dll loads the
// bundled one. Needs the staged artifacts/Native folders; exits 77
// (skipped) without them or without a D3D12 GPU.
#include "tgb/bridge_c_api.h"
#include "tgup_api.h"
#include "../native/neural/include/tgnr_api.h"
#include <Windows.h>
#include <psapi.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cmath>
#include <cwctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
static int failures=0;
static void expect(bool condition, const char* what) { std::printf("%s: %s\n", condition ? "ok  " : "FAIL", what); if(!condition) ++failures; }
static float half_to_float(uint16_t h) {
    const uint32_t sign=(h>>15)&1, exponent=(h>>10)&31, mantissa=h&1023;
    float value;
    if(exponent==0) value=std::ldexp(float(mantissa), -24);
    else if(exponent==31) value=mantissa ? NAN : INFINITY;
    else value=std::ldexp(float(mantissa|1024), int(exponent)-25);
    return sign ? -value : value;
}
static uint16_t float_to_half(float f) {
    uint32_t x; std::memcpy(&x, &f, 4);
    const uint32_t sign=(x>>16)&0x8000; int exponent=int((x>>23)&0xFF)-127+15; uint32_t mantissa=x&0x7FFFFF;
    if(exponent<=0) return uint16_t(sign);
    if(exponent>=31) return uint16_t(sign|0x7C00);
    return uint16_t(sign|(uint32_t(exponent)<<10)|(mantissa>>13));
}
static float halton(int index, int base) { float f=1, r=0; while(index>0) { f/=float(base); r+=f*float(index%base); index/=base; } return r; }
// The bundled DLSS: a load of an older nvngx_dlss.dll by full path (a system DLL under that name,
// version 10.0) maps the bundled copy instead. Loaded as a data file, so none of NVIDIA's code runs.
static void check_bundled_dlss(const fs::path& upscaling) {
    if(!fs::exists(upscaling/"DLSS"/"nvngx_dlss.dll")) { std::printf("      no DLSS staged in TGBetterUpscaling/DLSS; its check is skipped\n"); return; }
    wchar_t temp[MAX_PATH]{}, system[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp); GetSystemDirectoryW(system, MAX_PATH);
    const auto folder=fs::path(temp)/("tgup-dlss-"+std::to_string(GetCurrentProcessId()));
    std::error_code error;
    fs::create_directories(folder, error);
    const auto older=folder/"nvngx_dlss.dll";
    fs::copy_file(fs::path(system)/"version.dll", older, fs::copy_options::overwrite_existing, error);
    const HMODULE mapped=LoadLibraryExW(older.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    wchar_t name[MAX_PATH]{};
    if(mapped) GetMappedFileNameW(GetCurrentProcess(), reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mapped)&~uintptr_t(3)), name, MAX_PATH);
    std::wstring mappedName(name);
    for(auto& c:mappedName) c=wchar_t(towlower(c));
    // TG Better Upscaling's DLSS folder: TGBetterUpscaling/DLSS staged, TGBetterUpscaling/Native/DLSS installed.
    const std::wstring suffix=L"\\dlss\\nvngx_dlss.dll";
    expect(mappedName.find(L"tgbetterupscaling")!=std::wstring::npos && mappedName.size()>suffix.size() &&
           mappedName.compare(mappedName.size()-suffix.size(), suffix.size(), suffix)==0,
        "an older nvngx_dlss.dll loads the bundled DLSS");
    if(mapped) FreeLibrary(mapped);
    fs::remove_all(folder, error);
    // A DLSS mod's set (neural rendering beside it, as DLSS 5 installs) keeps its own nvngx_dlss.dll.
    const auto setFolder=fs::path(temp)/("tgup-dlss-set-"+std::to_string(GetCurrentProcessId()));
    fs::create_directories(setFolder, error);
    fs::copy_file(fs::path(system)/"version.dll", setFolder/"nvngx_dlss.dll", fs::copy_options::overwrite_existing, error);
    fs::copy_file(fs::path(system)/"version.dll", setFolder/"nvngx_dlssnr.dll", fs::copy_options::overwrite_existing, error);
    const auto kept=setFolder/"nvngx_dlss.dll";
    const HMODULE own=LoadLibraryExW(kept.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    wchar_t ownName[MAX_PATH]{};
    if(own) GetMappedFileNameW(GetCurrentProcess(), reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(own)&~uintptr_t(3)), ownName, MAX_PATH);
    std::wstring ownMapped(ownName);
    for(auto& c:ownMapped) c=wchar_t(towlower(c));
    const std::wstring setSuffix=L"\\nvngx_dlss.dll";
    expect(own && ownMapped.find(L"tgbetterupscaling")==std::wstring::npos && ownMapped.size()>setSuffix.size(),
        "a DLSS mod's set (DLSS 5) keeps its own nvngx_dlss.dll");
    if(own) FreeLibrary(own);
    fs::remove_all(setFolder, error);
}
// A crash names the module and offset it happened in, for a report without a debugger.
static LONG CALLBACK report_crash(EXCEPTION_POINTERS* info) {
    const DWORD code=info->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_ACCESS_VIOLATION && code!=EXCEPTION_ILLEGAL_INSTRUCTION && code!=EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    void* address=info->ExceptionRecord->ExceptionAddress;
    HMODULE module=nullptr; wchar_t name[MAX_PATH]=L"?";
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module))
        GetModuleFileNameW(module, name, MAX_PATH);
    std::printf("CRASH 0x%08lX at %ls+0x%llX (accessing 0x%llX)\n", code, name,
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(module)),
        info->ExceptionRecord->NumberParameters>1 ? static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]) : 0ull);
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // vendor runtimes may end the process without the CRT flushing stdio
    AddVectoredExceptionHandler(1, report_crash);
    const fs::path root=argc>1 ? fs::path(argv[1]) : fs::current_path()/"artifacts"/"Native";
    // --with-fg: TG Frame Generation too (Streamline's NGX on the same device); --with-pt: TG Path Tracing too,
    // the one extension DLSS Ray Reconstruction (and so NGX in this DLL) starts for.
    // --installed: the native files as a player's BepInEx folder holds them (BepInEx/<mod>/Native, the bridge in
    // each), beside this exe, so what the extensions find through the bridge's BepInEx root is what they find in
    // the game. Frame generation's Streamline also needs a Unity player (UnityPlayer.dll) beside this exe.
    bool withFg=false, withPt=false, withNr=false, factoryFirst=false, installed=false;
    for(int i=2;i<argc;++i) {
        if(std::strcmp(argv[i], "--with-fg")==0) withFg=true;
        if(std::strcmp(argv[i], "--with-pt")==0) withPt=true;
        if(std::strcmp(argv[i], "--with-nr")==0) withNr=true;
        if(std::strcmp(argv[i], "--factory-first")==0) factoryFirst=true;
        if(std::strcmp(argv[i], "--installed")==0) installed=true;
    }
    if(!fs::exists(root/"TGGraphicsBridge"/"TGGraphicsBridge.Native.dll") || !fs::exists(root/"TGBetterUpscaling"/"TGBetterUpscaling.Native.dll")) {
        std::printf("SKIP: build and stage the native DLLs first (%s)\n", root.string().c_str()); return 77;
    }
    std::function<fs::path(const char*)> native=[&](const char* mod) { return root/mod; };
    fs::path bridgePath=root/"TGGraphicsBridge"/"TGGraphicsBridge.Native.dll";
    if(installed) {
        wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path bepinex=fs::path(exe).parent_path()/"smoke-install"/"BepInEx";
        for(const char* mod:{"TGBetterUpscaling", "TGFrameGeneration", "TGPathTracing", "TGNeuralRendering"}) {
            if(!fs::exists(root/mod)) continue;
            std::error_code ec;
            const auto target=bepinex/mod/"Native";
            fs::create_directories(target, ec);
            fs::copy(root/mod, target, fs::copy_options::recursive|fs::copy_options::update_existing, ec);
            if(!ec) fs::copy(root/"TGGraphicsBridge"/"TGGraphicsBridge.Native.dll", target, fs::copy_options::update_existing, ec);
            if(ec) { std::printf("FAIL: stage %s as installed: %s\n", mod, ec.message().c_str()); return 1; }
        }
        native=[bepinex](const char* mod) { return bepinex/mod/"Native"; };
        bridgePath=native("TGBetterUpscaling")/"TGGraphicsBridge.Native.dll";
        std::printf("      installed layout: %s\n", bepinex.string().c_str());
    }
    const auto upPath=native("TGBetterUpscaling")/"TGBetterUpscaling.Native.dll";
    const auto fgPath=native("TGFrameGeneration")/"TGFG.Native.dll";
    const auto ptPath=native("TGPathTracing")/"TGPathTracing.Native.dll";
    if(!fs::exists(root/"TGBetterUpscaling"/"FidelityFX") && !fs::exists(root/"TGBetterUpscaling"/"XeSS")) { std::printf("SKIP: no upscaler runtimes staged\n"); return 77; }
    const HMODULE bridge=LoadLibraryW(bridgePath.c_str());
    if(!bridge) { std::printf("FAIL: load bridge %lu\n", GetLastError()); return 1; }
    auto addExtension=reinterpret_cast<decltype(&TGGraphicsBridge_AddExtension)>(GetProcAddress(bridge, "TGGraphicsBridge_AddExtension"));
    auto bootstrap=reinterpret_cast<decltype(&TGGraphicsBridge_Bootstrap)>(GetProcAddress(bridge, "TGGraphicsBridge_Bootstrap"));
    auto readBridge=reinterpret_cast<decltype(&TGGraphicsBridge_ReadStatus)>(GetProcAddress(bridge, "TGGraphicsBridge_ReadStatus"));
    expect(addExtension && bootstrap && readBridge, "bridge exports");
    if(failures) return 1;
    // Like Unity, which creates its DXGI factory and D3D11 device before the BepInEx preloader starts the
    // bridge, and the swapchain only later.
    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_1;
    if(factoryFirst) {
        if(FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) { std::printf("SKIP: no DXGI factory\n"); return 77; }
        ComPtr<IDXGIAdapter1> adapter; factory->EnumAdapters1(0, &adapter);
        if(FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) {
            std::printf("SKIP: no hardware D3D11 device\n"); return 77;
        }
    }
    expect(addExtension(upPath.c_str())==0, "upscaling extension registered");
    if(withFg && fs::exists(fgPath)) expect(addExtension(fgPath.c_str())==0, "frame generation extension registered");
    if(withPt) {
        if(!fs::exists(ptPath)) { std::printf("SKIP: TG Path Tracing's DLL is not staged (%s)\n", ptPath.string().c_str()); return 77; }
        expect(addExtension(ptPath.c_str())==0, "path tracing extension registered");
    }
    if(withNr) expect(addExtension((native("TGNeuralRendering")/"TGNeuralRendering.Native.dll").c_str())==0, "neural rendering extension registered");
    expect(bootstrap(nullptr)==0, "bridge bootstrap");
    check_bundled_dlss(native("TGBetterUpscaling"));
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"TGGraphicsBridgeSmoke";
    RegisterClassW(&wc);
    const HWND window=CreateWindowExW(0, wc.lpszClassName, L"TG Graphics Bridge smoke test", WS_OVERLAPPEDWINDOW, 0, 0, 1296, 759, nullptr, nullptr, wc.hInstance, nullptr);
    RECT client{}; GetClientRect(window, &client);
    if(!factory && FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) { std::printf("SKIP: no DXGI factory\n"); return 77; }
    if(!device && FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) {
        std::printf("SKIP: no hardware D3D11 device\n"); return 77;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=UINT(client.right); desc.Height=UINT(client.bottom); desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc={1, 0}; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapchain;
    expect(SUCCEEDED(factory->CreateSwapChainForHwnd(device.Get(), window, &desc, nullptr, nullptr, &swapchain)), "game swapchain created through the bridge hooks");
    TGB_Status bs{sizeof(TGB_Status), TGB_ABI};
    expect(readBridge(&bs)==0 && bs.gameDeviceObserved==1, "bridge observed the game's D3D11 device");
    expect(bs.d3d12DeviceReady==1, "bridge created the shared D3D12 device inside the swapchain creation");
    std::printf("      bridge: extensions active %u, presentation provider %d, device creator %d, swapchains seen/provided %u/%u\n",
        bs.extensionsActive, bs.presentationProvider, bs.deviceCreator, bs.swapchainsSeen, bs.swapchainsProvided);
    // The performance overlay's CPU, GPU and video memory figures: sampling starts with the first read.
    auto readPerformance=reinterpret_cast<decltype(&TGGraphicsBridge_ReadPerformance)>(GetProcAddress(bridge, "TGGraphicsBridge_ReadPerformance"));
    expect(readPerformance!=nullptr, "performance export");
    if(readPerformance) {
        TGB_Performance perf{sizeof(TGB_Performance), TGB_ABI};
        readPerformance(&perf);
        // The first sample comes half a second after the sampling thread has set up its counters.
        for(int wait=0; wait<50 && (perf.valid&5u)!=5u; ++wait) { Sleep(100); perf={sizeof(TGB_Performance), TGB_ABI}; readPerformance(&perf); }
        expect(readPerformance(&perf)==0 && (perf.valid&1u) && perf.cpuProcess>=0.f && perf.cpuProcess<=100.f, "CPU use sampled");
        expect((perf.valid&4u) && perf.vramProcess>0 && perf.vramTotal>=perf.vramProcess, "video memory of the game's adapter sampled");
        std::printf("      performance: valid 0x%X, CPU %.0f%% (system %.0f%%), GPU %.0f%% (system %.0f%%), VRAM %.2f of %.2f GB budget (%.2f GB)\n",
            perf.valid, perf.cpuProcess, perf.cpuSystem, perf.gpuProcess, perf.gpuSystem,
            perf.vramProcess/1073741824.0, perf.vramBudget/1073741824.0, perf.vramTotal/1073741824.0);
    }
    // Streamline (DLSS Frame Generation) starts NGX on the shared device before any other extension sees it.
    const bool streamline=GetModuleHandleW(L"sl.common.dll")!=nullptr;
    if(withFg) std::printf("      Streamline: %s\n", streamline ? "running on the shared device" : "not running (it needs UnityPlayer.dll beside this exe)");
    if(withFg) {
        expect(bs.swapchainsProvided==1, "frame generation replaced the swapchain");
        for(int i=0;i<30 && swapchain;++i) if(FAILED(swapchain->Present(0, 0))) { expect(false, "present through the frame generation bridge"); break; }
        std::printf("      presented 30 frames through the frame generation bridge\n");
    }
    const HMODULE up=GetModuleHandleW(L"TGBetterUpscaling.Native.dll");
    auto readUp=reinterpret_cast<decltype(&TGUP_ReadStatus)>(GetProcAddress(up, "TGUP_ReadStatus"));
    auto submit=reinterpret_cast<decltype(&TGUP_SubmitFrame)>(GetProcAddress(up, "TGUP_SubmitFrame"));
    auto eventFn=reinterpret_cast<decltype(&TGUP_GetRenderEvent)>(GetProcAddress(up, "TGUP_GetRenderEvent"));
    auto versionName=reinterpret_cast<decltype(&TGUP_GetFsrVersionName)>(GetProcAddress(up, "TGUP_GetFsrVersionName"));
    auto setOutput=reinterpret_cast<decltype(&TGUP_SetOutput)>(GetProcAddress(up, "TGUP_SetOutput"));
    expect(readUp && submit && eventFn && versionName && setOutput, "upscaling exports");
    if(failures) return 1;
    auto render=reinterpret_cast<void(__stdcall*)(int, void*)>(eventFn());
    TGUP_Status us{sizeof(TGUP_Status), TGUP_ABI};
    expect(readUp(&us)==0 && us.deviceReady==1, "upscaler has the shared device");
    std::printf("      upscaler: available 0x%X, FSR versions %u, XeSS %u.%u.%u\n", us.available, us.fsrVersionCount,
        us.xessVersion>>16, (us.xessVersion>>8)&255, us.xessVersion&255);
    for(uint32_t i=0;i<us.fsrVersionCount;++i) { char name[128]{}; versionName(i, name, sizeof(name)); std::printf("      FSR version %u: %s\n", i, name); }
    // The DLSS overrides in the NVIDIA driver's profile (for this test program's name, so usually none):
    // on an NVIDIA GPU the driver's settings are read within a few seconds.
    auto getOverride=reinterpret_cast<decltype(&TGUP_GetDlssOverride)>(GetProcAddress(up, "TGUP_GetDlssOverride"));
    expect(getOverride!=nullptr, "DLSS override export");
    if(getOverride && us.gpuVendor==0x10DE) {
        TGUP_DlssOverride driver{};
        for(int wait=0; wait<50 && getOverride(&driver)==0 && !driver.known; ++wait) Sleep(100);
        expect(driver.known==1, "the NVIDIA driver's profile was read");
        std::printf("      NVIDIA driver profile: preset override %u, override %u, mode %u, DLAA %u, scaling ratio %u\n",
            driver.presetOverride, driver.overrideEnabled, driver.modeOverride, driver.dlaaOverride, driver.scalingRatio);
    }
    // Each runtime's quality modes, from the highest render scale down, as the Video tab offers them.
    auto getModes=reinterpret_cast<decltype(&TGUP_GetModes)>(GetProcAddress(up, "TGUP_GetModes"));
    expect(getModes!=nullptr, "quality mode export");
    for(uint32_t backend : {uint32_t(TGUP_BACKEND_FSR), uint32_t(TGUP_BACKEND_XESS)}) {
        if(!getModes || !(us.available&(1u<<backend))) continue;
        TGUP_Mode modes[16]{};
        const int count=getModes(backend, modes, 16);
        std::string list; bool ordered=count>0;
        for(int i=0;i<count && i<16;++i) {
            char item[48]; std::snprintf(item, sizeof(item), "%s%u:%.2fx", i ? ", " : "", modes[i].id, modes[i].ratio); list+=item;
            if(i>0 && !(modes[i].ratio>modes[i-1].ratio)) ordered=false;
        }
        std::printf("      %s quality modes: %s\n", backend==TGUP_BACKEND_FSR ? "FSR" : "XeSS", list.c_str());
        if(backend==TGUP_BACKEND_FSR)
            expect(count==5 && modes[0].ratio==1.f && std::fabs(modes[3].ratio-2.f)<0.01f && std::fabs(modes[4].ratio-3.f)<0.01f && ordered,
                "FSR reports Native AA to Ultra Performance (1x to 3x)");
        else
            expect(count==7 && modes[0].id==106 && modes[6].id==100 && std::fabs(modes[6].ratio-3.f)<0.01f && ordered,
                "XeSS reports Native Anti-Aliasing to Ultra Performance (1x to 3x)");
    }
    // HDRP-like inputs: 9:16 render region inside a larger allocation, a moving bright square.
    const UINT RW=640, RH=360, AW=704, AH=400, OW=1280, OH=720;
    D3D11_TEXTURE2D_DESC t{}; t.MipLevels=t.ArraySize=t.SampleDesc.Count=1; t.Usage=D3D11_USAGE_DEFAULT;
    t.Width=AW; t.Height=AH; t.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; t.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color, depth, motion, output;
    device->CreateTexture2D(&t, nullptr, &color);
    t.Format=DXGI_FORMAT_R32_TYPELESS; t.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_DEPTH_STENCIL; device->CreateTexture2D(&t, nullptr, &depth);
    t.Format=DXGI_FORMAT_R16G16_FLOAT; t.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET; device->CreateTexture2D(&t, nullptr, &motion);
    t.Width=OW; t.Height=OH; t.Format=DXGI_FORMAT_R11G11B10_FLOAT; t.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS;
    device->CreateTexture2D(&t, nullptr, &output);
    expect(color && depth && motion && output, "HDRP-like input textures");
    ComPtr<ID3D11DepthStencilView> dsv; D3D11_DEPTH_STENCIL_VIEW_DESC dv{}; dv.Format=DXGI_FORMAT_D32_FLOAT; dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depth.Get(), &dv, &dsv);
    context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 0.25f, 0); // reversed-Z: near objects are large
    std::vector<uint16_t> pixels(size_t(AW)*AH*4);
    uint32_t sequence=1;
    for(uint32_t backend : {uint32_t(TGUP_BACKEND_FSR), uint32_t(TGUP_BACKEND_XESS)}) {
        if(!(us.available&(1u<<backend))) { std::printf("      %s not available here; skipped\n", backend==TGUP_BACKEND_FSR ? "FSR" : "XeSS"); continue; }
        const uint64_t before=us.framesUpscaled, fallbackBefore=us.framesFallback;
        const int frames=24;
        for(int n=0;n<frames;++n) {
            // Gray background with a gradient, a bright 60x60 square moving right one render pixel per frame.
            for(UINT y=0;y<AH;++y) for(UINT x=0;x<AW;++x) {
                const bool square=x>=100+UINT(n) && x<160+UINT(n) && y>=150 && y<210;
                const float v=square ? 4.f : 0.1f+0.4f*float(x)/float(RW);
                for(int c=0;c<3;++c) pixels[(size_t(y)*AW+x)*4+c]=float_to_half(v);
                pixels[(size_t(y)*AW+x)*4+3]=float_to_half(1.f);
            }
            context->UpdateSubresource(color.Get(), 0, nullptr, pixels.data(), AW*8, 0);
            std::vector<uint16_t> mv(size_t(AW)*AH*2, 0);
            // HDRP's convention: current minus previous in UV units; the square moved +1 pixel.
            for(UINT y=150;y<210;++y) for(UINT x=100+UINT(n);x<160+UINT(n);++x) mv[(size_t(y)*AW+x)*2]=float_to_half(1.f/float(RW));
            context->UpdateSubresource(motion.Get(), 0, nullptr, mv.data(), AW*4, 0);
            TGUP_Frame f{}; f.size=sizeof(f); f.abi=TGUP_ABI; f.sequence=sequence++; f.backend=backend;
            f.color=uint64_t(uintptr_t(color.Get())); f.depth=uint64_t(uintptr_t(depth.Get()));
            f.motion=uint64_t(uintptr_t(motion.Get())); f.output=uint64_t(uintptr_t(output.Get()));
            f.renderWidth=RW; f.renderHeight=RH; f.outputWidth=OW; f.outputHeight=OH;
            f.jitterX=halton(n+1, 2)-0.5f; f.jitterY=halton(n+1, 3)-0.5f;
            f.motionScaleX=-float(RW); f.motionScaleY=-float(RH);
            // FSR with HDRP's pre-exposure of a dark scene (the output keeps the input's brightness); XeSS without.
            f.cameraNear=0.1f; f.cameraFar=1000.f; f.fovY=1.0f; f.frameTimeMs=16.6f; f.sharpness=0.f;
            f.preExposure=backend==TGUP_BACKEND_FSR ? 0.4f : 1.f;
            f.flags=TGUP_FLAG_DEPTH_INVERTED|(n==0 ? TGUP_FLAG_RESET : 0u);
            // FSR takes the plugin's path: the destination arrives from a second pass, and event 2
            // writes it. XeSS takes the single-event path with the destination in the request.
            const bool twoPasses=backend==TGUP_BACKEND_FSR;
            if(twoPasses) f.output=0;
            if(submit(&f)!=0) { expect(false, "submit an upscaling request"); break; }
            render(1, reinterpret_cast<void*>(uintptr_t(f.sequence)));
            if(twoPasses) {
                if(setOutput(f.sequence, uint64_t(uintptr_t(output.Get())))!=0) { expect(false, "set the destination"); break; }
                render(2, reinterpret_cast<void*>(uintptr_t(f.sequence)));
            }
        }
        context->Flush();
        us={sizeof(TGUP_Status), TGUP_ABI}; readUp(&us);
        const std::string label=backend==TGUP_BACKEND_FSR ? "AMD FSR" : "Intel XeSS";
        expect(us.framesUpscaled-before==uint64_t(frames), (label+" upscaled every frame").c_str());
        expect(us.framesFallback==fallbackBefore, (label+" never fell back to the bilinear copy").c_str());
        // Read the output back: the background gradient must survive at the output's scale.
        D3D11_TEXTURE2D_DESC s{}; output->GetDesc(&s); s.Usage=D3D11_USAGE_STAGING; s.BindFlags=0; s.CPUAccessFlags=D3D11_CPU_ACCESS_READ; s.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&s, nullptr, &staging);
        context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        auto red=[&](UINT x, UINT y) {
            uint32_t packed; std::memcpy(&packed, static_cast<uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch+size_t(x)*4, 4);
            const uint32_t r=packed&0x7FF; return r==0 ? 0.f : std::ldexp(float((r&63)|64), int(r>>6)-15-6);
        };
        const float left=red(40, 600), right=red(1200, 600), squareCenter=red(2*(130+frames-1), 2*180);
        context->Unmap(staging.Get(), 0);
        std::printf("      %s: background %.3f (left) %.3f (right), moving square %.3f; preset/version %u/%u\n", label.c_str(),
            left, right, squareCenter, us.xessPreset, us.activeFsrVersion);
        expect(std::fabs(left-(0.1f+0.4f*20.f/RW))<0.05f && std::fabs(right-(0.1f+0.4f*600.f/RW))<0.05f, (label+" keeps the background gradient").c_str());
        expect(squareCenter>2.f, (label+" keeps the moving square where it is this frame").c_str());
        if(backend==TGUP_BACKEND_XESS) {
            // The player's preset wins over the narrowest one by ratio while its range holds the render
            // size: Performance (up to 2.3x) at 2x, where the ratio alone would pick Balanced.
            TGUP_Frame f{}; f.size=sizeof(f); f.abi=TGUP_ABI; f.sequence=sequence++; f.backend=backend; f.mode=101;
            f.color=uint64_t(uintptr_t(color.Get())); f.depth=uint64_t(uintptr_t(depth.Get()));
            f.motion=uint64_t(uintptr_t(motion.Get())); f.output=uint64_t(uintptr_t(output.Get()));
            f.renderWidth=RW; f.renderHeight=RH; f.outputWidth=OW; f.outputHeight=OH;
            f.motionScaleX=-float(RW); f.motionScaleY=-float(RH);
            f.cameraNear=0.1f; f.cameraFar=1000.f; f.fovY=1.0f; f.frameTimeMs=16.6f; f.preExposure=1.f; f.flags=TGUP_FLAG_DEPTH_INVERTED|TGUP_FLAG_RESET;
            expect(submit(&f)==0, "submit a request naming a XeSS preset");
            render(1, reinterpret_cast<void*>(uintptr_t(f.sequence)));
            context->Flush();
            us={sizeof(TGUP_Status), TGUP_ABI}; readUp(&us);
            expect(us.xessPreset==101, "XeSS runs the preset the request names");
        }
    }
    // Ray Reconstruction starts NGX only for TG Path Tracing, and only once it asks (render event 3, which the
    // plugin issues when TG Path Tracing traces with its Denoiser on Auto): nothing is started before that.
    if(us.gpuVendor==0x10DE) {
        expect(!(us.available&(1u<<TGUP_BACKEND_RAY_RECONSTRUCTION)), "Ray Reconstruction does not start NGX before TG Path Tracing asks");
        render(TGUP_EVENT_PREPARE_RAY_RECONSTRUCTION, nullptr);
        us={sizeof(TGUP_Status), TGUP_ABI}; readUp(&us);
    }
    if(us.gpuVendor==0x10DE) {
        const bool rr=(us.available&(1u<<TGUP_BACKEND_RAY_RECONSTRUCTION))!=0;
        if(withPt) expect(rr, streamline ? "with TG Path Tracing, DLSS Ray Reconstruction is available through Streamline's NGX session"
                                         : "with TG Path Tracing, DLSS Ray Reconstruction is available on this NVIDIA GPU");
        else expect(!rr, "without TG Path Tracing, DLSS Ray Reconstruction (and NGX for it) is never started");
    }
    // DLSS Ray Reconstruction (NVIDIA RTX): a noisy image, as a one-sample path tracer leaves it, with the
    // guides TG Path Tracing writes, denoised and upscaled on the D3D11 device through NGX in render event 1.
    if(us.available&(1u<<TGUP_BACKEND_RAY_RECONSTRUCTION)) {
        D3D11_TEXTURE2D_DESC g{}; g.MipLevels=g.ArraySize=g.SampleDesc.Count=1; g.Usage=D3D11_USAGE_DEFAULT; g.Width=RW; g.Height=RH;
        g.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> diffuse, specular, normals, hitDistance;
        g.Format=DXGI_FORMAT_R8G8B8A8_UNORM; device->CreateTexture2D(&g, nullptr, &diffuse); device->CreateTexture2D(&g, nullptr, &specular);
        g.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; device->CreateTexture2D(&g, nullptr, &normals);
        // TG Path Tracing's optional inputs, as its composite writes them: the reflections' hit distance (one float
        // channel) and the color before transparency (here the input color itself).
        g.Format=DXGI_FORMAT_R16_FLOAT; device->CreateTexture2D(&g, nullptr, &hitDistance);
        expect(diffuse && specular && normals && hitDistance, "Ray Reconstruction guide textures");
        { std::vector<uint16_t> five(size_t(RW)*RH, float_to_half(5.f)); context->UpdateSubresource(hitDistance.Get(), 0, nullptr, five.data(), RW*2, 0); }
        std::vector<uint32_t> gray(size_t(RW)*RH, 0xFF808080u), dim(size_t(RW)*RH, 0xFF0A0A0Au);
        context->UpdateSubresource(diffuse.Get(), 0, nullptr, gray.data(), RW*4, 0);
        context->UpdateSubresource(specular.Get(), 0, nullptr, dim.data(), RW*4, 0);
        std::vector<uint16_t> up(size_t(RW)*RH*4);
        for(size_t i=0;i<size_t(RW)*RH;++i) { up[i*4]=float_to_half(0.f); up[i*4+1]=float_to_half(0.f); up[i*4+2]=float_to_half(1.f); up[i*4+3]=float_to_half(0.6f); }
        context->UpdateSubresource(normals.Get(), 0, nullptr, up.data(), RW*8, 0);
        us={sizeof(TGUP_Status), TGUP_ABI}; readUp(&us);
        const uint64_t before=us.framesUpscaled, fallbackBefore=us.framesFallback;
        const int frames=32; uint32_t seed=7;
        for(int n=0;n<frames;++n) {
            for(UINT y=0;y<AH;++y) for(UINT x=0;x<AW;++x) {
                const bool square=x>=100+UINT(n) && x<160+UINT(n) && y>=150 && y<210;
                seed=seed*1664525u+1013904223u;
                const float noise=float(seed>>8)/16777216.f*2.f; // mean 1: each pixel an independent sample
                const float v=(square ? 4.f : 0.1f+0.4f*float(x)/float(RW))*noise;
                for(int c=0;c<3;++c) pixels[(size_t(y)*AW+x)*4+c]=float_to_half(v);
                pixels[(size_t(y)*AW+x)*4+3]=float_to_half(1.f);
            }
            context->UpdateSubresource(color.Get(), 0, nullptr, pixels.data(), AW*8, 0);
            std::vector<uint16_t> mv(size_t(AW)*AH*2, 0);
            for(UINT y=150;y<210;++y) for(UINT x=100+UINT(n);x<160+UINT(n);++x) mv[(size_t(y)*AW+x)*2]=float_to_half(1.f/float(RW));
            context->UpdateSubresource(motion.Get(), 0, nullptr, mv.data(), AW*4, 0);
            TGUP_Frame f{}; f.size=sizeof(f); f.abi=TGUP_ABI; f.sequence=sequence++; f.backend=TGUP_BACKEND_RAY_RECONSTRUCTION;
            f.mode=0; // NGX's Performance: 2x per axis
            f.color=uint64_t(uintptr_t(color.Get())); f.depth=uint64_t(uintptr_t(depth.Get()));
            f.motion=uint64_t(uintptr_t(motion.Get())); f.output=uint64_t(uintptr_t(output.Get()));
            f.diffuseAlbedo=uint64_t(uintptr_t(diffuse.Get())); f.specularAlbedo=uint64_t(uintptr_t(specular.Get()));
            f.normalRoughness=uint64_t(uintptr_t(normals.Get()));
            f.specularHitDistance=uint64_t(uintptr_t(hitDistance.Get())); f.colorBeforeTransparency=uint64_t(uintptr_t(color.Get()));
            // A camera at the origin looking down +Z with a reversed-Z perspective (row vectors on the left).
            for(int i=0;i<16;++i) { f.worldToView[i]=(i%5==0) ? 1.f : 0.f; f.viewToClip[i]=0.f; }
            const float ys=1.f/std::tan(0.5f), xs=ys*float(RH)/float(RW), zn=0.1f, zf=1000.f;
            f.viewToClip[0]=xs; f.viewToClip[5]=ys; f.viewToClip[10]=zn/(zn-zf); f.viewToClip[11]=1.f; f.viewToClip[14]=-zf*zn/(zn-zf);
            f.renderWidth=RW; f.renderHeight=RH; f.outputWidth=OW; f.outputHeight=OH;
            f.jitterX=halton(n+1, 2)-0.5f; f.jitterY=halton(n+1, 3)-0.5f;
            f.motionScaleX=-float(RW); f.motionScaleY=-float(RH);
            f.cameraNear=0.1f; f.cameraFar=1000.f; f.fovY=1.0f; f.frameTimeMs=16.6f; f.preExposure=1.f;
            f.flags=TGUP_FLAG_DEPTH_INVERTED|(n==0 ? TGUP_FLAG_RESET : 0u);
            using NrQueue=void*(__cdecl*)(const TGNR_Frame*,uint32_t);
            using NrRead=int(__cdecl*)(TGNR_Status*);
            NrQueue queueNr=nullptr; NrRead readNr=nullptr;
            void(__stdcall* executeNr)(int,void*)=nullptr;
            TGNR_Frame nr{sizeof(nr),TGNR_ABI,f.sequence,1,f.color,0,0,OW,OH,RW,RH,9,0,0,0,0.1f,1000};
            // No depth/motion guesses are provided by the camera packet: NR must consume the
            // actual RR handoff. Wrong-camera mutation must fail this same evaluation check.
            if(withNr && n<frames-2) {
                auto module=GetModuleHandleW(L"TGNeuralRendering.Native.dll");
                queueNr=reinterpret_cast<NrQueue>(GetProcAddress(module,"TGNR_QueueFrame"));
                auto eventNr=reinterpret_cast<void*(__cdecl*)()>(GetProcAddress(module,"TGNR_GetRenderEvent"));
                readNr=reinterpret_cast<NrRead>(GetProcAddress(module,"TGNR_ReadStatus"));
                expect(queueNr && eventNr && readNr,"NR exports beside RR");
                if(!queueNr || !eventNr || !readNr) return 1;
                executeNr=reinterpret_cast<void(__stdcall*)(int,void*)>(eventNr());
                if(std::getenv("TGNR_TEST_WRONG_CAMERA")) nr.color=f.output;
                auto arm=queueNr(&nr,4); expect(arm!=nullptr,"NR arms the RR camera without substitute guides"); executeNr(4,arm);
            }
            if(submit(&f)!=0) { expect(false, "submit a Ray Reconstruction request"); break; }
            render(1, reinterpret_cast<void*>(uintptr_t(f.sequence)));
            if(executeNr) {
                nr.color=f.output;
                auto scene=queueNr(&nr,5); expect(scene!=nullptr,"NR accepts denoised scene"); executeNr(5,scene);
                TGNR_Status ns{sizeof(ns),TGNR_ABI}; readNr(&ns);
                expect(ns.evaluated==uint64_t(n+1) && ns.failed==0,"NR evaluated from actual RR inputs on their shared device");
                if(ns.failed) break;
                if(n==15 || n==frames-3) executeNr(3,nullptr);
            }
        }
        context->Flush();
        us={sizeof(TGUP_Status), TGUP_ABI}; readUp(&us);
        expect(us.framesUpscaled-before==uint64_t(frames), "DLSS Ray Reconstruction upscaled every frame");
        expect(us.framesFallback==fallbackBefore, "DLSS Ray Reconstruction never fell back to the bilinear copy");
        expect(us.activeBackend==TGUP_BACKEND_RAY_RECONSTRUCTION, "DLSS Ray Reconstruction is the active technology");
        D3D11_TEXTURE2D_DESC s{}; output->GetDesc(&s); s.Usage=D3D11_USAGE_STAGING; s.BindFlags=0; s.CPUAccessFlags=D3D11_CPU_ACCESS_READ; s.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&s, nullptr, &staging);
        context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        auto red=[&](UINT x, UINT y) {
            uint32_t packed; std::memcpy(&packed, static_cast<uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch+size_t(x)*4, 4);
            const uint32_t r=packed&0x7FF; return r==0 ? 0.f : std::ldexp(float((r&63)|64), int(r>>6)-15-6);
        };
        // A band of background where the input's noise is +-100%: the mean must stay, the pixel-to-pixel noise go.
        double mean=0, rough=0; int count=0;
        for(UINT x=200;x<400;++x) { const float a=red(x, 600), b=red(x+1, 600); mean+=a; rough+=std::fabs(a-b); ++count; }
        mean/=count; rough/=count;
        const double expected=0.1+0.4*(300.0/2.0)/RW;
        const float squareCenter=red(2*(130+frames-1), 2*180);
        // Bloom-like halos: the background just above and below the bright square (10 to 40 output pixels from its
        // edge), against what the background is there. Light spread out of the square reads as a glow.
        double ring=0, ringExpected=0;
        for(UINT x=2*(100+frames-1);x<2*(160+frames-1);++x)
            for(UINT y:{2*150-40, 2*150-25, 2*150-10, 2*210+10, 2*210+25, 2*210+40}) { ring+=red(x, y); ringExpected+=0.1+0.4*(x/2.0)/RW; }
        context->Unmap(staging.Get(), 0);
        std::printf("      DLSS Ray Reconstruction: background mean %.3f (expected %.3f), pixel-to-pixel noise %.4f (input +-100%%), moving square %.3f, "
            "ring around it %.3f of its background\n", mean, expected, rough, squareCenter, ring/ringExpected);
        expect(std::fabs(mean-expected)<0.25*expected, "DLSS Ray Reconstruction keeps the background's brightness");
        expect(rough<0.1*expected, "DLSS Ray Reconstruction removes the per-pixel noise");
        expect(squareCenter>1.5f, "DLSS Ray Reconstruction keeps the moving square where it is this frame");
    } else {
        std::printf("      DLSS Ray Reconstruction not available here; skipped\n");
        if(withNr) expect(false,"NR/RR coexistence requires actual RR evaluations");
    }
    // XeSS just below native: the narrowest preset by ratio (Native Anti-Aliasing) refuses the size, so
    // a wider one serves it. That choice is made once, not re-made (with a blocking re-initialization)
    // every frame.
    if(us.available&(1u<<TGUP_BACKEND_XESS)) {
        const UINT NW=1270, NH=714;
        D3D11_TEXTURE2D_DESC n{}; n.MipLevels=n.ArraySize=n.SampleDesc.Count=1; n.Usage=D3D11_USAGE_DEFAULT; n.Width=OW; n.Height=OH;
        ComPtr<ID3D11Texture2D> ncolor, ndepth, nmotion;
        n.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; n.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET; device->CreateTexture2D(&n, nullptr, &ncolor);
        n.Format=DXGI_FORMAT_R32_TYPELESS; n.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_DEPTH_STENCIL; device->CreateTexture2D(&n, nullptr, &ndepth);
        n.Format=DXGI_FORMAT_R16G16_FLOAT; n.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET; device->CreateTexture2D(&n, nullptr, &nmotion);
        const auto logPath=native("TGBetterUpscaling")/"TGBetterUpscaling.Native.log";
        auto inits=[&]() {
            FILE* file=nullptr; size_t count=0;
            if(_wfopen_s(&file, logPath.c_str(), L"rb")==0 && file) {
                char line[1024];
                while(fgets(line, sizeof(line), file)) if(std::strstr(line, "Intel XeSS ") && std::strstr(line, "(render 1270x714)")) ++count;
                fclose(file);
            }
            return count;
        };
        const size_t before=inits();
        const uint64_t upscaledBefore=us.framesUpscaled;
        for(int k=0;k<8;++k) {
            TGUP_Frame f{}; f.size=sizeof(f); f.abi=TGUP_ABI; f.sequence=sequence++; f.backend=TGUP_BACKEND_XESS;
            f.color=uint64_t(uintptr_t(ncolor.Get())); f.depth=uint64_t(uintptr_t(ndepth.Get()));
            f.motion=uint64_t(uintptr_t(nmotion.Get())); f.output=uint64_t(uintptr_t(output.Get()));
            f.renderWidth=NW; f.renderHeight=NH; f.outputWidth=OW; f.outputHeight=OH;
            f.jitterX=halton(k+1, 2)-0.5f; f.jitterY=halton(k+1, 3)-0.5f; f.motionScaleX=-float(NW); f.motionScaleY=-float(NH);
            f.cameraNear=0.1f; f.cameraFar=1000.f; f.fovY=1.0f; f.frameTimeMs=16.6f; f.preExposure=1.f;
            f.flags=TGUP_FLAG_DEPTH_INVERTED|(k==0 ? TGUP_FLAG_RESET : 0u);
            if(submit(&f)!=0) { expect(false, "submit a near-native XeSS request"); break; }
            render(1, reinterpret_cast<void*>(uintptr_t(f.sequence)));
        }
        context->Flush();
        us={sizeof(TGUP_Status), TGUP_ABI}; readUp(&us);
        std::printf("      Intel XeSS near native: preset %u, %zu initialization(s) for 8 frames\n", us.xessPreset, inits()-before);
        expect(us.framesUpscaled-upscaledBefore==8, "Intel XeSS upscaled every near-native frame");
        expect(inits()-before==1, "Intel XeSS chose its near-native preset once, not every frame");
    }
    if(failures) { std::printf("%d smoke checks failed\n", failures); return 1; }
    std::printf("GPU smoke test passed\n");
    return 0;
}
