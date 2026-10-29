// share-probe: can a D3D12 device open D3D11 textures made like a game's, with no copy?
//   share-probe matrix   features + one line per texture case (debug layers on when installed)
//   share-probe cost     video memory, sampling time, many textures, the CreateTexture2D hook (no debug layers)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include <pdh.h>
#pragma comment(lib, "pdh.lib")
using Microsoft::WRL::ComPtr;

static FILE* g_log = nullptr;
static void say(const char* f, ...) {
    char b[8192]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
    fputs(b, stdout); fflush(stdout); if (g_log) { fputs(b, g_log); fflush(g_log); }
}
static std::string hx(HRESULT hr) { char b[16]; snprintf(b, sizeof b, "0x%08X", unsigned(hr)); return b; }
#define CHECK(x) do { HRESULT h_ = (x); if (FAILED(h_)) { say("FATAL %s at line %d: %s\n", hx(h_).c_str(), __LINE__, #x); exit(2); } } while (0)
static double now_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

constexpr UINT kShared = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
static bool g_legacy = false; // the matrix with the old-style flag: SHARED alone, a global handle from IDXGIResource::GetSharedHandle
// Which adapter (second argument): "high" (default), "warp" (both on EnumWarpAdapter), "warpdt" (D3D11 D3D_DRIVER_TYPE_WARP,
// D3D12 on EnumWarpAdapter), "vendor:0x1002", or any part of the adapter's description ("Radeon").
static std::string g_pick = "high";
static wchar_t g_luidFilter[64] = L""; // the chosen adapter's LUID as Windows' GPU counters name it, lower case
static bool g_cpuSync = false;         // no shared fence on this adapter: the CPU waits for D3D11 instead

// Windows' own video memory counters (Task Manager reads the same): this process, and the chosen adapter only.
// Dedicated and shared (system memory the GPU uses, which is all an integrated GPU has beyond its carve-out) summed.
static double pdh_mb(const wchar_t* path, const wchar_t* filter) {
    PDH_HQUERY q = nullptr; if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return -1;
    PDH_HCOUNTER h = nullptr; if (PdhAddEnglishCounterW(q, path, 0, &h) != ERROR_SUCCESS) { PdhCloseQuery(q); return -1; }
    PdhCollectQueryData(q);
    DWORD size = 0, count = 0; PdhGetFormattedCounterArrayW(h, PDH_FMT_LARGE, &size, &count, nullptr);
    std::vector<uint8_t> buf(size + 64); auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data()); double total = 0;
    if (PdhGetFormattedCounterArrayW(h, PDH_FMT_LARGE, &size, &count, items) == ERROR_SUCCESS)
        for (DWORD i = 0; i < count; ++i) {
            std::wstring n = items[i].szName; for (auto& ch : n) ch = towlower(ch);
            if ((!filter || n.find(filter) != std::wstring::npos) && (!g_luidFilter[0] || n.find(g_luidFilter) != std::wstring::npos)) total += double(items[i].FmtValue.largeValue);
        }
    PdhCloseQuery(q); return total / (1024.0 * 1024.0);
}
static double process_mb() { wchar_t f[32]; swprintf(f, 32, L"pid_%lu_", GetCurrentProcessId()); return pdh_mb(L"\\GPU Process Memory(*)\\Dedicated Usage", f) + pdh_mb(L"\\GPU Process Memory(*)\\Shared Usage", f); }
static double card_mb() { return pdh_mb(L"\\GPU Adapter Memory(*)\\Dedicated Usage", nullptr) + pdh_mb(L"\\GPU Adapter Memory(*)\\Shared Usage", nullptr); }

struct Fmt { DXGI_FORMAT f; const char* name; uint32_t bytes; bool bc; DXGI_FORMAT view[2]; };
static const Fmt kFormats[] = {
    {DXGI_FORMAT_BC1_UNORM, "BC1_UNORM", 8, true, {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC1_UNORM_SRGB, "BC1_SRGB", 8, true, {DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC1_TYPELESS, "BC1_TYPELESS", 8, true, {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM_SRGB}},
    {DXGI_FORMAT_BC3_UNORM, "BC3_UNORM", 16, true, {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC3_UNORM_SRGB, "BC3_SRGB", 16, true, {DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC3_TYPELESS, "BC3_TYPELESS", 16, true, {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB}},
    {DXGI_FORMAT_BC4_UNORM, "BC4_UNORM", 8, true, {DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC5_UNORM, "BC5_UNORM", 16, true, {DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC6H_UF16, "BC6H_UF16", 16, true, {DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC7_UNORM, "BC7_UNORM", 16, true, {DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC7_UNORM_SRGB, "BC7_SRGB", 16, true, {DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_BC7_TYPELESS, "BC7_TYPELESS", 16, true, {DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB}},
    {DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8_UNORM", 4, false, {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "RGBA8_SRGB", 4, false, {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN}},
    {DXGI_FORMAT_R8G8B8A8_TYPELESS, "RGBA8_TYPELESS", 4, false, {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}},
};
static const Fmt& fmt(DXGI_FORMAT f) { for (auto& x : kFormats) if (x.f == f) return x; return kFormats[0]; }

static uint32_t full_mips(uint32_t w, uint32_t h) { uint32_t m = 1; while (w > 1 || h > 1) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); ++m; } return m; }
struct Sub { uint32_t w, h, rowBytes, rows; };
static Sub sub_info(const Fmt& f, uint32_t W, uint32_t H, uint32_t mip) {
    const uint32_t w = std::max(1u, W >> mip), h = std::max(1u, H >> mip);
    if (f.bc) return {w, h, ((w + 3) / 4) * f.bytes, (h + 3) / 4};
    return {w, h, w * f.bytes, h};
}
static uint64_t texture_bytes(const Fmt& f, uint32_t W, uint32_t H, uint32_t mips, uint32_t slices) {
    uint64_t t = 0; for (uint32_t m = 0; m < mips; ++m) { auto s = sub_info(f, W, H, m); t += uint64_t(s.rowBytes) * s.rows; } return t * slices;
}
static std::vector<std::vector<uint8_t>> make_data(const Fmt& f, uint32_t W, uint32_t H, uint32_t mips, uint32_t slices, uint64_t seed) {
    std::vector<std::vector<uint8_t>> d(size_t(mips) * slices);
    for (uint32_t s = 0; s < slices; ++s) for (uint32_t m = 0; m < mips; ++m) {
        auto si = sub_info(f, W, H, m); auto& v = d[m + s * mips]; v.resize(size_t(si.rowBytes) * si.rows);
        uint64_t x = seed * 0x9E3779B97F4A7C15ull + (uint64_t(s) << 32) + m + 1;
        for (auto& b : v) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; b = uint8_t(x >> 24); }
    }
    return d;
}

static const char* kHashHlsl = R"(
Texture2DArray<float4> tex : register(t0);
RWByteAddressBuffer outBuf : register(u0);
cbuffer C : register(b0) { uint mip; uint slice; uint w; uint h; uint slot; uint p0; uint p1; uint p2; };
uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= w || id.y >= h) return;
    float4 v = tex.Load(int4(id.x, id.y, slice, mip));
    uint idx = id.y * w + id.x;
    uint hv = hash(asuint(v.x) ^ hash(asuint(v.y) ^ hash(asuint(v.z) ^ hash(asuint(v.w) ^ hash(idx)))));
    uint o; outBuf.InterlockedAdd(slot * 8, hv, o); outBuf.InterlockedXor(slot * 8 + 4, hash(hv + idx), o);
})";
static const char* kSampleHlsl = R"(
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
RWByteAddressBuffer o : register(u0);
cbuffer C : register(b0) { uint mode; uint size; uint iters; uint pad; };
uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
    float4 acc = 0; float inv = 1.0 / size;
    [loop] for (uint i = 0; i < iters; ++i) {
        float2 uv; float lod;
        if (mode == 0) { uv = (float2(id.xy) + 0.5 + float2(i * 3, i * 7)) * inv; lod = 0; }
        else { uint hh = hash(id.x * 73856093u ^ id.y * 19349663u ^ i * 83492791u); uv = float2(hh & 0xffffu, hh >> 16) / 65536.0; lod = float(hh & 3u) * 0.5; }
        acc += tex.SampleLevel(smp, uv, lod);
    }
    o.Store(((id.y * 64 + id.x) & 1023) * 4, asuint(dot(acc, 1)));
})";
static ComPtr<ID3DBlob> compile(const char* src, const char* target) {
    ComPtr<ID3DBlob> code, err;
    if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
        say("shader compile failed: %s\n", err ? (const char*)err->GetBufferPointer() : "?"); exit(2);
    }
    return code;
}

struct Ctx {
    ComPtr<IDXGIAdapter3> adapter;
    ComPtr<ID3D11Device5> d11; ComPtr<ID3D11DeviceContext4> c11; ComPtr<ID3D11Query> idle;
    ComPtr<ID3D12Device> d12; ComPtr<ID3D12CommandQueue> q;
    ComPtr<ID3D12CommandAllocator> alloc; ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> f12; ComPtr<ID3D11Fence> f11; uint64_t fv = 0;
    ComPtr<ID3D12Fence> cpu; uint64_t cv = 0; HANDLE ev = nullptr;
    ComPtr<ID3D12InfoQueue> iq12; ComPtr<ID3D11InfoQueue> iq11;
    ComPtr<ID3D11ComputeShader> hash11; ComPtr<ID3D11Buffer> hb11, hs11, cb11; ComPtr<ID3D11UnorderedAccessView> hu11;
    ComPtr<ID3D12RootSignature> rs; ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12Resource> hb12, hz12, hr12; ComPtr<ID3D12DescriptorHeap> heap; UINT inc = 0;
};

static void wait11(Ctx& c) {
    c.c11->End(c.idle.Get()); c.c11->Flush(); BOOL done = FALSE; const double t = now_ms();
    while (c.c11->GetData(c.idle.Get(), &done, sizeof done, 0) != S_OK) { if (now_ms() - t > 20000) { say("FATAL D3D11 hang, removed reason %s\n", hx(c.d11->GetDeviceRemovedReason()).c_str()); exit(3); } Sleep(0); }
}
static void wait12(Ctx& c) {
    CHECK(c.q->Signal(c.cpu.Get(), ++c.cv));
    if (c.cpu->GetCompletedValue() < c.cv) {
        CHECK(c.cpu->SetEventOnCompletion(c.cv, c.ev));
        if (WaitForSingleObject(c.ev, 20000) != WAIT_OBJECT_0) { say("FATAL D3D12 hang, removed reason %s\n", hx(c.d12->GetDeviceRemovedReason()).c_str()); exit(3); }
    }
}
static void run12(Ctx& c) {
    CHECK(c.list->Close()); ID3D12CommandList* l[] = {c.list.Get()}; c.q->ExecuteCommandLists(1, l); wait12(c);
    CHECK(c.alloc->Reset()); CHECK(c.list->Reset(c.alloc.Get(), nullptr));
}
static void d11_to_d12(Ctx& c) {
    if (g_cpuSync) { wait11(c); return; } // D3D11 has finished everything before D3D12 is given any work
    CHECK(c.c11->Signal(c.f11.Get(), ++c.fv)); c.c11->Flush(); CHECK(c.q->Wait(c.f12.Get(), c.fv));
}

static ComPtr<ID3D12Resource> buffer12(Ctx& c, uint64_t size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = type;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
    ComPtr<ID3D12Resource> r; CHECK(c.d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r))); return r;
}
static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER x{}; x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.pResource = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; x.Transition.StateBefore = a; x.Transition.StateAfter = b; return x;
}

