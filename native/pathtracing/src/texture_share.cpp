// SPDX-License-Identifier: MIT
// PT-45: the game's own D3D11 textures read by the tracer's D3D12 device, with no copy (texture_share.hpp).
#include "texture_share.hpp"
#include "runtime.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace tgpt::shaders {
#include "pt_share_test.h"
}

namespace tgpt::share {
namespace {
// The registry's object in a texture's private data: D3D11 releases it when the texture dies.
const GUID kNotice={0x5d2c8a71, 0x3f4e, 0x4b19, {0x9a, 0x6d, 0x21, 0xe7, 0x4c, 0x80, 0xb3, 0x5f}};
// A texture the hook asked to share and the driver refused: made as the game asked, tagged with its format.
const GUID kRefused={0x7e41b2d9, 0x6c03, 0x4a8f, {0xb1, 0x52, 0x0d, 0x9e, 0x3a, 0x67, 0xc4, 0x18}};

DXGI_FORMAT view_format(DXGI_FORMAT f);

// ------------------------------------------------------------------ what the start-up self-test proved
// The formats the self-test (below) read back bit for bit on this card, by DXGI number: the hook adds the flag only to
// textures in one of them (a typeless one by the format its view reads), and the registry opens no other.
std::atomic<uint64_t> passedFormats[4]{};
bool format_passed(DXGI_FORMAT format) {
    const UINT f=UINT(view_format(format));
    return f<256 && ((passedFormats[f>>6].load(std::memory_order_relaxed)>>(f&63))&1u)!=0;
}
// The same for 3D textures (the game's fog volumes' masks, PT-112): proven apart, a 2D verdict says nothing of a 3D layout.
std::atomic<uint64_t> passedVolumeFormats[4]{};
bool volume_format_passed(DXGI_FORMAT format) {
    const UINT f=UINT(view_format(format));
    return f<256 && ((passedVolumeFormats[f>>6].load(std::memory_order_relaxed)>>(f&63))&1u)!=0;
}
// What it found, for TGPT_SharedTexture.selfTest: formats passed (bits 0-7), formats tested (8-15), its time in
// milliseconds (16-31); 0 until it has ended.
std::atomic<uint32_t> selfTestSummary{0};

// ------------------------------------------------------------------ the hook
// It runs on whatever thread the game makes a texture on (Unity's loading threads among them): it only changes the
// description and counts. No locks, no large stack, no D3D12 and no use of the game's immediate context.
using Create2D=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
using Create2D1=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device3*, const D3D11_TEXTURE2D_DESC1*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D1**);
using Create3D=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE3D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture3D**);
using Create3D1=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device3*, const D3D11_TEXTURE3D_DESC1*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture3D1**);
void* realCreate2D=nullptr;  // MinHook's trampolines: the bridge's add_hook writes them before it enables the detours
void* realCreate2D1=nullptr;
void* realCreate3D=nullptr;
void* realCreate3D1=nullptr;
// 0 not in, 1 in, 2 could not be installed, 3 its start-up self-test is running, 4 out: the self-test passed no format
// (or could not run), so nothing is made shareable (TGPT_SharedTexture.hook).
std::atomic<uint32_t> hookState{0};
std::atomic<uint64_t> calls2D{0}, calls2D1{0}, calls3D{0}, calls3D1{0}, made{0}, refused{0};

// Made only for shaders to read, the way the game makes its material textures, and 64 KB or more. Render targets,
// depth, UAVs, staging and dynamic textures, multisampled ones and anything already shared (or with mips the GPU makes)
// are left as they are.
template<class Desc> bool shareable(const Desc& d) {
    return (d.Usage==D3D11_USAGE_DEFAULT || d.Usage==D3D11_USAGE_IMMUTABLE) && d.BindFlags==D3D11_BIND_SHADER_RESOURCE && d.CPUAccessFlags==0 &&
           d.SampleDesc.Count==1 && (d.MiscFlags&~UINT(D3D11_RESOURCE_MISC_TEXTURECUBE))==0;
}
// One plain 2D texture: the only 2D shape the registry opens and the self-test proves. Arrays and cube maps keep no flag.
template<class Desc> bool plain(const Desc& d) { return d.ArraySize==1 && !(d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE); }
// A 3D texture made only for shaders to read (the game's fog volumes' masks: 3D noise, PT-112), as shareable() says for 2D.
template<class Desc> bool shareable_volume(const Desc& d) {
    return (d.Usage==D3D11_USAGE_DEFAULT || d.Usage==D3D11_USAGE_IMMUTABLE) && d.BindFlags==D3D11_BIND_SHADER_RESOURCE && d.CPUAccessFlags==0 && d.MiscFlags==0;
}
// A format's storage: bytes per 4x4 block (BC1 and BC4 8, the other BC formats 16), else per texel (4 for any not named).
struct Storage { bool block; UINT bytes; };
Storage storage(DXGI_FORMAT format) {
    const UINT f=UINT(format);
    if((f>=70 && f<=72) || (f>=79 && f<=81)) return {true, 8};
    if((f>=73 && f<=78) || (f>=82 && f<=84) || (f>=94 && f<=99)) return {true, 16};
    return {false, f<=4 ? 16u : f<=8 ? 12u : f<=22 ? 8u : f<=47 ? 4u : f<=59 ? 2u : f<=66 ? 1u : (f==85 || f==86 || f==115) ? 2u : 4u};
}
// One mip's rows (of blocks or texels) and the bytes of a row.
void mip_rows(DXGI_FORMAT format, UINT width, UINT height, UINT mip, UINT& rows, UINT& rowBytes) {
    const auto s=storage(format);
    const UINT w=std::max(1u, width>>mip), h=std::max(1u, height>>mip);
    rows=s.block ? (h+3)/4 : h; rowBytes=(s.block ? (w+3)/4 : w)*s.bytes;
}
// A texture's bytes with its mips and slices (D3D11's own layout, before any rounding). A shared texture is placed in 64 KB
// pages of its own, so one under 64 KB takes up to many times its memory (a 128x128 BC1 with mips, 11 KB, takes 64 KB:
// docs/agents/texture-share.md); shared all the same since PT-64 (docs/agents/materials.md has what it costs).
uint64_t texture_bytes(DXGI_FORMAT format, UINT width, UINT height, UINT mips, UINT slices) {
    if(!mips) { mips=1; for(UINT s=std::max(width, height);s>1;s>>=1) ++mips; } // 0: the whole chain
    uint64_t bytes=0;
    for(UINT m=0;m<mips;++m) { UINT rows=0, rowBytes=0; mip_rows(format, width, height, m, rows, rowBytes); bytes+=uint64_t(rows)*rowBytes; }
    return bytes*std::max(1u, slices);
}
// A 3D texture's bytes with its mips (each mip's depth halves with it).
uint64_t volume_bytes(DXGI_FORMAT format, UINT width, UINT height, UINT depth, UINT mips) {
    if(!mips) { mips=1; for(UINT s=std::max(std::max(width, height), depth);s>1;s>>=1) ++mips; }
    uint64_t bytes=0;
    for(UINT m=0;m<mips;++m) {
        UINT rows=0, rowBytes=0; mip_rows(format, width, height, m, rows, rowBytes);
        bytes+=uint64_t(rows)*rowBytes*std::max(1u, depth>>m);
    }
    return bytes;
}
// The tracer's own textures (made while the bridge marks its thread internal) are not the game's; a format the self-test
// did not prove on this card is never shared.
template<class Desc> bool wants(const Desc& d) { return shareable(d) && plain(d) && format_passed(d.Format) && !host().internal(); }
template<class Desc> bool wants_volume(const Desc& d) { return shareable_volume(d) && volume_format_passed(d.Format) && !host().internal(); }

// The game asked for desc. With the flag first; if the driver refuses, exactly as the game asked: the game never sees the
// refusal, and the texture is tagged so the registry can say why it has no number.
HRESULT STDMETHODCALLTYPE create_2d(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out) {
    const auto real=reinterpret_cast<Create2D>(realCreate2D);
    calls2D.fetch_add(1, std::memory_order_relaxed);
    if(desc && out && wants(*desc)) {
        D3D11_TEXTURE2D_DESC shared=*desc; shared.MiscFlags|=D3D11_RESOURCE_MISC_SHARED;
        const HRESULT hr=real(device, &shared, init, out);
        if(SUCCEEDED(hr)) { made.fetch_add(1, std::memory_order_relaxed); return hr; }
        refused.fetch_add(1, std::memory_order_relaxed);
        const HRESULT plain=real(device, desc, init, out);
        if(SUCCEEDED(plain) && *out) { const UINT format=UINT(desc->Format); (*out)->SetPrivateData(kRefused, sizeof(format), &format); }
        return plain;
    }
    return real(device, desc, init, out);
}
HRESULT STDMETHODCALLTYPE create_2d1(ID3D11Device3* device, const D3D11_TEXTURE2D_DESC1* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D1** out) {
    const auto real=reinterpret_cast<Create2D1>(realCreate2D1);
    calls2D1.fetch_add(1, std::memory_order_relaxed);
    if(desc && out && desc->TextureLayout==D3D11_TEXTURE_LAYOUT_UNDEFINED && wants(*desc)) {
        D3D11_TEXTURE2D_DESC1 shared=*desc; shared.MiscFlags|=D3D11_RESOURCE_MISC_SHARED;
        const HRESULT hr=real(device, &shared, init, out);
        if(SUCCEEDED(hr)) { made.fetch_add(1, std::memory_order_relaxed); return hr; }
        refused.fetch_add(1, std::memory_order_relaxed);
        const HRESULT plain=real(device, desc, init, out);
        if(SUCCEEDED(plain) && *out) { const UINT format=UINT(desc->Format); (*out)->SetPrivateData(kRefused, sizeof(format), &format); }
        return plain;
    }
    return real(device, desc, init, out);
}
// The same for 3D textures, in a format the self-test proved as a 3D texture.
HRESULT STDMETHODCALLTYPE create_3d(ID3D11Device* device, const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture3D** out) {
    const auto real=reinterpret_cast<Create3D>(realCreate3D);
    calls3D.fetch_add(1, std::memory_order_relaxed);
    if(desc && out && wants_volume(*desc)) {
        D3D11_TEXTURE3D_DESC shared=*desc; shared.MiscFlags|=D3D11_RESOURCE_MISC_SHARED;
        const HRESULT hr=real(device, &shared, init, out);
        if(SUCCEEDED(hr)) { made.fetch_add(1, std::memory_order_relaxed); return hr; }
        refused.fetch_add(1, std::memory_order_relaxed);
        const HRESULT plain=real(device, desc, init, out);
        if(SUCCEEDED(plain) && *out) { const UINT format=UINT(desc->Format); (*out)->SetPrivateData(kRefused, sizeof(format), &format); }
        return plain;
    }
    return real(device, desc, init, out);
}
HRESULT STDMETHODCALLTYPE create_3d1(ID3D11Device3* device, const D3D11_TEXTURE3D_DESC1* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture3D1** out) {
    const auto real=reinterpret_cast<Create3D1>(realCreate3D1);
    calls3D1.fetch_add(1, std::memory_order_relaxed);
    if(desc && out && desc->TextureLayout==D3D11_TEXTURE_LAYOUT_UNDEFINED && wants_volume(*desc)) {
        D3D11_TEXTURE3D_DESC1 shared=*desc; shared.MiscFlags|=D3D11_RESOURCE_MISC_SHARED;
        const HRESULT hr=real(device, &shared, init, out);
        if(SUCCEEDED(hr)) { made.fetch_add(1, std::memory_order_relaxed); return hr; }
        refused.fetch_add(1, std::memory_order_relaxed);
        const HRESULT plain=real(device, desc, init, out);
        if(SUCCEEDED(plain) && *out) { const UINT format=UINT(desc->Format); (*out)->SetPrivateData(kRefused, sizeof(format), &format); }
        return plain;
    }
    return real(device, desc, init, out);
}

// Patches d3d11.dll's own functions (the bridge's MinHook registry), which every D3D11 device in the process shares: a
// wrapper such as ReShade forwards the game's calls to them, so the hook sees what Unity makes through it. It stays in for
// the process: taking it out while a loading thread is inside it would race, and the flag costs no memory on a texture of
// 64 KB or more (the probe's measurements), path tracing on or off. Only the self-test's thread calls it, once.
bool install(ID3D11Device* device, const std::string& when) {
    static std::mutex installMutex;
    if(!device) return false;
    std::lock_guard<std::mutex> lock(installMutex);
    const uint32_t state=hookState.load();
    if(state==1 || state==2) return state==1;
    void** table=*reinterpret_cast<void***>(device);
    if(!host().add_hook(table[5], reinterpret_cast<void*>(&create_2d), &realCreate2D)) {
        hookState.store(2);
        log("Texture sharing (PT-45): the hook on the game's CreateTexture2D could not be installed; rays keep reading the texture atlas");
        return false;
    }
    // CreateTexture2D1 does not pass through CreateTexture2D (measured on the probe's device).
    bool both=false;
    ComPtr<ID3D11Device3> device3;
    if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device3)))) {
        void** table3=*reinterpret_cast<void***>(device3.Get());
        both=host().add_hook(table3[54], reinterpret_cast<void*>(&create_2d1), &realCreate2D1);
    }
    // 3D textures (fog volumes' masks, PT-112), when the self-test proved some 3D format: CreateTexture3D and CreateTexture3D1.
    std::string volumes="; no 3D format passed, so 3D textures are left as they are";
    bool anyVolume=false;
    for(const auto& bits:passedVolumeFormats) anyVolume|=bits.load()!=0;
    if(anyVolume) {
        const bool three=host().add_hook(table[6], reinterpret_cast<void*>(&create_3d), &realCreate3D);
        bool three1=false;
        if(device3) { void** table3=*reinterpret_cast<void***>(device3.Get()); three1=host().add_hook(table3[55], reinterpret_cast<void*>(&create_3d1), &realCreate3D1); }
        volumes=three ? (three1 ? "; 3D textures too (CreateTexture3D and CreateTexture3D1 hooked)" : "; 3D textures too (CreateTexture3D hooked; CreateTexture3D1 could not be)")
                      : "; the hook on CreateTexture3D could not be installed, so 3D textures are left as they are";
    }
    hookState.store(1);
    log("Texture sharing (PT-45): the game's textures made only for shaders to read, in a format the self-test passed, are made shareable from "+
        when+(both ? " (CreateTexture2D and CreateTexture2D1 hooked)" : " (CreateTexture2D hooked; CreateTexture2D1 could not be)")+volumes);
    return true;
}