// Debug-layer messages at warning or worse since the last call; the first one's text goes to `first`.
static int drain(Ctx& c, std::string* first) {
    int n = 0;
    if (c.iq12) {
        for (UINT64 i = 0, k = c.iq12->GetNumStoredMessages(); i < k; ++i) {
            SIZE_T len = 0; c.iq12->GetMessage(i, nullptr, &len); std::vector<char> b(len); auto m = reinterpret_cast<D3D12_MESSAGE*>(b.data());
            if (SUCCEEDED(c.iq12->GetMessage(i, m, &len)) && m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) { ++n; if (first && first->empty()) *first = std::string("D3D12: ") + m->pDescription; }
        }
        c.iq12->ClearStoredMessages();
    }
    if (c.iq11) {
        for (UINT64 i = 0, k = c.iq11->GetNumStoredMessages(); i < k; ++i) {
            SIZE_T len = 0; c.iq11->GetMessage(i, nullptr, &len); std::vector<char> b(len); auto m = reinterpret_cast<D3D11_MESSAGE*>(b.data());
            if (SUCCEEDED(c.iq11->GetMessage(i, m, &len)) && m->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) { ++n; if (first && first->empty()) *first = std::string("D3D11: ") + m->pDescription; }
        }
        c.iq11->ClearStoredMessages();
    }
    return n;
}

static void init(Ctx& c, bool debug) {
    ComPtr<IDXGIFactory6> f; CHECK(CreateDXGIFactory2(0, IID_PPV_ARGS(&f)));
    ComPtr<IDXGIAdapter1> a;
    std::string pick = g_pick; for (auto& ch : pick) ch = char(tolower(ch));
    const bool warp = pick == "warp" || pick == "warpdt", warpDriverType = pick == "warpdt";
    if (warp) CHECK(f->EnumWarpAdapter(IID_PPV_ARGS(&a)));
    else if (pick == "high") CHECK(f->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)));
    else {
        const bool byVendor = pick.rfind("vendor:", 0) == 0; const unsigned vendor = byVendor ? unsigned(strtoul(pick.c_str() + 7, nullptr, 16)) : 0;
        ComPtr<IDXGIAdapter1> x;
        for (UINT i = 0; f->EnumAdapters1(i, &x) != DXGI_ERROR_NOT_FOUND; ++i, x.Reset()) {
            DXGI_ADAPTER_DESC1 xd{}; x->GetDesc1(&xd); char n[128]; WideCharToMultiByte(CP_UTF8, 0, xd.Description, -1, n, sizeof n, nullptr, nullptr);
            std::string ln = n; for (auto& ch : ln) ch = char(tolower(ch));
            if ((byVendor && xd.VendorId == vendor) || (!byVendor && ln.find(pick) != std::string::npos)) { a = x; break; }
        }
        if (!a) { say("FATAL no adapter matches '%s'\n", g_pick.c_str()); exit(2); }
    }
    CHECK(a.As(&c.adapter));
    DXGI_ADAPTER_DESC1 ad{}; a->GetDesc1(&ad); LARGE_INTEGER umd{}; a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd);
    char name[128]; WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof name, nullptr, nullptr);
    say("Adapter: %s, vendor 0x%04X, %llu MB dedicated, %llu MB shared system memory, driver %u.%u.%u.%u, flags 0x%X\n", name, ad.VendorId, (unsigned long long)(ad.DedicatedVideoMemory >> 20),
        (unsigned long long)(ad.SharedSystemMemory >> 20), HIWORD(umd.HighPart), LOWORD(umd.HighPart), HIWORD(umd.LowPart), LOWORD(umd.LowPart), ad.Flags);
    bool dbg12 = false;
    if (debug) { ComPtr<ID3D12Debug> d; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d)))) { d->EnableDebugLayer(); dbg12 = true; } }
    CHECK(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&c.d12)));
    D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1}, got{};
    ComPtr<ID3D11Device> d; ComPtr<ID3D11DeviceContext> ctx;
    IDXGIAdapter* a11 = warpDriverType ? nullptr : a.Get(); const D3D_DRIVER_TYPE dt = warpDriverType ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_UNKNOWN;
    HRESULT hr = D3D11CreateDevice(a11, dt, nullptr, debug ? D3D11_CREATE_DEVICE_DEBUG : 0, want, 1, D3D11_SDK_VERSION, &d, &got, &ctx);
    bool dbg11 = debug && SUCCEEDED(hr);
    if (FAILED(hr) && debug) hr = D3D11CreateDevice(a11, dt, nullptr, 0, want, 1, D3D11_SDK_VERSION, &d, &got, &ctx);
    CHECK(hr); CHECK(d.As(&c.d11)); CHECK(ctx.As(&c.c11));
    if (dbg11) c.d11.As(&c.iq11);
    if (dbg12) c.d12.As(&c.iq12);
    say("D3D11 feature level 0x%X (driver type %s), debug layers: D3D11 %s, D3D12 %s\n", unsigned(got), warpDriverType ? "WARP" : "adapter", c.iq11 ? "on" : "off", c.iq12 ? "on" : "off");
    const LUID l12 = c.d12->GetAdapterLuid();
    LUID l11{}; { ComPtr<IDXGIDevice> dd; ComPtr<IDXGIAdapter> da; DXGI_ADAPTER_DESC de{}; if (SUCCEEDED(c.d11.As(&dd)) && SUCCEEDED(dd->GetAdapter(&da)) && SUCCEEDED(da->GetDesc(&de))) l11 = de.AdapterLuid; }
    say("LUIDs: adapter %08X:%08X, D3D11 device %08X:%08X, D3D12 device %08X:%08X; same adapter for both devices: %s\n", ad.AdapterLuid.HighPart, ad.AdapterLuid.LowPart,
        l11.HighPart, l11.LowPart, l12.HighPart, l12.LowPart, (l12.LowPart == l11.LowPart && l12.HighPart == l11.HighPart) ? "yes" : "NO");
    swprintf(g_luidFilter, 64, L"luid_0x%08x_0x%08x", unsigned(ad.AdapterLuid.HighPart), unsigned(ad.AdapterLuid.LowPart));
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0}; CHECK(c.d11->CreateQuery(&qd, &c.idle));
    D3D12_COMMAND_QUEUE_DESC qdesc{}; qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; CHECK(c.d12->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&c.q)));
    CHECK(c.d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&c.alloc)));
    CHECK(c.d12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, c.alloc.Get(), nullptr, IID_PPV_ARGS(&c.list)));
    {
        HRESULT hf = c.d12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&c.f12)); HANDLE fh = nullptr;
        if (SUCCEEDED(hf)) hf = c.d12->CreateSharedHandle(c.f12.Get(), nullptr, GENERIC_ALL, nullptr, &fh);
        if (SUCCEEDED(hf)) { hf = c.d11->OpenSharedFence(fh, IID_PPV_ARGS(&c.f11)); CloseHandle(fh); }
        g_cpuSync = FAILED(hf);
        say("Shared fence D3D12 -> D3D11: %s\n", g_cpuSync ? ("refused " + hx(hf) + ", the CPU waits for D3D11 instead").c_str() : "works");
    }
    CHECK(c.d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&c.cpu))); c.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // Hash pass, both APIs.
    auto cs50 = compile(kHashHlsl, "cs_5_0"), cs51 = compile(kHashHlsl, "cs_5_1");
    CHECK(c.d11->CreateComputeShader(cs50->GetBufferPointer(), cs50->GetBufferSize(), nullptr, &c.hash11));
    D3D11_BUFFER_DESC bd{4096, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0};
    CHECK(c.d11->CreateBuffer(&bd, nullptr, &c.hb11));
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 1024; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    CHECK(c.d11->CreateUnorderedAccessView(c.hb11.Get(), &ud, &c.hu11));
    D3D11_BUFFER_DESC sd{4096, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0}; CHECK(c.d11->CreateBuffer(&sd, nullptr, &c.hs11));
    D3D11_BUFFER_DESC cd{32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0}; CHECK(c.d11->CreateBuffer(&cd, nullptr, &c.cb11));
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    D3D12_ROOT_PARAMETER p[3]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 8};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable = {1, &range};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; p[2].Descriptor = {0, 0};
    D3D12_ROOT_SIGNATURE_DESC rd{3, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, err; CHECK(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err));
    CHECK(c.d12->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&c.rs)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = c.rs.Get(); pd.CS = {cs51->GetBufferPointer(), cs51->GetBufferSize()};
    CHECK(c.d12->CreateComputePipelineState(&pd, IID_PPV_ARGS(&c.pso)));
    c.hb12 = buffer12(c, 4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    c.hz12 = buffer12(c, 4096, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    c.hr12 = buffer12(c, 4096, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    void* z = nullptr; CHECK(c.hz12->Map(0, nullptr, &z)); memset(z, 0, 4096); c.hz12->Unmap(0, nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    CHECK(c.d12->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&c.heap))); c.inc = c.d12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    drain(c, nullptr);
}

static void features(Ctx& c) {
    D3D11_FEATURE_DATA_D3D11_OPTIONS o{}; c.d11->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof o);
    D3D11_FEATURE_DATA_D3D11_OPTIONS4 o4{}; c.d11->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS4, &o4, sizeof o4);
    D3D11_FEATURE_DATA_D3D11_OPTIONS5 o5{}; c.d11->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS5, &o5, sizeof o5);
    say("D3D11 ExtendedResourceSharing: %d\n", o.ExtendedResourceSharing);
    say("D3D11 ExtendedNV12SharedTextureSupported: %d\n", o4.ExtendedNV12SharedTextureSupported);
    say("D3D11 SharedResourceTier: %d\n", int(o5.SharedResourceTier));
    D3D12_FEATURE_DATA_D3D12_OPTIONS p{}; c.d12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &p, sizeof p);
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 p4{}; c.d12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &p4, sizeof p4);
    D3D12_FEATURE_DATA_D3D12_OPTIONS8 p8{}; c.d12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS8, &p8, sizeof p8);
    say("D3D12 ResourceHeapTier: %d, SharedResourceCompatibilityTier: %d, UnalignedBlockTexturesSupported: %d\n",
        int(p.ResourceHeapTier), int(p4.SharedResourceCompatibilityTier), p8.UnalignedBlockTexturesSupported);
    DXGI_QUERY_VIDEO_MEMORY_INFO vi{}; c.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi);
    say("Video memory budget for this process: %llu MB, in use by it: %llu MB\n", (unsigned long long)(vi.Budget >> 20), (unsigned long long)(vi.CurrentUsage >> 20));
    say("Per format (D3D11 FORMAT_SUPPORT2_SHAREABLE / D3D12 Texture2D+ShaderLoad):\n");
    for (auto& f : kFormats) {
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 s2{f.f, 0}; HRESULT h2 = c.d11->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &s2, sizeof s2);
        D3D12_FEATURE_DATA_FORMAT_SUPPORT s12{f.f}; HRESULT h12 = c.d12->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &s12, sizeof s12);
        say("  %-15s shareable %s, D3D12 %s\n", f.name, FAILED(h2) ? "(query failed)" : (s2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_SHAREABLE) ? "yes" : "no",
            FAILED(h12) ? "(query failed)" : ((s12.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) ? ((s12.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) ? "tex2d+load" : "tex2d") : "no tex2d"));
    }
}