// ------------------------------------------------------------------ the start-up self-test
// Microsoft promises nothing for the legacy flag on compressed textures ("behavior ... might vary from driver to driver"),
// three drivers agree today and Intel's was never tested (docs/agents/texture-share.md, "Other cards"). So before the hook
// adds the flag to anything, a worker thread shares one texture of each format the game uses, on the player's own card,
// made exactly as the hook makes them, and reads it back through the tracer's own path: the legacy handle opened on the
// shared D3D12 device, the view open() writes (view_desc), behind the fence handoff the tracer uses (D3D11 signals a
// shared fence, the D3D12 queue waits on it). Every byte of every mip copied back must equal what D3D11 was given, and
// every texel read through that view must equal what D3D12 reads from a texture it made itself from the same bytes
// (pt_share_test.hlsl). A format that fails anywhere never gets the flag: its textures keep the atlas copies. If none
// passes, the hook stays out. Every start, no cache: a driver update can break what worked (NVIDIA 580.88 broke another
// sharing path). The game's textures made before it ends are born unshared and keep their copies too.
struct TestCase { DXGI_FORMAT format; const char* name; UINT width, height; UINT depth=1; }; // depth above 1: a 3D texture
// The formats the game's textures come in (the harness's textures.txt: BC1, BC3, BC4, BC7, RGBA8, A8, RGBA16F, RGBA32F,
// RG16F, R16) and their neighbours Unity also makes (BC2, BC5, BC6H, BGRA8, RG8, R8), sRGB forms where they have one; each
// 2:1, 64 KB or more with its mips (the hook's own rule), every mip down to 1x1. The game's most common colour and normal
// map formats (BC1 sRGB, BC3) once more at 2048x1024: a large texture's memory layout.
constexpr TestCase kCases[]={
    {DXGI_FORMAT_BC1_UNORM, "BC1", 512, 256}, {DXGI_FORMAT_BC1_UNORM_SRGB, "BC1 sRGB", 512, 256}, {DXGI_FORMAT_BC2_UNORM, "BC2", 512, 256},
    {DXGI_FORMAT_BC2_UNORM_SRGB, "BC2 sRGB", 512, 256}, {DXGI_FORMAT_BC3_UNORM, "BC3", 512, 256}, {DXGI_FORMAT_BC3_UNORM_SRGB, "BC3 sRGB", 512, 256},
    {DXGI_FORMAT_BC4_UNORM, "BC4", 512, 256}, {DXGI_FORMAT_BC5_UNORM, "BC5", 512, 256}, {DXGI_FORMAT_BC6H_UF16, "BC6H", 512, 256},
    {DXGI_FORMAT_BC7_UNORM, "BC7", 512, 256}, {DXGI_FORMAT_BC7_UNORM_SRGB, "BC7 sRGB", 512, 256},
    {DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8", 256, 128}, {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "RGBA8 sRGB", 256, 128},
    {DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA8", 256, 128}, {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, "BGRA8 sRGB", 256, 128},
    {DXGI_FORMAT_R16G16B16A16_FLOAT, "RGBA16F", 256, 128}, {DXGI_FORMAT_R32G32B32A32_FLOAT, "RGBA32F", 128, 64}, {DXGI_FORMAT_R16G16_FLOAT, "RG16F", 256, 128},
    {DXGI_FORMAT_R8G8_UNORM, "RG8", 512, 256}, {DXGI_FORMAT_R16_UNORM, "R16", 512, 256}, {DXGI_FORMAT_R8_UNORM, "R8", 512, 256},
    {DXGI_FORMAT_A8_UNORM, "A8", 512, 256},
    {DXGI_FORMAT_BC1_UNORM_SRGB, "BC1 sRGB 2048x1024", 2048, 1024}, {DXGI_FORMAT_BC3_UNORM, "BC3 2048x1024", 2048, 1024},
    // 3D textures (PT-112): the game's fog volumes' masks are 32x32x32 A8 noise with mips; their neighbours, and one that is
    // not a cube (each mip's depth halves on its own).
    {DXGI_FORMAT_A8_UNORM, "A8 3D", 32, 32, 32}, {DXGI_FORMAT_R8_UNORM, "R8 3D", 32, 32, 32}, {DXGI_FORMAT_R8G8_UNORM, "RG8 3D", 32, 32, 32},
    {DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8 3D", 32, 32, 32}, {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "RGBA8 sRGB 3D", 32, 32, 32},
    {DXGI_FORMAT_R16G16B16A16_FLOAT, "RGBA16F 3D", 32, 32, 32}, {DXGI_FORMAT_BC4_UNORM, "BC4 3D", 32, 32, 32}, {DXGI_FORMAT_BC7_UNORM, "BC7 3D", 32, 32, 32},
    {DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8 3D 64x32x16", 64, 32, 16}};
constexpr uint32_t kCaseCount=uint32_t(sizeof(kCases)/sizeof(kCases[0]));

// The view open() writes for a shared texture, over all its mips (a far hit reads a small one); the self-test reads through
// the same one. UNKNOWN format: not readable here.
D3D12_SHADER_RESOURCE_VIEW_DESC view_desc(const D3D12_RESOURCE_DESC& rd) {
    D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format=view_format(rd.Format); view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if(rd.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D) { view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D; view.Texture3D.MipLevels=rd.MipLevels; }
    else { view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels=rd.MipLevels; }
    return view;
}
std::string hex(HRESULT hr) { char text[16]; std::snprintf(text, sizeof text, "0x%08lX", static_cast<unsigned long>(hr)); return text; }
double since(std::chrono::steady_clock::time_point from) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-from).count(); }
std::chrono::steady_clock::time_point testStarted;
std::atomic<uint32_t> startMicroseconds{0}; // start()'s own time on the game's thread

// Runs once, on its own thread. game: the game's D3D11 device (the hook goes on its functions); device: the shared D3D12 one.
void self_test(ComPtr<ID3D11Device> game, ComPtr<ID3D12Device> device, const std::string& when) {
    tgb::InternalScope internal(&host()); // its device and textures are none of the game's
    struct Case {
        UINT mips=0; std::vector<std::vector<uint8_t>> data; uint64_t texels=0;
        ComPtr<ID3D11Resource> d11; ComPtr<ID3D12Resource> shared, reference;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints; std::vector<UINT> rows; std::vector<UINT64> rowBytes;
        std::string why; // empty: still passing
    };
    std::vector<Case> cases(kCaseCount);
    std::string card="this card", failure;
    double tDevice=0, tTextures=0, tGpu=0, tCheck=0;
    try {
        auto lap=std::chrono::steady_clock::now();
        // The shared D3D12 device's adapter: the game's (the bridge refuses any other; checked again, Microsoft: "no
        // mechanism to share cross-adapter resources across D3D API versions").
        const LUID luid=device->GetAdapterLuid();
        ComPtr<IDXGIFactory4> factory; check(host().create_native_factory(IID_PPV_ARGS(&factory)), "DXGI factory");
        ComPtr<IDXGIAdapter1> adapter; check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "the D3D12 device's adapter");
        {
            DXGI_ADAPTER_DESC1 ad{}; adapter->GetDesc1(&ad);
            char name[128]{}; WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof name, nullptr, nullptr);
            card=name; LARGE_INTEGER umd{};
            if(SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
                char version[64]; std::snprintf(version, sizeof version, " (driver %u.%u.%u.%u)", unsigned(HIWORD(umd.HighPart)), unsigned(LOWORD(umd.HighPart)),
                                                unsigned(HIWORD(umd.LowPart)), unsigned(LOWORD(umd.LowPart)));
                card+=version;
            }
        }
        ComPtr<IDXGIDevice> gameDxgi; ComPtr<IDXGIAdapter> gameAdapter; DXGI_ADAPTER_DESC gd{};
        if(SUCCEEDED(game.As(&gameDxgi)) && SUCCEEDED(gameDxgi->GetAdapter(&gameAdapter)) && SUCCEEDED(gameAdapter->GetDesc(&gd)) &&
           (gd.AdapterLuid.LowPart!=luid.LowPart || gd.AdapterLuid.HighPart!=luid.HighPart))
            throw std::runtime_error("the D3D12 device is on another GPU than the game");
        // A D3D11 device of its own on that adapter: the game's immediate context belongs to the game's thread.
        ComPtr<ID3D11Device> d11; ComPtr<ID3D11DeviceContext> c11; ComPtr<ID3D11Device5> d11v5; ComPtr<ID3D11DeviceContext4> c11v4;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &d11, nullptr, &c11), "a D3D11 device of its own");
        check(d11.As(&d11v5), "ID3D11Device5"); check(c11.As(&c11v4), "ID3D11DeviceContext4");
        tgb::SharedFence fence; fence.create(device.Get(), d11v5.Get());
        ComPtr<ID3D12CommandQueue> queue; ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "a command queue");
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "a command allocator");
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "a command list");
        ComPtr<ID3D12RootSignature> root; ComPtr<ID3D12PipelineState> pso;
        check(device->CreateRootSignature(0, shaders::shareTest, sizeof(shaders::shareTest), IID_PPV_ARGS(&root)), "the self-test's root signature");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root.Get(); pd.CS={shaders::shareTest, sizeof(shaders::shareTest)};
        check(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)), "the self-test's pipeline");
        tDevice=since(lap); lap=std::chrono::steady_clock::now();

        // For tests only (tests/pathtracing_gpu_test.cpp): TGPT_TEST_SHARE_BREAK=<DXGI format> makes that format read one
        // wrong byte, as a driver that gets it wrong would.
        char brokenText[16]{}; const UINT broken=GetEnvironmentVariableA("TGPT_TEST_SHARE_BREAK", brokenText, sizeof brokenText) ? UINT(std::strtoul(brokenText, nullptr, 10)) : 0u;
        // The textures, every mip filled with known random bytes at creation, with the legacy flag: as the hook makes them.
        uint64_t seed=0x9E3779B97F4A7C15ull;
        for(uint32_t i=0;i<kCaseCount;++i) {
            const auto& t=kCases[i]; auto& c=cases[i];
            const bool volume=t.depth>1;
            for(UINT s=std::max(std::max(t.width, t.height), t.depth);s>0;s>>=1) ++c.mips;
            std::vector<D3D11_SUBRESOURCE_DATA> init(c.mips); c.data.resize(c.mips);
            for(UINT m=0;m<c.mips;++m) {
                UINT rows=0, rowBytes=0; mip_rows(t.format, t.width, t.height, m, rows, rowBytes);
                const UINT slices=std::max(1u, t.depth>>m);
                auto& bytes=c.data[m]; bytes.resize(size_t(rows)*rowBytes*slices);
                for(size_t k=0;k<bytes.size();k+=8) {
                    seed^=seed>>12; seed^=seed<<25; seed^=seed>>27; const uint64_t v=seed*0x2545F4914F6CDD1Dull;
                    std::memcpy(&bytes[k], &v, std::min<size_t>(8, bytes.size()-k));
                }
                init[m]={bytes.data(), rowBytes, volume ? rows*rowBytes : 0u};
                c.texels+=uint64_t(std::max(1u, t.width>>m))*std::max(1u, t.height>>m)*slices;
            }
            HRESULT hr;
            if(volume) {
                const D3D11_TEXTURE3D_DESC d{t.width, t.height, t.depth, c.mips, t.format, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_SHARED};
                ComPtr<ID3D11Texture3D> made3; hr=d11->CreateTexture3D(&d, init.data(), &made3); c.d11=made3;
            } else {
                const D3D11_TEXTURE2D_DESC d{t.width, t.height, c.mips, 1, t.format, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_SHARED};
                ComPtr<ID3D11Texture2D> made2; hr=d11->CreateTexture2D(&d, init.data(), &made2); c.d11=made2;
            }
            if(FAILED(hr)) { c.why="D3D11 refused to make it shareable ("+hex(hr)+")"; continue; }
            if(UINT(t.format)==broken) c.data[0][0]^=0x5A;
        }
        // The tracer's handoff: D3D11 signals what it wrote, the D3D12 queue waits for it.
        check(c11v4->Signal(fence.d11.Get(), 1), "signal the self-test's textures"); c11->Flush();
        tTextures=since(lap); lap=std::chrono::steady_clock::now();

        // Opened on D3D12 by the legacy handle as open() opens them; a texture of D3D12's own from the same bytes beside each.
        UINT64 offset=0;
        for(uint32_t i=0;i<kCaseCount;++i) {
            const auto& t=kCases[i]; auto& c=cases[i];
            if(!c.why.empty()) continue;
            ComPtr<IDXGIResource> dxgi; HANDLE handle=nullptr;
            if(FAILED(c.d11.As(&dxgi)) || FAILED(dxgi->GetSharedHandle(&handle)) || !handle) { c.why="no legacy handle"; continue; }
            const HRESULT opened=device->OpenSharedHandle(handle, IID_PPV_ARGS(&c.shared));
            if(FAILED(opened)) { c.why="D3D12 could not open it ("+hex(opened)+")"; continue; }
            const auto rd=c.shared->GetDesc();
            const auto dimension=t.depth>1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            if(rd.Dimension!=dimension || rd.Width!=t.width || rd.Height!=t.height || rd.DepthOrArraySize!=t.depth || rd.MipLevels!=c.mips ||
               rd.Format!=t.format || view_format(rd.Format)==DXGI_FORMAT_UNKNOWN) {
                c.why="D3D12 sees another texture ("+std::to_string(rd.Width)+"x"+std::to_string(rd.Height)+", "+std::to_string(rd.MipLevels)+" mips, format "+
                      std::to_string(unsigned(rd.Format))+")";
                c.shared.Reset(); continue;
            }
            D3D12_RESOURCE_DESC own=rd; own.Alignment=0; own.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN; own.Flags=D3D12_RESOURCE_FLAG_NONE;
            D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
            const HRESULT ref=device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &own, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&c.reference));
            if(FAILED(ref)) { c.why="D3D12 could not make its own copy to compare with ("+hex(ref)+")"; c.shared.Reset(); continue; }
            c.footprints.resize(c.mips); c.rows.resize(c.mips); c.rowBytes.resize(c.mips); UINT64 total=0;
            device->GetCopyableFootprints(&own, 0, c.mips, offset, c.footprints.data(), c.rows.data(), c.rowBytes.data(), &total);
            for(UINT m=0;m<c.mips && c.why.empty();++m) {
                UINT rows=0, rowBytes=0; mip_rows(t.format, t.width, t.height, m, rows, rowBytes);
                if(c.rows[m]!=rows || c.rowBytes[m]!=rowBytes || c.footprints[m].Footprint.Depth!=std::max(1u, t.depth>>m))
                    c.why="D3D12 lays mip "+std::to_string(m)+" out differently";
            }
            if(!c.why.empty()) { c.shared.Reset(); c.reference.Reset(); continue; }
            offset=(offset+total+D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT-1)&~UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT-1);
        }
        const UINT64 countsAt=offset, bufferBytes=offset+16ull*kCaseCount;
        auto buffer=[&](D3D12_HEAP_TYPE type, UINT64 bytes, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const char* what) {
            D3D12_HEAP_PROPERTIES hp{}; hp.Type=type; D3D12_RESOURCE_DESC bd{}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=bytes; bd.Height=1;
            bd.DepthOrArraySize=1; bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; bd.Flags=flags;
            ComPtr<ID3D12Resource> r; check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(&r)), what); return r;
        };
        auto upload=buffer(D3D12_HEAP_TYPE_UPLOAD, bufferBytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "the self-test's upload buffer");
        auto readback=buffer(D3D12_HEAP_TYPE_READBACK, bufferBytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "the self-test's read-back buffer");
        // A committed buffer starts zeroed: the counters.
        auto counts=buffer(D3D12_HEAP_TYPE_DEFAULT, 16ull*kCaseCount, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "the self-test's counters");
        {
            uint8_t* to=nullptr; check(upload->Map(0, nullptr, reinterpret_cast<void**>(&to)), "map the upload buffer");
            for(auto& c:cases) if(c.reference) for(UINT m=0;m<c.mips;++m)
                for(UINT y=0;y<c.rows[m]*c.footprints[m].Footprint.Depth;++y)
                    std::memcpy(to+c.footprints[m].Offset+size_t(y)*c.footprints[m].Footprint.RowPitch, c.data[m].data()+size_t(y)*c.rowBytes[m], size_t(c.rowBytes[m]));
            upload->Unmap(0, nullptr);
        }
        ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2*kCaseCount+1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "the self-test's descriptor heap");
        const UINT increment=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto at=[&](UINT n) { auto h=heap->GetCPUDescriptorHandleForHeapStart(); h.ptr+=size_t(n)*increment; return h; };
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=DXGI_FORMAT_R32_TYPELESS; u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
            u.Buffer.NumElements=4*kCaseCount; u.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
            device->CreateUnorderedAccessView(counts.Get(), nullptr, &u, at(2*kCaseCount));
        }
        auto barrier=[](ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
            D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition={r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to}; return b;
        };
        const auto readState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_COPY_SOURCE;
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        for(auto& c:cases) if(c.reference) barriers.push_back(barrier(c.shared.Get(), D3D12_RESOURCE_STATE_COMMON, readState));
        if(!barriers.empty()) list->ResourceBarrier(UINT(barriers.size()), barriers.data());
        for(uint32_t i=0;i<kCaseCount;++i) {
            auto& c=cases[i];
            if(!c.reference) continue;
            const auto view=view_desc(c.shared->GetDesc());
            device->CreateShaderResourceView(c.shared.Get(), &view, at(2*i)); device->CreateShaderResourceView(c.reference.Get(), &view, at(2*i+1));
            for(UINT m=0;m<c.mips;++m) {
                D3D12_TEXTURE_COPY_LOCATION from{}, to{};
                from.pResource=upload.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint=c.footprints[m];
                to.pResource=c.reference.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; to.SubresourceIndex=m;
                list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                from.pResource=c.shared.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; from.SubresourceIndex=m;
                to.pResource=readback.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint=c.footprints[m];
                list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            }
        }
        barriers.clear();
        for(auto& c:cases) if(c.reference) barriers.push_back(barrier(c.reference.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        barriers.push_back(barrier(counts.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        list->ResourceBarrier(UINT(barriers.size()), barriers.data());
        ID3D12DescriptorHeap* heaps[]={heap.Get()}; list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root.Get()); list->SetPipelineState(pso.Get());
        const UINT heapBase=0; // ResourceDescriptorHeap indices from the heap's start
        for(uint32_t i=0;i<kCaseCount;++i) {
            const auto& t=kCases[i]; const auto& c=cases[i];
            if(!c.reference) continue;
            for(UINT m=0;m<c.mips;++m) {
                const UINT w=std::max(1u, t.width>>m), h=std::max(1u, t.height>>m), slices=std::max(1u, t.depth>>m);
                const uint32_t k[8]={heapBase+2*i, heapBase+2*i+1, heapBase+2*kCaseCount, i, m, w, h, t.depth>1 ? 1u : 0u};
                list->SetComputeRoot32BitConstants(0, 8, k, 0); list->Dispatch((w+7)/8, (h+7)/8, slices);
            }
        }
        barriers.clear();
        barriers.push_back(barrier(counts.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
        for(auto& c:cases) if(c.reference) barriers.push_back(barrier(c.shared.Get(), readState, D3D12_RESOURCE_STATE_COMMON));
        list->ResourceBarrier(UINT(barriers.size()), barriers.data());
        list->CopyBufferRegion(readback.Get(), countsAt, counts.Get(), 0, 16ull*kCaseCount);
        check(list->Close(), "close the self-test's commands");
        check(queue->Wait(fence.d12.Get(), 1), "wait for D3D11's textures");
        ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1, lists);
        check(queue->Signal(fence.d12.Get(), 2), "signal the self-test's end");
        fence.wait_cpu(2, 5000);
        tGpu=since(lap); lap=std::chrono::steady_clock::now();

        // Every byte of every mip, then the texels the shader compared.
        void* mapped=nullptr; const D3D12_RANGE all{0, SIZE_T(bufferBytes)};
        check(readback->Map(0, &all, &mapped), "map the read-back buffer");
        const auto* from=static_cast<const uint8_t*>(mapped);
        for(uint32_t i=0;i<kCaseCount;++i) {
            auto& c=cases[i];
            if(!c.reference) continue;
            for(UINT m=0;m<c.mips && c.why.empty();++m)
                for(UINT y=0;y<c.rows[m]*c.footprints[m].Footprint.Depth;++y)
                    if(std::memcmp(from+c.footprints[m].Offset+size_t(y)*c.footprints[m].Footprint.RowPitch, c.data[m].data()+size_t(y)*c.rowBytes[m], size_t(c.rowBytes[m]))) {
                        c.why="D3D12 read different bytes at mip "+std::to_string(m); break;
                    }
            uint32_t n[3]; std::memcpy(n, from+countsAt+16ull*i, sizeof n);
            if(c.why.empty() && n[1]!=c.texels) c.why="the view read "+std::to_string(n[1])+" of "+std::to_string(c.texels)+" texels";
            if(c.why.empty() && n[0]) c.why=std::to_string(n[0])+" texels read differently through the tracer's view";
            if(c.why.empty() && n[2]<c.texels/4) c.why="the view read nothing";
        }
        const D3D12_RANGE none{0, 0}; readback->Unmap(0, &none);
        tCheck=since(lap);
    } catch(const std::exception& e) {
        failure=e.what();
    } catch(...) {
        failure="an unknown error";
    }
    // The verdict per format: every case of it passed (2D and 3D apart).
    uint64_t pass[4]{}, tested[4]{}, passVolume[4]{}, testedVolume[4]{};
    for(uint32_t i=0;i<kCaseCount;++i) {
        const UINT f=UINT(kCases[i].format);
        auto& t=kCases[i].depth>1 ? testedVolume : tested; auto& p=kCases[i].depth>1 ? passVolume : pass;
        t[f>>6]|=1ull<<(f&63);
        if(failure.empty() && cases[i].why.empty()) p[f>>6]|=1ull<<(f&63);
    }
    for(uint32_t i=0;i<kCaseCount;++i) if(!cases[i].why.empty() || !failure.empty()) {
        const UINT f=UINT(kCases[i].format); auto& p=kCases[i].depth>1 ? passVolume : pass; p[f>>6]&=~(1ull<<(f&63));
    }
    uint32_t formats=0, passedCount=0, casesPassed=0, volumeFormats=0, volumePassed=0;
    for(int k=0;k<4;++k) {
        for(int b=0;b<64;++b) {
            formats+=uint32_t((tested[k]>>b)&1u); passedCount+=uint32_t((pass[k]>>b)&1u);
            volumeFormats+=uint32_t((testedVolume[k]>>b)&1u); volumePassed+=uint32_t((passVolume[k]>>b)&1u);
        }
        passedFormats[k].store(pass[k]); passedVolumeFormats[k].store(passVolume[k]);
    }
    // One line: the card, the count, the time, and each case's verdict (ok, or what failed).
    std::string each;
    for(uint32_t i=0;i<kCaseCount;++i) {
        const bool ok=failure.empty() && cases[i].why.empty();
        casesPassed+=ok ? 1u : 0u;
        each+=std::string(i ? ", " : "")+kCases[i].name+(ok ? " ok" : " FAILED: "+cases[i].why);
    }
    cases.clear(); // every test texture let go of before the hook goes in
    const double total=since(testStarted);
    selfTestSummary.store(std::max(1u, passedCount|(formats<<8)|(uint32_t(std::min(total, 65535.0))<<16)));
    std::string line="Texture sharing self-test on "+card+": ";
    if(failure.empty()) {
        char numbers[256];
        std::snprintf(numbers, sizeof numbers, "%u of %u passed, %u of %u formats (and %u of %u as 3D textures), in %.0f ms on a worker thread (device and pipeline %.0f ms, "
                      "textures %.0f ms, GPU %.0f ms, check %.0f ms; %.2f ms on the game's thread): ", casesPassed, kCaseCount, passedCount, formats, volumePassed, volumeFormats,
                      total, tDevice, tTextures, tGpu, tCheck, startMicroseconds.load()/1000.0);
        line+=numbers+each;
    } else line+="could not run ("+failure+")";
    log(line);
    if(passedCount==0) {
        hookState.store(4);
        log("Texture sharing: no format passed the self-test on this card, so the hook stays out and every texture keeps its atlas copy");
        return;
    }
    if(passedCount<formats) log("Texture sharing: textures in a format that failed the self-test keep their atlas copies; the others are read directly");
    install(game.Get(), when+", "+std::to_string(int(since(testStarted)))+" ms after the game's device");
}
// The self-test on its own thread; the hook goes in when it ends (if anything passed). Once.
void start(ID3D11Device* game, ID3D12Device* device, const char* when) {
    uint32_t expected=0;
    if(!game || !device || !hookState.compare_exchange_strong(expected, 3)) return;
    testStarted=std::chrono::steady_clock::now();
    try {
        std::thread([g=ComPtr<ID3D11Device>(game), d=ComPtr<ID3D12Device>(device), w=std::string(when)] {
            SetThreadDescription(GetCurrentThread(), L"TGPT texture sharing self-test");
            self_test(g, d, w);
        }).detach();
    } catch(...) {
        hookState.store(4);
        log("Texture sharing: its self-test could not start a thread; the hook stays out and every texture keeps its atlas copy");
    }
    startMicroseconds.store(uint32_t(since(testStarted)*1000.0));
}

// ------------------------------------------------------------------ the registry
constexpr uint8_t kFree=0, kOpen=1, kDead=2;
struct Slot {
    uintptr_t texture=0;   // the D3D11 texture, as a number: no reference is held, or its death would never come
    uint32_t serial=0;     // its death notice's, which a notice must match (a texture registered again gets a new one)
    ComPtr<ID3D12Resource> resource;
    uint64_t bytes=0;
    uint64_t retireAt=0;   // dead: the consumer fence value after which no frame recorded before its death runs
    uint8_t state=kFree;
    bool published=false;  // its view is in the tracer's heap
};
struct Notice { uintptr_t texture; uint32_t serial; };
struct Registry {
    std::mutex mutex;
    // A leaf lock: the notices' list alone, never held while anything else is taken or a D3D11 object released (a texture
    // dying under the registry's own lock would otherwise deadlock).
    std::mutex noticeMutex; std::vector<Notice> notices;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12DescriptorHeap> staging; uint32_t increment=0;
    std::vector<Slot> slots; std::vector<uint32_t> freeSlots; uint32_t nextSlot=0;
    std::unordered_map<uintptr_t, uint32_t> byTexture;
    std::vector<uint32_t> unpublished, dying;
    // The tracer's heap (publish): where the range starts, and how far the GPU got.
    bool haveHeap=false; D3D12_CPU_DESCRIPTOR_HANDLE heapStart{};
    ComPtr<ID3D12Fence> fence; uint64_t retire=0;
    uint32_t serial=0, open=0, opened=0, freed=0; uint64_t openBytes=0;
    // Moves on whenever a handle may have started or stopped reading its texture (one published, one dead): the materials
    // are checked again before the next upload (resolve).
    uint64_t generation=0;
    std::string lastLine; uint64_t lastLog=0;
    // The video memory cap (within_budget): the card's adapter, the textures it kept from being opened, and its log line.
    ComPtr<IDXGIAdapter3> adapter; bool adapterTried=false;
    uint32_t overBudget=0; uint64_t lastBudgetLog=0; std::string budgetLine;
    OpenCount openCount; // PT-87 (memory3): what the opens did to Windows' count (texture_share.hpp)
    uint64_t ownShared=0; // the tracer's own textures both devices hold, bytes, as it last said (own_shared; within_budget's trial)
};
// A death not yet behind a frame: the first frame recorded after it (publish) no longer names its number, and only once
// the GPU is past that frame does its D3D12 side go.
constexpr uint64_t kPending=UINT64_MAX;
int32_t make_handle(uint32_t n, uint32_t serial) { return int32_t(n|((serial&kSerialMask)<<kNumberBits)); }
Registry& registry() { static Registry* instance=new Registry(); return *instance; } // never destroyed: textures die at exit too

// D3D11 releases it with the texture (or when the registry tags the texture again).
class Notifier final : public IUnknown {
    std::atomic<ULONG> refs{1};
    const uintptr_t texture; const uint32_t serial;
public:
    Notifier(uintptr_t t, uint32_t s) : texture(t), serial(s) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if(!out) return E_POINTER;
        if(id==__uuidof(IUnknown)) { *out=static_cast<IUnknown*>(this); AddRef(); return S_OK; }
        *out=nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left=--refs;
        if(left==0) {
            try { auto& r=registry(); std::lock_guard<std::mutex> g(r.noticeMutex); r.notices.push_back(Notice{texture, serial}); } catch(...) {}
            delete this;
        }
        return left;
    }
};

D3D12_CPU_DESCRIPTOR_HANDLE staging_view(Registry& r, uint32_t n) { auto h=r.staging->GetCPUDescriptorHandleForHeapStart(); h.ptr+=size_t(n)*r.increment; return h; }
// A typeless texture is read as its plain variant (sRGB textures are typed: their views read linear color).
DXGI_FORMAT view_format(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R8G8_TYPELESS: return DXGI_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
    case DXGI_FORMAT_BC4_TYPELESS: return DXGI_FORMAT_BC4_UNORM;
    case DXGI_FORMAT_BC5_TYPELESS: return DXGI_FORMAT_BC5_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_BC6H_TYPELESS: return DXGI_FORMAT_BC6H_UF16;
    case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM;
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_UNKNOWN; // depth: never read here
    default: return f;
    }
}