enum Mode { DEF_INIT, IMM_INIT, DEF_UPDATE };
static const char* kModeName[] = {"DEFAULT+data", "IMMUTABLE+data", "DEFAULT+UpdateSubresource"};

struct CaseResult { std::string text; bool clean; };

static CaseResult run_case(Ctx& c, const Fmt& f, uint32_t W, uint32_t H, uint32_t mips, uint32_t slices, bool cube, Mode mode, uint64_t seed, FILE* detail) {
    std::string notes, firstMsg; int msgs = 0;
    const uint32_t subs = mips * slices;
    auto data = make_data(f, W, H, mips, slices, seed);
    std::vector<D3D11_SUBRESOURCE_DATA> init(subs);
    for (uint32_t s = 0; s < slices; ++s) for (uint32_t m = 0; m < mips; ++m) { auto i = m + s * mips; init[i] = {data[i].data(), sub_info(f, W, H, m).rowBytes, 0}; }
    D3D11_TEXTURE2D_DESC d{W, H, mips, slices, f.f, {1, 0}, mode == IMM_INIT ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                           (g_legacy ? UINT(D3D11_RESOURCE_MISC_SHARED) : kShared) | (cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0u)};
    ComPtr<ID3D11Texture2D> t;
    HRESULT hr = c.d11->CreateTexture2D(&d, mode == DEF_UPDATE ? nullptr : init.data(), &t);
    msgs += drain(c, &firstMsg);
    if (FAILED(hr)) {
        D3D11_TEXTURE2D_DESC plain = d; plain.MiscFlags &= ~kShared; ComPtr<ID3D11Texture2D> u;
        const HRESULT hp = c.d11->CreateTexture2D(&plain, mode == DEF_UPDATE ? nullptr : init.data(), &u);
        drain(c, nullptr);
        if (FAILED(hp)) return {"invalid even unshared (" + hx(hp) + ")", false};
        notes = "shared create " + hx(hr);
        if (mode != IMM_INIT) return {notes + "; unshared works", false};
        const HRESULT immutable = hr;
        d.Usage = D3D11_USAGE_DEFAULT; hr = c.d11->CreateTexture2D(&d, init.data(), &t); drain(c, nullptr);
        if (FAILED(hr)) return {notes + "; as DEFAULT also " + hx(hr), false};
        notes = "IMMUTABLE refused " + hx(immutable) + ", as DEFAULT";
    }
    if (mode == DEF_UPDATE) for (uint32_t s = 0; s < slices; ++s) for (uint32_t m = 0; m < mips; ++m) {
        auto i = m + s * mips; c.c11->UpdateSubresource(t.Get(), i, nullptr, data[i].data(), sub_info(f, W, H, m).rowBytes, 0);
    }
    ComPtr<IDXGIResource1> dx; CHECK(t.As(&dx)); HANDLE h = nullptr;
    hr = g_legacy ? dx->GetSharedHandle(&h) : dx->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h);
    if (FAILED(hr)) return {"handle " + hx(hr), false};
    ComPtr<ID3D12Resource> r; hr = c.d12->OpenSharedHandle(h, IID_PPV_ARGS(&r)); if (!g_legacy) CloseHandle(h); // an old-style handle is not a kernel handle: never closed
    msgs += drain(c, &firstMsg);
    if (FAILED(hr)) return {(notes.empty() ? "" : notes + "; ") + "D3D12 open " + hx(hr), false};
    const D3D12_RESOURCE_DESC rd = r->GetDesc();
    if (rd.Width != W || rd.Height != H || rd.MipLevels != mips || rd.DepthOrArraySize != slices || rd.Format != f.f)
        notes += (notes.empty() ? "" : "; ") + std::string("D3D12 sees a different desc");
    // D3D11's own reference: hashes through a view, and the bytes back through a staging copy.
    std::vector<std::vector<uint32_t>> ref;
    for (int v = 0; v < 2 && f.view[v] != DXGI_FORMAT_UNKNOWN; ++v) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = f.view[v]; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; sd.Texture2DArray = {0, mips, 0, slices};
        ComPtr<ID3D11ShaderResourceView> srv; CHECK(c.d11->CreateShaderResourceView(t.Get(), &sd, &srv));
        std::vector<uint32_t> z(1024, 0); c.c11->UpdateSubresource(c.hb11.Get(), 0, nullptr, z.data(), 0, 0);
        c.c11->CSSetShader(c.hash11.Get(), nullptr, 0); ID3D11ShaderResourceView* sv[] = {srv.Get()}; c.c11->CSSetShaderResources(0, 1, sv);
        ID3D11UnorderedAccessView* uv[] = {c.hu11.Get()}; c.c11->CSSetUnorderedAccessViews(0, 1, uv, nullptr); ID3D11Buffer* cb[] = {c.cb11.Get()}; c.c11->CSSetConstantBuffers(0, 1, cb);
        for (uint32_t s = 0; s < slices; ++s) for (uint32_t m = 0; m < mips; ++m) {
            auto si = sub_info(f, W, H, m); uint32_t k[8] = {m, s, si.w, si.h, m + s * mips, 0, 0, 0};
            c.c11->UpdateSubresource(c.cb11.Get(), 0, nullptr, k, 0, 0); c.c11->Dispatch((si.w + 7) / 8, (si.h + 7) / 8, 1);
        }
        ID3D11ShaderResourceView* ns[] = {nullptr}; c.c11->CSSetShaderResources(0, 1, ns); ID3D11UnorderedAccessView* nu[] = {nullptr}; c.c11->CSSetUnorderedAccessViews(0, 1, nu, nullptr);
        c.c11->CopyResource(c.hs11.Get(), c.hb11.Get());
        D3D11_MAPPED_SUBRESOURCE mp{}; CHECK(c.c11->Map(c.hs11.Get(), 0, D3D11_MAP_READ, 0, &mp));
        ref.emplace_back((uint32_t*)mp.pData, (uint32_t*)mp.pData + 2 * subs); c.c11->Unmap(c.hs11.Get(), 0);
    }
    std::string d11bits = "exact";
    {
        D3D11_TEXTURE2D_DESC st = d; st.Usage = D3D11_USAGE_STAGING; st.BindFlags = 0; st.CPUAccessFlags = D3D11_CPU_ACCESS_READ; st.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        ComPtr<ID3D11Texture2D> stage; HRESULT hs = c.d11->CreateTexture2D(&st, nullptr, &stage);
        if (FAILED(hs)) { st.MiscFlags = 0; hs = c.d11->CreateTexture2D(&st, nullptr, &stage); }
        if (FAILED(hs)) d11bits = "no staging copy " + hx(hs); else c.c11->CopyResource(stage.Get(), t.Get());
        for (uint32_t i = 0; i < subs && d11bits == "exact"; ++i) {
            auto si = sub_info(f, W, H, i % mips); D3D11_MAPPED_SUBRESOURCE mp{}; CHECK(c.c11->Map(stage.Get(), i, D3D11_MAP_READ, 0, &mp));
            for (uint32_t y = 0; y < si.rows; ++y) if (memcmp((uint8_t*)mp.pData + size_t(y) * mp.RowPitch, data[i].data() + size_t(y) * si.rowBytes, si.rowBytes)) { d11bits = "D3D11 itself differs at sub " + std::to_string(i); break; }
            c.c11->Unmap(stage.Get(), i);
        }
    }
    msgs += drain(c, &firstMsg);
    d11_to_d12(c); // the D3D12 queue waits for everything D3D11 did to the texture so far
    // D3D12: views (the hash one as a 2D array, plus the shape the tracer would use), the byte copy and the hash pass.
    auto cpuSlot = [&](int i) { auto x = c.heap->GetCPUDescriptorHandleForHeapStart(); x.ptr += SIZE_T(i) * c.inc; return x; };
    auto gpuSlot = [&](int i) { auto x = c.heap->GetGPUDescriptorHandleForHeapStart(); x.ptr += UINT64(i) * c.inc; return x; };
    int views = 0;
    for (int v = 0; v < 2 && f.view[v] != DXGI_FORMAT_UNKNOWN; ++v, ++views) {
        D3D12_SHADER_RESOURCE_VIEW_DESC a{}; a.Format = f.view[v]; a.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        a.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; a.Texture2DArray.MipLevels = mips; a.Texture2DArray.ArraySize = slices;
        c.d12->CreateShaderResourceView(r.Get(), &a, cpuSlot(v));
        D3D12_SHADER_RESOURCE_VIEW_DESC b{}; b.Format = f.view[v]; b.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (cube) { b.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; b.TextureCube.MipLevels = UINT(-1); }
        else if (slices == 1) { b.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; b.Texture2D.MipLevels = UINT(-1); }
        else { b.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; b.Texture2DArray.MipLevels = UINT(-1); b.Texture2DArray.ArraySize = slices; }
        c.d12->CreateShaderResourceView(r.Get(), &b, cpuSlot(4 + v));
    }
    const int srvMsgs = drain(c, &firstMsg); msgs += srvMsgs;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(subs); std::vector<UINT> rows(subs); std::vector<UINT64> rowSize(subs); UINT64 total = 0;
    c.d12->GetCopyableFootprints(&rd, 0, subs, 0, fp.data(), rows.data(), rowSize.data(), &total);
    auto rb = buffer12(c, total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_COPY_SOURCE;
    auto b0 = transition(r.Get(), D3D12_RESOURCE_STATE_COMMON, readState); c.list->ResourceBarrier(1, &b0);
    for (uint32_t i = 0; i < subs; ++i) {
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = rb.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp[i];
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = r.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = i;
        c.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    auto bz = transition(c.hb12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST); c.list->ResourceBarrier(1, &bz);
    c.list->CopyBufferRegion(c.hb12.Get(), 0, c.hz12.Get(), 0, 4096);
    auto bu = transition(c.hb12.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); c.list->ResourceBarrier(1, &bu);
    ID3D12DescriptorHeap* heaps[] = {c.heap.Get()}; c.list->SetDescriptorHeaps(1, heaps);
    c.list->SetComputeRootSignature(c.rs.Get()); c.list->SetPipelineState(c.pso.Get());
    c.list->SetComputeRootUnorderedAccessView(2, c.hb12->GetGPUVirtualAddress());
    for (int v = 0; v < views; ++v) {
        c.list->SetComputeRootDescriptorTable(1, gpuSlot(v));
        for (uint32_t s = 0; s < slices; ++s) for (uint32_t m = 0; m < mips; ++m) {
            auto si = sub_info(f, W, H, m); uint32_t k[8] = {m, s, si.w, si.h, uint32_t(v) * 256 + m + s * mips, 0, 0, 0};
            c.list->SetComputeRoot32BitConstants(0, 8, k, 0); c.list->Dispatch((si.w + 7) / 8, (si.h + 7) / 8, 1);
        }
    }
    auto bc = transition(c.hb12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE); c.list->ResourceBarrier(1, &bc);
    c.list->CopyBufferRegion(c.hr12.Get(), 0, c.hb12.Get(), 0, 4096);
    D3D12_RESOURCE_BARRIER back[2] = {transition(c.hb12.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON), transition(r.Get(), readState, D3D12_RESOURCE_STATE_COMMON)};
    c.list->ResourceBarrier(2, back);
    run12(c);
    msgs += drain(c, &firstMsg);
    std::string bits = "exact";
    {
        uint8_t* p = nullptr; CHECK(rb->Map(0, nullptr, (void**)&p));
        for (uint32_t i = 0; i < subs && bits == "exact"; ++i) {
            auto si = sub_info(f, W, H, i % mips);
            if (rows[i] != si.rows || rowSize[i] != si.rowBytes) { bits = "layout differs at sub " + std::to_string(i); break; }
            for (uint32_t y = 0; y < si.rows; ++y) if (memcmp(p + fp[i].Offset + size_t(y) * fp[i].Footprint.RowPitch, data[i].data() + size_t(y) * si.rowBytes, si.rowBytes)) { bits = "differs at sub " + std::to_string(i); break; }
        }
        rb->Unmap(0, nullptr);
    }
    std::string view = "same";
    {
        uint32_t* p = nullptr; CHECK(c.hr12->Map(0, nullptr, (void**)&p));
        for (int v = 0; v < views; ++v) for (uint32_t i = 0; i < subs; ++i)
            if (p[(v * 256 + i) * 2] != ref[v][i * 2] || p[(v * 256 + i) * 2 + 1] != ref[v][i * 2 + 1]) { if (view == "same") view = "view " + std::to_string(v) + " differs at sub " + std::to_string(i); }
        bool zero = true; for (uint32_t i = 0; i < subs; ++i) if (p[i * 2] || p[i * 2 + 1]) zero = false;
        if (zero) view = "D3D12 hash pass wrote nothing";
        c.hr12->Unmap(0, nullptr);
    }
    const bool clean = bits == "exact" && view == "same" && d11bits == "exact" && srvMsgs == 0 && notes.find("differen") == std::string::npos;
    std::string text = notes.empty() ? "" : notes + "; ";
    text += clean ? "ok" : ("bits " + bits + ", view " + view + ", D3D11 " + d11bits + (srvMsgs ? ", SRV warnings" : ""));
    if (msgs) text += " [" + std::to_string(msgs) + " debug msgs]";
    if (detail && !firstMsg.empty()) fprintf(detail, "%s %ux%u m%u s%u%s %s: %s\n", f.name, W, H, mips, slices, cube ? " cube" : "", kModeName[mode], firstMsg.c_str());
    return {text, clean};
}

static void matrix(Ctx& c, const char* dir) {
    std::string dp = std::string(dir) + "\\matrix-debug-messages.txt"; FILE* detail = fopen(dp.c_str(), "w");
    struct Shape { uint32_t w, h, mips, slices; bool cube; };
    std::vector<std::pair<const Fmt*, Shape>> cases;
    const uint32_t sizes[3][2] = {{2048, 2048}, {1024, 512}, {1000, 600}};
    for (auto& f : kFormats) {
        for (auto& s : sizes) { cases.push_back({&f, {s[0], s[1], full_mips(s[0], s[1]), 1, false}}); cases.push_back({&f, {s[0], s[1], 1, 1, false}}); }
        cases.push_back({&f, {1024, 512, full_mips(1024, 512), 2, false}});
    }
    cases.push_back({&fmt(DXGI_FORMAT_BC6H_UF16), {256, 256, full_mips(256, 256), 6, true}});
    cases.push_back({&fmt(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), {256, 256, full_mips(256, 256), 6, true}});
    cases.push_back({&fmt(DXGI_FORMAT_BC7_UNORM_SRGB), {1001, 601, full_mips(1001, 601), 1, false}});
    cases.push_back({&fmt(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), {1001, 601, full_mips(1001, 601), 1, false}});
    say("\n| format | size | mips | slices | DEFAULT + initial data | IMMUTABLE + initial data | DEFAULT + UpdateSubresource |\n|---|---|---|---|---|---|---|\n");
    int n = 0, clean = 0, total = 0; const double t0 = now_ms();
    for (auto& [f, s] : cases) {
        std::string cell[3];
        for (int m = 0; m < 3; ++m) { auto r = run_case(c, *f, s.w, s.h, s.mips, s.slices, s.cube, Mode(m), ++n, detail); cell[m] = r.text; clean += r.clean; ++total; }
        say("| %s | %ux%u | %u | %u%s | %s | %s | %s |\n", f->name, s.w, s.h, s.mips, s.slices, s.cube ? " (cube)" : "", cell[0].c_str(), cell[1].c_str(), cell[2].c_str());
    }
    say("\nMatrix: %d of %d cases clean, %.1f s\n", clean, total, (now_ms() - t0) / 1000);
    if (detail) fclose(detail);
}

// ---------- the other way round: the texture is born on D3D12 (shared heap), filled by D3D12, opened by D3D11 ----------
struct Made12 { HRESULT create = E_FAIL, handle = E_FAIL, open = E_FAIL; ComPtr<ID3D12Resource> r; ComPtr<ID3D11Texture2D> t; };
static Made12 make_on_d12(Ctx& c, const Fmt& f, uint32_t W, uint32_t H, uint32_t mips, uint32_t slices, const std::vector<std::vector<uint8_t>>& data, bool fill = true) {
    Made12 m;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = W; rd.Height = H; rd.DepthOrArraySize = UINT16(slices); rd.MipLevels = UINT16(mips);
    rd.Format = f.f; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    m.create = c.d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m.r));
    if (FAILED(m.create)) return m;
    if (fill) {
        const uint32_t subs = mips * slices;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(subs); std::vector<UINT> rows(subs); std::vector<UINT64> rowSize(subs); UINT64 total = 0;
        c.d12->GetCopyableFootprints(&rd, 0, subs, 0, fp.data(), rows.data(), rowSize.data(), &total);
        auto up = buffer12(c, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        uint8_t* p = nullptr; CHECK(up->Map(0, nullptr, (void**)&p));
        for (uint32_t i = 0; i < subs; ++i) { auto si = sub_info(f, W, H, i % mips); for (uint32_t y = 0; y < si.rows; ++y) memcpy(p + fp[i].Offset + size_t(y) * fp[i].Footprint.RowPitch, data[i].data() + size_t(y) * si.rowBytes, si.rowBytes); }
        up->Unmap(0, nullptr);
        auto b0 = transition(m.r.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST); c.list->ResourceBarrier(1, &b0);
        for (uint32_t i = 0; i < subs; ++i) {
            D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = m.r.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = i;
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = up.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp[i];
            c.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
        auto b1 = transition(m.r.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON); c.list->ResourceBarrier(1, &b1);
        run12(c); // the CPU waits: the copy is done before D3D11 ever sees the texture
    }
    HANDLE h = nullptr; m.handle = c.d12->CreateSharedHandle(m.r.Get(), nullptr, GENERIC_ALL, nullptr, &h);
    if (FAILED(m.handle)) return m;
    m.open = c.d11->OpenSharedResource1(h, IID_PPV_ARGS(&m.t)); CloseHandle(h);
    return m;
}
static std::string d11_bits(Ctx& c, ID3D11Texture2D* t, const Fmt& f, uint32_t W, uint32_t H, uint32_t mips, uint32_t slices, const std::vector<std::vector<uint8_t>>& data) {
    D3D11_TEXTURE2D_DESC st{}; t->GetDesc(&st); st.Usage = D3D11_USAGE_STAGING; st.BindFlags = 0; st.CPUAccessFlags = D3D11_CPU_ACCESS_READ; st.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> stage; HRESULT hs = c.d11->CreateTexture2D(&st, nullptr, &stage); if (FAILED(hs)) return "no staging " + hx(hs);
    c.c11->CopyResource(stage.Get(), t);
    for (uint32_t i = 0; i < mips * slices; ++i) {
        auto si = sub_info(f, W, H, i % mips); D3D11_MAPPED_SUBRESOURCE mp{}; HRESULT hm = c.c11->Map(stage.Get(), i, D3D11_MAP_READ, 0, &mp); if (FAILED(hm)) return "map " + hx(hm);
        for (uint32_t y = 0; y < si.rows; ++y) if (memcmp((uint8_t*)mp.pData + size_t(y) * mp.RowPitch, data[i].data() + size_t(y) * si.rowBytes, si.rowBytes)) { c.c11->Unmap(stage.Get(), i); return "differs at sub " + std::to_string(i); }
        c.c11->Unmap(stage.Get(), i);
    }
    return "exact";
}
static void other_ways(Ctx& c) {
    say("\nThe formats D3D11 refused, and two other ways (1024x1024 all mips; cubes 256, 6 faces):\n");
    say("| format | D3D11 + sharing flags | D3D11 + old-style SHARED only, D3D12 open | made on D3D12, opened by D3D11: create / handle / open | D3D11 sees usage, bind, misc | D3D11 view | D3D11 reads D3D12's bytes |\n|---|---|---|---|---|---|---|\n");
    struct X { DXGI_FORMAT f; bool cube; };
    const X xs[] = {{DXGI_FORMAT_BC4_UNORM, false}, {DXGI_FORMAT_BC5_UNORM, false}, {DXGI_FORMAT_BC6H_UF16, false}, {DXGI_FORMAT_BC7_UNORM, false},
                    {DXGI_FORMAT_BC7_UNORM_SRGB, false}, {DXGI_FORMAT_BC7_TYPELESS, false}, {DXGI_FORMAT_BC6H_UF16, true}, {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true},
                    {DXGI_FORMAT_BC3_UNORM_SRGB, false}};
    for (auto& x : xs) {
        const Fmt& f = fmt(x.f);
        const uint32_t W = x.cube ? 256 : 1024, mips = full_mips(W, W), slices = x.cube ? 6 : 1;
        auto data = make_data(f, W, W, mips, slices, 99);
        std::vector<D3D11_SUBRESOURCE_DATA> init(mips * slices);
        for (uint32_t i = 0; i < mips * slices; ++i) init[i] = {data[i].data(), sub_info(f, W, W, i % mips).rowBytes, 0};
        const UINT cubeFlag = x.cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0u;
        D3D11_TEXTURE2D_DESC d{W, W, mips, slices, f.f, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, kShared | cubeFlag};
        std::string why; drain(c, nullptr);
        ComPtr<ID3D11Texture2D> t; HRESULT h1 = c.d11->CreateTexture2D(&d, init.data(), &t); drain(c, &why);
        std::string a = SUCCEEDED(h1) ? "works" : hx(h1) + (why.empty() ? "" : " (" + why + ")");
        d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | cubeFlag; ComPtr<ID3D11Texture2D> t2; HRESULT h2 = c.d11->CreateTexture2D(&d, init.data(), &t2); drain(c, nullptr);
        std::string b = "create " + hx(h2);
        if (SUCCEEDED(h2)) {
            ComPtr<IDXGIResource> dx; t2.As(&dx); HANDLE kh = nullptr; HRESULT hk = dx->GetSharedHandle(&kh);
            ComPtr<ID3D12Resource> r; HRESULT ho = SUCCEEDED(hk) ? c.d12->OpenSharedHandle(kh, IID_PPV_ARGS(&r)) : hk; drain(c, nullptr);
            b += ", D3D12 open " + hx(ho);
        }
        auto m = make_on_d12(c, f, W, W, mips, slices, data); std::string rwhy; drain(c, &rwhy);
        std::string cc = hx(m.create) + " / " + hx(m.handle) + " / " + hx(m.open) + (FAILED(m.open) && !rwhy.empty() ? " (" + rwhy + ")" : ""), dd = "-", vv = "-", bb = "-";
        if (m.t) {
            D3D11_TEXTURE2D_DESC g{}; m.t->GetDesc(&g); char s[96]; snprintf(s, sizeof s, "%d, 0x%X, 0x%X", int(g.Usage), g.BindFlags, g.MiscFlags); dd = s;
            D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = f.view[1] != DXGI_FORMAT_UNKNOWN ? f.view[1] : f.view[0];
            if (x.cube) { sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; sv.TextureCube.MipLevels = UINT(-1); }
            else { sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = UINT(-1); }
            ComPtr<ID3D11ShaderResourceView> srv; HRESULT hv = c.d11->CreateShaderResourceView(m.t.Get(), &sv, &srv);
            std::string vwhy; drain(c, &vwhy); vv = (x.cube ? "cube " : "2D ") + hx(hv) + (FAILED(hv) && !vwhy.empty() ? " (" + vwhy + ")" : "");
            if (x.cube && FAILED(hv)) {
                sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; sv.Texture2DArray = {0, UINT(-1), 0, 6};
                ComPtr<ID3D11ShaderResourceView> s2; vv += ", as 2D array " + hx(c.d11->CreateShaderResourceView(m.t.Get(), &sv, &s2));
            }
            bb = d11_bits(c, m.t.Get(), f, W, W, mips, slices, data); drain(c, nullptr);
        }
        say("| %s%s | %s | %s | %s | %s | %s | %s |\n", f.name, x.cube ? " cube" : "", a.c_str(), b.c_str(), cc.c_str(), dd.c_str(), vv.c_str(), bb.c_str());
    }
}

// ---------- cost ----------
static uint64_t vram(Ctx& c) { DXGI_QUERY_VIDEO_MEMORY_INFO i{}; c.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &i); return i.CurrentUsage; }
static uint64_t settle(Ctx& c) {
    c.c11->ClearState(); wait11(c); wait12(c);
    uint64_t last = vram(c); int same = 0;
    for (int i = 0; i < 40 && same < 3; ++i) { Sleep(100); const uint64_t v = vram(c); same = v == last ? same + 1 : 0; last = v; }
    return last;
}
static double mb(int64_t b) { return double(b) / (1024.0 * 1024.0); }

static void vram_test12(Ctx& c, DXGI_FORMAT ff, uint32_t W, uint32_t H, uint32_t mips, int N) {
    const Fmt& f = fmt(ff); auto data = make_data(f, W, H, mips, 1, 8);
    const uint64_t base = settle(c); std::vector<Made12> v; v.reserve(N);
    const double t0 = now_ms(); HRESULT bad = S_OK;
    for (int i = 0; i < N; ++i) {
        auto m = make_on_d12(c, f, W, H, mips, 1, data);
        if (!m.t) { bad = FAILED(m.create) ? m.create : FAILED(m.handle) ? m.handle : FAILED(m.open) ? m.open : E_FAIL; break; }
        v.push_back(std::move(m));
    }
    const double tc = now_ms() - t0; const uint64_t after = settle(c);
    say("  %-26s create, fill on D3D12 and wait, open on D3D11: %7.1f ms (%.3f ms each), video memory +%.1f MB = %.1f KB each%s\n", "made on D3D12", tc,
        tc / std::max<size_t>(1, v.size()), mb(int64_t(after - base)), (after - base) / 1024.0 / std::max<size_t>(1, v.size()),
        FAILED(bad) ? (" FAILED after " + std::to_string(v.size()) + ": " + hx(bad)).c_str() : "");
    v.clear(); settle(c);
}