// ------------------------------------------------------------------ the video memory cap Windows counts a
// shared texture once for each device that holds it open (the probe: 430 MB counted for 216 MB on the card)
// and weighs the game's count against the budget it gives the game (IDXGIAdapter3::QueryVideoMemoryInfo).
// Over it, Windows moves the game's memory out to the PC's own (stutter, blurred textures), path tracing on
// or off: on an 8 GB card a scene's textures counted a second time could push it there. So a texture is
// opened only while the game's count, the tracer's second count of what it opened included, stays under 97 %
// of that budget, and only if it takes at most a quarter of what is left: near the budget the largest stop
// first, the small ones last. A texture not opened keeps its atlas tile (the plugin asks again now and then:
// TextureShare.Refresh); those open stay until the game frees them. 97 %, not the 90 % of the first build.
// What the rule is for is the budget itself: over it Windows moves memory out. The tracer's OTHER memory is
// not under this rule, and outdoors it is what overran the budget that day (PT-87): that is cured by the
// tracer needing less, not here.
constexpr double kBudgetShare=0.97;
constexpr uint64_t kLargestShare=4;
// Under r.mutex. What Windows gives the game of the card's own memory and what it counts for the game now (the local
// segment group: every device of the process, the game's D3D11 one and the shared D3D12 one). False: not known.
bool query_video_memory(Registry& r, DXGI_QUERY_VIDEO_MEMORY_INFO& memory) {
    if(!r.adapterTried && r.device) {
        r.adapterTried=true;
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter3> adapter;
        if(SUCCEEDED(host().create_native_factory(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapterByLuid(r.device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))))
            r.adapter=adapter;
    }
    memory={};
    return r.adapter && SUCCEEDED(r.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)) && memory.Budget;
}
// Under r.mutex. Whether a texture of bytes may be opened now; when not, the log line waits in r.budgetLine (at most one
// every 30 s), written once the lock is let go.
bool within_budget(Registry& r, uint64_t bytes) {
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if(!query_video_memory(r, memory)) return true; // unknown: no cap
    uint64_t limit=uint64_t(double(memory.Budget)*kBudgetShare);
    // For tests only (tests/pathtracing_gpu_test.cpp): TGPT_TEST_SHARE_LIMIT_MB=<n> puts the limit at n MB.
    char text[24]{};
    const bool testLimit=GetEnvironmentVariableA("TGPT_TEST_SHARE_LIMIT_MB", text, sizeof text)>0;
    if(testLimit) limit=uint64_t(std::strtoull(text, nullptr, 10))<<20;
    // PT-87: Track actual memory use and residency; do not limit scene content to meet a budget.
    const bool once=GetEnvironmentVariableA("TGPT_TEST_SHARE_COUNT_ONCE", nullptr, 0)>0;
    const uint64_t secondCounts=once ? r.openBytes+r.ownShared : 0;
    const uint64_t usage=memory.CurrentUsage-std::min<uint64_t>(memory.CurrentUsage, secondCounts);
    const uint64_t left=limit>usage ? limit-usage : 0;
    if(bytes*kLargestShare<=left) return true;
    ++r.overBudget;
    const uint64_t now=GetTickCount64();
    if(r.lastBudgetLog==0 || now-r.lastBudgetLog>=30000) {
        r.lastBudgetLog=now;
        char line[512];
        std::snprintf(line, sizeof line, "Texture sharing: video memory near this card's budget (Windows gives the game %llu MB and counts %llu MB, the tracer's %u open "
                      "textures, %llu MB, a second time among them%s): textures over %.1f MB are not opened now (a quarter of the %llu MB left under %s), %u kept on "
                      "hold path tracing until their real textures can be read",
                      (unsigned long long)(memory.Budget>>20), (unsigned long long)(memory.CurrentUsage>>20), r.open, (unsigned long long)(r.openBytes>>20),
                      once ? "; on trial: the second counts are taken out of the count before it is weighed" : "",
                      double(left/kLargestShare)/1048576.0, (unsigned long long)(left>>20), testLimit ? "the test's limit" : "97% of the budget", r.overBudget);
        r.budgetLine=line;
    }
    return false;
}