static void vram_test(Ctx& c, DXGI_FORMAT ff, uint32_t W, uint32_t H, uint32_t mips, int N, int how = 0) {
    const Fmt& f = fmt(ff); const uint64_t raw = texture_bytes(f, W, H, mips, 1);
    DXGI_QUERY_VIDEO_MEMORY_INFO vi{}; c.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi);
    const uint64_t room = vi.Budget > vi.CurrentUsage ? vi.Budget - vi.CurrentUsage : 0;
    if (uint64_t(N) * raw * 2 > room / 2) N = std::max(1, int(room / 4 / std::max<uint64_t>(raw, 65536)));
    auto data = make_data(f, W, H, mips, 1, 7);
    std::vector<D3D11_SUBRESOURCE_DATA> init(mips); for (uint32_t m = 0; m < mips; ++m) init[m] = {data[m].data(), sub_info(f, W, H, m).rowBytes, 0};
    say("\n%s %ux%u, %u mips, %d textures (%.1f KB of texels each):\n", f.name, W, H, mips, N, raw / 1024.0);
    auto batch = [&](D3D11_USAGE usage, UINT misc, const char* label) {
        const uint64_t base = settle(c); std::vector<ComPtr<ID3D11Texture2D>> v; v.reserve(N);
        D3D11_TEXTURE2D_DESC d{W, H, mips, 1, ff, {1, 0}, usage, D3D11_BIND_SHADER_RESOURCE, 0, misc};
        const double t0 = now_ms(); HRESULT bad = S_OK;
        for (int i = 0; i < N; ++i) { ComPtr<ID3D11Texture2D> t; HRESULT hr = c.d11->CreateTexture2D(&d, init.data(), &t); if (FAILED(hr)) { bad = hr; break; } v.push_back(t); }
        const double tc = now_ms() - t0; const uint64_t after = settle(c);
        say("  %-26s create %7.1f ms (%.3f ms each), video memory +%.1f MB = %.1f KB each (texels %.1f KB)%s\n", label, tc, tc / std::max<size_t>(1, v.size()),
            mb(int64_t(after - base)), (after - base) / 1024.0 / std::max<size_t>(1, v.size()), raw / 1024.0, FAILED(bad) ? (" FAILED after " + std::to_string(v.size()) + ": " + hx(bad)).c_str() : "");
        if (!(misc & kShared)) return;
        const DWORD h0 = [] { DWORD n = 0; GetProcessHandleCount(GetCurrentProcess(), &n); return n; }();
        std::vector<HANDLE> hs; hs.reserve(v.size()); double t1 = now_ms(); HRESULT hbad = S_OK;
        const bool nt = (misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0;
        for (auto& t : v) { ComPtr<IDXGIResource1> dx; t.As(&dx); HANDLE h = nullptr; HRESULT hr = nt ? dx->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h) : dx->GetSharedHandle(&h); if (FAILED(hr)) { hbad = hr; break; } hs.push_back(h); }
        const double th = now_ms() - t1; DWORD h1 = 0; GetProcessHandleCount(GetCurrentProcess(), &h1);
        std::vector<ComPtr<ID3D12Resource>> rs; rs.reserve(hs.size()); t1 = now_ms(); HRESULT obad = S_OK;
        for (auto h : hs) { ComPtr<ID3D12Resource> r; HRESULT hr = c.d12->OpenSharedHandle(h, IID_PPV_ARGS(&r)); if (FAILED(hr)) { obad = hr; break; } rs.push_back(r); }
        const double to = now_ms() - t1; const uint64_t opened = settle(c);
        if (nt) for (auto h : hs) CloseHandle(h);
        DWORD h2 = 0; GetProcessHandleCount(GetCurrentProcess(), &h2);
        say("  %-26s NT handles %.1f ms (%.3f each), handles %lu -> %lu; D3D12 open %.1f ms (%.3f each), %zu opened, video memory after open +%.1f MB; handles after closing them %lu%s%s\n",
            "", th, th / std::max<size_t>(1, hs.size()), h0, h1, to, to / std::max<size_t>(1, rs.size()), rs.size(), mb(int64_t(opened - after)), h2,
            FAILED(hbad) ? (" HANDLE FAILED " + hx(hbad)).c_str() : "", FAILED(obad) ? (" OPEN FAILED " + hx(obad)).c_str() : "");
        if (!rs.empty()) {
            D3D12_RESOURCE_DESC rd = rs[0]->GetDesc(); auto ai = c.d12->GetResourceAllocationInfo(0, 1, &rd);
            rd.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT; auto tiny = c.d12->GetResourceAllocationInfo(0, 1, &rd);
            D3D12_HEAP_PROPERTIES hp{}; D3D12_HEAP_FLAGS hf{}; HRESULT hh = rs[0]->GetHeapProperties(&hp, &hf);
            say("  %-26s a D3D12-made twin would take %.1f KB (alignment %llu KB); asking for 4 KB alignment gives %.1f KB (alignment %llu); opened heap flags %s 0x%X\n", "",
                ai.SizeInBytes / 1024.0, (unsigned long long)(ai.Alignment / 1024), tiny.SizeInBytes / 1024.0, (unsigned long long)tiny.Alignment,
                hx(hh).c_str(), unsigned(hf));
        }
        rs.clear(); v.clear(); settle(c);
    };
    batch(D3D11_USAGE_IMMUTABLE, 0, "unshared IMMUTABLE");
    batch(D3D11_USAGE_DEFAULT, 0, "unshared DEFAULT");
    if (how == 0) batch(D3D11_USAGE_DEFAULT, kShared, "shared DEFAULT");
    else if (how == 1) { batch(D3D11_USAGE_DEFAULT, D3D11_RESOURCE_MISC_SHARED, "old-style shared DEFAULT"); batch(D3D11_USAGE_IMMUTABLE, D3D11_RESOURCE_MISC_SHARED, "old-style shared IMMUTABLE"); }
    else vram_test12(c, ff, W, H, mips, N);
}

static void sampling_test(Ctx& c) {
    say("\nD3D11 sampling time, 2048x2048 threads x 16 samples, median of 25 (rough: other programs share the GPU):\n");
    auto code = compile(kSampleHlsl, "cs_5_0"); ComPtr<ID3D11ComputeShader> cs; CHECK(c.d11->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &cs));
    D3D11_SAMPLER_DESC sd{D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0, 1, D3D11_COMPARISON_NEVER, {0, 0, 0, 0}, 0, D3D11_FLOAT32_MAX};
    ComPtr<ID3D11SamplerState> smp; CHECK(c.d11->CreateSamplerState(&sd, &smp));
    D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}; ComPtr<ID3D11Query> dj, ta, tb; CHECK(c.d11->CreateQuery(&qd, &dj));
    qd.Query = D3D11_QUERY_TIMESTAMP; CHECK(c.d11->CreateQuery(&qd, &ta)); CHECK(c.d11->CreateQuery(&qd, &tb));
    struct Tex { std::string label; ComPtr<ID3D11ShaderResourceView> srv; std::vector<double> ms[2]; };
    std::vector<Tex> texs;
    std::vector<Made12> keep;
    for (DXGI_FORMAT ff : {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM}) {
        const Fmt& f = fmt(ff); const uint32_t mips = full_mips(2048, 2048); auto data = make_data(f, 2048, 2048, mips, 1, 11);
        std::vector<D3D11_SUBRESOURCE_DATA> init(mips); for (uint32_t m = 0; m < mips; ++m) init[m] = {data[m].data(), sub_info(f, 2048, 2048, m).rowBytes, 0};
        struct V { D3D11_USAGE u; UINT misc; const char* n; } vs[] = {{D3D11_USAGE_IMMUTABLE, 0, "unshared IMMUTABLE"}, {D3D11_USAGE_DEFAULT, 0, "unshared DEFAULT"}, {D3D11_USAGE_DEFAULT, kShared, "shared DEFAULT"},
                                                                       {D3D11_USAGE_DEFAULT, D3D11_RESOURCE_MISC_SHARED, "old-style shared DEFAULT"}, {D3D11_USAGE_IMMUTABLE, D3D11_RESOURCE_MISC_SHARED, "old-style shared IMMUTABLE"}};
        for (auto& v : vs) {
            D3D11_TEXTURE2D_DESC d{2048, 2048, mips, 1, ff, {1, 0}, v.u, D3D11_BIND_SHADER_RESOURCE, 0, v.misc};
            ComPtr<ID3D11Texture2D> t; std::string label = v.n;
            const HRESULT hc = c.d11->CreateTexture2D(&d, init.data(), &t);
            if (FAILED(hc)) { say("  %s %s: not creatable (%s)\n", f.name, v.n, hx(hc).c_str()); continue; }
            Tex x; x.label = std::string(f.name) + " " + label; CHECK(c.d11->CreateShaderResourceView(t.Get(), nullptr, &x.srv)); texs.push_back(std::move(x));
        }
    }
    D3D11_BUFFER_DESC cd{16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0}; ComPtr<ID3D11Buffer> cb; CHECK(c.d11->CreateBuffer(&cd, nullptr, &cb));
    c.c11->CSSetShader(cs.Get(), nullptr, 0); ID3D11SamplerState* ss[] = {smp.Get()}; c.c11->CSSetSamplers(0, 1, ss);
    ID3D11UnorderedAccessView* uv[] = {c.hu11.Get()}; c.c11->CSSetUnorderedAccessViews(0, 1, uv, nullptr); ID3D11Buffer* cbs[] = {cb.Get()}; c.c11->CSSetConstantBuffers(0, 1, cbs);
    for (int rep = 0; rep < 28; ++rep) for (auto& x : texs) for (int mode = 0; mode < 2; ++mode) {
        uint32_t k[4] = {uint32_t(mode), 2048, 16, 0}; c.c11->UpdateSubresource(cb.Get(), 0, nullptr, k, 0, 0);
        ID3D11ShaderResourceView* sv[] = {x.srv.Get()}; c.c11->CSSetShaderResources(0, 1, sv);
        c.c11->Begin(dj.Get()); c.c11->End(ta.Get()); c.c11->Dispatch(256, 256, 1); c.c11->End(tb.Get()); c.c11->End(dj.Get());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dd{}; while (c.c11->GetData(dj.Get(), &dd, sizeof dd, 0) != S_OK) Sleep(0);
        UINT64 a = 0, b = 0; while (c.c11->GetData(ta.Get(), &a, sizeof a, 0) != S_OK) Sleep(0); while (c.c11->GetData(tb.Get(), &b, sizeof b, 0) != S_OK) Sleep(0);
        if (rep >= 3 && !dd.Disjoint) x.ms[mode].push_back(double(b - a) * 1000.0 / double(dd.Frequency));
    }
    for (auto& x : texs) {
        double med[2]; for (int m = 0; m < 2; ++m) { auto v = x.ms[m]; std::sort(v.begin(), v.end()); med[m] = v.empty() ? -1 : v[v.size() / 2]; }
        say("  %-32s neighbouring texels %.3f ms, scattered texels %.3f ms\n", x.label.c_str(), med[0], med[1]);
    }
    c.c11->ClearState();
}