// Under r.mutex. A texture let go of: from now on no material resolves to its number (resolve), and its D3D12 side goes
// once the GPU has finished the first frame recorded after this (publish gives it that frame's fence value): frames
// recorded before it, and the water pass after it, may still read it.
void kill(Registry& r, uint32_t n) {
    auto& s=r.slots[n];
    if(s.state!=kOpen) return;
    s.state=kDead; s.retireAt=r.retire;
    r.dying.push_back(n);
    ++r.generation;
}
// Under r.mutex: the death notices since the last look. One for a texture registered again since (another serial) is old.
void take_notices(Registry& r) {
    std::vector<Notice> got;
    { std::lock_guard<std::mutex> g(r.noticeMutex); got.swap(r.notices); }
    for(const auto& notice:got) {
        const auto found=r.byTexture.find(notice.texture);
        if(found==r.byTexture.end() || r.slots[found->second].serial!=notice.serial) continue;
        kill(r, found->second);
        r.byTexture.erase(found);
        ++r.freed;
    }
}
// Under r.mutex: the dead the GPU is done with let go of; a view the tracer's heap holds becomes a null one.
void release_due(Registry& r) {
    if(r.dying.empty()) return;
    const uint64_t completed=r.fence ? r.fence->GetCompletedValue() : UINT64_MAX;
    size_t kept=0;
    for(const uint32_t n:r.dying) {
        auto& s=r.slots[n];
        if(s.retireAt>completed) { r.dying[kept++]=n; continue; }
        if(s.published && r.haveHeap && r.device) {
            D3D12_SHADER_RESOURCE_VIEW_DESC none{}; none.Format=DXGI_FORMAT_R8G8B8A8_UNORM; none.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if(s.resource && s.resource->GetDesc().Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D) { none.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D; none.Texture3D.MipLevels=1; }
            else { none.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; none.Texture2D.MipLevels=1; }
            D3D12_CPU_DESCRIPTOR_HANDLE at=r.heapStart; at.ptr+=size_t(n)*r.increment;
            r.device->CreateShaderResourceView(nullptr, &none, at);
        }
        --r.open; r.openBytes-=s.bytes;
        s=Slot{};
        r.freeSlots.push_back(n);
    }
    r.dying.resize(kept);
}
void totals(Registry& r, TGPT_SharedTexture* info) {
    if(!info) return;
    info->open=r.open; info->opened=r.opened; info->freed=r.freed; info->openBytes=r.openBytes; info->hook=hookState.load();
    info->selfTest=selfTestSummary.load();
}
// Why a texture without the flag has none.
int32_t why_unshared(ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC& d) {
    if(!shareable(d)) return TGPT_SHARE_FLAGS;
    if(!plain(d)) return TGPT_SHARE_SHAPE;
    const uint32_t state=hookState.load();
    if((state==1 || state==4) && !format_passed(d.Format)) return TGPT_SHARE_SELF_TEST;
    UINT format=0, size=sizeof(format);
    if(SUCCEEDED(texture->GetPrivateData(kRefused, &size, &format)) && size==sizeof(format)) return TGPT_SHARE_REFUSED;
    return TGPT_SHARE_BEFORE_HOOK;
}
int32_t why_unshared_volume(ID3D11Texture3D* texture, const D3D11_TEXTURE3D_DESC& d) {
    if(!shareable_volume(d)) return TGPT_SHARE_FLAGS;
    const uint32_t state=hookState.load();
    if((state==1 || state==4) && !volume_format_passed(d.Format)) return TGPT_SHARE_SELF_TEST;
    UINT format=0, size=sizeof(format);
    if(SUCCEEDED(texture->GetPrivateData(kRefused, &size, &format)) && size==sizeof(format)) return TGPT_SHARE_REFUSED;
    return TGPT_SHARE_BEFORE_HOOK;
}
// Under r.mutex: a shared texture opened on D3D12, its view written, a number given.
// volume: a 3D texture (its view is a 3D one); bytes: its texels with their mips (texture_bytes, volume_bytes).
int32_t open(Registry& r, ID3D12Device* device, ID3D11Resource* texture, bool volume, uint64_t bytes, TGPT_SharedTexture* info) {
    const uintptr_t key=uintptr_t(texture);
    if(const auto found=r.byTexture.find(key); found!=r.byTexture.end()) {
        const auto& s=r.slots[found->second];
        if(info) {
            info->bytes=s.bytes; info->resource=uint64_t(uintptr_t(s.resource.Get())); info->view=uint64_t(staging_view(r, found->second).ptr);
            info->handle=make_handle(found->second, s.serial);
        }
        return int32_t(found->second);
    }
    if(!r.device) r.device=device; // the shared device, for the process
    if(!r.staging) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=kMaxShared; // CPU only: copied from
        if(FAILED(r.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&r.staging)))) return TGPT_SHARE_OPEN_FAILED;
        r.increment=r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        r.slots.resize(kMaxShared);
    }
    if(info) info->bytes=(bytes+65535)&~uint64_t(65535);
    // Its memory as a shared texture takes it (in 64 KB pages), weighed against the card's budget first.
    if(!within_budget(r, (bytes+65535)&~uint64_t(65535))) return TGPT_SHARE_BUDGET;
    uint32_t n;
    if(!r.freeSlots.empty()) { n=r.freeSlots.back(); r.freeSlots.pop_back(); }
    else if(r.nextSlot<kMaxShared) n=r.nextSlot++;
    else return TGPT_SHARE_FULL;
    auto refuse=[&](int32_t why) { r.freeSlots.push_back(n); return why; };
    // The legacy handle: a number, nothing to close.
    ComPtr<IDXGIResource> dxgi; HANDLE handle=nullptr;
    if(FAILED(texture->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetSharedHandle(&handle)) || !handle) return refuse(TGPT_SHARE_OPEN_FAILED);
    // (PT-87, memory3: Windows' count for the process right before and right after the open, summed in r.openCount.)
    DXGI_QUERY_VIDEO_MEMORY_INFO countBefore{}, countAfter{};
    const bool counting=query_video_memory(r, countBefore);
    ComPtr<ID3D12Resource> resource;
    if(FAILED(r.device->OpenSharedHandle(handle, IID_PPV_ARGS(&resource)))) return refuse(TGPT_SHARE_OPEN_FAILED);
    const bool counted=counting && query_video_memory(r, countAfter);
    const auto rd=resource->GetDesc();
    const bool shape=volume ? rd.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D : rd.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && rd.DepthOrArraySize==1;
    if(view_format(rd.Format)==DXGI_FORMAT_UNKNOWN || !shape) return refuse(TGPT_SHARE_OPEN_FAILED);
    // Every mip: a far hit reads a small one. The very view the self-test read through.
    const auto view=view_desc(rd);
    r.device->CreateShaderResourceView(resource.Get(), &view, staging_view(r, n));
    // Its death notice. Without one, D3D12's reference would keep the memory after the game frees the texture: not opened.
    // A handle's serial is never 0 (as kept: 17 bits), so a material's zeroed fields name no texture.
    uint32_t serial=++r.serial;
    if((serial&kSerialMask)==0) serial=++r.serial;
    auto* notifier=new Notifier(key, serial);
    const HRESULT tagged=texture->SetPrivateDataInterface(kNotice, notifier);
    notifier->Release(); // the texture holds the only reference now
    if(FAILED(tagged)) return refuse(TGPT_SHARE_OPEN_FAILED);
    auto& s=r.slots[n];
    s=Slot{}; s.texture=key; s.serial=serial; s.resource=resource; s.state=kOpen;
    s.bytes=r.device->GetResourceAllocationInfo(0, 1, &rd).SizeInBytes;
    r.byTexture[key]=n; r.unpublished.push_back(n);
    ++r.open; ++r.opened; r.openBytes+=s.bytes;
    if(counted) { ++r.openCount.textures; r.openCount.bytes+=s.bytes; r.openCount.moved+=int64_t(countAfter.CurrentUsage)-int64_t(countBefore.CurrentUsage); }
    if(info) {
        info->bytes=s.bytes; info->resource=uint64_t(uintptr_t(resource.Get())); info->view=uint64_t(staging_view(r, n).ptr);
        info->handle=make_handle(n, serial);
    }
    return int32_t(n);
}
// Under r.mutex: the number a handle reads this frame, or -1 (none, dead, another texture's since, not in the heap yet).
int32_t resolve_locked(const Registry& r, int32_t handle) {
    if(handle<0) return -1;
    const uint32_t n=uint32_t(handle)&((1u<<kNumberBits)-1u), serial=uint32_t(handle)>>kNumberBits;
    if(n>=r.slots.size()) return -1;
    const auto& s=r.slots[n];
    return s.state==kOpen && s.published && r.haveHeap && (s.serial&kSerialMask)==serial ? int32_t(n) : -1;
}
// A line in the native log when anything changed, at most every 30 s (r.mutex not held).
void report() {
    auto& r=registry();
    std::string line;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        const uint64_t now=GetTickCount64();
        if(now-r.lastLog<30000) return;
        const uint64_t a=calls2D.load(), b=calls2D1.load();
        const uint64_t volumes=calls3D.load()+calls3D1.load();
        line="Texture sharing: the game made "+std::to_string(a)+" textures through CreateTexture2D and "+std::to_string(b)+
            " through CreateTexture2D1 (and "+std::to_string(volumes)+" 3D textures); "+std::to_string(made.load())+" made shareable, "+std::to_string(refused.load())+" refused by the driver; "+
            std::to_string(r.open)+" open on D3D12 ("+std::to_string((r.openBytes+(1ull<<19))>>20)+" MB), "+std::to_string(r.opened)+" opened and "+
            std::to_string(r.freed)+" freed by the game since the start"+(r.overBudget ? "; "+std::to_string(r.overBudget)+" not opened near the video memory budget" : "");
        if(line==r.lastLine) return;
        r.lastLine=line; r.lastLog=now;
    }
    log(line);
}
} // namespace