// The CreateTexture2D hook, the way a mod would put it on the game's device.
using CreateTex2DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
static CreateTex2DFn g_real = nullptr; static int g_calls = 0, g_made = 0;
static bool eligible(const D3D11_TEXTURE2D_DESC& d) {
    return (d.Usage == D3D11_USAGE_IMMUTABLE || d.Usage == D3D11_USAGE_DEFAULT) && d.BindFlags == D3D11_BIND_SHADER_RESOURCE && d.CPUAccessFlags == 0 &&
           d.SampleDesc.Count == 1 && (d.MiscFlags & ~(D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_RESOURCE_CLAMP)) == 0;
}
static HRESULT STDMETHODCALLTYPE hooked_create(ID3D11Device* dev, const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out) {
    ++g_calls;
    if (desc && out && eligible(*desc)) {
        D3D11_TEXTURE2D_DESC m = *desc; if (m.Usage == D3D11_USAGE_IMMUTABLE) m.Usage = D3D11_USAGE_DEFAULT; m.MiscFlags |= kShared;
        const HRESULT hr = g_real(dev, &m, init, out); if (SUCCEEDED(hr)) { ++g_made; return hr; }
    }
    return g_real(dev, desc, init, out);
}
static void hook_test(Ctx& c) {
    say("\nCreateTexture2D hook (vtable slot 5 of ID3D11Device):\n");
    void** vt = *reinterpret_cast<void***>(c.d11.Get()); DWORD old = 0;
    VirtualProtect(&vt[5], sizeof(void*), PAGE_READWRITE, &old); g_real = reinterpret_cast<CreateTex2DFn>(vt[5]); vt[5] = reinterpret_cast<void*>(&hooked_create); VirtualProtect(&vt[5], sizeof(void*), old, &old);
    const Fmt& f = fmt(DXGI_FORMAT_BC3_UNORM_SRGB); const uint32_t mips = full_mips(1024, 1024); auto data = make_data(f, 1024, 1024, mips, 1, 3);
    std::vector<D3D11_SUBRESOURCE_DATA> init(mips); for (uint32_t m = 0; m < mips; ++m) init[m] = {data[m].data(), sub_info(f, 1024, 1024, m).rowBytes, 0};
    D3D11_TEXTURE2D_DESC d{1024, 1024, mips, 1, f.f, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ComPtr<ID3D11Device> base; c.d11.As(&base); ComPtr<ID3D11Texture2D> t; HRESULT hr = base->CreateTexture2D(&d, init.data(), &t);
    D3D11_TEXTURE2D_DESC got{}; if (t) t->GetDesc(&got);
    HANDLE h = nullptr; HRESULT hh = E_FAIL, ho = E_FAIL;
    if (t) { ComPtr<IDXGIResource1> dx; t.As(&dx); hh = dx->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h); }
    ComPtr<ID3D12Resource> r; if (SUCCEEDED(hh)) { ho = c.d12->OpenSharedHandle(h, IID_PPV_ARGS(&r)); CloseHandle(h); }
    say("  an IMMUTABLE BC3 sRGB 1024 texture made through the hooked slot: create %s, hook calls %d, made shared %d, its desc now usage %d misc 0x%X, NT handle %s, D3D12 open %s\n",
        hx(hr).c_str(), g_calls, g_made, int(got.Usage), got.MiscFlags, hx(hh).c_str(), hx(ho).c_str());
    D3D11_TEXTURE2D_DESC rt = d; rt.Usage = D3D11_USAGE_DEFAULT; rt.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rt.MipLevels = 1; rt.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> t2; const int before = g_made; base->CreateTexture2D(&rt, nullptr, &t2); D3D11_TEXTURE2D_DESC g2{}; if (t2) t2->GetDesc(&g2);
    say("  a render target through the same slot is left alone: made shared %d, misc 0x%X\n", g_made - before, g2.MiscFlags);
    {
        const Fmt& f7 = fmt(DXGI_FORMAT_BC7_UNORM_SRGB); auto d7data = make_data(f7, 1024, 1024, mips, 1, 4);
        std::vector<D3D11_SUBRESOURCE_DATA> i7(mips); for (uint32_t m = 0; m < mips; ++m) i7[m] = {d7data[m].data(), sub_info(f7, 1024, 1024, m).rowBytes, 0};
        D3D11_TEXTURE2D_DESC d7 = d; d7.Format = f7.f; ComPtr<ID3D11Texture2D> t7; const int made7 = g_made; HRESULT h7 = base->CreateTexture2D(&d7, i7.data(), &t7);
        D3D11_TEXTURE2D_DESC g7{}; if (t7) t7->GetDesc(&g7);
        say("  an IMMUTABLE BC7 sRGB 1024 texture (D3D11 refuses to share BC7): the game still gets it, create %s, made shared %d, usage %d misc 0x%X\n", hx(h7).c_str(), g_made - made7, int(g7.Usage), g7.MiscFlags);
    }
    ComPtr<ID3D11Device3> d3; const int callsBefore = g_calls;
    if (SUCCEEDED(c.d11.As(&d3))) {
        D3D11_TEXTURE2D_DESC1 d1{1024, 1024, mips, 1, f.f, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0, D3D11_TEXTURE_LAYOUT_UNDEFINED};
        ComPtr<ID3D11Texture2D1> t3; HRESULT h3 = d3->CreateTexture2D1(&d1, init.data(), &t3);
        say("  ID3D11Device3::CreateTexture2D1 (slot 54) %s through slot 5 (create %s): a mod must hook it too if the game uses it\n", g_calls > callsBefore ? "passes" : "does NOT pass", hx(h3).c_str());
    }
    VirtualProtect(&vt[5], sizeof(void*), PAGE_READWRITE, &old); vt[5] = reinterpret_cast<void*>(g_real); VirtualProtect(&vt[5], sizeof(void*), old, &old);
}