// Whatever the Mode row says: the harness and players turn path tracing on after the world has loaded, and a texture made
// before the hook can never be shared (2026-10-06's runs: 485 made before it with Mode Off at launch). The hook goes in
// once the self-test on its own thread has passed some format.
void game_device_ready(ID3D11Device* game, ID3D12Device* device) {
    start(game, device, "the game's device on");
}
int32_t register_texture(ID3D12Device* device, uint64_t pointer, TGPT_SharedTexture* info) {
    auto& r=registry();
    ComPtr<ID3D11Texture2D> texture;
    if(!pointer || FAILED(reinterpret_cast<IUnknown*>(uintptr_t(pointer))->QueryInterface(IID_PPV_ARGS(&texture)))) {
        // A 3D texture (a fog volume's mask, PT-112), kept by its ID3D11Texture3D interface.
        ComPtr<ID3D11Texture3D> volume;
        if(!pointer || FAILED(reinterpret_cast<IUnknown*>(uintptr_t(pointer))->QueryInterface(IID_PPV_ARGS(&volume)))) {
            std::lock_guard<std::mutex> lock(r.mutex); totals(r, info); return TGPT_SHARE_NOT_TEXTURE;
        }
        D3D11_TEXTURE3D_DESC d{}; volume->GetDesc(&d);
        const uint64_t bytes=volume_bytes(d.Format, d.Width, d.Height, d.Depth, d.MipLevels);
        if(info) {
            info->width=d.Width; info->height=d.Height; info->mips=d.MipLevels; info->arraySize=d.Depth; info->format=uint32_t(d.Format);
            info->bytes=(bytes+65535)&~uint64_t(65535); info->usage=uint32_t(d.Usage); info->bindFlags=d.BindFlags; info->miscFlags=d.MiscFlags;
        }
        if(hookState.load()==0) start(host().game_device(), device, "now on (the first texture path tracing registered)");
        int32_t result;
        if(!(d.MiscFlags&D3D11_RESOURCE_MISC_SHARED)) result=why_unshared_volume(volume.Get(), d);
        else if(!volume_format_passed(d.Format)) result=TGPT_SHARE_SELF_TEST;
        else {
            std::string budgetLine;
            {
                std::lock_guard<std::mutex> lock(r.mutex);
                take_notices(r); release_due(r);
                result=open(r, device, volume.Get(), true, bytes, info);
                totals(r, info);
                budgetLine.swap(r.budgetLine);
            }
            if(!budgetLine.empty()) log(budgetLine);
            return result;
        }
        std::lock_guard<std::mutex> lock(r.mutex); totals(r, info);
        return result;
    }
    D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
    if(info) {
        info->width=d.Width; info->height=d.Height; info->mips=d.MipLevels; info->arraySize=d.ArraySize; info->format=uint32_t(d.Format);
        info->bytes=(texture_bytes(d.Format,d.Width,d.Height,d.MipLevels,d.ArraySize)+65535)&~uint64_t(65535);
        info->usage=uint32_t(d.Usage); info->bindFlags=d.BindFlags; info->miscFlags=d.MiscFlags;
    }
    // The self-test starts with the game's device; should that have been missed, it starts now.
    if(hookState.load()==0) start(host().game_device(), device, "now on (the first texture path tracing registered)");
    int32_t result;
    if(!(d.MiscFlags&D3D11_RESOURCE_MISC_SHARED)) result=why_unshared(texture.Get(), d);
    else if(!plain(d)) result=TGPT_SHARE_SHAPE;
    else if(!format_passed(d.Format)) result=TGPT_SHARE_SELF_TEST; // shared by another hand, in a format not proven on this card
    else {
        std::string budgetLine;
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            // Notices first: a texture the game freed may have left its address to this one.
            take_notices(r); release_due(r);
            result=open(r, device, texture.Get(), false, texture_bytes(d.Format, d.Width, d.Height, d.MipLevels, 1), info);
            totals(r, info);
            budgetLine.swap(r.budgetLine);
        }
        if(!budgetLine.empty()) log(budgetLine);
        return result;
    }
    std::lock_guard<std::mutex> lock(r.mutex); totals(r, info);
    return result;
}
uint64_t memory_bytes() { auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex); return r.openBytes; }
void open_bytes(std::vector<uint64_t>& bytes) {
    auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex);
    bytes.assign(r.slots.size(), 0);
    for(size_t n=0;n<r.slots.size();++n) if(r.slots[n].state==kOpen) bytes[n]=r.slots[n].bytes;
}
bool video_memory(uint64_t& budget, uint64_t& counted, ID3D12Device* device) {
    auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex);
    if(!r.device && device) r.device=device; // the shared device, for the process (as open takes it)
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if(!query_video_memory(r, memory)) { budget=counted=0; return false; }
    budget=memory.Budget; counted=memory.CurrentUsage; return true;
}
bool video_memory_now(uint64_t& local, uint64_t& nonLocal, ID3D12Device* device) {
    auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex);
    if(!r.device && device) r.device=device;
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{}, other{};
    if(!query_video_memory(r, memory)) { local=nonLocal=0; return false; }
    local=memory.CurrentUsage;
    nonLocal=SUCCEEDED(r.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &other)) ? other.CurrentUsage : 0;
    return true;
}
OpenCount open_count() { auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex); return r.openCount; }
void own_shared(uint64_t bytes) { auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex); r.ownShared=bytes; }