// Who keeps the memory: 40 shared BC7 2048 textures opened on D3D12, then D3D11 lets go first.
struct Notifier : IUnknown {
    LONG refs = 1; int* fired;
    explicit Notifier(int* f) : fired(f) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override { if (id == __uuidof(IUnknown)) { *out = this; AddRef(); return S_OK; } *out = nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&refs)); }
    ULONG STDMETHODCALLTYPE Release() override { const LONG r = InterlockedDecrement(&refs); if (r == 0) { ++*fired; delete this; } return ULONG(r); }
};
static void lifetime_test(Ctx& c, DXGI_FORMAT ff, UINT misc, const char* label) {
    const Fmt& f = fmt(ff); const uint32_t mips = full_mips(2048, 2048); auto data = make_data(f, 2048, 2048, mips, 1, 5);
    std::vector<D3D11_SUBRESOURCE_DATA> init(mips); for (uint32_t m = 0; m < mips; ++m) init[m] = {data[m].data(), sub_info(f, 2048, 2048, m).rowBytes, 0};
    D3D11_TEXTURE2D_DESC d{2048, 2048, mips, 1, f.f, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, misc};
    static const GUID kTag = {0x6a1e2b3c, 0x4d5e, 0x4f60, {0x81, 0x92, 0xa3, 0xb4, 0xc5, 0xd6, 0xe7, 0xf8}};
    const bool nt = (misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0;
    struct Point { uint64_t dxgi; double proc, card; };
    auto point = [&] { const uint64_t v = settle(c); Sleep(1500); return Point{v, process_mb(), card_mb()}; };
    const Point base = point(); int fired = 0;
    std::vector<ComPtr<ID3D11Texture2D>> v; std::vector<ComPtr<ID3D12Resource>> r;
    for (int i = 0; i < 40; ++i) {
        ComPtr<ID3D11Texture2D> t; CHECK(c.d11->CreateTexture2D(&d, init.data(), &t));
        auto* n = new Notifier(&fired); t->SetPrivateDataInterface(kTag, n); n->Release(); // the texture now holds the only extra reference
        ComPtr<IDXGIResource1> dx; t.As(&dx); HANDLE h = nullptr;
        CHECK(nt ? dx->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h) : dx->GetSharedHandle(&h));
        ComPtr<ID3D12Resource> x; CHECK(c.d12->OpenSharedHandle(h, IID_PPV_ARGS(&x))); if (nt) CloseHandle(h); v.push_back(t); r.push_back(x);
    }
    const Point both = point();
    v.clear(); const Point d12only = point();
    r.clear(); const Point none = point();
    auto row = [&](const char* when, const Point& p) {
        say("  %-36s DXGI's count for this process +%7.1f MB, Windows' per-process counter +%7.1f MB, the whole card +%7.1f MB\n", when,
            mb(int64_t(p.dxgi - base.dxgi)), p.proc - base.proc, p.card - base.card);
    };
    say("\nLifetime and real memory, 40 %s 2048 textures (%.1f MB of texels in all), death notices %d of 40:\n", label, 40 * texture_bytes(f, 2048, 2048, mips, 1) / 1048576.0, fired);
    row("D3D11 and D3D12 both hold them", both); row("after D3D11 released them", d12only); row("after D3D12 released them too", none);
}

// What a start-up self-test in the mod would cost: one small texture per format the game might use (256x256, all mips,
// old-style flag, no debug layers), shared, opened on D3D12, read by a shader and copied back on both sides.
static void selftest(Ctx& c) {
    g_legacy = true;
    struct T { DXGI_FORMAT f; uint32_t slices; bool cube; };
    const T ts[] = {{DXGI_FORMAT_BC1_UNORM, 1, false}, {DXGI_FORMAT_BC1_UNORM_SRGB, 1, false}, {DXGI_FORMAT_BC3_UNORM, 1, false}, {DXGI_FORMAT_BC3_UNORM_SRGB, 1, false},
                    {DXGI_FORMAT_BC4_UNORM, 1, false}, {DXGI_FORMAT_BC5_UNORM, 1, false}, {DXGI_FORMAT_BC6H_UF16, 1, false}, {DXGI_FORMAT_BC7_UNORM, 1, false},
                    {DXGI_FORMAT_BC7_UNORM_SRGB, 1, false}, {DXGI_FORMAT_R8G8B8A8_UNORM, 1, false}, {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1, false},
                    {DXGI_FORMAT_BC6H_UF16, 6, true}, {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 6, true}, {DXGI_FORMAT_BC7_UNORM_SRGB, 2, false}};
    // Two sizes per format: a small one (under 64 KB for BC, where drivers may pick a different memory layout) and a
    // large one (1024, all mips). Cubes 128. All mips down to 1x1, so the packed small mips are covered too.
    say("\nSelf-test, old-style flag, all mips, no debug layers:\n");
    // One warm-up so shader and pipeline creation inside the drivers is not counted against the first format.
    run_case(c, fmt(DXGI_FORMAT_BC1_UNORM), 256, 256, full_mips(256, 256), 1, false, DEF_INIT, 1000, nullptr);
    const double t0 = now_ms(); int ok = 0, n = 0;
    for (auto& t : ts) for (uint32_t size : {128u, 1024u}) {
        if (t.cube && size > 128) continue;
        const double t1 = now_ms(); auto r = run_case(c, fmt(t.f), size, size, full_mips(size, size), t.slices, t.cube, DEF_INIT, 2000 + n, nullptr); ++n; ok += r.clean;
        say("  %-12s %4u %-8s: %s, %.2f ms\n", fmt(t.f).name, size, t.cube ? "cube" : t.slices > 1 ? "2 slices" : "2D", r.text.c_str(), now_ms() - t1);
    }
    say("Self-test: %d of %d clean, %.1f ms in all (includes making the random data on the CPU)\n", ok, n, now_ms() - t0);
    g_legacy = false;
}

int main(int argc, char** argv) {
    const std::string what = argc > 1 ? argv[1] : "matrix";
    if (argc > 2) g_pick = argv[2];
    std::string tag = g_pick; for (auto& ch : tag) if (!isalnum((unsigned char)ch)) ch = '-';
    char exe[MAX_PATH]; GetModuleFileNameA(nullptr, exe, MAX_PATH); std::string dir(exe); dir = dir.substr(0, dir.find_last_of('\\'));
    g_log = fopen((dir + "\\results-" + what + "-" + tag + ".txt").c_str(), "w");
    std::string ddir = dir + "\\dbg-" + what + "-" + tag; CreateDirectoryA(ddir.c_str(), nullptr); dir = ddir; // the matrix's debug-message file lands here
    Ctx c; init(c, what == "matrix" || what == "legacy");
    if (what == "selftest") selftest(c);
    if (what == "lifeorder") { // is the first set's leftover memory the flag's fault or just "first in the run"?
        lifetime_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, D3D11_RESOURCE_MISC_SHARED, "old-style shared BC7 sRGB (first)");
        lifetime_test(c, DXGI_FORMAT_BC3_UNORM_SRGB, kShared, "NT-handle shared BC3 sRGB (second)");
        lifetime_test(c, DXGI_FORMAT_BC3_UNORM_SRGB, kShared, "NT-handle shared BC3 sRGB (third)");
        lifetime_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, D3D11_RESOURCE_MISC_SHARED, "old-style shared BC7 sRGB (fourth)");
    }
    if (what == "matrix") { features(c); matrix(c, dir.c_str()); other_ways(c); }
    else if (what == "legacy") { say("The matrix with the old-style flag (D3D11_RESOURCE_MISC_SHARED alone, IDXGIResource::GetSharedHandle):\n"); g_legacy = true; matrix(c, dir.c_str()); g_legacy = false; other_ways(c); }
    else if (what == "legacycost") {
        lifetime_test(c, DXGI_FORMAT_BC3_UNORM_SRGB, kShared, "NT-handle shared BC3 sRGB");
        lifetime_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, D3D11_RESOURCE_MISC_SHARED, "old-style shared BC7 sRGB");
        vram_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, 2048, 2048, full_mips(2048, 2048), 100, 1);
        vram_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, 128, 128, full_mips(128, 128), 4000, 1);
        vram_test(c, DXGI_FORMAT_BC1_UNORM, 64, 64, 1, 4000, 1);
        sampling_test(c);
    }
    else if (what == "warpcost") { // cost and lifetime on WARP, without the long sampling test (WARP runs on the CPU)
        hook_test(c);
        lifetime_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, D3D11_RESOURCE_MISC_SHARED, "old-style shared BC7 sRGB (first)");
        lifetime_test(c, DXGI_FORMAT_BC3_UNORM_SRGB, kShared, "NT-handle shared BC3 sRGB (second)");
        lifetime_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, D3D11_RESOURCE_MISC_SHARED, "old-style shared BC7 sRGB (third)");
        vram_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, 2048, 2048, full_mips(2048, 2048), 100, 2);
        vram_test(c, DXGI_FORMAT_BC1_UNORM_SRGB, 512, 512, full_mips(512, 512), 500);
        vram_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, 128, 128, full_mips(128, 128), 4000, 2);
        vram_test(c, DXGI_FORMAT_BC1_UNORM, 64, 64, 1, 4000, 1);
    }
    else if (what == "cost") {
        hook_test(c);
        lifetime_test(c, DXGI_FORMAT_BC3_UNORM_SRGB, kShared, "NT-handle shared BC3 sRGB");
        vram_test(c, DXGI_FORMAT_BC3_UNORM_SRGB, 2048, 2048, full_mips(2048, 2048), 100);
        vram_test(c, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 2048, 2048, full_mips(2048, 2048), 25);
        vram_test(c, DXGI_FORMAT_BC1_UNORM_SRGB, 512, 512, full_mips(512, 512), 500);
        vram_test(c, DXGI_FORMAT_BC3_UNORM_SRGB, 128, 128, full_mips(128, 128), 4000);
        vram_test(c, DXGI_FORMAT_BC1_UNORM, 64, 64, 1, 4000);
        vram_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, 2048, 2048, full_mips(2048, 2048), 100, 2);
        vram_test(c, DXGI_FORMAT_BC7_UNORM_SRGB, 128, 128, full_mips(128, 128), 4000, 2);
        sampling_test(c);
    }
    say("\ndone\n");
    if (g_log) fclose(g_log);
    return 0;
}