void collect() {
    auto& r=registry();
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        take_notices(r); release_due(r);
    }
    if(hookState.load()==1) report();
}
void clear() {
    auto& r=registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    take_notices(r);
    for(const auto& entry:r.byTexture) kill(r, entry.second);
    r.byTexture.clear();
    release_due(r);
}
void publish(ID3D12Device* device, DescriptorHeap& heap, uint32_t base, ID3D12Fence* fence, uint64_t retire) {
    auto& r=registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.fence=fence; r.retire=std::max(r.retire, retire);
    if(base==kInvalid || !heap.get()) return;
    r.haveHeap=true; r.heapStart=heap.cpu(base);
    if(!r.increment) r.increment=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    take_notices(r);
    // The deaths since the last frame: this frame's materials no longer name them (the scene resolves its handles again
    // after this), so they go once the GPU is past this frame.
    for(const uint32_t n:r.dying) if(r.slots[n].retireAt==kPending) r.slots[n].retireAt=retire;
    release_due(r);
    for(const uint32_t n:r.unpublished) {
        auto& s=r.slots[n];
        if(s.state!=kOpen || s.published) continue;
        D3D12_CPU_DESCRIPTOR_HANDLE at=r.heapStart; at.ptr+=size_t(n)*r.increment;
        device->CopyDescriptorsSimple(1, at, staging_view(r, n), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        s.published=true;
        ++r.generation;
    }
    r.unpublished.clear();
}
uint64_t generation() {
    auto& r=registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.generation;
}
void resolve(TGPT_Material* materials, size_t count) {
    auto& r=registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    for(size_t i=0;i<count;++i) {
        auto& m=materials[i];
        m.colorTexture=resolve_locked(r, m.colorTexture);
        m.normalTexture=resolve_locked(r, m.normalTexture);
        m.maskTexture=resolve_locked(r, m.maskTexture);
        m.emissiveTexture=resolve_locked(r, m.emissiveTexture);
        const bool any=m.colorTexture>=0 || m.normalTexture>=0 || m.maskTexture>=0 || m.emissiveTexture>=0;
        m.flags=any ? (m.flags|TGPT_MATERIAL_SHARED) : (m.flags&~TGPT_MATERIAL_SHARED);
    }
}
void resolve(int32_t* handles, size_t count) {
    auto& r=registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    for(size_t i=0;i<count;++i) handles[i]=resolve_locked(r, handles[i]);
}
bool dimensions(int32_t number, uint32_t& width, uint32_t& height) {
    auto& r=registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    if(number<0 || size_t(number)>=r.slots.size() || !r.slots[size_t(number)].resource) return false;
    const auto d=r.slots[size_t(number)].resource->GetDesc();
    width=uint32_t(d.Width); height=d.Height;
    return width>0 && height>0;
}
int32_t handle_of(uint64_t pointer) {
    // Keyed as register_texture keys it: by its ID3D11Texture2D interface (a 3D texture by its ID3D11Texture3D one).
    if(!pointer) return -1;
    uintptr_t key=0;
    ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11Texture3D> volume;
    if(SUCCEEDED(reinterpret_cast<IUnknown*>(uintptr_t(pointer))->QueryInterface(IID_PPV_ARGS(&texture)))) key=uintptr_t(texture.Get());
    else if(SUCCEEDED(reinterpret_cast<IUnknown*>(uintptr_t(pointer))->QueryInterface(IID_PPV_ARGS(&volume)))) key=uintptr_t(volume.Get());
    else return -1;
    auto& r=registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    const auto found=r.byTexture.find(key);
    if(found==r.byTexture.end() || r.slots[found->second].state!=kOpen) return -1;
    return make_handle(found->second, r.slots[found->second].serial);
}
void heap_lost() {
    try { // release_all's error paths call it
        auto& r=registry();
        std::lock_guard<std::mutex> lock(r.mutex);
        r.haveHeap=false; r.fence.Reset(); r.retire=0; ++r.generation;
        r.unpublished.clear();
        for(uint32_t n=0;n<uint32_t(r.slots.size());++n) {
            auto& s=r.slots[n];
            s.published=false;
            if(s.state==kOpen) r.unpublished.push_back(n);
        }
        release_due(r);
    } catch(...) {}
}
} // namespace tgpt::share
