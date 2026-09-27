// SPDX-License-Identifier: MIT
// TG Path Tracing end to end on this machine's GPU, outside the game. TG Graphics Bridge boots with the
// path tracing extension and creates its D3D12 device inside a D3D11 swapchain creation, as in the
// game. A small scene (a room open at the front with a hole in its ceiling, a box, an emissive panel,
// a warm point light, the sun and a sky) is registered the way the plugin registers FoA's meshes: D3D11
// vertex and index buffers in Unity's layout, a texture for the atlas, materials, instances. Each frame
// the scene is rasterized into targets laid out as HDRP's (GBuffer0 sRGB, the octahedral normal buffer,
// GBuffer2's specular color, GBuffer3, reversed-Z depth with HDRP's deferred stencil bit, motion
// vectors, a pre-exposed color buffer holding direct sunlight), handed over, traced and composited
// through the real render events. The results are written as PNGs, and checked: finite, light where the
// sun does not reach, the scene fully covered by the traced geometry.
// Exits 77 (skipped) without the staged DLLs or a DXR 1.1 GPU.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "tgb/bridge_c_api.h"
#include "tgpt_api.h"
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The game's exe names an Agility SDK runtime; so does this one, for TG Graphics Bridge to point at TG Path
// Tracing's own (DirectX Raytracing 1.2) as it does in the game. Version 4 is older than any Windows' own
// runtime, so without a staged one Windows' serves the test.
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion=4;
__declspec(dllexport) extern const char* D3D12SDKPath=".\\";
}

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
static int failures=0;
static constexpr UINT W=1280, H=720;
static void expect(bool condition, const char* what) { std::printf("%s: %s\n", condition ? "ok  " : "FAIL", what); if(!condition) ++failures; }

// What the Direct3D 12 debug layer reports (TGPT_TEST_DEBUG_LAYER): errors and warnings as they come, the first 200 printed.
static int debugErrors=0, debugWarnings=0, debugPrinted=0;
static void CALLBACK on_debug_message(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR text, void*) {
    const bool error=severity<=D3D12_MESSAGE_SEVERITY_ERROR; // corruption or error
    if(error) ++debugErrors; else if(severity==D3D12_MESSAGE_SEVERITY_WARNING) ++debugWarnings; else return;
    if(debugPrinted<200) { ++debugPrinted; std::printf("      D3D12 %s %d: %s\n", error ? "ERROR" : "WARNING", int(id), text); }
}

// ------------------------------------------------------------------ math (row-major m[r][c])
struct V3 { float x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static V3 operator*(V3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static float dot(V3 a, V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static V3 cross(V3 a, V3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
static V3 normalize(V3 a) { float l=std::sqrt(dot(a, a)); return a*(1.f/l); }
struct M4 { float m[4][4]{}; };
static M4 identity() { M4 r; for(int i=0;i<4;++i) r.m[i][i]=1; return r; }
static M4 mul(const M4& a, const M4& b) { M4 r; for(int i=0;i<4;++i) for(int j=0;j<4;++j) { float s=0; for(int k=0;k<4;++k) s+=a.m[i][k]*b.m[k][j]; r.m[i][j]=s; } return r; }
static M4 inverse(const M4& a) {
    double m[4][8];
    for(int i=0;i<4;++i) for(int j=0;j<8;++j) m[i][j]=j<4 ? a.m[i][j] : (j-4==i ? 1 : 0);
    for(int c=0;c<4;++c) {
        int p=c; for(int r=c+1;r<4;++r) if(std::fabs(m[r][c])>std::fabs(m[p][c])) p=r;
        for(int j=0;j<8;++j) std::swap(m[c][j], m[p][j]);
        const double d=m[c][c]; for(int j=0;j<8;++j) m[c][j]/=d;
        for(int r=0;r<4;++r) if(r!=c) { const double f=m[r][c]; for(int j=0;j<8;++j) m[r][j]-=f*m[c][j]; }
    }
    M4 r; for(int i=0;i<4;++i) for(int j=0;j<4;++j) r.m[i][j]=float(m[i][j+4]); return r;
}
// Unity's Matrix4x4 memory order (column-major), as the plugin passes HDRP's matrices.
static void unity_layout(const M4& a, float out[16]) { for(int c=0;c<4;++c) for(int r=0;r<4;++r) out[c*4+r]=a.m[r][c]; }
static M4 translate_scale(V3 t, V3 s) { M4 r=identity(); r.m[0][0]=s.x; r.m[1][1]=s.y; r.m[2][2]=s.z; r.m[0][3]=t.x; r.m[1][3]=t.y; r.m[2][3]=t.z; return r; }
// HDRP's camera: Unity's worldToCameraMatrix without translation (camera-relative rendering), GL
// projection turned into D3D's reversed-Z, render-into-texture form by GL.GetGPUProjectionMatrix.
// Also HDRP's view matrix and projection without jitter, and the jitter in render pixels, as the plugin hands them over.
struct Camera { V3 position; M4 viewProj, invViewProj, view, nonJitteredProj, nonJitteredViewProj; float jitter[2]{}; };
static Camera make_camera(V3 position, V3 target, float fovDegrees, float aspect, float jitterX=0, float jitterY=0, int width=1, int height=1) {
    const V3 f=normalize(target-position), r=normalize(cross({0, 1, 0}, f)), u=cross(f, r);
    M4 view=identity();
    view.m[0][0]=r.x; view.m[0][1]=r.y; view.m[0][2]=r.z;
    view.m[1][0]=u.x; view.m[1][1]=u.y; view.m[1][2]=u.z;
    view.m[2][0]=-f.x; view.m[2][1]=-f.y; view.m[2][2]=-f.z;
    const float n=0.1f, fa=1000.f, t=1.f/std::tan(fovDegrees*3.14159265f/360.f);
    M4 plain; plain.m[0][0]=t/aspect; plain.m[1][1]=t; plain.m[2][2]=-(fa+n)/(fa-n); plain.m[2][3]=-2*fa*n/(fa-n); plain.m[3][2]=-1;
    M4 gl=plain; gl.m[0][2]+=jitterX*2.f/float(width); gl.m[1][2]+=jitterY*2.f/float(height);
    auto to_gpu=[](const M4& g) { M4 gpu=g; for(int c=0;c<4;++c) { gpu.m[1][c]=-g.m[1][c]; gpu.m[2][c]=-0.5f*g.m[2][c]+0.5f*g.m[3][c]; } return gpu; };
    Camera cam; cam.position=position; cam.viewProj=mul(to_gpu(gl), view); cam.invViewProj=inverse(cam.viewProj);
    cam.view=view; cam.nonJitteredProj=to_gpu(plain); cam.nonJitteredViewProj=mul(cam.nonJitteredProj, view); cam.jitter[0]=jitterX; cam.jitter[1]=jitterY;
    return cam;
}

// ------------------------------------------------------------------ PNG (stored deflate)
static uint32_t crc32(const uint8_t* d, size_t n, uint32_t c=0xFFFFFFFFu) {
    for(size_t i=0;i<n;++i) { c^=d[i]; for(int k=0;k<8;++k) c=(c>>1)^(0xEDB88320u&(0u-(c&1u))); }
    return c;
}
static void write_png(const fs::path& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw; raw.reserve(size_t(h)*(w*3+1));
    for(int y=0;y<h;++y) { raw.push_back(0); raw.insert(raw.end(), rgb.begin()+size_t(y)*w*3, rgb.begin()+size_t(y+1)*w*3); }
    std::vector<uint8_t> z={0x78, 0x01};
    uint32_t a=1, b=0;
    for(uint8_t v:raw) { a=(a+v)%65521; b=(b+a)%65521; }
    for(size_t i=0;i<raw.size();i+=65535) {
        const size_t n=std::min<size_t>(65535, raw.size()-i);
        z.push_back(i+n>=raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n>>8)); z.push_back(uint8_t(~n)); z.push_back(uint8_t((~n)>>8));
        z.insert(z.end(), raw.begin()+i, raw.begin()+i+n);
    }
    const uint32_t adler=(b<<16)|a; for(int s=24;s>=0;s-=8) z.push_back(uint8_t(adler>>s));
    std::ofstream f(path, std::ios::binary);
    const uint8_t sig[8]={137, 80, 78, 71, 13, 10, 26, 10}; f.write(reinterpret_cast<const char*>(sig), 8);
    auto chunk=[&](const char* type, const std::vector<uint8_t>& data) {
        uint8_t len[4]={uint8_t(data.size()>>24), uint8_t(data.size()>>16), uint8_t(data.size()>>8), uint8_t(data.size())};
        f.write(reinterpret_cast<const char*>(len), 4);
        std::vector<uint8_t> body(type, type+4); body.insert(body.end(), data.begin(), data.end());
        f.write(reinterpret_cast<const char*>(body.data()), body.size());
        const uint32_t c=crc32(body.data(), body.size())^0xFFFFFFFFu;
        uint8_t cb[4]={uint8_t(c>>24), uint8_t(c>>16), uint8_t(c>>8), uint8_t(c)}; f.write(reinterpret_cast<const char*>(cb), 4);
    };
    std::vector<uint8_t> ihdr={uint8_t(w>>24), uint8_t(w>>16), uint8_t(w>>8), uint8_t(w), uint8_t(h>>24), uint8_t(h>>16), uint8_t(h>>8), uint8_t(h), 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr); chunk("IDAT", z); chunk("IEND", {});
}
static float half_to_float(uint16_t hbits) {
    const uint32_t sign=(hbits>>15)&1, exponent=(hbits>>10)&31, mantissa=hbits&1023;
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

// ------------------------------------------------------------------ scene
struct Vertex { float px, py, pz, nx, ny, nz, u, v; };
struct MeshBuffers { ComPtr<ID3D11Buffer> vb, ib; std::vector<TGPT_Submesh> submeshes; uint32_t vertexCount=0, indexCount=0; };
// feature: HDRP Lit's material feature in GBuffer2.a (2: transmission); forward: drawn as HDRP draws hair (no deferred
// stencil bit, nothing in GBuffer0), shaded by "HDRP" itself.
// raster: the drawn material (an index into the drawn colors) where the traced one is no drawn color (glass); roughness:
// HDRP's in its normal buffer, as drawn. gb2g, gb2b: GBuffer2's green and blue bytes where set (transmission: the thickness,
// and the transmission mask packed with the diffusion profile), in place of the specular color's.
struct Object { int mesh; M4 world; std::vector<int> materials; bool tracedOnly=false; bool alphaTest=false; uint32_t feature=0; bool forward=false;
                int raster=-1; float roughness=0.6f; int gb2g=-1, gb2b=-1; };

static const char* kSceneShader=R"HLSL(
cbuffer Object : register(b0) {
    row_major float4x4 viewProj; row_major float4x4 motionViewProj; row_major float4x4 prevViewProj; row_major float4x4 world;
    float4 cameraPos; float4 prevCameraPos; float4 albedo; float4 f0rough; float4 emissive; float4 sunDir; float4 sunColor; float4 misc;
};
Texture2D checker : register(t0); SamplerState wrap : register(s0);
struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float3 wpos : TEXCOORD0; float3 nrm : TEXCOORD1; float2 uv : TEXCOORD2; float4 cur : TEXCOORD3; float4 prev : TEXCOORD4; };
VSOut vsMain(VSIn i) {
    VSOut o;
    float3 w = mul(world, float4(i.pos, 1)).xyz;
    o.wpos = w; o.nrm = normalize(mul((float3x3)world, i.nrm)); o.uv = i.uv;
    o.pos = mul(viewProj, float4(w - cameraPos.xyz, 1));
    o.cur = mul(motionViewProj, float4(w - cameraPos.xyz, 1)); // motion without the jitter, as HDRP's
    o.prev = mul(prevViewProj, float4(w - prevCameraPos.xyz, 1));
    return o;
}
float2 OctEncode(float3 n) { n *= rcp(max(dot(abs(n), 1.0), 1e-6)); float t = saturate(-n.z); return n.xy + float2(n.x >= 0.0 ? t : -t, n.y >= 0.0 ? t : -t); }
float3 Pack888(float2 f) { uint2 i = (uint2)(f * 4095.5); uint2 hi = i >> 8; uint2 lo = i & 255; uint3 cb = uint3(lo, hi.x | (hi.y << 4)); return cb / 255.0; }
float3 FastLinearToSRGB(float3 c) { return saturate(1.055 * pow(abs(c), 0.416666667) - 0.055); }
struct PSOut { float4 gb0 : SV_Target0; float4 nb : SV_Target1; float4 gb2 : SV_Target2; float4 gb3 : SV_Target3; float2 mv : SV_Target4; float4 color : SV_Target5; };
PSOut psMain(VSOut i) {
    PSOut o;
    float3 base = albedo.rgb;
    if (misc.x > 0) base *= checker.Sample(wrap, i.uv).rgb;
    float3 n = normalize(i.nrm);
    // Normal-mapped surfaces as in the game: bumps of about 15 cm and 3 cm, tilted up to about 30 degrees.
    if (misc.z > 0) {
        float3 w = i.wpos;
        float3 b = 0.35 * float3(sin(41.0 * w.x + 13.0 * w.z), sin(47.0 * w.y + 17.0 * w.x), sin(43.0 * w.z + 19.0 * w.y))
                 + 0.2 * float3(sin(197.0 * w.y + 61.0 * w.z), sin(211.0 * w.z + 53.0 * w.x), sin(223.0 * w.x + 59.0 * w.y));
        n = normalize(n + b - dot(b, n) * n);
    }
    o.gb0 = albedo.a > 0.5 ? float4(0, 0, 0, 0) : float4(base, 1); // albedo.a: drawn forward
    o.nb = float4(Pack888(saturate(OctEncode(n) * 0.5 + 0.5)), f0rough.w);
    o.gb2 = float4(FastLinearToSRGB(f0rough.rgb), misc.w / 255.0);
    float exposure = misc.y;
    o.gb3 = any(emissive.rgb > 0) ? float4(emissive.rgb * exposure, 0) : float4(2048, 1, 1024, 0);
    float2 v = i.cur.xy / i.cur.w - i.prev.xy / i.prev.w; v.y = -v.y; o.mv = v * 0.5;
    // "HDRP's" direct light: the sun without shadows, and emission, pre-exposed.
    o.color = float4((base / 3.14159265 * sunColor.rgb * saturate(dot(n, sunDir.xyz)) + emissive.rgb) * exposure, 1);
    return o;
}
)HLSL";

static ComPtr<ID3DBlob> compile(const char* source, const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code, errors;
    if(FAILED(D3DCompile(source, std::strlen(source), "scene.hlsl", nullptr, nullptr, entry, profile, 0, 0, &code, &errors))) {
        if(errors) std::printf("%s\n", static_cast<const char*>(errors->GetBufferPointer()));
        return nullptr;
    }
    return code;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    fs::path root=argc>1 ? fs::path(argv[1]) : fs::current_path()/"artifacts"/"Native";
    const fs::path images=argc>2 ? fs::path(argv[2]) : fs::current_path()/"tgpt-images";
    if(!fs::exists(root/"TGGraphicsBridge"/"TGGraphicsBridge.Native.dll") || !fs::exists(root/"TGPathTracing"/"TGPathTracing.Native.dll")) {
        std::printf("SKIP: build and stage the native DLLs first (%s)\n", root.string().c_str()); return 77;
    }
    std::error_code ec; fs::create_directories(images, ec);
    // TG Graphics Bridge takes an extension's Agility SDK runtime only from inside the exe's folder (as the
    // game's), so with one staged the two mods' folders are copied beside this exe and loaded from there.
    // TGPT_TEST_NO_AGILITY=1 runs on Windows' own runtime instead (Shader Model 6.6, no micromaps).
    const bool agility=fs::exists(root/"TGPathTracing"/"D3D12"/"D3D12Core.dll") && GetEnvironmentVariableA("TGPT_TEST_NO_AGILITY", nullptr, 0)==0;
    // TGPT_TEST_DEBUG_LAYER=<folder holding the d3d12SDKLayers.dll of the staged runtime's version (its NuGet package's
    // build/native/bin/x64)>: the bridge's device runs under the Direct3D 12 debug layer, with TGPT_TEST_GPU_VALIDATION=1
    // under its GPU-based validation too; what it reports is printed, and an error fails the run.
    wchar_t layers[MAX_PATH]{};
    const bool debugLayer=GetEnvironmentVariableW(L"TGPT_TEST_DEBUG_LAYER", layers, MAX_PATH)>0;
    const bool gpuValidation=debugLayer && GetEnvironmentVariableA("TGPT_TEST_GPU_VALIDATION", nullptr, 0)>0;
    if(debugLayer && !agility) { std::printf("SKIP: the debug layer comes with the staged Agility SDK runtime, and none is staged\n"); return 77; }
    if(agility) {
        // TGPT_TEST_STAGE=<folder name>: another folder beside the exe, so that two runs can go at once.
        wchar_t exe[MAX_PATH]{}, name[64]{L"tgpt-native"}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        GetEnvironmentVariableW(L"TGPT_TEST_STAGE", name, 64);
        const fs::path stage=fs::path(exe).parent_path()/name;
        for(const char* mod:{"TGGraphicsBridge", "TGPathTracing"}) {
            fs::create_directories(stage/mod, ec);
            fs::copy(root/mod, stage/mod, fs::copy_options::recursive|fs::copy_options::overwrite_existing, ec);
            if(ec) { std::printf("FAIL: copy %s beside the test: %s\n", mod, ec.message().c_str()); return 1; }
        }
        // The debug layer loads from the runtime's folder; a run without it leaves none behind there.
        const fs::path staged=stage/"TGPathTracing"/"D3D12"/"d3d12SDKLayers.dll";
        if(debugLayer) {
            fs::copy_file(fs::path(layers)/"d3d12SDKLayers.dll", staged, fs::copy_options::overwrite_existing, ec);
            if(ec) { std::printf("FAIL: stage d3d12SDKLayers.dll from %ls: %s\n", layers, ec.message().c_str()); return 1; }
        } else fs::remove(staged, ec);
        root=stage;
    }
    std::printf("      Agility SDK runtime: %s\n", agility ? "TG Path Tracing's, through TG Graphics Bridge's redirect" : "none staged (or TGPT_TEST_NO_AGILITY); Windows' own");
    const auto bridgePath=root/"TGGraphicsBridge"/"TGGraphicsBridge.Native.dll";
    const auto ptPath=root/"TGPathTracing"/"TGPathTracing.Native.dll";
    const HMODULE bridge=LoadLibraryW(bridgePath.c_str());
    if(!bridge) { std::printf("FAIL: load bridge %lu\n", GetLastError()); return 1; }
    auto addExtension=reinterpret_cast<decltype(&TGGraphicsBridge_AddExtension)>(GetProcAddress(bridge, "TGGraphicsBridge_AddExtension"));
    auto bootstrap=reinterpret_cast<decltype(&TGGraphicsBridge_Bootstrap)>(GetProcAddress(bridge, "TGGraphicsBridge_Bootstrap"));
    auto readBridge=reinterpret_cast<decltype(&TGGraphicsBridge_ReadStatus)>(GetProcAddress(bridge, "TGGraphicsBridge_ReadStatus"));
    expect(addExtension && bootstrap && readBridge, "bridge exports");
    if(failures) return 1;
    expect(addExtension(ptPath.c_str())==0, "path tracing extension registered");
    expect(bootstrap(nullptr)==0, "bridge bootstrap");
    // After the bootstrap (it points the exe's Agility SDK request at the staged runtime) and before the game's swapchain,
    // whose creation makes the bridge's device.
    if(debugLayer) {
        wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
        const HMODULE runtime=LoadLibraryW((fs::path(system)/"d3d12.dll").c_str());
        auto getDebug=runtime ? reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(GetProcAddress(runtime, "D3D12GetDebugInterface")) : nullptr;
        ComPtr<ID3D12Debug> debug; ComPtr<ID3D12Debug1> debug1;
        const HRESULT hr=getDebug ? getDebug(IID_PPV_ARGS(&debug)) : E_NOINTERFACE;
        if(FAILED(hr)) {
            wchar_t core[MAX_PATH]{}; const HMODULE loaded=GetModuleHandleW(L"D3D12Core.dll");
            if(loaded) GetModuleFileNameW(loaded, core, MAX_PATH);
            auto* version=reinterpret_cast<const UINT*>(GetProcAddress(GetModuleHandleW(nullptr), "D3D12SDKVersion"));
            auto* path=reinterpret_cast<const char* const*>(GetProcAddress(GetModuleHandleW(nullptr), "D3D12SDKPath"));
            std::printf("      D3D12GetDebugInterface failed: 0x%08lX (D3D12Core loaded: %ls; the exe asks for %u from %s)\n", static_cast<unsigned long>(hr),
                loaded ? core : L"none", version ? *version : 0u, path && *path ? *path : "?");
        }
        if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING) std::printf("      (the runtime takes the Agility SDK's debug layer only with Windows' Graphics Tools optional feature installed)\n");
        expect(SUCCEEDED(hr), "the Direct3D 12 debug layer loads beside the staged runtime");
        if(failures) return 1;
        debug->EnableDebugLayer();
        if(gpuValidation) { expect(SUCCEEDED(debug.As(&debug1)), "GPU-based validation is available"); if(debug1) debug1->SetEnableGPUBasedValidation(TRUE); }
        std::printf("      Direct3D 12 debug layer on%s\n", gpuValidation ? ", with GPU-based validation" : "");
    }
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"TGPathTracingTest";
    RegisterClassW(&wc);
    const HWND window=CreateWindowExW(0, wc.lpszClassName, L"TG Path Tracing test", WS_OVERLAPPEDWINDOW, 0, 0, 1296, 759, nullptr, nullptr, wc.hInstance, nullptr);
    ComPtr<IDXGIFactory2> factory;
    if(FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) { std::printf("SKIP: no DXGI factory\n"); return 77; }
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_1;
    // TGPT_TEST_ADAPTER=<vendor id in hex>: the first GPU of that vendor (1002 AMD, 10DE NVIDIA, 8086 Intel), else the default.
    ComPtr<IDXGIAdapter1> chosen;
    {
        char wanted[16]{};
        if(GetEnvironmentVariableA("TGPT_TEST_ADAPTER", wanted, sizeof wanted)) {
            const unsigned vendor=unsigned(std::strtoul(wanted, nullptr, 16));
            ComPtr<IDXGIFactory1> f1; factory.As(&f1);
            for(UINT i=0; f1; ++i) {
                ComPtr<IDXGIAdapter1> a; if(f1->EnumAdapters1(i, &a)==DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
                if(d.VendorId!=vendor) continue;
                char name[128]{}; WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, nullptr, nullptr);
                std::printf("      GPU chosen by TGPT_TEST_ADAPTER: %s (%04X:%04X)\n", name, d.VendorId, d.DeviceId);
                chosen=a; break;
            }
            if(!chosen) { std::printf("SKIP: no GPU of vendor %s\n", wanted); return 77; }
        }
    }
    if(FAILED(D3D11CreateDevice(chosen.Get(), chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) { std::printf("SKIP: no D3D11 device\n"); return 77; }
    DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=1280; sd.Height=720; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc={1, 0};
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=2; sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapchain;
    expect(SUCCEEDED(factory->CreateSwapChainForHwnd(device.Get(), window, &sd, nullptr, nullptr, &swapchain)), "game swapchain created through the bridge hooks");
    TGB_Status bs{sizeof(TGB_Status), TGB_ABI};
    expect(readBridge(&bs)==0 && bs.d3d12DeviceReady==1, "bridge created the shared D3D12 device");
    // Direct3D 12 has one device per adapter in a process: asking for one on the game's adapter returns the bridge's.
    DWORD debugCookie=0; ComPtr<ID3D12InfoQueue1> infoQueue;
    if(debugLayer) {
        ComPtr<IDXGIDevice> dxgiDevice; ComPtr<IDXGIAdapter> adapter; ComPtr<ID3D12Device> d12;
        auto create=reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12CreateDevice"));
        const bool found=create && SUCCEEDED(device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
            SUCCEEDED(create(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d12))) && SUCCEEDED(d12.As(&infoQueue)) &&
            SUCCEEDED(infoQueue->RegisterMessageCallback(on_debug_message, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &debugCookie));
        expect(found, "the debug layer's messages about the bridge's device are read");
        if(failures) return 1;
    }
    const HMODULE pt=GetModuleHandleW(L"TGPathTracing.Native.dll");
#define LOAD(name) auto name=reinterpret_cast<decltype(&TGPT_##name)>(GetProcAddress(pt, "TGPT_" #name))
    LOAD(ReadStatus); LOAD(GetUnsupportedReason); LOAD(RegisterMesh); LOAD(RegisterTexture); LOAD(SetMaterials); LOAD(SetScene);
    LOAD(SetLights); LOAD(SetSky); LOAD(SubmitFrame); LOAD(SetComposite); LOAD(GetRenderEvent); LOAD(RegisterCpuMesh); LOAD(RegisterAlphaTexture);
    LOAD(RegisterSkinnedMesh); LOAD(SetSkinned); LOAD(RegisterCookie); LOAD(SetShadowDetail); LOAD(SetDiffusionProfiles);
    LOAD(SetSkinTransmission); LOAD(GetStageTimes);
#undef LOAD
    expect(ReadStatus && RegisterMesh && SubmitFrame && GetRenderEvent && RegisterCpuMesh, "path tracing exports");
    if(failures) return 1;
    TGPT_Status status{sizeof(TGPT_Status), TGPT_ABI};
    ReadStatus(&status);
    if(!status.supported) {
        char reason[512]{}; GetUnsupportedReason(reason, sizeof(reason));
        std::printf("SKIP: path tracing unsupported here: %s\n", reason); return 77;
    }
    expect(status.deviceReady==1, "path tracer has the shared device and inline ray tracing");
    auto render=reinterpret_cast<void(__stdcall*)(int, void*)>(GetRenderEvent());

    // Meshes in Unity's layout: position, normal, UV interleaved (32 bytes), 16-bit indices.
    auto make_mesh=[&](const std::vector<Vertex>& verts, const std::vector<std::vector<uint16_t>>& subs) {
        MeshBuffers m; std::vector<uint16_t> all;
        for(const auto& s:subs) { m.submeshes.push_back(TGPT_Submesh{uint32_t(all.size()), uint32_t(s.size()), 0, 0}); all.insert(all.end(), s.begin(), s.end()); }
        D3D11_BUFFER_DESC d{}; d.ByteWidth=UINT(verts.size()*sizeof(Vertex)); d.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init{verts.data()}; device->CreateBuffer(&d, &init, &m.vb);
        d.ByteWidth=UINT(all.size()*2); d.BindFlags=D3D11_BIND_INDEX_BUFFER; init.pSysMem=all.data(); device->CreateBuffer(&d, &init, &m.ib);
        m.vertexCount=uint32_t(verts.size()); m.indexCount=uint32_t(all.size());
        return m;
    };
    // A unit quad in XZ facing +Y, and a unit cube (top face its own submesh).
    std::vector<Vertex> quad={{-0.5f, 0, -0.5f, 0, 1, 0, 0, 0}, {0.5f, 0, -0.5f, 0, 1, 0, 4, 0}, {0.5f, 0, 0.5f, 0, 1, 0, 4, 4}, {-0.5f, 0, 0.5f, 0, 1, 0, 0, 4}};
    MeshBuffers quadMesh=make_mesh(quad, {{0, 2, 1, 0, 3, 2}});
    std::vector<Vertex> cube; std::vector<uint16_t> sides, top;
    const V3 normals[6]={{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, 1, 0}};
    for(int f=0;f<6;++f) {
        const V3 n=normals[f], t=std::fabs(n.y)>0.5f ? V3{1, 0, 0} : V3{0, 1, 0}, b=cross(n, t);
        const uint16_t base=uint16_t(cube.size());
        for(int k=0;k<4;++k) {
            const float su=(k==1 || k==2) ? 0.5f : -0.5f, sv=(k>=2) ? 0.5f : -0.5f;
            const V3 p=n*0.5f+t*su+b*sv;
            cube.push_back({p.x, p.y, p.z, n.x, n.y, n.z, su+0.5f, sv+0.5f});
        }
        auto& list=f==5 ? top : sides;
        // Wound as Unity winds its meshes (its Quad: cross(v1-v0, v2-v0) points out of the front, the side seen
        // clockwise in its left-handed space), which DXR's default front face matches: here with b = n x t that is
        // 0, 1, 2. (Wound the other way the box was inside out, which nothing noticed until rays told the faces apart.)
        list.insert(list.end(), {base, uint16_t(base+1), uint16_t(base+2), base, uint16_t(base+2), uint16_t(base+3)});
    }
    MeshBuffers cubeMesh=make_mesh(cube, {sides, top});
    // A leaf card: the texture once across it, so each of its micro-triangles spans a few atlas texels and
    // opacity micromaps can settle most of them ahead of the alpha test.
    std::vector<Vertex> card={{-0.5f, 0, -0.5f, 0, 1, 0, 0, 0}, {0.5f, 0, -0.5f, 0, 1, 0, 1, 0}, {0.5f, 0, 0.5f, 0, 1, 0, 1, 1}, {-0.5f, 0, 0.5f, 0, 1, 0, 0, 1}};
    MeshBuffers cardMesh=make_mesh(card, {{0, 2, 1, 0, 3, 2}});
    auto register_mesh=[&](MeshBuffers& m, uint64_t key) {
        TGPT_MeshDesc d{sizeof(TGPT_MeshDesc), TGPT_ABI};
        d.key=key; d.vertexBuffers[0]=uint64_t(uintptr_t(m.vb.Get())); d.strides[0]=sizeof(Vertex);
        d.positionStream=0; d.positionOffset=0; d.positionFormat=TGPT_VF_FLOAT32; d.positionDimension=3;
        d.uvStream=0; d.uvOffset=24; d.uvFormat=TGPT_VF_FLOAT32; d.uvDimension=2;
        d.colorStream=0xFFFFFFFFu;
        d.indexBuffer=uint64_t(uintptr_t(m.ib.Get())); d.indexFormat=16; d.vertexCount=m.vertexCount;
        d.submeshCount=uint32_t(m.submeshes.size()); d.submeshes=m.submeshes.data();
        return RegisterMesh(&d);
    };
    const int quadHandle=register_mesh(quadMesh, 1), cubeHandle=register_mesh(cubeMesh, 2), cardHandle=register_mesh(cardMesh, 4);
    expect(quadHandle>0 && cubeHandle>0 && cardHandle>0, "meshes registered");
    // A terrain-like CPU mesh (a gentle mound outside the room) with vertex colors.
    std::vector<float> tp; std::vector<uint32_t> tc, ti;
    const int grid=16;
    for(int z=0;z<=grid;++z) for(int x=0;x<=grid;++x) {
        const float fx=float(x)/grid*24-12, fz=float(z)/grid*12-24;
        tp.insert(tp.end(), {fx, -0.01f+0.3f*std::sin(fx*0.5f)*std::cos(fz*0.5f), fz});
        tc.push_back(0xFF2A6A3Au);
    }
    for(int z=0;z<grid;++z) for(int x=0;x<grid;++x) { const uint32_t a=z*(grid+1)+x; ti.insert(ti.end(), {a, a+grid+1, a+1, a+1, a+grid+1, a+grid+2}); }
    TGPT_CpuMeshDesc tdesc{sizeof(TGPT_CpuMeshDesc), TGPT_ABI}; tdesc.key=3; tdesc.positions=tp.data(); tdesc.colors=tc.data(); tdesc.indices=ti.data();
    tdesc.vertexCount=uint32_t(tp.size()/3); tdesc.indexCount=uint32_t(ti.size());
    const int terrainHandle=RegisterCpuMesh(&tdesc);
    expect(terrainHandle>0, "CPU mesh registered");

    // A checker texture (sRGB, mipmapped) for the atlas and for the raster pass.
    ComPtr<ID3D11Texture2D> checker; ComPtr<ID3D11ShaderResourceView> checkerSrv;
    {
        const int size=256; std::vector<uint32_t> px(size*size);
        for(int y=0;y<size;++y) for(int x=0;x<size;++x) px[y*size+x]=((x/32+y/32)&1) ? 0xFFE0E0E0u : 0xFF3050C0u;
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=0; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET; d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        device->CreateTexture2D(&d, nullptr, &checker);
        context->UpdateSubresource(checker.Get(), 0, nullptr, px.data(), size*4, 0);
        device->CreateShaderResourceView(checker.Get(), nullptr, &checkerSrv);
        context->GenerateMips(checkerSrv.Get());
    }
    const int tile=RegisterTexture(42, uint64_t(uintptr_t(checker.Get())));
    expect(tile>=0, "texture registered for the atlas");
    // A leaf texture: green with a checkerboard of fully transparent squares, for alpha-tested shadows.
    ComPtr<ID3D11Texture2D> leaf;
    {
        const int size=128; std::vector<uint32_t> px(size*size);
        for(int y=0;y<size;++y) for(int x=0;x<size;++x) px[y*size+x]=((x/16+y/16)&1) ? 0x00307020u : 0xFF307020u;
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{px.data(), UINT(size*4), 0};
        device->CreateTexture2D(&d, &init, &leaf);
    }
    const int leafTile=RegisterTexture(43, uint64_t(uintptr_t(leaf.Get())));
    expect(leafTile>=0 && leafTile!=tile, "alpha-tested texture registered for the atlas");
    // Its alpha at 256x256 as well, which the alpha test and the micromaps read instead of the 64x64 tile's.
    const int leafAlpha=RegisterAlphaTexture(43, uint64_t(uintptr_t(leaf.Get())), 0.5f);
    expect(leafAlpha>=0, "alpha-tested texture registered for the alpha atlas");
    // Materials: white, red, green, checker, emissive, terrain (vertex color), and the furnace's glowing grey (drawn only
    // where a test builds the furnace).
    struct Mat { float r, g, b; bool checker; float er, eg, eb; };
    const Mat mats[7]={{0.75f, 0.75f, 0.75f, false, 0, 0, 0}, {0.75f, 0.08f, 0.06f, false, 0, 0, 0}, {0.08f, 0.6f, 0.1f, false, 0, 0, 0},
                       {1, 1, 1, true, 0, 0, 0}, {0.2f, 0.2f, 0.2f, false, 6.f, 3.f, 1.f}, {1, 1, 1, false, 0, 0, 0}, {0.5f, 0.5f, 0.5f, false, 0.5f, 0.5f, 0.5f}};
    std::vector<TGPT_Material> materials(12);
    for(int i=0;i<6;++i) {
        auto& m=materials[size_t(i)]; m.baseColor[0]=mats[i].r; m.baseColor[1]=mats[i].g; m.baseColor[2]=mats[i].b; m.baseColor[3]=1;
        m.uvScaleOffset[0]=m.uvScaleOffset[1]=1; m.emissive[0]=mats[i].er; m.emissive[1]=mats[i].eg; m.emissive[2]=mats[i].eb;
        m.albedoTile=mats[i].checker ? tile : -1; m.emissiveTile=-1; m.smoothness=0.3f;
        if(i==5) m.flags=TGPT_MATERIAL_VERTEX_COLOR;
    }
    // 6: an alpha-tested leaf; 7: the plugin's "invisible to rays" material (a cutoff above 1, no texture).
    materials[6]=materials[0]; materials[6].albedoTile=leafTile; materials[6].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_ALPHA_TILE; materials[6].alphaTile=leafAlpha; materials[6].alphaCutoff=0.5f;
    materials[7]=materials[0]; materials[7].flags=TGPT_MATERIAL_ALPHA_TEST; materials[7].alphaCutoff=2.f;
    // 8: seen by camera rays only (eyes, unlit decals): solid to them, passed through by every other ray.
    materials[8]=materials[0]; materials[8].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_CAMERA_ONLY; materials[8].alphaCutoff=0.f;
    // 9: grey glass (luminance 0.36: a closed object lets 36 % of the light through); 10: clear glass (lets it all through).
    materials[9]=materials[0]; materials[9].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS; materials[9].alphaCutoff=0.f;
    materials[9].baseColor[0]=materials[9].baseColor[1]=materials[9].baseColor[2]=0.36f;
    materials[10]=materials[9]; materials[10].baseColor[0]=materials[10].baseColor[1]=materials[10].baseColor[2]=1.f;
    // 11: the furnace's walls: albedo 0.5, glowing 0.5 (the drawn material 6).
    materials[11]=materials[0];
    for(int c=0;c<3;++c) { materials[11].baseColor[c]=mats[6].r; materials[11].emissive[c]=mats[6].er; }
    expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "materials set");
    // The room: 12 m square, 6 m tall, open at the front (-z), the ceiling open over its back third.
    const float pi=3.14159265f;
    auto rot_x=[&](float a) { M4 r=identity(); r.m[1][1]=std::cos(a); r.m[1][2]=-std::sin(a); r.m[2][1]=std::sin(a); r.m[2][2]=std::cos(a); return r; };
    auto rot_z=[&](float a) { M4 r=identity(); r.m[0][0]=std::cos(a); r.m[0][1]=-std::sin(a); r.m[1][0]=std::sin(a); r.m[1][1]=std::cos(a); return r; };
    std::vector<Object> objects={
        {0, translate_scale({0, 0, 0}, {12, 1, 12}), {0}},                                           // floor
        {0, mul(translate_scale({0, 3, 6}, {12, 6, 1}), rot_x(-pi/2)), {0}},                           // back wall, facing -z
        {0, mul(translate_scale({-6, 3, 0}, {1, 6, 12}), rot_z(-pi/2)), {1}},                          // left wall, red, facing +x
        {0, mul(translate_scale({6, 3, 0}, {1, 6, 12}), rot_z(pi/2)), {2}},                            // right wall, green, facing -x
        {0, mul(translate_scale({0, 6, -2}, {12, 1, 8}), rot_x(pi)), {0}},                             // ceiling over z -6..2, facing down
        {1, translate_scale({1.5f, 1, 0}, {2, 2, 2}), {3, 0}},                                         // box: checker sides, white top
        {0, mul(translate_scale({-5.98f, 3, 3}, {1, 1.2f, 2}), rot_z(-pi/2)), {4}},                    // emissive panel on the left wall
        {2, identity(), {5}},                                                                           // terrain outside
        {0, translate_scale({-1.f, 4.f, 3.5f}, {2, 1, 2}), {6}, true, true},                          // traced-only leaf: checkered shadow
        {0, translate_scale({3.5f, 4.f, 4.f}, {2, 1, 2}), {7}, true, true},                           // traced-only invisible quad: no shadow
        {3, translate_scale({-3.3f, 2.f, 1.1f}, {1.6f, 1, 1.6f}), {6}, true, true},                   // traced-only leaf card: micromaps settle it
    };
    std::vector<TGPT_Instance> instances; std::vector<uint32_t> indices;
    const int handles[4]={quadHandle, cubeHandle, terrainHandle, cardHandle};
    for(const auto& o:objects) {
        TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=o.world.m[r][c];
        in.mesh=handles[o.mesh]; in.materialBase=uint32_t(indices.size()); in.mask=0xFF; in.flags=o.alphaTest ? TGPT_INSTANCE_ALPHA_TEST : 0u;
        for(int m:o.materials) indices.push_back(uint32_t(m));
        instances.push_back(in);
    }
    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene set");
    // A warm point light in the room's front left.
    TGPT_Light light{}; light.position[0]=-4; light.position[1]=1.2f; light.position[2]=-3; light.type=0;
    light.color[0]=40; light.color[1]=22; light.color[2]=8; light.range=12; light.radius=0.05f; light.id=1; // 0: an empty slot
    expect(SetLights(&light, 1)==0, "lights set");
    // The sky: a cubemap with a blue zenith and a pale horizon.
    ComPtr<ID3D11Texture2D> sky;
    {
        const int size=32; std::vector<uint16_t> faces[6];
        for(int f=0;f<6;++f) {
            faces[f].resize(size_t(size)*size*4);
            for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
                const float u=(x+0.5f)/size*2-1, v=(y+0.5f)/size*2-1;
                V3 d;
                switch(f) { case 0: d={1, -v, -u}; break; case 1: d={-1, -v, u}; break; case 2: d={u, 1, v}; break; case 3: d={u, -1, -v}; break; case 4: d={u, -v, 1}; break; default: d={-u, -v, -1}; break; }
                d=normalize(d);
                const float t=std::max(0.f, d.y);
                const float c[3]={0.9f-0.6f*t, 1.0f-0.4f*t, 1.2f};
                const float s=d.y<0 ? 0.3f : 1.f;
                for(int k=0;k<3;++k) faces[f][(size_t(y)*size+x)*4+k]=float_to_half(c[k]*s*0.8f);
                faces[f][(size_t(y)*size+x)*4+3]=float_to_half(1);
            }
        }
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=6; d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        D3D11_SUBRESOURCE_DATA init[6]; for(int f=0;f<6;++f) init[f]={faces[f].data(), UINT(size*8), 0};
        device->CreateTexture2D(&d, init, &sky);
    }
    expect(SetSky(uint64_t(uintptr_t(sky.Get())))==0, "sky set");

    // HDRP-like targets.
    auto tex=[&](DXGI_FORMAT f, UINT bind, UINT w=W, UINT h=H) { D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=1; d.ArraySize=1; d.Format=f; d.SampleDesc.Count=1; d.BindFlags=bind;
        ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, nullptr, &t); return t; };
    auto gb0=tex(DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    auto nb=tex(DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    auto gb2=tex(DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    auto gb3=tex(DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    auto mv=tex(DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    auto color=tex(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    auto depth=tex(DXGI_FORMAT_R32G8X24_TYPELESS, D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE);
    auto exposureTex=tex(DXGI_FORMAT_R32G32_FLOAT, D3D11_BIND_SHADER_RESOURCE, 1, 1);
    // HDRP's exposure multiplier: everything HDRP renders (and the traced light the tracer hands back) is pre-exposed by it.
    float exposure=0.25f;
    { const float e[2]={exposure, 0}; context->UpdateSubresource(exposureTex.Get(), 0, nullptr, e, 8, 0); }
    ComPtr<ID3D11RenderTargetView> rtv[6]; ComPtr<ID3D11DepthStencilView> dsv;
    { D3D11_RENDER_TARGET_VIEW_DESC v{}; v.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; v.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D; device->CreateRenderTargetView(gb0.Get(), &v, &rtv[0]); }
    device->CreateRenderTargetView(nb.Get(), nullptr, &rtv[1]); device->CreateRenderTargetView(gb2.Get(), nullptr, &rtv[2]);
    device->CreateRenderTargetView(gb3.Get(), nullptr, &rtv[3]); device->CreateRenderTargetView(mv.Get(), nullptr, &rtv[4]);
    device->CreateRenderTargetView(color.Get(), nullptr, &rtv[5]);
    { D3D11_DEPTH_STENCIL_VIEW_DESC v{}; v.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT; v.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D; device->CreateDepthStencilView(depth.Get(), &v, &dsv); }
    auto vsCode=compile(kSceneShader, "vsMain", "vs_5_0"), psCode=compile(kSceneShader, "psMain", "ps_5_0");
    expect(vsCode && psCode, "scene shaders compile");
    if(failures) return 1;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11InputLayout> layout;
    device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs);
    device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps);
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    device->CreateInputLayout(elements, 3, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &layout);
    ComPtr<ID3D11Buffer> ocb; { D3D11_BUFFER_DESC d{}; d.ByteWidth=16*4*4+16*8; /* four matrices, eight vectors */ d.BindFlags=D3D11_BIND_CONSTANT_BUFFER; device->CreateBuffer(&d, nullptr, &ocb); }
    ComPtr<ID3D11DepthStencilState> dss;
    { D3D11_DEPTH_STENCIL_DESC d{}; d.DepthEnable=TRUE; d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc=D3D11_COMPARISON_GREATER;
      d.StencilEnable=TRUE; d.StencilReadMask=d.StencilWriteMask=0xFF;
      d.FrontFace={D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS}; d.BackFace=d.FrontFace;
      device->CreateDepthStencilState(&d, &dss); }
    ComPtr<ID3D11RasterizerState> rs; { D3D11_RASTERIZER_DESC d{}; d.FillMode=D3D11_FILL_SOLID; d.CullMode=D3D11_CULL_NONE; d.DepthClipEnable=TRUE; device->CreateRasterizerState(&d, &rs); }
    ComPtr<ID3D11SamplerState> wrap; { D3D11_SAMPLER_DESC d{}; d.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; d.AddressU=d.AddressV=d.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; d.MaxLOD=D3D11_FLOAT32_MAX; device->CreateSamplerState(&d, &wrap); }
    const V3 sunDir=normalize({0.35f, 1.f, 0.55f});
    const float sunLux=6.f;
    float skyColor[4]={0.55f*exposure, 0.75f*exposure, 0.95f*exposure, 1};
    auto setExposure=[&](float e) {
        exposure=e;
        const float v[2]={e, 0}; context->UpdateSubresource(exposureTex.Get(), 0, nullptr, v, 8, 0);
        skyColor[0]=0.55f*e; skyColor[1]=0.75f*e; skyColor[2]=0.95f*e;
    };

    struct Frame { V3 camPos, camTarget; };
    bool bumpy=false; // normal-mapped surfaces
    auto raster=[&](const Camera& cam, const Camera& prev) {
        const float clear0[4]={0, 0, 0, 0};
        for(int i=0;i<5;++i) context->ClearRenderTargetView(rtv[i].Get(), clear0);
        context->ClearRenderTargetView(rtv[5].Get(), skyColor);
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 0.f, 0);
        ID3D11RenderTargetView* targets[6]; for(int i=0;i<6;++i) targets[i]=rtv[i].Get();
        context->OMSetRenderTargets(6, targets, dsv.Get());
        context->OMSetDepthStencilState(dss.Get(), 2); // HDRP's RequiresDeferredLighting stencil bit
        D3D11_VIEWPORT vp{0, 0, float(W), float(H), 0, 1}; context->RSSetViewports(1, &vp); context->RSSetState(rs.Get());
        context->IASetInputLayout(layout.Get()); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
        auto* cb=ocb.Get(); context->VSSetConstantBuffers(0, 1, &cb); context->PSSetConstantBuffers(0, 1, &cb);
        auto* srv=checkerSrv.Get(); context->PSSetShaderResources(0, 1, &srv); auto* smp=wrap.Get(); context->PSSetSamplers(0, 1, &smp);
        for(const auto& o:objects) {
            if(o.mesh==2 || o.tracedOnly) continue; // traced only: the terrain (outside the view), the alpha test quads
            context->OMSetDepthStencilState(dss.Get(), o.forward ? 0 : 2); // no deferred bit for a surface drawn forward
            const MeshBuffers& m=o.mesh==0 ? quadMesh : cubeMesh;
            for(size_t s=0;s<m.submeshes.size();++s) {
                const Mat& mat=mats[o.raster>=0 ? o.raster : o.materials[std::min(s, o.materials.size()-1)]];
                struct { float vp[16], mvp[16], pvp[16], world[16]; float cam[4], pcam[4], albedo[4], f0r[4], em[4], sun[4], sunc[4], misc[4]; } c{};
                std::memcpy(c.vp, cam.viewProj.m, 64); std::memcpy(c.mvp, cam.nonJitteredViewProj.m, 64); std::memcpy(c.pvp, prev.nonJitteredViewProj.m, 64);
                std::memcpy(c.world, o.world.m, 64);
                c.cam[0]=cam.position.x; c.cam[1]=cam.position.y; c.cam[2]=cam.position.z;
                c.pcam[0]=prev.position.x; c.pcam[1]=prev.position.y; c.pcam[2]=prev.position.z;
                c.albedo[0]=mat.r; c.albedo[1]=mat.g; c.albedo[2]=mat.b; c.albedo[3]=o.forward ? 1.f : 0.f; c.f0r[0]=c.f0r[1]=c.f0r[2]=0.04f; c.f0r[3]=o.roughness;
                // The value the shader's FastLinearToSRGB turns into that byte.
                if(o.gb2g>=0) c.f0r[1]=std::pow((float(o.gb2g)/255.f+0.055f)/1.055f, 2.4f);
                if(o.gb2b>=0) c.f0r[2]=std::pow((float(o.gb2b)/255.f+0.055f)/1.055f, 2.4f);
                c.em[0]=mat.er; c.em[1]=mat.eg; c.em[2]=mat.eb; c.sun[0]=sunDir.x; c.sun[1]=sunDir.y; c.sun[2]=sunDir.z;
                c.sunc[0]=c.sunc[1]=c.sunc[2]=sunLux; c.misc[0]=mat.checker ? 1.f : 0.f; c.misc[1]=exposure; c.misc[2]=bumpy ? 1.f : 0.f; c.misc[3]=float(o.feature);
                context->UpdateSubresource(ocb.Get(), 0, nullptr, &c, 0, 0);
                UINT stride=sizeof(Vertex), offset=0; auto* vb=m.vb.Get();
                context->IASetVertexBuffers(0, 1, &vb, &stride, &offset); context->IASetIndexBuffer(m.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
                context->DrawIndexed(m.submeshes[s].indexCount, m.submeshes[s].indexStart, 0);
            }
        }
        ID3D11RenderTargetView* none[6]{}; context->OMSetRenderTargets(6, none, nullptr);
        ID3D11ShaderResourceView* noSrv=nullptr; context->PSSetShaderResources(0, 1, &noSrv);
    };
    auto readback=[&]() {
        D3D11_TEXTURE2D_DESC d{}; color->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
        context->CopyResource(staging.Get(), color.Get());
        D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
        std::vector<float> px(size_t(W)*H*3);
        for(UINT y=0;y<H;++y) { const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData)+size_t(y)*m.RowPitch);
            for(UINT x=0;x<W;++x) for(int k=0;k<3;++k) px[(size_t(y)*W+x)*3+k]=half_to_float(row[x*4+k]); }
        context->Unmap(staging.Get(), 0);
        return px;
    };
    auto save=[&](const std::vector<float>& px, const char* name, bool tonemap) {
        // HDRP's render targets are upside down against the screen (its final blit flips them): flipped back here.
        std::vector<uint8_t> out(px.size());
        for(size_t i=0;i<px.size();++i) {
            const size_t y=i/(size_t(W)*3), rest=i%(size_t(W)*3);
            float v=px[(size_t(H)-1-y)*size_t(W)*3+rest]; if(!std::isfinite(v)) v=1e3f;
            if(tonemap) v=v/(1.f+v);
            out[i]=uint8_t(std::lround(std::clamp(std::pow(std::clamp(v, 0.f, 1.f), 1.f/2.2f), 0.f, 1.f)*255.f));
        }
        write_png(images/name, int(W), int(H), out);
    };
    // ReSTIR's histories in frames: direct light's, and bounced light's (0: the same as direct light's).
    uint32_t sequence=0, rays=1, history=20, giHistory=0, thickProfiles=0;
    float tracedGain=0; // the traced light's own exposure (0: 1)
    bool sunOn=true;
    bool sunShadowedByHdrp=false; // the plugin's sun HDRP shadows itself (its cascades): TGPT_Frame.sunColor's w 2
    uint32_t spatialSamples=4, bounces=2;
    float rayDistance=500;
    float indirectColor=1; // Indirect Color: 1 as traced
    float reflectionBoost=0; // Reflection Boost: 0 as the materials are
    float skyIntensity=1; // the sky's light (1e-6: none, as the plugin sends for interiors; 0 means the default, 1)
    bool rayReconstruction=false; // frames that ask for Ray Reconstruction's guides and hand the light out undenoised
    bool cut=false; // the frame starts over as after a camera cut (TGPT_FRAME_RESET), keeping the radiance cache
    // The share of the game's ambient light HDRP leaves what it shades itself (TGPT_Frame.forward): 1, all of it, unless a
    // test gives the traced bounced light the rest.
    float forwardKept=1.f;
    TGPT_Frame last{};
    auto run=[&](uint32_t view, const Camera& cam, const Camera& prev, uint32_t frameIndex, uint32_t options=0) {
        raster(cam, prev);
        TGPT_Frame f{sizeof(TGPT_Frame), TGPT_ABI};
        f.sequence=++sequence; f.flags=(frameIndex==0 || cut) ? TGPT_FRAME_RESET : 0;
        f.gbuffer0=uint64_t(uintptr_t(gb0.Get())); f.normal=uint64_t(uintptr_t(nb.Get())); f.gbuffer2=uint64_t(uintptr_t(gb2.Get()));
        f.gbuffer3=uint64_t(uintptr_t(gb3.Get())); f.depth=uint64_t(uintptr_t(depth.Get())); f.motion=uint64_t(uintptr_t(mv.Get()));
        f.exposure=uint64_t(uintptr_t(exposureTex.Get()));
        f.renderWidth=W; f.renderHeight=H; f.debugView=view;
        unity_layout(cam.invViewProj, f.invViewProj); unity_layout(cam.viewProj, f.viewProj);
        unity_layout(mul(prev.nonJitteredViewProj, translate_scale(cam.position-prev.position, {1, 1, 1})), f.prevViewProj); // as HDRP's
        unity_layout(cam.view, f.view); unity_layout(cam.nonJitteredProj, f.nonJitteredProj); f.jitter[0]=cam.jitter[0]; f.jitter[1]=cam.jitter[1];
        f.cameraPosition[0]=cam.position.x; f.cameraPosition[1]=cam.position.y; f.cameraPosition[2]=cam.position.z;
        f.prevCameraPosition[0]=prev.position.x; f.prevCameraPosition[1]=prev.position.y; f.prevCameraPosition[2]=prev.position.z;
        f.sunDirection[0]=sunDir.x; f.sunDirection[1]=sunDir.y; f.sunDirection[2]=sunDir.z; f.sunDirection[3]=0.0047f;
        f.sunColor[0]=f.sunColor[1]=f.sunColor[2]=sunLux; f.sunColor[3]=sunOn ? (sunShadowedByHdrp ? 2.f : 1.f) : 0.f;
        f.frameIndex=frameIndex; f.bounces=bounces; f.spatialSamples=spatialSamples; f.diHistory=history; f.giHistory=giHistory ? giHistory : history; f.thickProfiles=thickProfiles; f.tracedGain=tracedGain; f.options=options; f.raysPerPixel=rays;
        // A sequence's first frame runs without the radiance cache, which empties it for the next: each case starts from
        // nothing, as a new scene does in game (where a camera cut keeps the cache's cells, the world's).
        if(frameIndex==0) f.options|=TGPT_OPTION_NO_RADIANCE_CACHE;
        if(rayReconstruction) { f.flags|=TGPT_FRAME_GUIDES; f.options|=TGPT_OPTION_RAY_RECONSTRUCTION; }
        f.maxRayDistance=rayDistance; f.skyIntensity=skyIntensity; f.indirectColor=indirectColor; f.reflectionBoost=reflectionBoost;
        f.forward[0]=forwardKept;
        if(SubmitFrame(&f)!=0) { expect(false, "frame submitted"); return; }
        render(TGPT_EVENT_TRACE, reinterpret_cast<void*>(uintptr_t(f.sequence)));
        SetComposite(f.sequence, uint64_t(uintptr_t(color.Get())));
        render(TGPT_EVENT_COMPOSITE, reinterpret_cast<void*>(uintptr_t(f.sequence)));
        last=f;
    };
    const Camera cam=make_camera({0, 3.2f, -9}, {0, 2.2f, 2}, 60.f, float(W)/float(H));
    // Warm-up: meshes are read back and built over the first frames.
    for(uint32_t i=0;i<8;++i) { run(TGPT_VIEW_NONE, cam, cam, i); context->Flush(); Sleep(20); }
    ReadStatus(&status);
    std::printf("      after warm-up: meshes ready %u queued %u failed %u, instances traced %u of %u, triangles %llu, textures %u, frames traced %llu failed %llu\n",
        status.meshesReady, status.meshesQueued, status.meshesFailed, status.instancesTraced, status.instances, (unsigned long long)status.triangles,
        status.textures, (unsigned long long)status.framesTraced, (unsigned long long)status.framesFailed);
    expect(status.meshesReady==4 && status.instancesTraced==uint32_t(instances.size()), "every mesh read back, built and traced");
    // HDRP's image without path tracing, for comparison.
    raster(cam, cam); const auto base=readback(); save(base, "0-hdrp-direct-only.png", true);
    // Path tracing, converged over a few dozen frames: the sun's and the point light's direct light with traced
    // shadows, the bounced light and reflections.
    std::vector<float> traced;
    for(uint32_t i=0;i<48;++i) run(TGPT_VIEW_NONE, cam, cam, 8+i);
    traced=readback(); save(traced, "1-path-tracing.png", true);
    {
        const int probes[3][2]={{640, 600}, {200, 400}, {1100, 400}};
        for(const auto& pr:probes) { const size_t i=(size_t(pr[1])*W+pr[0])*3;
            std::printf("      probe (%d,%d): HDRP direct %.4f %.4f %.4f, path traced %.4f %.4f %.4f\n", pr[0], pr[1], base[i], base[i+1], base[i+2], traced[i], traced[i+1], traced[i+2]); }
    }
    ReadStatus(&status);
    std::printf("      render thread: D3D11 %.2f ms, flush %.2f ms, waiting %.2f ms, recording %.2f ms\n", status.d3d11Ms, status.flushMs, status.waitMs, status.recordMs);
    std::printf("      GPU %.2f ms a frame, render thread %.2f ms, frames traced %llu failed %llu\n", status.gpuMs, status.frameMs,
        (unsigned long long)status.framesTraced, (unsigned long long)status.framesFailed);
    bool finite=true; for(float v:traced) if(!std::isfinite(v)) { finite=false; break; }
    expect(finite, "the path traced image is finite");
    // The sky the camera sees against the open sky's (for what HDRP still lights with its sky: transparent surfaces,
    // fog): the camera stands in front of the room, open above and behind, so some of it but never more than all.
    std::printf("      sky seen from the camera: %.3f of the open sky's (radiance %.3f %.3f %.3f)\n", status.cameraLight,
        status.cameraRadiance[0], status.cameraRadiance[1], status.cameraRadiance[2]);
    expect(status.cameraLight>0.05f && status.cameraLight<=1.f, "the sky the camera sees is measured against the open sky's");
    // Under the ceiling, out of the sun: HDRP's direct light (the sun alone in this harness) leaves it dark, the
    // point light and the bounced light reach it.
    auto region=[&](const std::vector<float>& px, int x0, int y0, int x1, int y1) { double s=0; int n=0;
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) { const size_t i=(size_t(y)*W+x)*3; s+=0.2126*px[i]+0.7152*px[i+1]+0.0722*px[i+2]; ++n; } return s/n; };
    const double ceilingBase=region(base, 500, 650, 780, 700), ceilingTraced=region(traced, 500, 650, 780, 700);
    std::printf("      ceiling underside luminance: HDRP direct %.4f, path traced %.4f\n", ceilingBase, ceilingTraced);
    expect(ceilingTraced>ceilingBase+0.005, "light reaches the ceiling's underside");
    // Indirect Color: the white ceiling by the red wall takes the wall's color from the bounced light; 0 leaves it the
    // lamp's, more reddens it further, and its brightness stays (read-back rows are bottom-up: 650-705 is the ceiling).
    {
        auto rgb=[&](const std::vector<float>& px, int x0, int y0, int x1, int y1) {
            double s[3]{}; int n=0;
            for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) { const size_t i=(size_t(y)*W+x)*3; for(int c=0;c<3;++c) s[c]+=px[i+c]; ++n; }
            return std::array<double, 3>{s[0]/n, s[1]/n, s[2]/n};
        };
        const float amounts[3]={0.f, 1.f, 3.f};
        double ratio[3]{}, level[3]{};
        for(int k=0;k<3;++k) {
            indirectColor=amounts[k];
            for(uint32_t i=0;i<32;++i) run(TGPT_VIEW_NONE, cam, cam, 900+uint32_t(k)*40+i);
            const auto c=rgb(readback(), 120, 650, 300, 705);
            ratio[k]=c[1]>0 ? c[0]/c[1] : 0; level[k]=0.2126*c[0]+0.7152*c[1]+0.0722*c[2];
        }
        indirectColor=1;
        std::printf("      Indirect Color on the ceiling by the red wall: red/green %.3f at 0, %.3f at 1, %.3f at 3; luminance %.4f, %.4f, %.4f\n",
            ratio[0], ratio[1], ratio[2], level[0], level[1], level[2]);
        expect(ratio[0]<ratio[1] && ratio[1]<ratio[2], "Indirect Color scales the tint bounced light carries");
        expect(std::fabs(level[2]-level[1])<0.15*level[1] && std::fabs(level[0]-level[1])<0.15*level[1], "Indirect Color leaves the bounced light's brightness");
    }
    // The traced light doesn't depend on HDRP's exposure: the same frames at a dark view's exposure (100 times the sunlit
    // one) come out 100 times brighter and otherwise the same. Clamps measured in pre-exposed units cut the bounced light
    // of a hall lit by a sunlit courtyard, and it followed the auto exposure as the camera turned between bright and dark.
    {
        auto at=[&](float e) {
            setExposure(e);
            for(uint32_t i=0;i<24;++i) run(TGPT_VIEW_NONE, cam, cam, 1600+i); // each run starts over (a frame gap)
            return readback();
        };
        const auto sunlit=at(0.25f), dark=at(25.f);
        setExposure(0.25f);
        double diff=0, level=0, ceilingDiff=0, ceilingLevel=0;
        for(UINT y=2;y<H-2;++y) for(UINT x=2;x<W-2;++x) {
            const size_t j=(size_t(y)*W+x)*3;
            const double a=(0.2126*sunlit[j]+0.7152*sunlit[j+1]+0.0722*sunlit[j+2])/0.25, b=(0.2126*dark[j]+0.7152*dark[j+1]+0.0722*dark[j+2])/25.0;
            diff+=std::fabs(a-b); level+=a;
            if(y>=650 && y<700 && x>=500 && x<780) { ceilingDiff+=std::fabs(a-b); ceilingLevel+=a; }
        }
        std::printf("      exposure 0.25 against 25, divided out: mean difference %.5f of the image, %.5f under the ceiling (bounced light)\n",
            level>0 ? diff/level : 0.0, ceilingLevel>0 ? ceilingDiff/ceilingLevel : 0.0);
        expect(level>0 && diff<0.01*level && ceilingDiff<0.01*ceilingLevel, "the traced light doesn't depend on HDRP's exposure");
    }
    // The traced light's own exposure: a gain of 4 makes the traced light 4 times brighter (the ceiling, lit only by
    // bounced light), and the meter reports it as it would be shown without the gain.
    {
        auto at=[&](float gain) {
            tracedGain=gain;
            for(uint32_t i=0;i<24;++i) run(TGPT_VIEW_NONE, cam, cam, 1700+i);
            return readback();
        };
        const auto one=at(1.f), four=at(4.f);
        tracedGain=0;
        double a=0, b=0;
        for(UINT y=650;y<700;++y) for(UINT x=500;x<780;++x) {
            const size_t j=(size_t(y)*W+x)*3;
            a+=0.2126*one[j]+0.7152*one[j+1]+0.0722*one[j+2]; b+=0.2126*four[j]+0.7152*four[j+1]+0.0722*four[j+2];
        }
        ReadStatus(&status);
        std::printf("      traced light gain 4 against 1 under the ceiling: %.3f times; meter median log2 %.2f, 98th percentile %.2f, weight %u\n",
            a>0 ? b/a : 0.0, status.tracedLog2Mid, status.tracedLog2High, status.tracedSamples);
        expect(a>0 && std::fabs(b/a-4.0)<0.2, "the traced light's gain scales the traced light");
        expect(status.tracedSamples>0 && std::isfinite(status.tracedLog2Mid) && status.tracedLog2High>=status.tracedLog2Mid,
               "the meter reports the traced light's median and 98th percentile");
    }
    // A surface HDRP draws forward (hair, beards, some props: outside its deferred lighting, nothing in the GBuffer) is
    // found along the camera ray in the traced scene and lit by the tracer like a deferred one, its color the traced
    // material's; HDRP's own shading (here the sun alone, which misses the box's face towards the camera) is replaced.
    {
        auto boxFace=[&](bool drawnForward) {
            objects[5].forward=drawnForward;
            for(uint32_t i=0;i<32;++i) run(TGPT_VIEW_NONE, cam, cam, 1700+i); // each run starts over (a frame gap)
            const auto px=readback();
            if(drawnForward) save(px, "1b-box-drawn-forward.png", true);
            return region(px, 700, 190, 810, 300); // the box's face towards the camera (read-back rows are bottom-up)
        };
        const double deferredBox=boxFace(false), forwardBox=boxFace(true);
        objects[5].forward=false;
        std::printf("      box face luminance: drawn deferred %.4f, drawn forward %.4f\n", deferredBox, forwardBox);
        expect(deferredBox>0.01 && std::fabs(forwardBox-deferredBox)<0.15*deferredBox, "a surface HDRP draws forward is lit by the tracer like a deferred one");
        // A surface HDRP shades itself (the eyes: its eye shader refracts the iris from textures the tracer doesn't read)
        // stops the camera ray and keeps HDRP's shading, as a surface missing from the traced scene does. Missing, the eyes
        // let the ray on to the socket just behind them, which the tracer lit in their place (white eyes, no irises).
        {
            auto flagged=instances; flagged[5].flags|=TGPT_INSTANCE_ALPHA_TEST; // tested per ray, as a character with eyes is
            expect(SetScene(flagged.data(), uint32_t(flagged.size()), indices.data(), uint32_t(indices.size()))==0, "scene set");
            auto faceMarked=[&](uint32_t flags, float cutoff, uint32_t seed) {
                auto changed=materials; changed[3].flags=flags; changed[3].alphaCutoff=cutoff;
                expect(SetMaterials(0, changed.data(), uint32_t(changed.size()))==0, "materials set");
                objects[5].forward=true;
                for(uint32_t i=0;i<32;++i) run(TGPT_VIEW_NONE, cam, cam, seed+i);
                objects[5].forward=false;
                return region(readback(), 700, 190, 810, 300);
            };
            const double missing=faceMarked(TGPT_MATERIAL_ALPHA_TEST, 2.f, 1740); // invisible to every ray: HDRP's shading stays
            const double hdrpShaded=faceMarked(TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_HDRP_SHADED, 0.f, 1780);
            expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "materials set");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene set");
            std::printf("      box face drawn forward: missing from the traced scene %.4f, marked as HDRP's %.4f, traced %.4f\n", missing, hdrpShaded, forwardBox);
            expect(std::fabs(forwardBox-missing)>0.005 && std::fabs(hdrpShaded-missing)<0.1*std::fabs(forwardBox-missing),
                   "a surface HDRP shades itself stops the camera ray and keeps its shading");
        }
    }
    // Debug views.
    const std::pair<uint32_t, const char*> views[]={{TGPT_VIEW_ALBEDO, "2-view-albedo.png"}, {TGPT_VIEW_NORMALS, "3-view-normals.png"},
        {TGPT_VIEW_COVERAGE, "4-view-coverage.png"}, {TGPT_VIEW_MATERIALS, "5-view-materials.png"}, {TGPT_VIEW_INDIRECT, "6-view-indirect.png"},
        {TGPT_VIEW_INDIRECT_RAW, "7-view-indirect-raw.png"}, {TGPT_VIEW_SURFACES, "8-view-surfaces.png"},
        {TGPT_VIEW_SPECULAR, "9-view-specular.png"}};
    for(const auto& v:views) {
        // Twice: the composite applies the previous frame's traced output (one frame of the view before would show).
        run(v.first, cam, cam, 100); run(v.first, cam, cam, 101);
        const auto px=readback(); save(px, v.second, v.first==TGPT_VIEW_INDIRECT || v.first==TGPT_VIEW_INDIRECT_RAW);
        if(v.first==TGPT_VIEW_COVERAGE) {
            int covered=0, total=0;
            for(UINT y=0;y<H;y+=4) for(UINT x=0;x<W;x+=4) { const size_t i=(size_t(y)*W+x)*3; const bool geometry=base[i]!=skyColor[0] || base[i+1]!=skyColor[1];
                if(!geometry) continue; ++total; if(px[i]+px[i+1]+px[i+2]>0.3f) ++covered; }
            std::printf("      coverage: %d of %d sampled geometry pixels have traced geometry in front of or at HDRP's surface\n", covered, total);
            expect(total>0 && covered>total*95/100, "the traced scene covers what HDRP drew");
        }
        if(v.first==TGPT_VIEW_SURFACES) {
            // Every surface the harness draws is deferred: coloured by its material type, none magenta (shaded by HDRP).
            int coloured=0, forward=0, total=0;
            for(UINT y=0;y<H;y+=4) for(UINT x=0;x<W;x+=4) { const size_t i=(size_t(y)*W+x)*3; const bool geometry=base[i]!=skyColor[0] || base[i+1]!=skyColor[1];
                if(!geometry) continue; ++total; if(px[i]+px[i+1]+px[i+2]>0.05f) ++coloured; if(px[i]>0.3f && px[i+2]>0.3f && px[i+1]<0.1f) ++forward; }
            std::printf("      surfaces view: %d of %d sampled geometry pixels coloured, %d shown as shaded by HDRP\n", coloured, total, forward);
            expect(total>0 && coloured>total*95/100 && forward==0, "the Surfaces view colours every deferred pixel by its material type");
        }
        if(v.first==TGPT_VIEW_SPECULAR) {
            // The traced reflections alone: some light on the room's surfaces, nothing broken.
            int lit=0; bool finiteView=true;
            for(UINT y=0;y<H;y+=4) for(UINT x=0;x<W;x+=4) { const size_t i=(size_t(y)*W+x)*3; if(!std::isfinite(px[i]) || !std::isfinite(px[i+1]) || !std::isfinite(px[i+2])) finiteView=false; else if(px[i]+px[i+1]+px[i+2]>0.002f) ++lit; }
            std::printf("      specular view: %d sampled pixels with reflected light\n", lit);
            expect(finiteView && lit>100, "the Specular view shows the traced reflections");
        }
    }
    // What the traced scene has under the screen's centre (for the F8 capture's report): the room, about 11 m away.
    ReadStatus(&status);
    std::printf("      traced under the screen's centre: %s, instance %u, material %u, %.2f m\n", status.pickFound ? "found" : "nothing", status.pickInstance, status.pickMaterial, status.pickDistance);
    expect(status.pickFound==1 && status.pickDistance>3.f && status.pickDistance<20.f, "the tracer reports what is under the screen's centre");
    // The box shadows the floor in the sun's direction: traced shadows must be darker than lit floor.
    run(TGPT_VIEW_DIRECT, cam, cam, 300);
    const auto direct=readback(); save(direct, "9-view-direct.png", true);
    // A moving camera: history must follow HDRP's motion vectors (no smearing into NaNs).
    Camera prev=cam;
    for(uint32_t i=0;i<24;++i) {
        const float t=float(i)/23.f;
        const Camera c=make_camera({-1.5f+3*t, 3.2f, -9+t}, {0, 2.2f, 2}, 60.f, float(W)/float(H));
        run(TGPT_VIEW_NONE, c, prev, 400+i);
        prev=c;
    }
    const auto moving=readback(); save(moving, "10-moving-camera.png", true);
    finite=true; for(float v:moving) if(!std::isfinite(v)) { finite=false; break; }
    expect(finite, "the image stays finite with a moving camera");
    // Throughput: HDRP renders shadows and lighting between the two events, hiding the D3D11 -> D3D12 -> D3D11
    // round trip. A compute load of a few milliseconds stands in for that work here.
    {
        const char* busySource=R"HLSL(
RWTexture2D<float4> t : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) { float4 v = t[p]; [loop] for (int i = 0; i < 1500; ++i) v = frac(v * 1.0001 + 0.37); t[p] = v; }
)HLSL";
        auto busyCode=compile(busySource, "main", "cs_5_0");
        ComPtr<ID3D11ComputeShader> busy; device->CreateComputeShader(busyCode->GetBufferPointer(), busyCode->GetBufferSize(), nullptr, &busy);
        auto busyTex=tex(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
        ComPtr<ID3D11UnorderedAccessView> busyUav; device->CreateUnorderedAccessView(busyTex.Get(), nullptr, &busyUav);
        auto hdrp_work=[&]() {
            auto* u=busyUav.Get(); context->CSSetShader(busy.Get(), nullptr, 0); context->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
            context->Dispatch(W/8, H/8, 1);
            ID3D11UnorderedAccessView* none=nullptr; context->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
        };
        ComPtr<ID3D11Query> done; { D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0}; device->CreateQuery(&q, &done); }
        auto finish=[&]() { context->End(done.Get()); while(context->GetData(done.Get(), nullptr, 0, 0)==S_FALSE) {} };
        auto measure=[&](bool traced, bool work) {
            finish();
            const auto t0=std::chrono::steady_clock::now();
            const int frames=40;
            for(int i=0;i<frames;++i) {
                if(traced) {
                    raster(cam, cam);
                    TGPT_Frame f=last; f.sequence=++sequence; f.frameIndex=500+i; f.debugView=0; f.flags=0;
                    SubmitFrame(&f);
                    render(TGPT_EVENT_TRACE, reinterpret_cast<void*>(uintptr_t(f.sequence)));
                    if(work) hdrp_work();
                    SetComposite(f.sequence, uint64_t(uintptr_t(color.Get())));
                    render(TGPT_EVENT_COMPOSITE, reinterpret_cast<void*>(uintptr_t(f.sequence)));
                } else { raster(cam, cam); if(work) hdrp_work(); }
                context->Flush();
            }
            finish();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-t0).count()/frames;
        };
        const double baseOnly=measure(false, false), baseWork=measure(false, true), traced=measure(true, false), tracedWork=measure(true, true);
        std::printf("      throughput: raster %.2f ms, raster + HDRP-like work %.2f ms; traced %.2f ms, traced + HDRP-like work %.2f ms a frame\n",
            baseOnly, baseWork, traced, tracedWork);
        std::printf("      path tracing adds %.2f ms a frame when HDRP's work sits between the events\n", tracedWork-baseWork);
    }
    // Many lights, one of them near: 600 small lights 60-150 m from the room (candles across a town) besides
    // the room's own. Path vertices must choose among the lights that reach them, not among all of them, or
    // the room's light is chosen rarely and then counted hundreds of times: fireflies.
    {
        auto speckles=[&](const std::vector<float>& px) {
            int n=0;
            for(UINT y=1;y+1<H;++y) for(UINT x=1;x+1<W;++x) {
                float v[9]; int k=0;
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    const size_t i=(size_t(int(y)+dy)*W+size_t(int(x)+dx))*3; v[k++]=0.2126f*px[i]+0.7152f*px[i+1]+0.0722f*px[i+2]; }
                const float c=v[4]; std::nth_element(v, v+4, v+9);
                if(c>v[4]*2.f+0.05f) ++n;
            }
            return n;
        };
        std::vector<TGPT_Light> many(1, light);
        uint32_t seed=12345; auto rnd=[&]() { seed=seed*1664525u+1013904223u; return float(seed>>8)/16777216.f; };
        for(uint32_t i=1;i<=600;++i) {
            TGPT_Light l{}; const float a=rnd()*6.2831853f, d=60.f+rnd()*90.f;
            l.position[0]=std::cos(a)*d; l.position[1]=1.f+rnd()*4.f; l.position[2]=std::sin(a)*d; l.type=0;
            l.color[0]=40; l.color[1]=22; l.color[2]=8; l.range=6; l.radius=0.05f; l.id=i;
            many.push_back(l);
        }
        expect(SetLights(many.data(), uint32_t(many.size()))==0, "600 distant lights set");
        for(uint32_t i=0;i<48;++i) run(TGPT_VIEW_NONE, cam, cam, 600+i);
        const auto crowd=readback(); save(crowd, "11-with-600-distant-lights.png", true);
        const int one=speckles(traced), withCrowd=speckles(crowd);
        const double ceilingCrowd=region(crowd, 500, 650, 780, 700);
        std::printf("      speckles (pixels over twice their neighbourhood's median): one light %d, with 600 distant lights %d\n", one, withCrowd);
        std::printf("      ceiling underside luminance: one light %.4f, with 600 distant lights %.4f\n", ceilingTraced, ceilingCrowd);
        expect(withCrowd<=one*2+50, "distant lights add no fireflies");
        expect(std::fabs(ceilingCrowd-ceilingTraced)<0.1*ceilingTraced+0.002, "distant lights leave the room's light as it was");
        // More rays per pixel: the same light with less noise, for about proportionally more GPU time.
        rays=4;
        for(uint32_t i=0;i<48;++i) run(TGPT_VIEW_NONE, cam, cam, 700+i);
        ReadStatus(&status);
        const auto four=readback(); save(four, "12-4-rays-per-pixel.png", true);
        std::printf("      4 rays per pixel: GPU %.2f ms a frame, speckles %d, ceiling underside %.4f\n", status.gpuMs, speckles(four), region(four, 500, 650, 780, 700));
        rays=1;
        expect(SetLights(&light, 1)==0, "one light again");
    }
    // Longer ReSTIR history must not leave more grain: its samples persist across frames, which the denoiser
    // has to see as noise (not as converged detail). Grain: mean deviation from the 3x3 mean, relative to the
    // mean, under the ceiling (read-back rows are bottom-up: rows 600-710 are the ceiling), lit only indirectly.
    // Also as the game shows it: HDRP jitters the camera every frame for its upscaler, and the camera moves,
    // which restarts history where surfaces appear. Flicker: the mean change from one frame to the next,
    // relative to the mean, over the last frames. The whole image counts too (edges add the same to every case).
    {
        auto grain=[&](const std::vector<float>& px, UINT x0, UINT y0, UINT x1, UINT y1) {
            double sum=0, level=0; int n=0;
            for(UINT y=y0;y<y1;++y) for(UINT x=x0;x<x1;++x) {
                double c=0, mean=0;
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    const size_t i=(size_t(int(y)+dy)*W+size_t(int(x)+dx))*3;
                    const double l=0.2126*px[i]+0.7152*px[i+1]+0.0722*px[i+2];
                    mean+=l; if(dx==0 && dy==0) c=l;
                }
                mean/=9; sum+=std::fabs(c-mean); level+=mean; ++n;
            }
            return level>0 ? sum/level : 0.0;
        };
        auto halton=[](uint32_t i, uint32_t b) { float f=1, r=0; while(i) { f/=float(b); r+=f*float(i%b); i/=b; } return r; };
        // An interior at night as well: no sun, and 24 torches along the walls, some behind the box.
        std::vector<TGPT_Light> torches;
        for(uint32_t i=0;i<24;++i) {
            TGPT_Light l{}; const float t=float(i%8)/7.f;
            if(i<8) { l.position[0]=-5.6f; l.position[2]=-5+10*t; } else if(i<16) { l.position[0]=5.6f; l.position[2]=-5+10*t; } else { l.position[0]=-5+10*t; l.position[2]=5.6f; }
            l.position[1]=1.6f+0.4f*float(i%3); l.type=0; l.color[0]=12; l.color[1]=6.5f; l.color[2]=2.4f; l.range=7; l.radius=0.04f; l.id=100+i;
            torches.push_back(l);
        }
        // And one high above the checkered leaf: its shadow on the floor is fine detail, like a rope net's on a wall.
        { TGPT_Light l{}; l.position[0]=-1; l.position[1]=7; l.position[2]=3.5f; l.type=0; l.color[0]=90; l.color[1]=60; l.color[2]=30; l.range=14; l.radius=0.03f; l.id=200; torches.push_back(l); }
        // Limit: the floor's error against the undenoised frame, where the sun's sharp shadows are (the box's and
        // the checkered leaf's). 0.106 while the denoiser blurred them: the sun filtered with the bounced light,
        // and steered by a single frame's noise (and a 3x3 spread that took every shadow edge for noise).
        struct Case { const char* name; uint32_t cap; bool jitter, moving, night, bumps=false; double floorLimit=0; uint32_t giCap=0; uint32_t options=0; };
        const Case cases[]={
            {"history 1, still", 1, false, false, false, false, 0.06}, {"history 20, still", 20, false, false, false},
            {"history 1, jittered", 1, true, false, false}, {"history 20, jittered", 20, true, false, false},
            {"history 1, moving", 1, true, true, false}, {"history 20, moving", 20, true, true, false},
            {"night, history 1, still", 1, false, false, true}, {"night, history 1, jittered", 1, true, false, true},
            {"night, history 1, moving", 1, true, true, true}, {"night, history 20, moving", 20, true, true, true},
            {"bumpy, history 1, moving", 1, true, true, false, true},
            {"bumpy night, history 1, moving", 1, true, true, true, true}, {"bumpy night, history 20, moving", 20, true, true, true, true},
            // The plugin's defaults, NVIDIA RTXDI's: direct light 20 frames, bounced light 8.
            {"defaults (20/8), still", 20, false, false, false, false, 0, 8}, {"defaults (20/8), moving", 20, true, true, false, false, 0, 8},
            {"night, defaults (20/8), moving", 20, true, true, true, false, 0, 8},
            // Spatial reuse feeding the next frame (an option, off by default), for the record.
            {"defaults, moving, spatial feedback", 20, true, true, false, false, 0, 8, TGPT_OPTION_SPATIAL_FEEDBACK},
            {"night, defaults, moving, spatial feedback", 20, true, true, true, false, 0, 8, TGPT_OPTION_SPATIAL_FEEDBACK},
            // NVIDIA NRD (RELAX) in place of the built-in denoiser, on the same scenes and cameras as the defaults' cases.
            {"NRD: defaults (20/8), still", 20, false, false, false, false, 0, 8, TGPT_OPTION_NRD},
            {"NRD: defaults (20/8), moving", 20, true, true, false, false, 0, 8, TGPT_OPTION_NRD},
            {"NRD: night, defaults (20/8), moving", 20, true, true, true, false, 0, 8, TGPT_OPTION_NRD},
            {"NRD: bumpy night, history 20, moving", 20, true, true, true, true, 0, 0, TGPT_OPTION_NRD}};
        // Each case's errors and flicker, by name, for NRD's comparison with the built-in denoiser after the cases.
        struct Measured { double whole=0, floor=0, ceiling=0, flicker=0, gpuMs=0; bool finite=true, nrdActive=false; };
        std::vector<std::pair<std::string, Measured>> measured;
        ReadStatus(&status);
        const bool haveNrd=(status.features&TGPT_FEATURE_NRD)!=0;
#if TGPT_EXPECT_NRD
        expect(haveNrd, "NVIDIA NRD started on the device (this build has it)");
#endif
        double still[2]{};
        uint32_t index=800;
        for(const Case& k:cases) {
            if((k.options&TGPT_OPTION_NRD) && !haveNrd) { std::printf("      %s: skipped (no NRD in this build)\n", k.name); continue; }
            history=k.cap; giHistory=k.giCap; bumpy=k.bumps;
            if(k.night==sunOn) { sunOn=!k.night; expect(k.night ? SetLights(torches.data(), uint32_t(torches.size()))==0 : SetLights(&light, 1)==0, k.night ? "torches set" : "one light again"); }
            Camera prev{};
            std::vector<float> px, before;
            double flicker=0; int flickerFrames=0;
            for(uint32_t i=0;i<48;++i) {
                const float jx=k.jitter ? halton(i%8+1, 2)-0.5f : 0.f, jy=k.jitter ? halton(i%8+1, 3)-0.5f : 0.f;
                const Camera c=make_camera({k.moving ? -0.6f+0.025f*float(i) : 0.f, 3.2f, -9}, {0, 2.2f, 2}, 60.f, float(W)/float(H), jx, jy, int(W), int(H));
                run(TGPT_VIEW_NONE, c, i ? prev : c, i ? index++ : 0, k.options); // each case starts over
                prev=c;
                if(i<40) continue;
                px=readback();
                if(!before.empty()) {
                    double change=0, level=0;
                    for(UINT y=600;y<710;++y) for(UINT x=300;x<980;++x) {
                        const size_t j=(size_t(y)*W+x)*3;
                        const double l=0.2126*px[j]+0.7152*px[j+1]+0.0722*px[j+2], lb=0.2126*before[j]+0.7152*before[j+1]+0.0722*before[j+2];
                        change+=std::fabs(l-lb); level+=l;
                    }
                    if(level>0) { flicker+=change/level; ++flickerFrames; }
                }
                before=px;
            }
            ReadStatus(&status);
            const bool nrdRan=(status.features&TGPT_FEATURE_NRD_ACTIVE)!=0; // before the reference's frames, which run without a denoiser
            const double caseGpuMs=status.gpuMs;
            const double ceiling=grain(px, 300, 600, 980, 710), whole=grain(px, 2, 2, W-2, H-2);
            // Bump shading is detail, not grain, and a denoiser that blurs shadows away is wrong too: the error
            // against a converged frame at the last frame's camera tells them apart. The reference is undenoised:
            // 48 frames of 8 rays a pixel without the denoiser, averaged, the camera still.
            const uint32_t keepRays=rays; rays=8;
            for(uint32_t i=0;i<8;++i) run(TGPT_VIEW_NONE, prev, prev, index++, TGPT_OPTION_NO_DENOISER);
            std::vector<double> sum(size_t(W)*H*3, 0.0);
            for(uint32_t i=0;i<48;++i) {
                run(TGPT_VIEW_NONE, prev, prev, index++, TGPT_OPTION_NO_DENOISER);
                const auto r=readback();
                for(size_t j=0;j<sum.size();++j) sum[j]+=r[j];
            }
            rays=keepRays;
            std::vector<float> reference(sum.size());
            for(size_t j=0;j<sum.size();++j) reference[j]=float(sum[j]/48.0);
            if(k.bumps && !k.night) { save(px, "15-bumpy-full-moving.png", true); save(reference, "16-bumpy-full-reference.png", true); }
            if(!k.jitter && !k.night && k.cap==1) { save(px, "17-full-still.png", true); save(reference, "18-full-still-reference.png", true); }
            // The still night scene, light by light: the denoised direct and bounced light against their undenoised
            // averages, as a mean ratio (1: no light lost or gained) and an error, on the floor.
            if(k.night && !k.moving && !k.jitter) {
                struct Part { uint32_t view, spatial; const char* name; };
                const Part parts[3]={{TGPT_VIEW_DIRECT, 4, "direct"}, {TGPT_VIEW_DIRECT, 0, "direct, no spatial reuse"}, {TGPT_VIEW_INDIRECT, 4, "bounced"}};
                for(const auto& part:parts) {
                    spatialSamples=part.spatial;
                    for(uint32_t i=0;i<48;++i) run(part.view, prev, prev, i ? index++ : 0);
                    const auto denoised=readback();
                    std::vector<double> total(size_t(W)*H*3, 0.0);
                    rays=8;
                    for(uint32_t i=0;i<8;++i) run(part.view, prev, prev, index++, TGPT_OPTION_NO_DENOISER);
                    for(uint32_t i=0;i<48;++i) { run(part.view, prev, prev, index++, TGPT_OPTION_NO_DENOISER); const auto r=readback(); for(size_t j=0;j<total.size();++j) total[j]+=r[j]; }
                    rays=keepRays;
                    double a0=0, a1=0, e=0;
                    for(UINT y=150;y<300;++y) for(UINT x=150;x<1000;++x) {
                        const size_t j=(size_t(y)*W+x)*3;
                        const double l=0.2126*denoised[j]+0.7152*denoised[j+1]+0.0722*denoised[j+2];
                        const double lr=(0.2126*total[j]+0.7152*total[j+1]+0.0722*total[j+2])/48.0;
                        a0+=l; a1+=lr; e+=std::fabs(l-lr);
                    }
                    std::printf("      night floor, %s light: denoised/undenoised mean %.3f, error %.4f\n", part.name, a1>0 ? a0/a1 : 0.0, a1>0 ? e/a1 : 0.0);
                    std::vector<float> avg(total.size()); for(size_t j=0;j<total.size();++j) avg[j]=float(total[j]/48.0);
                    const char* names[3][2]={{"21-night-direct.png", "22-night-direct-reference.png"}, {"25-night-direct-no-spatial.png", "26-night-direct-no-spatial-reference.png"},
                                             {"23-night-bounced.png", "24-night-bounced-reference.png"}};
                    const size_t n=size_t(&part-parts);
                    save(denoised, names[n][0], true); save(avg, names[n][1], true);
                }
                spatialSamples=4;
            }
            if(k.night && !k.moving) { save(px, k.jitter ? "19-night-full-jittered.png" : "19-night-full.png", true); save(reference, k.jitter ? "20-night-full-jittered-reference.png" : "20-night-full-reference.png", true); }
            auto error=[&](UINT x0, UINT y0, UINT x1, UINT y1) {
                double e=0, level=0;
                for(UINT y=y0;y<y1;++y) for(UINT x=x0;x<x1;++x) {
                    const size_t j=(size_t(y)*W+x)*3;
                    const double l=0.2126*px[j]+0.7152*px[j+1]+0.0722*px[j+2], lr=0.2126*reference[j]+0.7152*reference[j+1]+0.0722*reference[j+2];
                    e+=std::fabs(l-lr); level+=lr;
                }
                return level>0 ? e/level : 0.0;
            };
            // The floor in front of the back wall: the box's and the leaf's shadows (read-back rows are bottom-up).
            const double errorCeiling=error(300, 600, 980, 710), errorWhole=error(2, 2, W-2, H-2), errorFloor=error(150, 150, 1000, 300);
            std::printf("      noise (relative) %-36s error ceiling %.4f, floor %.4f, whole %.4f | grain ceiling %.5f, whole %.5f | flicker %.5f\n", k.name,
                errorCeiling, errorFloor, errorWhole, ceiling, whole, flickerFrames ? flicker/flickerFrames : 0.0);
            if(k.floorLimit>0) expect(errorFloor<k.floorLimit, "the denoiser keeps the sun's sharp shadows (the box's, the leaf's checkers)");
            {
                Measured m; m.whole=errorWhole; m.floor=errorFloor; m.ceiling=errorCeiling; m.flicker=flickerFrames ? flicker/flickerFrames : 0.0;
                for(float v:px) if(!std::isfinite(v)) { m.finite=false; break; }
                m.nrdActive=nrdRan; m.gpuMs=caseGpuMs;
                measured.emplace_back(k.name, m);
            }
            if(!k.jitter && !k.night && !k.giCap) still[k.cap==1 ? 0 : 1]=ceiling;
            if(k.night && k.moving && k.cap==1) save(px, k.bumps ? "14-bumpy-night-full-moving.png" : "13-night-full-moving.png", true);
        }
        history=20; giHistory=0; bumpy=false;
        // NRD against the built-in denoiser on the same scenes and cameras: it ran, its output is finite, and its error
        // against the undenoised reference and its flicker are no worse than the built-in denoiser's by more than half.
        {
            auto find=[&](const char* name) -> const Measured* { for(auto& m:measured) if(m.first==name) return &m.second; return nullptr; };
            const std::pair<const char*, const char*> pairs[]={{"NRD: defaults (20/8), still", "defaults (20/8), still"},
                {"NRD: defaults (20/8), moving", "defaults (20/8), moving"}, {"NRD: night, defaults (20/8), moving", "night, defaults (20/8), moving"},
                {"NRD: bumpy night, history 20, moving", "bumpy night, history 20, moving"}};
            for(const auto& pr:pairs) {
                const Measured* a=find(pr.first); const Measured* b=find(pr.second);
                if(!a || !b) continue;
                std::printf("      %s | NRD against built-in: error whole %.4f / %.4f, floor %.4f / %.4f, ceiling %.4f / %.4f | flicker %.5f / %.5f | GPU %.2f / %.2f ms\n", pr.first,
                    a->whole, b->whole, a->floor, b->floor, a->ceiling, b->ceiling, a->flicker, b->flicker, a->gpuMs, b->gpuMs);
                expect(a->nrdActive && a->finite, "NRD denoised the frames and its output is finite");
                expect(a->whole<=b->whole*1.5, "NRD's error against the undenoised reference is within half again of the built-in denoiser's");
                expect(a->flicker<=b->flicker*1.5+1e-4, "NRD flickers no more than half again as much as the built-in denoiser");
                expect(a->ceiling<=b->ceiling*1.25, "NRD's error in the bounced light alone (under the ceiling) is within a quarter of the built-in denoiser's");
            }
        }
        // Energy (experiment, TGPT_ENERGY set): the bounced light against an honest reference, the plain path tracer with
        // nothing that changes what it estimates (no ReSTIR reuse, no boiling filter, no radiance clamp, no floor on a
        // light's distance), 8 rays a pixel over 256 frames, for the torches and for one light 10 cm from the back wall;
        // then ReSTIR as it ships, and with each of its estimator's changes undone in turn. Each figure has its standard
        // error from eight batches of frames. (The first version compared with "no reuse" that still ran the boiling
        // filter and the clamps, which are no reference.)
        if(std::getenv("TGPT_ENERGY")) {
            sunOn=false; skyIntensity=1e-6f; // 0 would mean the default, 1
            const uint32_t keepRays=rays;
            auto measure=[&](uint32_t options, uint32_t raysPerPixel, uint32_t frames, double& standardError) {
                rays=raysPerPixel;
                const uint32_t batches=8, per=frames/batches;
                double sum=0, sum2=0;
                for(uint32_t i=0;i<16;++i) run(TGPT_VIEW_INDIRECT, cam, cam, i ? index++ : 0, options);
                for(uint32_t batch=0;batch<batches;++batch) {
                    double mean=0;
                    for(uint32_t i=0;i<per;++i) {
                        run(TGPT_VIEW_INDIRECT, cam, cam, index++, options);
                        const auto r=readback(); double s=0;
                        for(size_t j=0;j<r.size();j+=3) s+=0.2126*r[j]+0.7152*r[j+1]+0.0722*r[j+2];
                        mean+=s/(double(W)*H);
                    }
                    mean/=per; sum+=mean; sum2+=mean*mean;
                }
                rays=keepRays;
                const double mean=sum/batches;
                standardError=std::sqrt(std::max(0.0, sum2/batches-mean*mean)/(batches-1));
                return mean;
            };
            const uint32_t plain=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL;
            const uint32_t honest=plain|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS;
            const uint32_t open=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS|TGPT_OPTION_NO_BOILING|TGPT_OPTION_EXACT_JACOBIAN;
            struct Variant { const char* name; uint32_t options; };
            const Variant variants[]={
                {"ReSTIR as it ships", TGPT_OPTION_NO_DENOISER},
                {"no reuse, with the clamps and the boiling filter", plain},
                {"ReSTIR without the clamps", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS},
                {"  and without the boiling filter", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS|TGPT_OPTION_NO_BOILING},
                {"  and with the exact Jacobian", open},
                {"  and RTXDI's surface where rays start", open|TGPT_OPTION_OFFSET_RECEIVER},
                {"  temporal reuse alone (exact Jacobian)", open|TGPT_OPTION_NO_SPATIAL},
                {"  spatial reuse alone (exact Jacobian)", open|TGPT_OPTION_NO_TEMPORAL}};
            TGPT_Light wall{}; wall.position[0]=0; wall.position[1]=2.f; wall.position[2]=5.9f; wall.type=0;
            wall.color[0]=12; wall.color[1]=6.5f; wall.color[2]=2.4f; wall.range=12; wall.radius=0.04f; wall.id=300;
            for(int scene=0;scene<2;++scene) {
                const char* sceneName=scene ? "one light 10 cm from the wall" : "24 torches";
                expect(scene ? SetLights(&wall, 1)==0 : SetLights(torches.data(), uint32_t(torches.size()))==0, "lights set");
                double referenceError=0; const double reference=measure(honest, 8, 256, referenceError);
                std::printf("      ENERGY %s, bounced light: honest reference %.6f (standard error %.2f%%)\n", sceneName, reference, 100.0*referenceError/reference);
                for(const auto& v:variants) {
                    double error=0; const double m=measure(v.options, 1, 512, error);
                    const double ratio=m/reference, ratioError=ratio*std::sqrt((error/m)*(error/m)+(referenceError/reference)*(referenceError/reference));
                    std::printf("      ENERGY %s: %-50s %.3f of the reference (+-%.3f)\n", sceneName, v.name, ratio, ratioError);
                }
            }
            expect(SetLights(torches.data(), uint32_t(torches.size()))==0, "torches set"); skyIntensity=1;
        }
        // Lamp shadows follow the light's size: the checkered leaf's shadow on the floor from the light above it,
        // alone and without the sun, converged without the denoiser, is crisper from a 2 cm light than a 10 cm one.
        // (A 10 cm minimum light size made both the same.)
        {
            sunOn=false;
            auto sharpness=[&](float radius, uint32_t extra=0) {
                TGPT_Light one=torches.back(); one.radius=radius;
                expect(SetLights(&one, 1)==0, "the light above the leaf alone");
                const uint32_t keep=rays; rays=8;
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_DIRECT, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER|extra);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                rays=keep;
                std::vector<float> avg(acc.size()); for(size_t j=0;j<acc.size();++j) avg[j]=float(acc[j]/32.0);
                if(!extra) save(avg, radius<0.05f ? "27-lamp-2cm.png" : "28-lamp-10cm.png", true);
                // The box's shadow, where its far edge crosses the floor away from the box (read-back rows are
                // bottom-up): on each row, the pixels between 20% and 80% of the lit floor beside it, the penumbra.
                auto lum=[&](UINT x, UINT y) { const size_t j=(size_t(y)*W+x)*3; return 0.2126*acc[j]+0.7152*acc[j+1]+0.0722*acc[j+2]; };
                double width=0; int rows=0;
                for(UINT y=119;y<160;++y) {
                    double lit=0; for(UINT x=850;x<1010;++x) lit=std::max(lit, lum(x, y));
                    if(lit<=0) continue;
                    int inside=0; for(UINT x=850;x<1010;++x) { const double f=lum(x, y)/lit; if(f>0.2 && f<0.8) ++inside; }
                    width+=inside; ++rows;
                }
                return rows ? width/rows : 0.0;
            };
            const double tiny=sharpness(0.02f), wide=sharpness(0.1f);
            std::printf("      lamp shadow penumbra on the floor (pixels across): 2 cm light %.2f, 10 cm light %.2f\n", tiny, wide);
            expect(tiny>0 && wide>tiny*1.5, "a small lamp casts a crisper shadow than a large one");
            // Direct light's visibility reuse (an option, off by default): the trade-off it makes, for the record.
            const double tinyVisible=sharpness(0.02f, TGPT_OPTION_DI_VISIBILITY);
            std::printf("      with direct light's visibility reuse (option): 2 cm light %.2f pixels\n", tinyVisible);
        }
        if(!sunOn) { sunOn=true; expect(SetLights(&light, 1)==0, "one light again"); }
        // A light's cookie masks its light by direction, in the light's own space (x right, y up, z forward): a cubemap
        // bright on the light's +x half and dark on its -x half, on a light facing +z with +y up, leaves the room's left
        // half (world -x, as the camera sees it) far darker than without it, and its right half as it was.
        {
            ComPtr<ID3D11Texture2D> cookieTex;
            {
                const int size=16; std::vector<uint8_t> faces[6];
                for(int f=0;f<6;++f) {
                    faces[f].resize(size_t(size)*size*4);
                    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
                        const float u=(x+0.5f)/size*2-1, v=(y+0.5f)/size*2-1;
                        V3 d;
                        switch(f) { case 0: d={1, -v, -u}; break; case 1: d={-1, -v, u}; break; case 2: d={u, 1, v}; break; case 3: d={u, -1, -v}; break; case 4: d={u, -v, 1}; break; default: d={-u, -v, -1}; break; }
                        const uint8_t c=d.x>0 ? 255 : 8;
                        for(int k=0;k<3;++k) faces[f][(size_t(y)*size+x)*4+k]=c;
                        faces[f][(size_t(y)*size+x)*4+3]=255;
                    }
                }
                D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=6; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1;
                d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
                D3D11_SUBRESOURCE_DATA init[6]; for(int f=0;f<6;++f) init[f]={faces[f].data(), UINT(size*4), 0};
                device->CreateTexture2D(&d, init, &cookieTex);
            }
            const int cookie=RegisterCookie ? RegisterCookie(77, uint64_t(uintptr_t(cookieTex.Get()))) : -100;
            expect(cookie>=0, "a light's cookie registered");
            sunOn=false;
            auto halves=[&](uint32_t flags) {
                TGPT_Light c=light; c.position[0]=0; c.position[1]=2.5f; c.position[2]=-1; c.range=14; c.radius=0.05f;
                c.direction[0]=0; c.direction[1]=0; c.direction[2]=1; c.up[0]=0; c.up[1]=1; c.up[2]=0; c.flags=flags;
                expect(SetLights(&c, 1)==0, "the cookie's light alone");
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<24;++i) {
                    run(TGPT_VIEW_DIRECT, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                double left=0, right=0;
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const size_t j=(size_t(y)*W+x)*3; const double l=0.2126*acc[j]+0.7152*acc[j+1]+0.0722*acc[j+2];
                    (x<W/2 ? left : right)+=l;
                }
                return std::make_pair(left, right);
            };
            const auto plain=halves(0), masked=halves(cookie>=0 ? uint32_t(cookie)+1 : 0u);
            std::printf("      light with a half-dark cookie: left half %.3f, right half %.3f of the image without it\n",
                plain.first>0 ? masked.first/plain.first : 0.0, plain.second>0 ? masked.second/plain.second : 0.0);
            expect(plain.first>0 && plain.second>0 && masked.first<0.5*plain.first && masked.second>0.7*plain.second,
                   "a light's cookie masks its light by direction, in the light's own space");
            sunOn=true; expect(SetLights(&light, 1)==0, "one light again");
        }
        expect(still[1]<=still[0]*1.25+1e-4, "a longer ReSTIR history leaves no more grain than history 1");
        // What the quality settings change, measured on the room in the sun (in game DLSS Ray Reconstruction removes
        // most of the noise they trade against, so they show little there):
        //   Bounces: light under the ceiling, converged, 1 bounce against 4;
        //   Ray Distance: the same light with rays reaching 2 m instead of 500 m (misses count as sky);
        //   Rays Per Pixel and Spatial Samples: a single frame's error against the converged frame, undenoised.
        {
            uint32_t noCache=0;
            auto average=[&](uint32_t frames) {
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<frames+8;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER|noCache);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                std::vector<float> out(acc.size()); for(size_t j=0;j<acc.size();++j) out[j]=float(acc[j]/frames);
                return out;
            };
            // One frame's error, averaged over four frames after the twelfth: a single frame's moved by a tenth from one run's
            // random numbers to another's, as much as the settings compared change it (eight rays a pixel left 0.77 of one
            // ray's error in one run and 0.84 in the next; over four frames 0.85 without the radiance cache, 0.84 with it).
            // And how much of a frame's error the next frame keeps, in blotches (the error's mean over 8x8 pixel blocks, and the
            // correlation of two frames' block errors): boiling, what reuse carries from frame to frame, which the denoisers can't
            // tell from light. Pixel by pixel reuse keeps almost none (its permutation sampling decorrelates single pixels).
            double carried=0;
            auto frameError=[&](const std::vector<float>& reference) {
                double e=0, level=0, cross=0, squares=0, previousSquares=0;
                const UINT bw=W/8, bh=H/8;
                std::vector<double> previous;
                for(uint32_t i=0;i<16;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER|noCache);
                    if(i<11) continue;
                    const auto px=readback();
                    std::vector<double> blocks(size_t(bw)*bh, 0.0);
                    for(UINT y=2;y<H-2;++y) for(UINT x=2;x<W-2;++x) {
                        const size_t j=(size_t(y)*W+x)*3;
                        const double error=(0.2126*px[j]+0.7152*px[j+1]+0.0722*px[j+2])-(0.2126*reference[j]+0.7152*reference[j+1]+0.0722*reference[j+2]);
                        if(x/8<bw && y/8<bh) blocks[size_t(y/8)*bw+x/8]+=error/64.0;
                        if(i<12) continue;
                        e+=std::fabs(error);
                        level+=0.2126*reference[j]+0.7152*reference[j+1]+0.0722*reference[j+2];
                    }
                    if(i>=12) for(size_t k=0;k<blocks.size();++k) { cross+=blocks[k]*previous[k]; squares+=blocks[k]*blocks[k]; previousSquares+=previous[k]*previous[k]; }
                    previous=std::move(blocks);
                }
                carried=squares>0 && previousSquares>0 ? cross/std::sqrt(squares*previousSquares) : 0.0;
                return level>0 ? e/level : 0.0;
            };
            const uint32_t keepRays=rays; rays=4;
            noCache=TGPT_OPTION_NO_RADIANCE_CACHE;
            bounces=1; const double oneBounce=region(average(24), 500, 650, 780, 700);
            bounces=4; const double fourBounces=region(average(24), 500, 650, 780, 700);
            noCache=0;
            bounces=1; const double oneCached=region(average(24), 500, 650, 780, 700);
            bounces=4; const double fourCached=region(average(24), 500, 650, 780, 700);
            bounces=2; rayDistance=2; const double shortRays=region(average(24), 500, 650, 780, 700);
            rayDistance=500; const double longRays=region(average(24), 500, 650, 780, 700);
            rays=8; const auto reference=average(32);
            rays=1; const double oneRay=frameError(reference); const double oneRayCarried=carried;
            rays=8; const double eightRays=frameError(reference); const double eightRaysCarried=carried;
            rays=16; const double sixteenRays=frameError(reference); const double sixteenRaysCarried=carried;
            rays=1; spatialSamples=0; const double noSpatial=frameError(reference);
            spatialSamples=8; const double eightSpatial=frameError(reference);
            spatialSamples=4; rays=keepRays;
            std::printf("      settings: under the ceiling, 1 bounce %.4f, 4 bounces %.4f; rays to 2 m %.4f, to 500 m %.4f\n", oneBounce, fourBounces, shortRays, longRays);
            std::printf("      radiance cache: under the ceiling, 1 bounce %.4f, 4 bounces %.4f (without it %.4f, %.4f)\n", oneCached, fourCached, oneBounce, fourBounces);
            std::printf("      settings: one frame's error, 1 ray %.4f, 8 rays %.4f, 16 rays %.4f; no spatial reuse %.4f, 8 spatial samples %.4f\n", oneRay, eightRays, sixteenRays, noSpatial, eightSpatial);
            std::printf("      settings: the share of a frame's error the next frame keeps, 1 ray %.3f, 8 rays %.3f, 16 rays %.3f\n", oneRayCarried, eightRaysCarried, sixteenRaysCarried);
            {
                bounces=1;
                auto level=[&]() { return region(readback(), 500, 650, 780, 700); };
                for(uint32_t i=0;i<48;++i) run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                double before=0; for(uint32_t i=0;i<8;++i) { run(TGPT_VIEW_NONE, cam, cam, index++, TGPT_OPTION_NO_DENOISER); before+=level()/8; }
                cut=true; run(TGPT_VIEW_NONE, cam, cam, index++, TGPT_OPTION_NO_DENOISER); cut=false;
                run(TGPT_VIEW_NONE, cam, cam, index++, TGPT_OPTION_NO_DENOISER); const double afterCut=level();
                run(TGPT_VIEW_NONE, cam, cam, 0, TGPT_OPTION_NO_DENOISER);
                run(TGPT_VIEW_NONE, cam, cam, index++, TGPT_OPTION_NO_DENOISER); const double afterStart=level();
                bounces=2;
                std::printf("      radiance cache across a camera cut: under the ceiling %.4f before, %.4f the frame after the cut, %.4f the frame after a new start\n",
                    before, afterCut, afterStart);
                expect(afterCut>before*0.9, "a camera cut keeps the radiance cache's light");
                expect(afterStart<before*0.85, "a new start empties the radiance cache");
            }
            expect(fourBounces>oneBounce*1.02, "more bounces brighten the light under the ceiling");
            expect(oneCached-oneBounce>0.6*(fourBounces-oneBounce), "one bounce ending on the radiance cache comes most of the way to four without it");
            expect(fourCached>=fourBounces*0.98 && fourCached<fourBounces*1.1, "four bounces ending on the radiance cache add only what lies past them");
            expect(std::fabs(shortRays-longRays)>0.02*longRays, "Ray Distance changes the light under the ceiling");
            expect(eightRays<oneRay*0.9, "more rays per pixel leave less noise");
            expect(sixteenRays<eightRays, "sixteen rays per pixel leave less noise than eight");
            // Boiling: more rays each count on their own, and less of a frame's error lives on in the next.
            expect(eightRaysCarried<oneRayCarried*0.85 && sixteenRaysCarried<eightRaysCarried, "more rays per pixel carry less of a frame's error into the next");
            expect(eightSpatial<noSpatial, "more spatial samples leave less noise");
        }
        // Rays that start inside the traced mesh: a traced-only slab, 10 cm thick, around part of the floor, as when
        // the drawn surface lies a few centimetres inside the traced one (an arch drawn smoother than its triangles,
        // a coarser traced mesh). The floor over it stays lit instead of turning black.
        {
            auto withSlab=instances; auto slabIndices=indices;
            TGPT_Instance slab{}; const M4 w=translate_scale({-3.f, 0.02f, 4.2f}, {3.f, 0.1f, 2.f});
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) slab.transform[r*4+c]=w.m[r][c];
            slab.mesh=cubeHandle; slab.materialBase=uint32_t(slabIndices.size()); slab.mask=0xFF;
            slabIndices.push_back(0); slabIndices.push_back(0);
            withSlab.push_back(slab);
            auto converged=[&]() {
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<32;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                std::vector<float> out(acc.size()); for(size_t j=0;j<acc.size();++j) out[j]=float(acc[j]/24.0);
                return out;
            };
            const auto plain=converged();
            expect(SetScene(withSlab.data(), uint32_t(withSlab.size()), slabIndices.data(), uint32_t(slabIndices.size()))==0, "scene with a slab around the floor");
            const auto slabbed=converged();
            save(slabbed, "29-floor-inside-traced-slab.png", true);
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the slab");
            int black=0, lit=0;
            for(size_t j=0;j+2<plain.size();j+=3) {
                const double lp=0.2126*plain[j]+0.7152*plain[j+1]+0.0722*plain[j+2], ls=0.2126*slabbed[j]+0.7152*slabbed[j+1]+0.0722*slabbed[j+2];
                if(lp>0.01) { ++lit; if(ls<0.2*lp) ++black; }
            }
            std::printf("      floor inside a traced-only slab: %d of %d lit pixels turned black\n", black, lit);
            expect(lit>0 && black<lit/500, "rays starting inside the traced mesh pass out through it");
            // The same slab in an object with an alpha-tested material elsewhere (a character's hair, a building's ivy):
            // its opaque faces still let such rays out; only alpha-tested ones (cards seen from either side) stop them.
            {
                auto mixed=instances; auto mixedIndices=indices;
                TGPT_Instance part=slab; part.materialBase=uint32_t(mixedIndices.size()); part.flags=TGPT_INSTANCE_ALPHA_TEST;
                mixedIndices.push_back(7); mixedIndices.push_back(0); // sides: alpha-tested, no ray stops at them; top: opaque
                mixed.push_back(part);
                expect(SetScene(mixed.data(), uint32_t(mixed.size()), mixedIndices.data(), uint32_t(mixedIndices.size()))==0, "scene with a slab in an object with an alpha-tested material");
                const auto px=converged();
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without that slab");
                int blackMixed=0;
                for(size_t j=0;j+2<plain.size();j+=3) {
                    const double lp=0.2126*plain[j]+0.7152*plain[j+1]+0.0722*plain[j+2], lm=0.2126*px[j]+0.7152*px[j+1]+0.0722*px[j+2];
                    if(lp>0.01 && lm<0.2*lp) ++blackMixed;
                }
                std::printf("      floor inside a slab whose object also has an alpha-tested material: %d of %d lit pixels turned black\n", blackMixed, lit);
                expect(blackMixed<lit/500, "rays starting inside an object with alpha-tested parts pass out through its opaque faces");
            }
            // A stand-in (the game's shadow-only meshes: coarser copies of what it draws) close over the drawn surface,
            // facing it: rays from the floor pass through it there. As an ordinary mesh the same quad shadows the floor.
            auto withQuad=[&](uint32_t flags) {
                auto list=instances; auto quadIndices=indices;
                TGPT_Instance q{}; const M4 qw=mul(translate_scale({-3.f, 0.12f, 4.2f}, {3.f, 1.f, 2.f}), rot_x(pi));
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=qw.m[r][c];
                q.mesh=quadHandle; q.materialBase=uint32_t(quadIndices.size()); q.mask=0xFF; q.flags=flags;
                quadIndices.push_back(0); list.push_back(q);
                expect(SetScene(list.data(), uint32_t(list.size()), quadIndices.data(), uint32_t(quadIndices.size()))==0, "scene with a quad over the floor");
                const auto px=converged();
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the quad");
                int dark=0;
                for(size_t j=0;j+2<plain.size();j+=3) {
                    const double lp=0.2126*plain[j]+0.7152*plain[j+1]+0.0722*plain[j+2], lq=0.2126*px[j]+0.7152*px[j+1]+0.0722*px[j+2];
                    if(lp>0.01 && lq<0.5*lp) ++dark;
                }
                return dark;
            };
            const int standIn=withQuad(TGPT_INSTANCE_STAND_IN), ordinary=withQuad(0);
            std::printf("      floor under a quad 12 cm above it, facing it: %d pixels darkened as a stand-in, %d as an ordinary mesh\n", standIn, ordinary);
            expect(ordinary>1000 && standIn<ordinary/50, "rays from a drawn surface pass through a stand-in close by, whichever way it faces");
            // What the wind moves (TGPT_MATERIAL_SWAYS: drawn grass and cloth sway where the traced stand still) lets the rays from
            // a drawn surface through within 20 cm of it, and stops them beyond: a quad of it 12 cm over the floor darkens nothing,
            // one 60 cm up shadows the floor as much as the same quad of an ordinary material. (Close to the drawn grass, rays
            // met its still copy: black spots on the ground swaying with the grass, laundry black on its inner side.)
            {
                auto swaying=materials[0]; swaying.flags|=TGPT_MATERIAL_SWAYS;
                const uint32_t swayIndex=uint32_t(materials.size());
                expect(SetMaterials(swayIndex, &swaying, 1)==0, "a material the wind moves");
                auto withSwaying=[&](float height, uint32_t material) {
                    auto list=instances; auto quadIndices=indices;
                    TGPT_Instance q{}; const M4 qw=mul(translate_scale({-3.f, height, 4.2f}, {3.f, 1.f, 2.f}), rot_x(pi));
                    for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=qw.m[r][c];
                    q.mesh=quadHandle; q.materialBase=uint32_t(quadIndices.size()); q.mask=0xFF;
                    quadIndices.push_back(material); list.push_back(q);
                    expect(SetScene(list.data(), uint32_t(list.size()), quadIndices.data(), uint32_t(quadIndices.size()))==0, "scene with a quad over the floor");
                    const auto px=converged();
                    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the quad");
                    int dark=0;
                    for(size_t j=0;j+2<plain.size();j+=3) {
                        const double lp=0.2126*plain[j]+0.7152*plain[j+1]+0.0722*plain[j+2], lq=0.2126*px[j]+0.7152*px[j+1]+0.0722*px[j+2];
                        if(lp>0.01 && lq<0.5*lp) ++dark;
                    }
                    return dark;
                };
                const int nearSway=withSwaying(0.12f, swayIndex), farSway=withSwaying(0.6f, swayIndex), farStill=withSwaying(0.6f, 0);
                std::printf("      floor under a quad the wind moves: %d pixels darkened with it 12 cm above (%d as an ordinary mesh), %d with it 60 cm up (%d ordinary)\n",
                            nearSway, ordinary, farSway, farStill);
                expect(nearSway<ordinary/50, "rays from a drawn surface pass through what the wind moves close by");
                expect(farStill>1000 && farSway>farStill*8/10, "what the wind moves stops rays farther from the drawn surface");
            }
        }
        // Translucent surfaces (HDRP's transmission: grass blades, leaves) are lit through from behind: a drawn quad
        // facing the camera with the sun behind it gets direct light as a translucent surface, none as an opaque one.
        // High enough that the back wall leaves it in the sun (lower, the wall shaded it and the comparison measured
        // noise), and every variant traced with the same frame indices, so only the material differs.
        {
            const uint32_t first=index;
            auto directWith=[&](uint32_t feature) {
                objects.push_back(Object{0, mul(translate_scale({-3.5f, 4.3f, 4.5f}, {1.5f, 1, 1.5f}), rot_x(-pi/2)), {0}, false, false, feature});
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                index=first;
                for(uint32_t i=0;i<32;++i) {
                    run(TGPT_VIEW_DIRECT, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                objects.pop_back();
                return acc;
            };
            const auto opaque=directWith(0), translucent=directWith(2);
            // The same quad with its diffusion profile marked thick (HDRP's Thick Object transmission: skin), whichever
            // profile its GBuffer holds: lit as a solid surface.
            thickProfiles=0xFFFFu; const auto thick=directWith(2); thickProfiles=0;
            int through=0, thickThrough=0;
            auto lum=[](const std::vector<double>& v, size_t j) { return (0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2])/24.0; };
            for(size_t j=0;j+2<opaque.size();j+=3) {
                if(lum(translucent, j)>lum(opaque, j)+0.02) ++through;
                if(lum(thick, j)>lum(opaque, j)+0.02) ++thickThrough;
            }
            std::printf("      a quad lit from behind by the sun: %d pixels lit through as a translucent surface, %d with a thick profile\n", through, thickThrough);
            expect(through>1000, "a translucent surface (grass, leaves) is lit through from behind");
            expect(thickThrough<through/50, "a surface whose diffusion profile is thick (skin) is not lit through from behind");
        }
        // Thin cloth (TGPT_MATERIAL_THIN_CLOTH: a banner, laundry, canvas) is lit through from behind as well, dimmer than a leaf
        // (0.3 against 0.5): the same quad drawn as an ordinary surface (the GBuffer says nothing of cloth) and traced with a
        // thin cloth material, which the camera ray finds; traced with an ordinary material, nothing comes through.
        {
            const uint32_t first=index;
            auto clothMaterial=materials[0]; clothMaterial.flags|=TGPT_MATERIAL_THIN_CLOTH;
            const uint32_t clothIndex=uint32_t(materials.size());
            expect(SetMaterials(clothIndex, &clothMaterial, 1)==0, "a thin cloth material");
            const M4 w=mul(translate_scale({-3.5f, 4.3f, 4.5f}, {1.5f, 1, 1.5f}), rot_x(-pi/2));
            auto directWith=[&](uint32_t feature, uint32_t material) {
                objects.push_back(Object{0, w, {0}, false, false, feature});
                auto list=instances; auto listIndices=indices;
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                listIndices.push_back(material); list.push_back(q);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with the drawn quad traced");
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                index=first;
                for(uint32_t i=0;i<32;++i) {
                    run(TGPT_VIEW_DIRECT, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                objects.pop_back();
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return acc;
            };
            const auto opaque=directWith(0, 0), cloth=directWith(0, clothIndex), leaf=directWith(2, 0);
            auto lum=[](const std::vector<double>& v, size_t j) { return (0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2])/24.0; };
            double throughCloth=0, throughLeaf=0; int lit=0;
            for(size_t j=0;j+2<opaque.size();j+=3) {
                if(lum(leaf, j)<=lum(opaque, j)+0.02) continue;
                throughLeaf+=lum(leaf, j)-lum(opaque, j); throughCloth+=lum(cloth, j)-lum(opaque, j); ++lit;
            }
            std::printf("      the quad lit from behind by the sun (%d pixels): the light through thin cloth %.2f of a leaf's (0.6 expected)\n",
                        lit, throughLeaf>0 ? throughCloth/throughLeaf : 0.0);
            expect(lit>1000 && throughLeaf>0 && throughCloth/throughLeaf>0.45 && throughCloth/throughLeaf<0.75,
                   "thin cloth is lit through from behind, dimmer than a leaf");
        }
        // Reflections on Glossy Surfaces (TGPT_OPTION_GLOSSY_REFLECTIONS): the room drawn rough (0.8), its reflections from the
        // radiance cache in place of traced rays come out about as bright as the traced ones; and a glossy room (0.3) is
        // traced either way.
        {
            const uint32_t first=index;
            const auto keepObjects=objects;
            auto specular=[&](float roughness, uint32_t options) {
                for(auto& o:objects) o.roughness=roughness;
                double sum=0; int n=0;
                index=first;
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_SPECULAR, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER|options);
                    if(i<16) continue;
                    const auto r=readback();
                    for(size_t j=0;j+2<r.size();j+=3) { sum+=0.2126*r[j]+0.7152*r[j+1]+0.0722*r[j+2]; ++n; }
                }
                objects=keepObjects;
                return n ? sum/n : 0.0;
            };
            const double roughTraced=specular(0.8f, 0), roughCached=specular(0.8f, TGPT_OPTION_GLOSSY_REFLECTIONS);
            const double glossyTraced=specular(0.3f, 0), glossyOption=specular(0.3f, TGPT_OPTION_GLOSSY_REFLECTIONS);
            std::printf("      reflections, the room rough: traced %.5f, from the radiance cache %.5f (%.2f); glossy: %.5f, with the option %.5f\n",
                        roughTraced, roughCached, roughTraced>0 ? roughCached/roughTraced : 0.0, glossyTraced, glossyOption);
            expect(roughTraced>0 && roughCached>0.5*roughTraced && roughCached<2.0*roughTraced,
                   "rough surfaces' reflections from the radiance cache are about as bright as traced ones");
            expect(glossyTraced>0 && std::fabs(glossyOption/glossyTraced-1.0)<0.02, "glossy surfaces trace their reflections with Glossy Surfaces too");
        }
        // And where a path meets one (TGPT_MATERIAL_TRANSLUCENT: the game's grass and leaves): a traced-only canopy in the sun
        // through the ceiling's opening, a metre over the floor, shades the floor under it; translucent, its underside is lit
        // through by the sun and the floor in its shade takes that light, which an opaque canopy of the same color doesn't give.
        // (Lit from its own side alone, every sunlit blade's underside was black to the rays that met it, and the ground under
        // grass took no light through the grass.) Undenoised, averaged, with the same frame indices for both.
        {
            const uint32_t first=index;
            auto canopy=materials[0]; canopy.flags|=TGPT_MATERIAL_TRANSLUCENT;
            const uint32_t canopyIndex=uint32_t(materials.size());
            expect(SetMaterials(canopyIndex, &canopy, 1)==0, "a translucent canopy's material");
            auto under=[&](int material) {
                auto list=instances; auto listIndices=indices;
                if(material>=0) {
                    TGPT_Instance q{}; const M4 w=translate_scale({-0.3f, 1.2f, 2.8f}, {2.4f, 1.f, 1.6f});
                    for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                    q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                    listIndices.push_back(uint32_t(material)); list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with a canopy over the floor");
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                index=first;
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]/32.0; }
                }
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return acc;
            };
            const auto open=under(-1), opaque=under(0), translucent=under(int(canopyIndex));
            auto lum=[](const std::vector<double>& v, size_t j) { return 0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2]; };
            // The floor in the canopy's shade: what the opaque canopy darkened to less than half.
            double shadeOpaque=0, shadeTranslucent=0; int shaded=0;
            for(size_t j=0;j+2<open.size();j+=3) {
                if(!(lum(open, j)>0.01) || lum(opaque, j)>0.5*lum(open, j)) continue;
                shadeOpaque+=lum(opaque, j); shadeTranslucent+=lum(translucent, j); ++shaded;
            }
            std::printf("      the floor in a sunlit canopy's shade (%d pixels): %.4f under an opaque one, %.4f under a translucent one (%.2f)\n",
                        shaded, shadeOpaque/std::max(shaded, 1), shadeTranslucent/std::max(shaded, 1), shadeOpaque>0 ? shadeTranslucent/shadeOpaque : 0.0);
            expect(shaded>1000, "a canopy in the sun shades the floor");
            expect(shadeOpaque>0 && shadeTranslucent>1.15*shadeOpaque, "the light through a translucent canopy reaches the floor in its shade");
        }
        // See-through glass (pt_specular's refraction): a solid cube of clear glass, drawn smooth (as the glass finish leaves
        // a bottle), shows what lies behind it, refracted and a little dimmed by its color and the light it reflects: the
        // image with it stays much closer to the room without it than with the same cube opaque.
        {
            const uint32_t first=index;
            // Drawn into the GBuffer (white, as smooth as the glass finish leaves glass, or as rough as the rest) and traced
            // (the clear glass material, or the white one).
            auto withCube=[&](int present, bool glass) {
                const M4 w=translate_scale({3.4f, 1.3f, 0.5f}, {1.4f, 1.4f, 1.4f});
                if(present) {
                    objects.push_back(Object{1, w, {0, 0}, false, false, 0, false, -1, glass ? 0.05f : 0.6f});
                    auto list=instances; auto listIndices=indices;
                    TGPT_Instance cube{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) cube.transform[r*4+c]=w.m[r][c];
                    cube.mesh=cubeHandle; cube.materialBase=uint32_t(listIndices.size()); cube.mask=0xFF; cube.flags=glass ? TGPT_INSTANCE_ALPHA_TEST : 0u;
                    listIndices.push_back(glass ? 10u : 0u); listIndices.push_back(glass ? 10u : 0u);
                    list.push_back(cube);
                    expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, glass ? "scene with a glass cube" : "scene with an opaque cube");
                }
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                index=first;
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0);
                    if(i>=16) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]/24.0; }
                }
                if(present) {
                    objects.pop_back();
                    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the cube");
                }
                return acc;
            };
            const auto none=withCube(0, false), opaque=withCube(1, false), glass=withCube(1, true);
            auto differs=[&](const std::vector<double>& a) { double d=0; for(size_t j=0;j<a.size();++j) d+=std::fabs(a[j]-none[j]); return d/double(a.size()); };
            const double dOpaque=differs(opaque), dGlass=differs(glass);
            auto toFloat=[](const std::vector<double>& v) { return std::vector<float>(v.begin(), v.end()); };
            save(toFloat(glass), "33-see-through-glass.png", true); save(toFloat(opaque), "34-opaque-cube.png", true);
            std::printf("      a solid cube of clear glass: the image differs from the room without it by %.5f, the same cube opaque by %.5f (%.2f)\n",
                dGlass, dOpaque, dGlass/std::max(dOpaque, 1e-12));
            expect(dOpaque>1e-4, "an opaque cube changes the image"); // it covers about 1 % of it
            expect(dGlass<dOpaque*0.5, "see-through glass shows what lies behind it");
            // Where the opaque cube stands, the glass one shows the room about as bright as it is without it (not black: seen
            // with its direct light alone, the back wall behind it, lit by bounced light, was).
            auto lum=[](const std::vector<double>& v, size_t j) { return 0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2]; };
            double behindNone=0, behindGlass=0; int covered=0;
            for(size_t j=0;j+2<none.size();j+=3)
                if(std::fabs(lum(opaque, j)-lum(none, j))>0.02) { behindNone+=lum(none, j); behindGlass+=lum(glass, j); ++covered; }
            const double seen=behindGlass/std::max(behindNone, 1e-12);
            std::printf("      where the opaque cube stands (%d pixels), the glass one shows %.2f of the room's light without it\n", covered, seen);
            expect(covered>500 && seen>0.6 && seen<1.4, "see-through glass shows the room behind it about as bright as it is");
        }
        // Coloured glass tints the light through it (transmittance, pt_common.hlsli): a traced-only cube of green glass
        // (0.15, 0.6, 0.15) floating over the sunlit floor lets through, of the direct light the same cube opaque stops
        // there, its color channel by channel (a closed object's two crossings), with the sun alone and with a lamp alone
        // above it (RTXDI's final shading); grey glass (0.36) as much in every channel, clear glass all of it. And at path
        // vertices: under a traced-only sheet of it over the ceiling's opening, the bounced light under the ceiling turns
        // green. Undenoised, unclamped, without the boiling filters, averaged over 32 frames with the same frame indices
        // for every variant, so only the material differs.
        {
            const uint32_t first=index;
            const uint32_t green=uint32_t(materials.size());
            TGPT_Material greenGlass=materials[9]; greenGlass.baseColor[0]=0.15f; greenGlass.baseColor[1]=0.6f; greenGlass.baseColor[2]=0.15f;
            expect(SetMaterials(green, &greenGlass, 1)==0, "green glass set");
            const uint32_t honest=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS|TGPT_OPTION_NO_BOILING;
            TGPT_Light lamp{}; lamp.position[0]=-0.3f; lamp.position[1]=3.2f; lamp.position[2]=2.f; lamp.type=0;
            lamp.color[0]=lamp.color[1]=lamp.color[2]=30.f; lamp.range=10; lamp.radius=0.05f; lamp.id=400;
            // material <0: none; 0 the opaque white one. sheet: the sheet over the ceiling's opening instead of the cube.
            auto averaged=[&](int material, bool sheet, bool lampOnly, uint32_t view) {
                auto list=instances; auto listIndices=indices;
                if(material>=0) {
                    TGPT_Instance t{};
                    const M4 w=sheet ? translate_scale({0.f, 5.9f, 4.f}, {12.f, 1.f, 4.f}) : translate_scale({-0.3f, 1.f, 2.f}, {0.8f, 0.8f, 0.8f});
                    for(int r=0;r<3;++r) for(int c=0;c<4;++c) t.transform[r*4+c]=w.m[r][c];
                    t.mesh=sheet ? quadHandle : cubeHandle; t.materialBase=uint32_t(listIndices.size()); t.mask=0xFF;
                    t.flags=material==0 ? 0u : TGPT_INSTANCE_ALPHA_TEST;
                    for(int s=0;s<(sheet ? 1 : 2);++s) listIndices.push_back(uint32_t(material));
                    list.push_back(t);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with glass over the floor");
                sunOn=!lampOnly;
                expect(lampOnly ? SetLights(&lamp, 1)==0 : SetLights(nullptr, 0)==0, lampOnly ? "the lamp above the cube alone" : "the sun alone");
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                index=first;
                for(uint32_t i=0;i<40;++i) {
                    run(view, cam, cam, i ? index++ : 0, honest);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]/32.0; }
                }
                return acc;
            };
            auto lum=[](const std::vector<double>& v, size_t j) { return 0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2]; };
            // Of the direct light the opaque cube stops (where it stops most of it), the part the glass one lets through.
            auto through=[&](const std::vector<double>& none, const std::vector<double>& opaque, const std::vector<double>& glass, int& pixels) {
                double blocked[3]{}, passed[3]{}; pixels=0;
                for(size_t j=0;j+2<none.size();j+=3) {
                    if(!(lum(none, j)>0.02) || lum(opaque, j)>0.3*lum(none, j)) continue;
                    ++pixels;
                    for(int c=0;c<3;++c) { blocked[c]+=none[j+c]-opaque[j+c]; passed[c]+=glass[j+c]-opaque[j+c]; }
                }
                return std::array<double, 3>{passed[0]/std::max(blocked[0], 1e-9), passed[1]/std::max(blocked[1], 1e-9), passed[2]/std::max(blocked[2], 1e-9)};
            };
            for(int lampOnly=0;lampOnly<2;++lampOnly) {
                const auto none=averaged(-1, false, lampOnly, TGPT_VIEW_DIRECT), opaque=averaged(0, false, lampOnly, TGPT_VIEW_DIRECT);
                const auto greenCube=averaged(int(green), false, lampOnly, TGPT_VIEW_DIRECT);
                const auto greyCube=averaged(9, false, lampOnly, TGPT_VIEW_DIRECT), clearCube=averaged(10, false, lampOnly, TGPT_VIEW_DIRECT);
                if(!lampOnly) { std::vector<float> f(greenCube.size()); for(size_t j=0;j<f.size();++j) f[j]=float(greenCube[j]); save(f, "35-green-glass-shadow.png", true); }
                int pixels=0;
                const auto tg=through(none, opaque, greenCube, pixels), ty=through(none, opaque, greyCube, pixels), tc=through(none, opaque, clearCube, pixels);
                const char* light=lampOnly ? "a lamp" : "the sun";
                std::printf("      glass cube's shadow from %s (%d pixels), of the light an opaque one stops: green %.3f %.3f %.3f (0.15 0.6 0.15), grey %.3f %.3f %.3f (0.36), clear %.3f %.3f %.3f\n",
                    light, pixels, tg[0], tg[1], tg[2], ty[0], ty[1], ty[2], tc[0], tc[1], tc[2]);
                expect(pixels>300, lampOnly ? "an opaque cube shadows the floor from the lamp above it" : "an opaque cube shadows the floor from the sun");
                expect(std::fabs(tg[1]-0.6)<0.08 && std::fabs(tg[0]-0.15)<0.06 && std::fabs(tg[2]-0.15)<0.06 && tg[0]/tg[1]<0.4,
                       lampOnly ? "green glass tints a lamp's light through it green, channel by channel" : "green glass tints the sun's light through it green, channel by channel");
                expect(std::fabs(ty[0]-0.36)<0.08 && std::fabs(ty[1]-0.36)<0.08 && std::fabs(ty[2]-0.36)<0.08, "grey glass lets through as much of every channel as its color");
                expect(tc[0]>0.93 && tc[1]>0.93 && tc[2]>0.93, "clear glass lets the light through");
            }
            // Path vertices: the bounced light under the ceiling (read-back rows are bottom-up), lit by the floor and walls the
            // sun reaches through the sheet (a single crossing: the square root of its color, twice the green against red),
            // without the sky, whose light comes in through the room's open front as well. The emissive panel's light (warm)
            // doesn't come through the sheet: 1.6 times measured, 1.0 with glass passed by chance as before.
            auto tint=[&](const std::vector<double>& v) {
                double r=0, gr=0;
                for(UINT y=650;y<700;++y) for(UINT x=500;x<780;++x) { const size_t j=(size_t(y)*W+x)*3; r+=v[j]; gr+=v[j+1]; }
                return gr/std::max(r, 1e-9);
            };
            skyIntensity=1e-6f; // 0 would mean the default, 1
            const double open=tint(averaged(-1, true, false, TGPT_VIEW_INDIRECT)), greenSheet=tint(averaged(int(green), true, false, TGPT_VIEW_INDIRECT));
            const double greySheet=tint(averaged(9, true, false, TGPT_VIEW_INDIRECT));
            skyIntensity=1;
            std::printf("      bounced light under the ceiling, green/red: open %.3f, under a sheet of green glass %.3f (%.2f times), of grey glass %.3f (%.2f times)\n",
                open, greenSheet, greenSheet/std::max(open, 1e-9), greySheet, greySheet/std::max(open, 1e-9));
            expect(greenSheet>open*1.35, "light through green glass reaches path vertices green (bounced light)");
            expect(std::fabs(greySheet/std::max(open, 1e-9)-1.0)<0.1, "grey glass leaves the bounced light's color");
            sunOn=true;
            expect(SetLights(&light, 1)==0, "one light again");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the glass");
        }
        // See-through glass reflects the lamps around it (pt_specular reflected_lamps): the see-through test's cube of clear
        // glass, and a lamp of 25 cm radius 2 m along the mirror direction from the cube's face towards the camera, the sun and
        // the sky off. Where a face pixel's mirror ray meets the lamp's sphere, the glass shows the lamp's radiance by the
        // Fresnel term (I / (pi r^2) for intensity I and radius r), worked out here for the pixels whose mirror ray passes
        // well inside the sphere, above the pixels whose ray passes well outside it (the Specular view, undenoised, with the
        // light the glass lets through much the same in both). Sampled from the lamp's disc, 2 % of it arrived.
        {
            const uint32_t first=index;
            const M4 w=translate_scale({3.4f, 1.3f, 0.5f}, {1.4f, 1.4f, 1.4f});
            objects.push_back(Object{1, w, {0, 0}, false, false, 0, false, -1, 0.05f});
            auto list=instances; auto listIndices=indices;
            TGPT_Instance glassCube{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) glassCube.transform[r*4+c]=w.m[r][c];
            glassCube.mesh=cubeHandle; glassCube.materialBase=uint32_t(listIndices.size()); glassCube.mask=0xFF; glassCube.flags=TGPT_INSTANCE_ALPHA_TEST;
            listIndices.push_back(10u); listIndices.push_back(10u);
            list.push_back(glassCube);
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with a glass cube");
            const V3 faceCentre{3.4f, 1.3f, -0.2f};
            const V3 d0=normalize(faceCentre-cam.position), r0{d0.x, d0.y, -d0.z};
            const V3 lampAt=faceCentre+r0*2.f; const float lampRadius=0.25f, lampI=40.f;
            TGPT_Light lamp{}; lamp.position[0]=lampAt.x; lamp.position[1]=lampAt.y; lamp.position[2]=lampAt.z; lamp.type=0;
            lamp.color[0]=lamp.color[1]=lamp.color[2]=lampI; lamp.range=20; lamp.radius=lampRadius; lamp.id=500;
            sunOn=false; skyIntensity=1e-6f;
            expect(SetLights(&lamp, 1)==0, "the lamp the glass reflects");
            std::vector<double> reflects(size_t(W)*H*3, 0.0);
            index=first;
            for(uint32_t i=0;i<40;++i) {
                run(TGPT_VIEW_SPECULAR, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                if(i>=8) { const auto r=readback(); for(size_t j=0;j<reflects.size();++j) reflects[j]+=r[j]/32.0; }
            }
            // Face pixels (kept clear of the box in front and of the face's edges), whose mirror ray passes within 0.7 of the
            // lamp's radius from its centre or between 1.5 and 2 radii from it; the expected highlight in the first,
            // pre-exposed as the Specular view shows it.
            std::vector<int> inside, ring;
            double expected=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                const float t=(-0.2f-cam.position.z)/d.z; const V3 f=cam.position+d*t;
                if(!(f.x>2.95f && f.x<4.05f && f.y>0.65f && f.y<1.95f)) continue;
                const V3 m{d.x, d.y, -d.z}, oc=f-lampAt;
                const float b=dot(oc, m), passes=dot(oc, oc)-b*b; // the squared distance of the ray's line from the centre
                if(b>=0.f) continue;
                const float nv=d.z; // the face's normal is -z, the view vector -d
                const float fresnel=0.04f+0.96f*std::pow(1.f-nv, 5.f);
                const float r2=lampRadius*lampRadius;
                if(passes<0.49f*r2) { inside.push_back(int(y*W+x)); expected+=fresnel*lampI/(3.14159265f*r2)*exposure; }
                else if(passes>2.25f*r2 && passes<4.f*r2) ring.push_back(int(y*W+x));
            }
            if(!inside.empty()) expected/=double(inside.size());
            double highlight=0, around=0;
            for(int i:inside) highlight+=0.2126*reflects[size_t(i)*3]+0.7152*reflects[size_t(i)*3+1]+0.0722*reflects[size_t(i)*3+2];
            for(int i:ring) around+=0.2126*reflects[size_t(i)*3]+0.7152*reflects[size_t(i)*3+1]+0.0722*reflects[size_t(i)*3+2];
            const double excess=(inside.empty() ? 0.0 : highlight/double(inside.size()))-(ring.empty() ? 0.0 : around/double(ring.size()));
            { std::vector<float> f(reflects.size()); for(size_t j=0;j<f.size();++j) f[j]=float(reflects[j]); save(f, "36-glass-reflects-a-lamp.png", true); }
            std::printf("      a lamp reflected in see-through glass (%zu pixels, %zu around them): %.3f above its surroundings, %.3f expected\n",
                inside.size(), ring.size(), excess, expected);
            expect(inside.size()>200 && ring.size()>200, "the lamp's mirror image lies on the glass cube's face");
            expect(excess>expected*0.85 && excess<expected*1.15, "see-through glass reflects the lamps around it");
            objects.pop_back();
            sunOn=true; skyIntensity=1;
            expect(SetLights(&light, 1)==0, "one light again");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the cube");
        }
        // See-through glass reflects the sun (pt_specular): a pane of clear glass, drawn smooth and traced, turned so that the
        // camera sees the sun's mirror image at its centre, the lamp and the sky off. Over the pane's pixels whose mirror ray
        // lies within 2 degrees of the sun, the reflected light times each pixel's solid angle adds up to the sun's illuminance
        // by the Fresnel term, as a mirror's image of the sun does (a flat mirror maps the camera's rays onto as many
        // directions as they span), undenoised and unclamped. From the direct light's sample of the sun, 2.5 % of it arrived.
        {
            const uint32_t first=index;
            const V3 paneAt{3.f, 2.6f, 2.5f}; const float half=0.6f;
            const V3 toCamera=normalize(cam.position-paneAt), n=normalize(sunDir+toCamera);
            const V3 t=normalize(cross(n, V3{1, 0, 0})), b=cross(t, n);
            M4 turn=identity(); // the quad's +y onto n, its x and z onto t and b
            const V3 axes[3]={t, n, b};
            for(int c=0;c<3;++c) { turn.m[0][c]=axes[c].x; turn.m[1][c]=axes[c].y; turn.m[2][c]=axes[c].z; }
            const M4 w=mul(translate_scale(paneAt, {1, 1, 1}), mul(turn, translate_scale({0, 0, 0}, {2*half, 1, 2*half})));
            objects.push_back(Object{0, w, {0}, false, false, 0, false, -1, 0.05f});
            auto list=instances; auto listIndices=indices;
            TGPT_Instance pane{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) pane.transform[r*4+c]=w.m[r][c];
            pane.mesh=quadHandle; pane.materialBase=uint32_t(listIndices.size()); pane.mask=0xFF; pane.flags=TGPT_INSTANCE_ALPHA_TEST;
            listIndices.push_back(10u);
            list.push_back(pane);
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with a pane of glass");
            expect(SetLights(nullptr, 0)==0, "the sun alone");
            skyIntensity=1e-6f;
            std::vector<double> reflects(size_t(W)*H*3, 0.0);
            index=first;
            for(uint32_t i=0;i<40;++i) {
                run(TGPT_VIEW_SPECULAR, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS);
                if(i>=8) { const auto r=readback(); for(size_t j=0;j<reflects.size();++j) reflects[j]+=r[j]/32.0; }
            }
            const float pixel=2.f*std::tan(30.f*3.14159265f/180.f)/float(H); // a pixel's side on the image plane 1 m out
            const V3 forward=normalize(V3{0, 2.2f, 2}-cam.position);
            double energy=0; int count=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                const float dn=dot(d, n);
                if(dn>=0.f) continue;
                const V3 q=cam.position+d*(dot(paneAt-cam.position, n)/dn)-paneAt;
                if(std::fabs(dot(q, t))>half*0.95f || std::fabs(dot(q, b))>half*0.95f) continue;
                if(dot(d-n*(2.f*dn), sunDir)<std::cos(2.f*3.14159265f/180.f)) continue;
                const size_t j=(size_t(y)*W+x)*3; const double c=dot(d, forward);
                energy+=(0.2126*reflects[j]+0.7152*reflects[j+1]+0.0722*reflects[j+2])*double(pixel)*double(pixel)*c*c*c; ++count;
            }
            const float fresnel=0.04f+0.96f*std::pow(1.f-dot(n, toCamera), 5.f);
            const double expected=double(fresnel)*double(sunLux)*double(exposure);
            { std::vector<float> f(reflects.size()); for(size_t j=0;j<f.size();++j) f[j]=float(reflects[j]); save(f, "37-glass-reflects-the-sun.png", true); }
            std::printf("      the sun reflected in see-through glass (%d pixels): reflected light times solid angle %.5f, %.5f expected (Fresnel %.3f)\n",
                count, energy, expected, fresnel);
            expect(count>20, "the sun's mirror image lies on the pane");
            expect(energy>expected*0.85 && energy<expected*1.15, "see-through glass reflects the sun");
            objects.pop_back();
            skyIntensity=1;
            expect(SetLights(&light, 1)==0, "one light again");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the pane");
        }
        // Light scattered under skin (TGPT_SetDiffusionProfiles, pt_sss.hlsl): the floor drawn as skin (transmission through
        // a thick profile) with a profile that scatters 3 cm (most of its light within about 7 pixels here, its filter radius
        // some 40). The sun's shadows on it (the box's, the leaf's checkers) come out softer, and it keeps the light it had.
        {
            const uint32_t first=index;
            const float scatter=0.03f;
            // HDRP's filter radius: where 99.7 % of the profile's energy lies (DiffusionProfile.SampleBurleyDiffusionProfile).
            const double u=0.003, gg=1.0+4.0*u*(2.0*u+std::sqrt(1.0+4.0*u*u)), nn=std::pow(gg, -1.0/3.0), pp=gg*nn*nn, cc=1.0+pp+nn;
            const float radius=float(3.0*std::log(cc/(4.0*u))*scatter);
            float table[64]{};
            for(int i=0;i<16;++i) { table[i*4]=table[i*4+1]=table[i*4+2]=1.f/scatter; table[i*4+3]=radius; }
            auto floorAs=[&](bool scatters) {
                objects[0].feature=2; thickProfiles=0xFFFFu;
                expect(SetDiffusionProfiles(table, scatters ? 16u : 0u)==0, scatters ? "a diffusion profile that scatters set" : "no scattering set");
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                index=first;
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0);
                    if(i>=16) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                objects[0].feature=0; thickProfiles=0; SetDiffusionProfiles(nullptr, 0);
                return acc;
            };
            const auto sharp=floorAs(false), soft=floorAs(true);
            auto mean=[&](const std::vector<double>& v) { std::vector<float> f(v.size()); for(size_t j=0;j<v.size();++j) f[j]=float(v[j]/24.0); return f; };
            save(mean(sharp), "31-skin-scattering-off.png", true); save(mean(soft), "32-skin-scattering-on.png", true);
            // The floor with the box's and the leaf's shadows (read-back rows are bottom-up), as the denoiser's floor error: how
            // sharp its edges are between 4x4 blocks (the shadows', not the grain of the scattering's own samples; squared, as
            // an edge spread wider keeps the sum of its steps but not of their squares), and its light.
            double edgesSharp=0, edgesSoft=0, sumSharp=0, sumSoft=0;
            auto lum=[](const std::vector<double>& v, size_t j) { return 0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2]; };
            auto block=[&](const std::vector<double>& v, UINT bx, UINT by) {
                double t=0; for(UINT y=0;y<4;++y) for(UINT x=0;x<4;++x) t+=lum(v, (size_t(150+by*4+y)*W+150+bx*4+x)*3); return t/16.0;
            };
            const UINT bw=(1000-150)/4, bh=(300-150)/4;
            for(UINT by=0;by<bh;++by) for(UINT bx=0;bx<bw;++bx) {
                const double a=block(sharp, bx, by), c=block(soft, bx, by);
                sumSharp+=a; sumSoft+=c;
                auto sq=[](double d) { return d*d; };
                if(bx+1<bw) { edgesSharp+=sq(block(sharp, bx+1, by)-a); edgesSoft+=sq(block(soft, bx+1, by)-c); }
                if(by+1<bh) { edgesSharp+=sq(block(sharp, bx, by+1)-a); edgesSoft+=sq(block(soft, bx, by+1)-c); }
            }
            const double edges=edgesSoft/std::max(edgesSharp, 1e-9), total=sumSoft/std::max(sumSharp, 1e-9);
            std::printf("      skin scattering 3 cm (filter radius %.1f cm): the shadow edges on the floor %.2f of the sharp ones, its light %.3f of the sharp floor's\n",
                radius*100.f, edges, total);
            expect(edges<0.8, "light scattered under skin softens the shadow edges on it");
            expect(std::fabs(total-1.0)<0.04, "light scattered under skin keeps the light's total");
        }
        // The radiance cache's energy (a furnace): a closed box whose walls all glow (0.5) and reflect half the light that
        // reaches them, nothing else lit. Its walls' radiance is 0.5 / (1 - 0.5) = 1: the glow and every bounce of it. Paths
        // of the Bounces setting's length alone see 1 + 0.5 + ... of the glow as far as they go (0.75 with one bounce, 0.97
        // with four); ending on the cache, one bounce comes to the whole of it, and so do two.
        {
            const uint32_t first=index;
            const bool keepSun=sunOn; const float keepSky=skyIntensity; const uint32_t keepBounces=bounces;
            sunOn=false; skyIntensity=1e-6f;
            expect(SetLights(nullptr, 0)==0, "no lights for the furnace");
            const auto keepObjects=objects;
            const float pi=3.14159265f;
            auto rx=[&](float a) { M4 r=identity(); r.m[1][1]=std::cos(a); r.m[1][2]=-std::sin(a); r.m[2][1]=std::sin(a); r.m[2][2]=std::cos(a); return r; };
            auto rz=[&](float a) { M4 r=identity(); r.m[0][0]=std::cos(a); r.m[0][1]=-std::sin(a); r.m[1][0]=std::sin(a); r.m[1][1]=std::cos(a); return r; };
            const M4 walls[6]={translate_scale({0, 0, 0}, {12, 1, 12}), mul(translate_scale({0, 6, 0}, {12, 1, 12}), rx(pi)),
                               mul(translate_scale({0, 3, 6}, {12, 6, 1}), rx(-pi/2)), mul(translate_scale({0, 3, -6}, {12, 6, 1}), rx(pi/2)),
                               mul(translate_scale({-6, 3, 0}, {1, 6, 12}), rz(-pi/2)), mul(translate_scale({6, 3, 0}, {1, 6, 12}), rz(pi/2))};
            objects.clear();
            std::vector<TGPT_Instance> box; std::vector<uint32_t> boxIndices;
            for(const M4& w:walls) {
                objects.push_back(Object{0, w, {11}, false, false, 0, false, 6});
                TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=w.m[r][c];
                in.mesh=quadHandle; in.materialBase=uint32_t(boxIndices.size()); in.mask=0xFF; boxIndices.push_back(11u);
                box.push_back(in);
            }
            expect(SetScene(box.data(), uint32_t(box.size()), boxIndices.data(), uint32_t(boxIndices.size()))==0, "the furnace set");
            const Camera inside=make_camera({0, 3, -4}, {0, 2.5f, 4}, 60.f, float(W)/float(H));
            const uint32_t honest=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS|TGPT_OPTION_NO_SPECULAR;
            // The walls' radiance: the image's mean over the frames after the first few, pre-exposure divided out (their
            // reflections left out: those add the walls' light again times their Fresnel reflectance, some 3 %).
            auto glow=[&](uint32_t pathBounces, uint32_t options, uint32_t frames, uint32_t skip, uint32_t view=TGPT_VIEW_NONE) {
                bounces=pathBounces;
                double sum=0; uint32_t n=0;
                index=first;
                for(uint32_t i=0;i<frames;++i) {
                    run(view, inside, inside, i ? index++ : 0, options);
                    if(i<skip) continue;
                    const auto r=readback(); double s=0;
                    for(size_t j=0;j<r.size();j+=3) s+=0.2126*r[j]+0.7152*r[j+1]+0.0722*r[j+2];
                    sum+=s/(double(W)*H); ++n;
                }
                return sum/n/exposure;
            };
            const double one=glow(1, honest|TGPT_OPTION_NO_RADIANCE_CACHE, 40, 8), four=glow(4, honest|TGPT_OPTION_NO_RADIANCE_CACHE, 40, 8);
            // The cache starts empty when it is turned back on, and fills over its first frames.
            const double cachedOne=glow(1, honest, 400, 300), cachedTwo=glow(2, honest, 400, 300);
            std::printf("      furnace (walls' radiance, 1 expected): paths alone %.3f with one bounce (0.75 expected), %.3f with four (0.97); "
                        "ending on the radiance cache %.3f with one bounce, %.3f with two\n", one, four, cachedOne, cachedTwo);
            expect(std::fabs(one-0.75)<0.03 && std::fabs(four-0.969)<0.04, "the furnace's paths without the radiance cache carry what their bounces reach");
            expect(std::fabs(cachedOne-1.0)<0.04 && std::fabs(cachedTwo-1.0)<0.04, "paths ending on the radiance cache carry every bounce of the light, and no more");
            // With the glowing walls sampled as lights (Emissive Lights): ending on the radiance cache, the same radiance, 1, the
            // light sampling at each vertex taking in what the paths and the cache's samples then leave out (counting the walls'
            // light twice, the cache came to more than 1). With one bounce and no cache it is more than paths alone reach
            // (0.75), up to what two bounces do (0.875): the vertex's light sampling takes in the walls' light arriving there
            // within their reach, a bounce more of it, as it does a lamp's. Both without ReSTIR's reuse (with it, printed).
            const uint32_t plainEmitters=honest|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_EMISSIVE_LIGHTS;
            const double emittersOne=glow(1, plainEmitters|TGPT_OPTION_NO_RADIANCE_CACHE, 40, 8);
            const double emittersCached=glow(1, plainEmitters, 400, 300);
            const double emittersReused=glow(1, honest|TGPT_OPTION_EMISSIVE_LIGHTS, 400, 300);
            std::printf("      furnace with its walls sampled as lights: %.3f with one bounce (0.75 to 0.875), %.3f ending on the radiance cache "
                        "(1 expected), %.3f with ReSTIR's reuse too\n", emittersOne, emittersCached, emittersReused);
            expect(emittersOne>0.74 && emittersOne<0.88 && std::fabs(emittersCached-1.0)<0.04,
                   "glowing walls sampled as lights light the furnace with every bounce of their light once, ending on the radiance cache");
            // Reflections (the Specular view, which has nothing else here): a wall seen in another shows its glow (0.5) and,
            // with the radiance cache, the light it reflects as well (0.5 x 1): twice as bright, whatever the Fresnel term.
            const uint32_t reflections=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS;
            const double mirrored=glow(1, reflections|TGPT_OPTION_NO_RADIANCE_CACHE, 40, 8, TGPT_VIEW_SPECULAR);
            const double mirroredCached=glow(1, reflections, 260, 200, TGPT_VIEW_SPECULAR);
            std::printf("      furnace reflections: %.4f with the reflected wall's own light alone, %.4f ending on the radiance cache (%.3f times, 2 expected)\n",
                mirrored, mirroredCached, mirroredCached/std::max(mirrored, 1e-12));
            expect(mirrored>0 && std::fabs(mirroredCached/mirrored-2.0)<0.1, "a reflection shows the reflected surface's bounced light, from the radiance cache");
            objects=keepObjects;
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
            expect(SetLights(&light, 1)==0, "one light again");
            sunOn=keepSun; skyIntensity=keepSky; bounces=keepBounces;
        }
    }
    // Emissive surfaces as lights (the Emissive Lights row, TGPT_OPTION_EMISSIVE_LIGHTS). A glowing quad (0.5 m square, 200 nits,
    // facing down) hangs 1 m over the floor of the room, whose walls, ceiling and box are traced black (the leaves left out): no
    // sun, sky or lamps, so the floor gets the quad's light alone. With the row on, ReSTIR DI samples the quad's triangles and
    // the paths that meet it count what that leaves (emission_weight); off, only the paths that meet it see its light.
    {
        const float keepSky=skyIntensity;
        sunOn=false; skyIntensity=1e-6f; // 0 would mean the default, 1
        expect(SetLights(nullptr, 0)==0, "no lamps");
        uint32_t next=20000;
        // Materials 11 black, 12 glowing (grey, 200 nits white, single-sided), 13 glowing and black (it reflects nothing).
        auto glowing=materials; glowing.resize(14);
        glowing[11]=materials[0]; for(int c=0;c<3;++c) glowing[11].baseColor[c]=0.f;
        glowing[12]=materials[0]; for(int c=0;c<3;++c) { glowing[12].baseColor[c]=0.2f; glowing[12].emissive[c]=200.f; }
        glowing[13]=glowing[12]; for(int c=0;c<3;++c) glowing[13].baseColor[c]=0.f;
        expect(SetMaterials(0, glowing.data(), uint32_t(glowing.size()))==0, "materials with black and glowing ones set");
        // The room, everything but its floor black (or, lit, only its back wall white), and meshes in it (the quad unless another is
        // named).
        struct Placed { M4 world; uint32_t material, flags; int mesh; };
        auto setRoom=[&](const std::vector<Placed>& extra, size_t lit=0) {
            std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
            for(size_t k=0;k<8;++k) {
                TGPT_Instance in=instances[k]; in.materialBase=uint32_t(ids.size());
                for(size_t s=0;s<objects[k].materials.size();++s) ids.push_back(k==lit ? 0u : 11u);
                list.push_back(in);
            }
            for(const auto& e:extra) {
                TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=e.world.m[r][c];
                in.mesh=e.mesh ? e.mesh : quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=e.flags;
                ids.push_back(e.material); list.push_back(in);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "the dark room set");
        };
        const V3 quadAt{-2.f, 1.f, 1.5f};
        const M4 down=mul(translate_scale(quadAt, {0.5f, 1.f, 0.5f}), rot_x(pi)); // the quad's front (+y) turned to face down
        // Where a world point is in the read-back image.
        auto pixel_of=[&](V3 w) {
            const V3 r=w-cam.position; float c[4];
            for(int i=0;i<4;++i) c[i]=cam.viewProj.m[i][0]*r.x+cam.viewProj.m[i][1]*r.y+cam.viewProj.m[i][2]*r.z+cam.viewProj.m[i][3];
            return std::array<int, 2>{int((c[0]/c[3]*0.5f+0.5f)*float(W)), int((0.5f-c[1]/c[3]*0.5f)*float(H))};
        };
        const auto foot=pixel_of({quadAt.x, 0.f, quadAt.z});
        auto lum=[](const std::vector<float>& v, size_t j) { return 0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2]; };
        auto converge=[&](uint32_t view, uint32_t options, uint32_t frames, uint32_t raysPerPixel) {
            const uint32_t keepRays=rays; rays=raysPerPixel;
            std::vector<double> acc(size_t(W)*H*3, 0.0);
            for(uint32_t i=0;i<frames+8;++i) {
                run(view, cam, cam, i ? next++ : 0, options);
                if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
            }
            rays=keepRays;
            std::vector<float> out(acc.size()); for(size_t j=0;j<acc.size();++j) out[j]=float(acc[j]/frames);
            return out;
        };
        // The light of the floor and the lower walls (rows below 500: no sky), or of the rows and columns given, 8 rays a pixel, in 8
        // batches of 8 frames, with its standard error.
        auto energy=[&](uint32_t options, double& standardError, std::array<int, 4> area=std::array<int, 4>{0, 2, 0, 500}) {
            if(!area[2]) area[2]=int(W);
            const uint32_t keepRays=rays; rays=8;
            for(uint32_t i=0;i<8;++i) run(TGPT_VIEW_NONE, cam, cam, i ? next++ : 0, options);
            double sum=0, sum2=0; const uint32_t batches=8, per=8;
            for(uint32_t b=0;b<batches;++b) {
                double mean=0;
                for(uint32_t i=0;i<per;++i) { run(TGPT_VIEW_NONE, cam, cam, next++, options); mean+=region(readback(), area[0], area[1], area[2], area[3]); }
                mean/=per; sum+=mean; sum2+=mean*mean;
            }
            rays=keepRays;
            const double m=sum/batches; standardError=std::sqrt(std::max(0.0, sum2/batches-m*m)/double(batches-1));
            return m;
        };
        const uint32_t honest=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS, plain=honest|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL;
        const uint32_t on=TGPT_OPTION_EMISSIVE_LIGHTS;
        setRoom({{down, 12u, 0u, 0}});
        std::printf("      the glowing quad's foot on the floor at pixel %d, %d\n", foot[0], foot[1]);
        // 1. The same light, row on or off, where every path that carries the quad's light is traced whole: the quad black
        // (material 13: the only light is its own) and the traced reflections off (a reflection shades the one vertex it
        // meets, and with the row on a surface it shows is lit there by the quad, which off would take a second vertex), the
        // plain estimator (no reuse, no clamps, no boiling filter), 64 frames of 8 rays, the direct light taken where the
        // bounced light is (TGPT_OPTION_OFFSET_RECEIVER: where the surface's rays start, 2 cm and 0.2 % of the distance out
        // along its normal). By default the direct light is estimated at the surface itself (4. measures it there against its
        // analytic value), the bounced light where its rays start: the row moves the quad's light from the one to the other.
        // A dim lamp high in a far corner keeps the light grid's cells from listing the quad alone (a vertex whose cell lists
        // nothing counts every emission whole, the light sampling having nothing there).
        {
            setRoom({{down, 13u, 0u, 0}});
            TGPT_Light dim{}; dim.position[0]=5.f; dim.position[1]=5.f; dim.position[2]=5.f; dim.type=0;
            dim.color[0]=dim.color[1]=dim.color[2]=0.5f; dim.range=24; dim.radius=0.03f; dim.id=402;
            expect(SetLights(&dim, 1)==0, "a dim lamp in a far corner set");
            const uint32_t same=plain|TGPT_OPTION_NO_SPECULAR|TGPT_OPTION_OFFSET_RECEIVER;
            double seOff=0, seOn=0;
            const double off=energy(same, seOff), sampled=energy(same|on, seOn);
            const double combined=std::sqrt(seOff*seOff+seOn*seOn);
            std::printf("      glowing black quad, the room's diffuse light where rays start: row off %.5f (+-%.5f), on %.5f (+-%.5f): on/off %.4f\n", off, seOff,
                sampled, seOn, sampled/std::max(off, 1e-12));
            expect(off>0 && std::fabs(sampled-off)<3.0*combined+0.002*off, "an emissive surface's light converges to the same image with Emissive Lights on or off");
            // And where its light comes off another surface first: the traced floor black too and the back wall white, the quad
            // facing the wall 1.5 m from it. The floor and the box in front of the wall get its light off the wall, whose path
            // vertex samples the quad (98 % of its light there, its reach's window) and whose ray counts the rest (emission_weight
            // from that vertex). A path vertex takes its light at its surface, and its ray starts 2.5 cm off it (1 cm and 0.1 % of
            // its distance from the camera, 15 m), where 1.5 m from the quad its light is up to 3.4 % more (the inverse square):
            // with the row off, the wall's light is the ray's, so the floor gets a little more off it, never less (0.7 % more,
            // +-0.2 %, over 400 frames of 8 rays).
            {
                setRoom({{mul(translate_scale({-2.f, 1.5f, 4.5f}, {0.5f, 0.5f, 1.f}), rot_x(pi/2)), 13u, 0u, 0}}, 1);
                const std::array<int, 4> floorArea{400, 2, 880, 280};
                double seWallOff=0, seWallOn=0;
                const double wallOff=energy(same, seWallOff, floorArea), wallOn=energy(same|on, seWallOn, floorArea);
                const double ratio=wallOn/std::max(wallOff, 1e-12), ratioError=ratio*std::sqrt(std::pow(seWallOff/std::max(wallOff, 1e-12), 2.0)+std::pow(seWallOn/std::max(wallOn, 1e-12), 2.0));
                std::printf("      glowing black quad facing a white wall, the floor's light off the wall: row off %.5f (+-%.5f), on %.5f (+-%.5f): on/off %.4f (+-%.4f)\n",
                    wallOff, seWallOff, wallOn, seWallOn, ratio, ratioError);
                expect(wallOff>0 && ratio>0.96-3.0*ratioError && ratio<1.0+3.0*ratioError+0.002,
                       "an emissive surface's light off another surface converges to the same image with Emissive Lights on or off, less the ray offset's");
            }
            // For the record, the quad grey again and the lamp gone: the diffuse light at the surface (the default), then with
            // the reflections, ReSTIR's reuse as it ships (still without clamps or boiling filter), and the clamps and boiling
            // filter as the game runs them: off, the quad's light was cut at the clamps' fallback reference (no lamp to measure
            // against); on, the quad is the scene's brightest light and keeps it.
            expect(SetLights(nullptr, 0)==0, "no lamps");
            setRoom({{down, 12u, 0u, 0}});
            double e[8]{};
            const double surfaceOff=energy(plain|TGPT_OPTION_NO_SPECULAR, e[0]), surfaceOn=energy(plain|TGPT_OPTION_NO_SPECULAR|on, e[1]);
            const double defaultOff=energy(plain, e[2]), defaultOn=energy(plain|on, e[3]);
            const double reuseOff=energy(honest, e[4]), reuseOn=energy(honest|on, e[5]);
            const double clampedOff=energy(TGPT_OPTION_NO_DENOISER, e[6]), clampedOn=energy(TGPT_OPTION_NO_DENOISER|on, e[7]);
            std::printf("      glowing grey quad, diffuse light at the surface: row off %.5f (+-%.5f), on %.5f (+-%.5f): on/off %.4f\n", surfaceOff, e[0], surfaceOn, e[1],
                surfaceOn/std::max(surfaceOff, 1e-12));
            std::printf("      with the reflections: row off %.5f (+-%.5f), on %.5f (+-%.5f): on/off %.4f; with ReSTIR's reuse %.5f, %.5f: %.4f\n", defaultOff, e[2],
                defaultOn, e[3], defaultOn/std::max(defaultOff, 1e-12), reuseOff, reuseOn, reuseOn/std::max(reuseOff, 1e-12));
            std::printf("      as the game runs it (clamps, boiling filter): row off %.5f, on %.5f, of the unclamped light %.3f and %.3f\n", clampedOff, clampedOn,
                clampedOff/std::max(defaultOff, 1e-12), clampedOn/std::max(defaultOn, 1e-12));
        }
        // 2. Less noise around the quad's foot: one frame's error against its converged self (no clamps or boiling filter), averaged over
        // 4 frames: with ReSTIR's reuse as it ships, with its temporal reuse alone, both without a denoiser (against 96 frames of 8
        // rays), and through the built-in denoiser (against the mean of 96 of its frames at the same rays, its blur part of it).
        // ReSTIR DI's spatial reuse (RTXDI's Basic normalization) adds noise wherever neighbours 20 pixels apart see a light very
        // differently, as the floor right under a close light does: a point light where the quad is shows 0.015 of error without it
        // and 0.29 with it.
        {
            auto noise=[&](uint32_t options, uint32_t warmup, const char* image) {
                const bool denoised=(options&TGPT_OPTION_NO_DENOISER)==0;
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                const uint32_t keepRays=rays; if(!denoised) rays=8;
                for(uint32_t i=0;i<warmup+96;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? next++ : 0, options);
                    if(i>=warmup) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                rays=keepRays;
                std::vector<float> reference(acc.size()); for(size_t j=0;j<acc.size();++j) reference[j]=float(acc[j]/96.0);
                double error=0;
                for(uint32_t i=0;i<warmup+4;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? next++ : 0, options);
                    if(i<warmup) continue;
                    const auto px=readback();
                    if(image && i==warmup+3) { save(px, image, true); save(reference, "35-emissive-quad-converged.png", true); }
                    double e=0, l=0;
                    for(int y=std::max(foot[1]-60, 0);y<std::min(foot[1]+60, int(H));++y) for(int x=std::max(foot[0]-120, 0);x<std::min(foot[0]+120, int(W));++x) {
                        const size_t j=(size_t(y)*W+size_t(x))*3; e+=std::fabs(lum(px, j)-lum(reference, j)); l+=lum(reference, j);
                    }
                    error+=l>0 ? e/l/4.0 : 0.0;
                }
                return error;
            };
            const uint32_t fair=TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS;
            const double shippedOff=noise(fair|TGPT_OPTION_NO_DENOISER, 16, "36-emissive-quad-row-off-frame.png");
            const double shippedOn=noise(fair|TGPT_OPTION_NO_DENOISER|on, 16, "37-emissive-quad-row-on-frame.png");
            const double temporalOff=noise(fair|TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPATIAL, 16, nullptr);
            const double temporalOn=noise(fair|TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPATIAL|on, 16, nullptr);
            const double denoisedOff=noise(fair, 40, nullptr), denoisedOn=noise(fair|on, 40, nullptr);
            std::printf("      glowing quad, one frame's error around its foot, row off against on: %.4f / %.4f (%.2f times) with ReSTIR's reuse, %.4f / %.4f (%.2f) with its "
                "temporal reuse alone, %.4f / %.4f (%.2f) denoised\n", shippedOff, shippedOn, shippedOff/std::max(shippedOn, 1e-12), temporalOff, temporalOn,
                temporalOff/std::max(temporalOn, 1e-12), denoisedOff, denoisedOn, denoisedOff/std::max(denoisedOn, 1e-12));
            expect(shippedOn<0.65*shippedOff && temporalOn<0.25*temporalOff && denoisedOn<0.6*denoisedOff,
                   "an emissive surface sampled as a light lights the floor with far less noise");
        }
        // 3. Its reflection in a glossy floor (HDRP's roughness 0.05 there), the traced reflections alone, where either image shows
        // it (their mean past 5 % of its brightest: picked by one image alone, its noise would pick where it came out bright):
        // with the row on, ReSTIR DI gives the quad no specular light, and the reflection rays count it whole, so it is counted
        // once, as off. Counted twice, it would be twice as bright.
        {
            objects[0].roughness=0.05f;
            const auto specOff=converge(TGPT_VIEW_SPECULAR, honest, 64, 1), specOn=converge(TGPT_VIEW_SPECULAR, honest|on, 64, 1);
            objects[0].roughness=0.6f;
            save(specOn, "38-emissive-quad-reflection.png", true);
            double top=0; for(size_t j=0;j+2<specOff.size();j+=3) top=std::max(top, 0.5*(lum(specOff, j)+lum(specOn, j)));
            double a=0, b=0; int n=0;
            for(size_t j=0;j+2<specOff.size();j+=3) if(0.5*(lum(specOff, j)+lum(specOn, j))>0.05*top) { a+=lum(specOff, j); b+=lum(specOn, j); ++n; }
            std::printf("      glowing quad's reflection in a glossy floor: %d pixels, row on/off %.4f\n", n, b/std::max(a, 1e-12));
            expect(n>200 && a>0 && std::fabs(b/a-1.0)<0.02, "a reflection of an emissive surface counts its light once with Emissive Lights on");
        }
        // 4. Its units and sampling density: the direct light around its foot (VIEW_DIRECT: on a white surface, E / pi at the
        // exposure 0.25), pixel by pixel against the irradiance a Lambertian square of 200 nits gives the floor where each pixel's
        // centre ray meets it, E = L * the integral of cos * cos' / r^2 over the square, times the window (1 - (r / 5 m)^4)^2
        // at each of its points: its reach, sqrt(50 cd / 2 lux) = 5 m, leaves the paths the rest (0.4 % there). Summed on a
        // 200 x 200 grid; under its centre, without the window, that is the closed form of four corner rectangles, 4L X/sqrt(1+X^2)
        // atan(X/sqrt(1+X^2)) with X = 0.25. Mirrored (a negative scale), the same: its front is still the side its triangles
        // face in the mesh's own space.
        {
            // Where the ray through a pixel's centre meets the floor (the view matrix is camera-relative).
            auto floor_at=[&](int x, int y) {
                const float ndc[4]={(float(x)+0.5f)/float(W)*2.f-1.f, 1.f-(float(y)+0.5f)/float(H)*2.f, 0.5f, 1.f};
                double h[4]{};
                for(int i=0;i<4;++i) for(int k=0;k<4;++k) h[i]+=double(cam.invViewProj.m[i][k])*ndc[k];
                const V3 d=normalize(V3{float(h[0]/h[3]), float(h[1]/h[3]), float(h[2]/h[3])});
                return cam.position+d*(-cam.position.y/d.y);
            };
            auto irradiance=[&](V3 p, bool window) {
                const int n=200; const double cell=0.5/n, h=double(quadAt.y)-p.y;
                double e=0;
                for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
                    const double x=double(quadAt.x)-0.25+(i+0.5)*cell-p.x, z=double(quadAt.z)-0.25+(j+0.5)*cell-p.z, r2=x*x+h*h+z*z, f=r2/25.0;
                    e+=h*h/(r2*r2)*(window ? std::pow(std::max(0.0, 1.0-f*f), 2.0) : 1.0)*cell*cell;
                }
                return 200.0*e;
            };
            const double X=0.25, closed=4.0*200.0*X/std::sqrt(1.0+X*X)*std::atan(X/std::sqrt(1.0+X*X)), summed=irradiance({quadAt.x, 0.f, quadAt.z}, false);
            double expected=0;
            for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) expected+=irradiance(floor_at(foot[0]+dx, foot[1]+dy), true)/3.14159265358979*0.25/25.0;
            auto direct=[&](const M4& world) {
                setRoom({{world, 12u, 0u, 0}});
                const auto px=converge(TGPT_VIEW_DIRECT, plain|on, 96, 1);
                double s=0;
                for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) s+=lum(px, (size_t(foot[1]+dy)*W+size_t(foot[0]+dx))*3)/25.0;
                return s;
            };
            const double sampled=direct(down), mirrored=direct(mul(translate_scale(quadAt, {-0.5f, 1.f, 0.5f}), rot_x(pi)));
            setRoom({{down, 12u, 0u, 0}});
            std::printf("      E under the glowing quad's centre: summed %.3f lux, closed form %.3f; direct light around its foot %.4f, mirrored %.4f, a Lambertian "
                "square's %.4f (%.4f, %.4f)\n", summed, closed, sampled, mirrored, expected, sampled/expected, mirrored/expected);
            expect(std::fabs(summed/closed-1.0)<0.001, "the harness's sum of a square's irradiance matches its closed form");
            expect(std::fabs(sampled/expected-1.0)<0.015 && std::fabs(mirrored/expected-1.0)<0.015, "an emissive triangle's light is sampled in the right units and density");
        }
        // 5. Its front alone glows (a single-sided material, as HDRP culls its back): mirrored (a negative scale) and facing down,
        // it lights the room as before, to the paths that meet it too (their front is the side the triangles face in the mesh's
        // own space); black and facing up (its back to the floor), it lights nothing of the floor around its foot, row on or off
        // (the walls above it, which the camera sees in their own colors, it does light).
        {
            const uint32_t diffuse=plain|TGPT_OPTION_NO_SPECULAR;
            auto underIt=[&](uint32_t options) {
                return region(converge(TGPT_VIEW_NONE, options, 32, 8), foot[0]-120, foot[1]-60, foot[0]+120, foot[1]+60);
            };
            double se[2]{};
            setRoom({{down, 13u, 0u, 0}});
            const double facingDown=energy(diffuse, se[0]), downFloor=underIt(diffuse);
            setRoom({{mul(translate_scale(quadAt, {-0.5f, 1.f, 0.5f}), rot_x(pi)), 13u, 0u, 0}});
            const double mirrored=energy(diffuse, se[1]);
            setRoom({{translate_scale(quadAt, {0.5f, 1.f, 0.5f}), 13u, 0u, 0}});
            const double upOff=underIt(diffuse), upOn=underIt(diffuse|on);
            setRoom({{down, 12u, 0u, 0}});
            std::printf("      glowing black quad, the room's diffuse light facing down %.5f (+-%.5f), mirrored %.5f (+-%.5f); the floor around its foot facing down "
                "%.5f, facing up %.7f row off, %.7f on\n", facingDown, se[0], mirrored, se[1], downFloor, upOff, upOn);
            expect(facingDown>0 && std::fabs(mirrored-facingDown)<3.0*std::sqrt(se[0]*se[0]+se[1]*se[1])+0.002*facingDown,
                   "a mirrored emissive surface glows from the same side, for the paths that meet it");
            expect(downFloor>0 && upOff<0.001*downFloor && upOn<0.001*downFloor, "a single-sided emissive surface lights nothing behind it");
        }
        // 6. A kept light 2 cm under the quad (black now, so it reflects nothing): the quad is the light's own surface (a lantern's
        // glass round its candle), which lights nothing itself, row on or off: the room gets what the light gives with the same
        // quad not glowing. Without the traced reflections, which still show it (a glossy floor mirrors the lantern's glass, not
        // the light inside it).
        {
            TGPT_Light candle{}; candle.position[0]=quadAt.x; candle.position[1]=quadAt.y-0.02f; candle.position[2]=quadAt.z; candle.type=0;
            candle.color[0]=8; candle.color[1]=6; candle.color[2]=4; candle.range=12; candle.radius=0.03f; candle.id=400;
            expect(SetLights(&candle, 1)==0, "a light under the quad set");
            double seAlone=0, seOn=0, seOff=0;
            const uint32_t diffuse=plain|TGPT_OPTION_NO_SPECULAR;
            setRoom({{down, 11u, 0u, 0}});
            const double alone=energy(diffuse|on, seAlone);
            setRoom({{down, 13u, 0u, 0}});
            const double pairedOn=energy(diffuse|on, seOn), pairedOff=energy(diffuse, seOff);
            std::printf("      a light 2 cm under a glowing quad: the room's light %.5f with the quad dark (+-%.5f), glowing %.5f row on (+-%.5f), %.5f off (+-%.5f)\n",
                alone, seAlone, pairedOn, seOn, pairedOff, seOff);
            expect(alone>0 && std::fabs(pairedOn-alone)<3.0*std::sqrt(seAlone*seAlone+seOn*seOn)+0.005*alone &&
                   std::fabs(pairedOff-alone)<3.0*std::sqrt(seAlone*seAlone+seOff*seOff)+0.005*alone,
                   "an emissive surface right around a kept light is that light's surface: it adds no light of its own");
            expect(SetLights(nullptr, 0)==0, "no lamps");
        }
        // 7. What the Emissive debug view makes of glowing quads facing the camera: green sampled as a light, blue a kept light's own
        // surface (one 5 cm in front of it), red one the plugin saw move, and red a glowing mesh of more triangles than the budget
        // (a 130 x 130 grid, 33,800) while a small quad beside it is sampled; with the row off, only the light's own surface stays blue.
        {
            std::vector<float> gp; std::vector<uint32_t> gi;
            const int cells=130;
            for(int y=0;y<=cells;++y) for(int x=0;x<=cells;++x) gp.insert(gp.end(), {float(x)/cells*3.f, float(y)/cells*2.f, 0.f});
            for(int y=0;y<cells;++y) for(int x=0;x<cells;++x) {
                const uint32_t a=uint32_t(y*(cells+1)+x), b=a+1, c=a+uint32_t(cells+1), d=c+1;
                gi.insert(gi.end(), {a, c, b, b, c, d}); // wound to face -z, towards the camera
            }
            TGPT_CpuMeshDesc big{sizeof(TGPT_CpuMeshDesc), TGPT_ABI}; big.key=90; big.positions=gp.data(); big.indices=gi.data();
            big.vertexCount=uint32_t(gp.size()/3); big.indexCount=uint32_t(gi.size());
            const int bigHandle=RegisterCpuMesh(&big);
            expect(bigHandle>0, "a glowing mesh of 33,800 triangles registered");
            auto facingCamera=[&](float x, float y) { return mul(translate_scale({x, y, 4.f}, {0.8f, 0.8f, 1.f}), rot_x(-pi/2)); };
            const M4 sampledQuad=facingCamera(-3.f, 3.5f), ownQuad=facingCamera(-1.f, 3.5f), movingQuad=facingCamera(1.f, 3.5f), besideBig=facingCamera(-3.f, 2.2f);
            setRoom({{sampledQuad, 12u, 0u, 0}, {ownQuad, 12u, 0u, 0}, {movingQuad, 12u, TGPT_INSTANCE_MOVING, 0}, {besideBig, 12u, 0u, 0},
                     {translate_scale({2.f, 2.5f, 5.8f}, {1.f, 1.f, 1.f}), 12u, 0u, bigHandle}});
            TGPT_Light lamp{}; lamp.position[0]=-1.f; lamp.position[1]=3.5f; lamp.position[2]=3.95f; lamp.type=0;
            lamp.color[0]=5; lamp.color[1]=4; lamp.color[2]=3; lamp.range=6; lamp.radius=0.03f; lamp.id=401;
            expect(SetLights(&lamp, 1)==0, "a light in front of a glowing quad set");
            const V3 points[5]={{-3.f, 3.5f, 4.f}, {-1.f, 3.5f, 4.f}, {1.f, 3.5f, 4.f}, {-3.f, 2.2f, 4.f}, {3.5f, 3.5f, 5.8f}};
            auto classify=[&](uint32_t options) {
                for(uint32_t i=0;i<6;++i) run(TGPT_VIEW_EMISSIVE, cam, cam, i ? next++ : 0, options); // the big mesh converts meanwhile
                const auto px=readback();
                save(px, options&on ? "39-view-emissive.png" : "40-view-emissive-row-off.png", false);
                std::string colors;
                for(const V3& p:points) {
                    const auto at=pixel_of(p); const size_t j=(size_t(at[1])*W+size_t(at[0]))*3;
                    colors+=px[j+1]>px[j] && px[j+1]>px[j+2] ? 'g' : px[j+2]>px[j] && px[j+2]>px[j+1] ? 'b' : px[j]>px[j+1] && px[j]>px[j+2] ? 'r' : '?';
                }
                return colors;
            };
            const std::string rowOn=classify(on), rowOff=classify(0u);
            std::printf("      Emissive view (sampled, a light's own, moving, beside the big mesh, the big mesh): row on %s, off %s\n", rowOn.c_str(), rowOff.c_str());
            expect(rowOn=="gbrgr", "emissive surfaces are sampled as lights except a light's own, moving ones and those past the triangle budget");
            expect(rowOff=="rbrrr", "with Emissive Lights off none is sampled, and a light's own surface still lights nothing");
            expect(SetLights(nullptr, 0)==0, "no lamps");
        }
        expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "materials set");
        expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene set");
        expect(SetLights(&light, 1)==0, "one light again");
        sunOn=true; skyIntensity=keepSky;
    }
    // DLSS Ray Reconstruction's guides: frames that ask for them, then the textures TGPT_GetGuides hands out,
    // read back on the red wall (diffuse color 0.75, 0.08, 0.06).
    {
        auto getGuides=reinterpret_cast<decltype(&TGPT_GetGuides)>(GetProcAddress(pt, "TGPT_GetGuides"));
        expect(getGuides!=nullptr, "guides export");
        rayReconstruction=true;
        for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, cam, cam, 1000+i);
        rayReconstruction=false;
        const auto undenoised=readback();
        bool finiteRaw=true; for(float v:undenoised) if(!std::isfinite(v)) { finiteRaw=false; break; }
        expect(finiteRaw, "the undenoised light for Ray Reconstruction is finite");
        TGPT_Guides g{sizeof(TGPT_Guides), TGPT_ABI};
        const bool have=getGuides && getGuides(&g)==0 && g.width==W && g.height==H && g.diffuseAlbedo && g.specularAlbedo && g.normalRoughness;
        expect(have, "guides written at the render size");
        if(have) {
            auto texel=[&](uint64_t resource, UINT x, UINT y, bool half) {
                auto* t=reinterpret_cast<ID3D11Texture2D*>(uintptr_t(resource));
                D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
                ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
                context->CopyResource(staging.Get(), t);
                D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
                const uint8_t* row=static_cast<const uint8_t*>(m.pData)+size_t(y)*m.RowPitch;
                std::array<float, 4> v{};
                for(int k=0;k<4;++k) v[k]=half ? half_to_float(reinterpret_cast<const uint16_t*>(row)[x*4+k]) : row[x*4+k]/255.f;
                context->Unmap(staging.Get(), 0);
                return v;
            };
            const auto diffuse=texel(g.diffuseAlbedo, 200, 400, false), specular=texel(g.specularAlbedo, 200, 400, false), normal=texel(g.normalRoughness, 200, 400, true);
            const float length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
            std::printf("      guides on the red wall: diffuse %.3f %.3f %.3f, specular %.3f, normal %.2f %.2f %.2f (length %.3f), roughness %.2f\n",
                diffuse[0], diffuse[1], diffuse[2], specular[1], normal[0], normal[1], normal[2], length, normal[3]);
            expect(std::fabs(diffuse[0]-0.75f)<0.03f && std::fabs(diffuse[1]-0.08f)<0.03f && std::fabs(diffuse[2]-0.06f)<0.03f, "the diffuse albedo guide is the wall's linear color");
            expect(std::fabs(length-1.f)<0.02f && normal[0]>0.9f, "the normal guide is the wall's unit normal, facing into the room");
            expect(specular[1]>0.01f && specular[1]<0.2f, "the specular albedo guide is a dielectric's");
            // Written by the composite as it applied the traced light: the traced reflections' hit distance, and the color
            // right after it, before transparent surfaces (none here: the color target itself, texel for texel).
            const bool extras=g.extrasFrameIndex==g.frameIndex && g.specularHitDistance && g.colorBeforeTransparency;
            expect(extras, "the composite wrote Ray Reconstruction's hit distance and color before transparency for the frame");
            if(extras) {
                const auto before=texel(g.colorBeforeTransparency, 200, 400, true), final=texel(uint64_t(uintptr_t(color.Get())), 200, 400, true);
                auto* hitTexture=reinterpret_cast<ID3D11Texture2D*>(uintptr_t(g.specularHitDistance));
                D3D11_TEXTURE2D_DESC d{}; hitTexture->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
                ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
                context->CopyResource(staging.Get(), hitTexture);
                D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
                const float hit=half_to_float(reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData)+size_t(400)*m.RowPitch)[200]);
                context->Unmap(staging.Get(), 0);
                std::printf("      Ray Reconstruction's extras on the red wall: color before transparency %.4f %.4f %.4f (the frame's %.4f %.4f %.4f), reflection hit distance %.2f\n",
                    before[0], before[1], before[2], final[0], final[1], final[2], hit);
                expect(std::fabs(before[0]-final[0])<1e-3f && std::fabs(before[1]-final[1])<1e-3f && std::fabs(before[2]-final[2])<1e-3f,
                       "the color before transparency is the frame as the composite left it");
                expect(std::isfinite(hit) && hit>0.f, "the reflection hit distance is a distance");
            }
            // See-through glass (the see-through test's cube of clear glass, drawn white and smooth): its light is all
            // specular, so the composite hands Ray Reconstruction a near-mirror's guides there (the tracer's roughness 0.03),
            // no diffuse albedo, a specular albedo that covers the light the glass lets through (0.96 of its white 0.75 besides
            // its own reflectance, 0.04), and the distance to what is seen behind it as the hit distance: the bottom of the back
            // wall, some 6.7 m on along the view ray, where its reflection finds only the sky.
            {
                const M4 w=translate_scale({3.4f, 1.3f, 0.5f}, {1.4f, 1.4f, 1.4f});
                objects.push_back(Object{1, w, {0, 0}, false, false, 0, false, -1, 0.05f});
                auto list=instances; auto listIndices=indices;
                TGPT_Instance glassCube{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) glassCube.transform[r*4+c]=w.m[r][c];
                glassCube.mesh=cubeHandle; glassCube.materialBase=uint32_t(listIndices.size()); glassCube.mask=0xFF; glassCube.flags=TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(10u); listIndices.push_back(10u);
                list.push_back(glassCube);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with a glass cube");
                rayReconstruction=true;
                for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, cam, cam, 1100+i);
                rayReconstruction=false;
                objects.pop_back();
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the cube");
                // The texel at the centre of the cube's face towards the camera (the tracer's pixel from clip space).
                const V3 face{3.4f, 1.3f, -0.2f}, rel=face-cam.position;
                float clip[4]; for(int r=0;r<4;++r) clip[r]=cam.viewProj.m[r][0]*rel.x+cam.viewProj.m[r][1]*rel.y+cam.viewProj.m[r][2]*rel.z+cam.viewProj.m[r][3];
                const UINT gx=UINT((clip[0]/clip[3]*0.5f+0.5f)*float(W)), gy=UINT((0.5f-clip[1]/clip[3]*0.5f)*float(H));
                TGPT_Guides gg{sizeof(TGPT_Guides), TGPT_ABI};
                const bool again=getGuides(&gg)==0 && gg.extrasFrameIndex==gg.frameIndex && gg.specularHitDistance && gx<W && gy<H;
                expect(again, "the composite wrote the guides' extras for the frame with the glass cube");
                if(again) {
                    const auto gd=texel(gg.diffuseAlbedo, gx, gy, false), gs=texel(gg.specularAlbedo, gx, gy, false), gn=texel(gg.normalRoughness, gx, gy, true);
                    auto* hitTexture=reinterpret_cast<ID3D11Texture2D*>(uintptr_t(gg.specularHitDistance));
                    D3D11_TEXTURE2D_DESC d{}; hitTexture->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
                    ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
                    context->CopyResource(staging.Get(), hitTexture);
                    D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
                    const float hit=half_to_float(reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData)+size_t(gy)*m.RowPitch)[gx]);
                    context->Unmap(staging.Get(), 0);
                    std::printf("      guides on see-through glass (%u, %u): diffuse %.3f %.3f %.3f, specular %.3f %.3f %.3f, roughness %.3f, hit distance %.2f\n",
                        gx, gy, gd[0], gd[1], gd[2], gs[0], gs[1], gs[2], gn[3], hit);
                    expect(gd[0]<0.02f && gd[1]<0.02f && gd[2]<0.02f, "no diffuse albedo guide on see-through glass");
                    expect(gs[0]>0.7f && gs[0]<0.82f && std::fabs(gs[1]-gs[0])<0.02f, "the specular albedo guide on see-through glass covers the light it lets through");
                    expect(std::fabs(gn[3]-0.03f)<0.005f, "the guide on see-through glass has the tracer's near-mirror roughness");
                    expect(hit>4.f && hit<10.f, "the hit distance on see-through glass is the distance to what is seen behind it");
                }
            }
        }
    }
    // Opacity micromaps (DirectX Raytracing 1.2) settle ahead of the alpha test only what it would decide, so
    // the same frames with and without them must come out the same. The quad mesh's micromaps were made for
    // the leaf's material: the floor, walls and the invisible quad share its BLAS with micromaps disabled.
    {
        ReadStatus(&status);
        const bool dxr12=(status.features&TGPT_FEATURE_DXR12)!=0;
        std::printf("      DirectX Raytracing 1.2: %s, Shader Execution Reordering %s; micromaps: %u meshes, %u waiting\n", dxr12 ? "yes" : "no",
            (status.features&TGPT_FEATURE_SER) ? "on" : "off", status.micromapMeshes, status.micromapsWaiting);
        // What the driver itself reports on a device of the game's GPU (tier 1.2 and Shader Model 6.9): a runtime can't add
        // what the driver lacks (a Radeon RX 7000 may run tier 1.1 only), and the tracer must say DXR 1.2 exactly there.
        bool driverDxr12=false;
        {
            ComPtr<IDXGIDevice> dxgiDevice; ComPtr<IDXGIAdapter> adapter; ComPtr<ID3D12Device> d12;
            const HMODULE runtime=GetModuleHandleW(L"d3d12.dll");
            auto create=runtime ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(runtime, "D3D12CreateDevice")) : nullptr;
            if(create && SUCCEEDED(device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
               SUCCEEDED(create(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d12)))) {
                D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{}; D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL(0x69)};
                driverDxr12=SUCCEEDED(d12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5))) && int(o5.RaytracingTier)>=12 &&
                    SUCCEEDED(d12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) && int(sm.HighestShaderModel)>=0x69;
            }
        }
        std::printf("      the driver reports DirectX Raytracing 1.2 with Shader Model 6.9: %s\n", driverDxr12 ? "yes" : "no");
        if(agility) expect(dxr12==driverDxr12, driverDxr12 ? "TG Path Tracing's Agility SDK runtime brings DirectX Raytracing 1.2 (Shader Model 6.9)"
                                                          : "no DirectX Raytracing 1.2 where the driver has none (the 1.1 passes serve)");
        // The quad and the card mesh, and the cube since the slab test gave it an alpha-tested material.
        if(dxr12) expect(status.micromapMeshes==3 && status.micromapsWaiting==0, "every mesh with alpha-tested instances has opacity micromaps");
        std::vector<float> both[2]; uint32_t used[2]{};
        for(int k=0;k<2;++k) {
            for(uint32_t i=0;i<16;++i) run(TGPT_VIEW_NONE, cam, cam, i, k==0 ? TGPT_OPTION_NO_MICROMAPS : 0u);
            both[k]=readback();
            ReadStatus(&status); used[k]=status.micromapInstances;
        }
        save(both[0], "13-micromaps-off.png", true); save(both[1], "14-micromaps-on.png", true);
        double sum=0, most=0;
        for(size_t i=0;i<both[0].size();++i) { const double d=std::fabs(double(both[0][i])-double(both[1][i])); sum+=d; most=std::max(most, d); }
        std::printf("      instances tracing with micromaps: %u off, %u on; image difference: mean %.6f, largest %.5f\n", used[0], used[1], sum/double(both[0].size()), most);
        if(dxr12) expect(used[0]==0 && used[1]==2, "the leaf and the leaf card trace with micromaps, the rest without");
        expect(most<1e-3, "micromaps leave every pixel as the alpha test alone makes it");
    }
    // AMD FSR Ray Regeneration runs on a Radeon RX 9000 or newer only: elsewhere a frame asking for it gets NRD, which
    // the game asks for alongside it (Settings.Options: NRD for every choice but None).
    {
        ReadStatus(&status);
        const bool available=(status.features&TGPT_FEATURE_RAY_REGENERATION)!=0;
        for(uint32_t i=0;i<8;++i) run(TGPT_VIEW_NONE, cam, cam, 900+i, TGPT_OPTION_RAY_REGENERATION|TGPT_OPTION_NRD);
        const auto px=readback();
        bool finiteRrg=true; for(float v:px) if(!std::isfinite(v)) { finiteRrg=false; break; }
        ReadStatus(&status);
        std::printf("      AMD Ray Regeneration: %s on this GPU; the last frame %s\n", available ? "available" : "not available",
            (status.features&TGPT_FEATURE_RAY_REGENERATION_ACTIVE) ? "was denoised by it" : (status.features&TGPT_FEATURE_NRD_ACTIVE) ? "had NRD" : "had the tracer's own filters");
        expect(finiteRrg, "frames asking for AMD Ray Regeneration are finite");
        if(!available) expect(!(status.features&TGPT_FEATURE_RAY_REGENERATION_ACTIVE) &&
                              (!(status.features&TGPT_FEATURE_NRD) || (status.features&TGPT_FEATURE_NRD_ACTIVE)), "without AMD Ray Regeneration NRD serves");
        if(available) expect(!(status.features&TGPT_FEATURE_NRD_ACTIVE), "where AMD Ray Regeneration runs, NRD stands aside");
    }
    ReadStatus(&status);
    // Reflection Boost: the white floor (smoothness 0.6) becomes nearly a mirror, reflecting the room's colored walls.
    {
        auto floorColor=[&]() {
            for(uint32_t i=0;i<32;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 4000+uint32_t(reflectionBoost*100)+i : 0);
            const auto px=readback(); double r=0, g=0; int n=0;
            for(UINT y=20;y<140;++y) for(UINT x=200;x<1080;++x) { const size_t i=(size_t(y)*W+x)*3; r+=px[i]; g+=px[i+1]; ++n; }
            return std::array<double, 2>{r/n, g/n};
        };
        reflectionBoost=0; const auto plain=floorColor();
        reflectionBoost=1; const auto boosted=floorColor();
        reflectionBoost=0;
        std::printf("      Reflection Boost on the floor: red %.4f green %.4f as made, %.4f %.4f boosted\n", plain[0], plain[1], boosted[0], boosted[1]);
        expect(std::fabs(boosted[0]-plain[0])+std::fabs(boosted[1]-plain[1])>0.01*(plain[0]+plain[1]), "Reflection Boost makes the floor reflect the room");
    }
    // A smooth surface's highlight carries its light's energy: a lamp's direct specular light on the floor, added up over the
    // image (the Specular view without the reflection pass: the direct light's alone, undenoised), is about as much with the
    // floor smooth (roughness 0.05, as Reflection Boost makes it) as rough (0.4): the lobe narrows and brightens alike. With
    // a 1e-7 in the GGX distribution's denominator, the smooth floor's came to a few hundredths of the rough one's.
    {
        const bool keepSun=sunOn; const float keepSky=skyIntensity;
        sunOn=false; skyIntensity=1e-6f;
        TGPT_Light lamp{}; lamp.position[0]=0; lamp.position[1]=1.2f; lamp.position[2]=0; lamp.type=0;
        lamp.color[0]=lamp.color[1]=lamp.color[2]=20.f; lamp.range=12; lamp.radius=0.05f; lamp.id=610;
        expect(SetLights(&lamp, 1)==0, "a lamp over the floor");
        auto highlight=[&](float roughness) {
            const float keep=objects[0].roughness; objects[0].roughness=roughness;
            double sum=0;
            for(uint32_t i=0;i<40;++i) {
                run(TGPT_VIEW_SPECULAR, cam, cam, i ? 7000+i : 0, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPECULAR);
                if(i<8) continue;
                const auto px=readback();
                for(UINT y=0;y<H/2;++y) for(UINT x=0;x<W;++x) { const size_t j=(size_t(y)*W+x)*3; sum+=0.2126*px[j]+0.7152*px[j+1]+0.0722*px[j+2]; }
            }
            objects[0].roughness=keep;
            return sum;
        };
        const double smooth=highlight(0.05f), rough=highlight(0.4f);
        std::printf("      a lamp's direct highlight on the floor, summed over the image's lower half: smooth (0.05) %.3f, rough (0.4) %.3f (%.3f)\n",
            smooth, rough, smooth/std::max(rough, 1e-12));
        expect(rough>0 && smooth>0.6*rough && smooth<1.6*rough, "a smooth surface's highlight carries its light's energy, as a rough one's does");
        expect(SetLights(&light, 1)==0, "one light again");
        sunOn=keepSun; skyIntensity=keepSky;
    }
    // Mask maps (TGPT_RegisterMaskTexture): metallic and smoothness texel by texel where rays meet a material, as HDRP Lit's
    // metallic workflow has them: a metal has no diffuse color, and its base color is its specular color.
    {
        auto RegisterMaskTexture=reinterpret_cast<decltype(&TGPT_RegisterMaskTexture)>(GetProcAddress(pt, "TGPT_RegisterMaskTexture"));
        expect(RegisterMaskTexture!=nullptr, "mask map export");
        // 128x128, its left half one texel and its right half another (bytes red, green, blue, alpha from the lowest). At
        // twice the tile's size the bake's samples fall on its texels' centres: the halves stay apart in the tile.
        auto texture128=[&](DXGI_FORMAT format, UINT bind, uint32_t left, uint32_t right) {
            std::vector<uint32_t> px(128*128);
            for(int y=0;y<128;++y) for(int x=0;x<128;++x) px[size_t(y)*128+x]=x<64 ? left : right;
            D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=128; d.MipLevels=1; d.ArraySize=1; d.Format=format; d.SampleDesc.Count=1; d.BindFlags=bind;
            const D3D11_SUBRESOURCE_DATA init{px.data(), 128*4, 0};
            ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
        };
        // Keys as the plugin makes them: the texture's and the channels it is read with.
        auto maskKey=[](uint64_t id, uint32_t channels) { return id|(uint64_t(channels)<<32); };
        auto pointer=[](const ComPtr<ID3D11Texture2D>& t) { return uint64_t(uintptr_t(t.Get())); };
        std::vector<TGPT_Material> masked=materials; // the harness's materials and these tests' (the harness's are set again after)
        const uint32_t keepRays=rays;
        // A traced-only panel stands on the floor, lit by a lamp in front of it, its mask map (a linear texture, typeless, which
        // a color's view would read as sRGB) metallic on its left half: a path vertex, shaded diffusely, sends a metal's
        // reflected light on as diffuse light of the same energy (its specular color, which for a metal is its base color), so
        // the metallic half bounces onto the floor and shows in the floor's reflection as the plain panel's does, not black
        // (as its diffuse color alone made it), and so does its right half. Every variant traces the same frame indices with one
        // ray a pixel and none of the estimator's own changes (reuse, clamps, boiling filters, denoiser), so only the panel's
        // material differs and the light it adds is linear in its color: P plain, M masked, L and R black on its left or right
        // half, ML masked and black on its left half (M - ML: what the masked left half adds; ML - L: how the masked right half
        // differs).
        if(RegisterMaskTexture) {
            // Metallic 1 on the left and 0 on the right, occlusion 1, smoothness 0.5.
            auto metalLeft=texture128(DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_BIND_SHADER_RESOURCE, 0x8000FFFFu, 0x8000FF00u);
            auto blackLeft=texture128(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D11_BIND_SHADER_RESOURCE, 0xFF000000u, 0xFFFFFFFFu);
            auto blackRight=texture128(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D11_BIND_SHADER_RESOURCE, 0xFFFFFFFFu, 0xFF000000u);
            const int maskTile=RegisterMaskTexture(maskKey(60, 3u<<2), pointer(metalLeft), 3u<<2); // metallic red, smoothness alpha
            const int leftTile=RegisterTexture(61, pointer(blackLeft)), rightTile=RegisterTexture(62, pointer(blackRight));
            expect(maskTile>=0 && leftTile>=0 && rightTile>=0 && maskTile!=tile && maskTile!=leftTile && maskTile!=rightTile, "a mask map registered, a tile of the atlas of its own");
            // A warm metal's color (for a metal, HDRP Lit's base color is its specular color).
            TGPT_Material plain=materials[0]; plain.baseColor[0]=0.9f; plain.baseColor[1]=0.75f; plain.baseColor[2]=0.4f;
            const uint32_t first=uint32_t(masked.size());
            masked.push_back(plain);                                                                    // P
            TGPT_Material m=plain; m.flags|=TGPT_MATERIAL_MASK_TILE; m.maskTile=maskTile; m.maskRemap=0xFF00FF00u; // metallic and smoothness 0 to 1
            masked.push_back(m);                                                                        // M
            TGPT_Material l=plain; l.albedoTile=leftTile; masked.push_back(l);                          // L
            TGPT_Material r=plain; r.albedoTile=rightTile; masked.push_back(r);                         // R
            TGPT_Material ml=m; ml.albedoTile=leftTile; masked.push_back(ml);                           // ML
            expect(SetMaterials(0, masked.data(), uint32_t(masked.size()))==0, "materials with a mask map set");
            // x 2.8 to 4.8 (the mask's left half below 3.8), 2 m tall, facing -z; the lamp 1.5 m in front of it, the sun off.
            const M4 panelWorld=mul(translate_scale({3.8f, 1.02f, 3.f}, {2.f, 2.f, 1.f}), rot_x(-pi/2));
            TGPT_Light lamp{}; lamp.position[0]=3.8f; lamp.position[1]=1.f; lamp.position[2]=1.5f; lamp.type=0;
            lamp.color[0]=lamp.color[1]=lamp.color[2]=30.f; lamp.range=12; lamp.radius=0.05f; lamp.id=500;
            expect(SetLights(&lamp, 1)==0, "a lamp in front of the panel");
            sunOn=false; rays=1;
            auto image=[&](uint32_t material, uint32_t view, uint32_t options) {
                auto list=instances; auto listIndices=indices;
                TGPT_Instance p{}; for(int row=0;row<3;++row) for(int c=0;c<4;++c) p.transform[row*4+c]=panelWorld.m[row][c];
                p.mesh=cardHandle; p.materialBase=uint32_t(listIndices.size()); p.mask=0xFF;
                listIndices.push_back(material); list.push_back(p);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with the panel");
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<10;++i) {
                    run(view, cam, cam, i ? 5000+i : 0, options);
                    if(i>=2) { const auto px=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=px[j]/8.0; } // the composite shows the frame before
                }
                return acc;
            };
            const uint32_t honest=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS;
            struct Part { const char* name; uint32_t view, options; float boost; const char* images[2]; };
            const Part parts[2]={{"bounced light", TGPT_VIEW_INDIRECT, honest|TGPT_OPTION_NO_SPECULAR, 0.f, {"35-panel-bounce-plain.png", "36-panel-bounce-metal-left.png"}},
                                 {"reflection (a mirror floor)", TGPT_VIEW_SPECULAR, honest, 1.f, {"37-panel-reflection-plain.png", "38-panel-reflection-metal-left.png"}}};
            for(const auto& part:parts) {
                reflectionBoost=part.boost;
                const auto P=image(first, part.view, part.options), M=image(first+1, part.view, part.options), L=image(first+2, part.view, part.options);
                const auto R=image(first+3, part.view, part.options), ML=image(first+4, part.view, part.options);
                auto toFloat=[](const std::vector<double>& v) { return std::vector<float>(v.begin(), v.end()); };
                save(toFloat(P), part.images[0], true); save(toFloat(M), part.images[1], true);
                // The plain left half's light (P - L) against the masked left half's (M - ML), and the plain right half's (P - R)
                // against the difference the mask makes to it (ML - L).
                double left=0, leftOff=0, right=0, rightOff=0, total=0;
                for(size_t j=0;j<P.size();++j) {
                    left+=std::fabs(P[j]-L[j]); leftOff+=std::fabs(M[j]-ML[j]);
                    right+=std::fabs(P[j]-R[j]); rightOff+=std::fabs(ML[j]-L[j]);
                    total+=P[j];
                }
                std::printf("      a panel metallic on its left half, %s: the left half's light %.4f of the image's, the masked left half's %.4f of it; "
                            "the right half's %.4f, the masked right half off it by %.4f\n", part.name, left/std::max(total, 1e-12), leftOff/std::max(left, 1e-12),
                            right/std::max(total, 1e-12), rightOff/std::max(right, 1e-12));
                expect(left>0.002*total && std::fabs(leftOff-left)<0.02*left, part.view==TGPT_VIEW_INDIRECT ? "the metallic half of a mask map bounces its specular color's light"
                                                                                                        : "the metallic half of a mask map is reflected with its specular color, not black");
                expect(right>0.002*total && rightOff<0.02*right, part.view==TGPT_VIEW_INDIRECT ? "the dielectric half of a mask map bounces its light as before"
                                                                                            : "the dielectric half of a mask map is reflected as before");
            }
            reflectionBoost=0; sunOn=true; rays=keepRays;
            expect(SetLights(&light, 1)==0, "one light again");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the panel");
        }
        // A surface HDRP draws forward takes the traced material's finish from its mask map texel by texel: its roughness from
        // the mask's smoothness (read linear, from the channel its shader keeps it in, remapped), and a metal's specular color
        // from its base color, with no diffuse color. The box drawn forward, seen face on: the tracer's roughness under the
        // screen's centre (the capture's pick), and the face's albedo and reflections. A mask map whose tile was never baked
        // (its texture can't be read; so is any before its bake) reads as the remaps' low ends: as a rule a dielectric at its
        // roughest.
        if(RegisterMaskTexture) {
            auto finish=texture128(DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_BIND_SHADER_RESOURCE, 0xFF80FFFFu, 0xFF80FFFFu); // metallic 1, blue 0.5, alpha 1
            auto unreadable=texture128(DXGI_FORMAT_R8G8B8A8_TYPELESS, 0, 0u, 0u);
            const int alphaSmooth=RegisterMaskTexture(maskKey(63, 3u<<2), pointer(finish), 3u<<2), blueSmooth=RegisterMaskTexture(maskKey(63, 2u<<2), pointer(finish), 2u<<2);
            const int unbaked=RegisterMaskTexture(maskKey(64, 3u<<2), pointer(unreadable), 3u<<2);
            expect(alphaSmooth>=0 && blueSmooth>=0 && unbaked>=0 && alphaSmooth!=blueSmooth && RegisterMaskTexture(maskKey(63, 3u<<2), pointer(finish), 3u<<2)==alphaSmooth,
                   "a mask map has a tile for each way it is read, once");
            auto finishOf=[&](int maskTile, uint32_t remap) { TGPT_Material f=materials[3]; f.flags|=TGPT_MATERIAL_MASK_TILE; f.maskTile=maskTile; f.maskRemap=remap; return f; };
            const uint32_t first=uint32_t(masked.size());
            masked.push_back(finishOf(alphaSmooth, 0xFF00FF00u)); // metal, smoothness 1 (alpha)
            masked.push_back(finishOf(blueSmooth, 0xFF00FF00u));  // metal, smoothness 0.5 (blue)
            masked.push_back(finishOf(alphaSmooth, 0x9933FF00u)); // metal, smoothness 1 remapped into 0.2 to 0.6: 0.6
            masked.push_back(finishOf(alphaSmooth, 0xFF000000u)); // dielectric (metallic 0 to 0), smoothness 1
            masked.push_back(finishOf(unbaked, 0xE666FF40u));     // not baked: metallic 0.25 to 1, smoothness 0.4 to 0.9
            expect(SetMaterials(0, masked.data(), uint32_t(masked.size()))==0, "materials with a mask map set");
            const Camera faceOn=make_camera({1.5f, 1.f, -6.f}, {1.5f, 1.f, 0.f}, 60.f, float(W)/float(H));
            struct Seen { uint32_t roughness=0; bool forward=false; double albedo=0, specular=0; };
            auto look=[&](uint32_t material) {
                auto listIndices=indices; listIndices[instances[5].materialBase]=material; // the box's sides
                expect(SetScene(instances.data(), uint32_t(instances.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the box's sides in a finish");
                objects[5].forward=true;
                Seen s;
                for(uint32_t i=0;i<6;++i) run(TGPT_VIEW_ALBEDO, faceOn, faceOn, i ? 5100+i : 0, TGPT_OPTION_NO_DENOISER);
                s.albedo=region(readback(), 590, 310, 690, 410);
                ReadStatus(&status); s.roughness=(status.pickSurface>>16)&0xFFu; s.forward=(status.pickSurface&8u)!=0;
                for(uint32_t i=0;i<10;++i) {
                    run(TGPT_VIEW_SPECULAR, faceOn, faceOn, i ? 5200+i : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=6) s.specular+=region(readback(), 590, 310, 690, 410)/4.0;
                }
                objects[5].forward=false;
                return s;
            };
            const Seen metal=look(first), blue=look(first+1), remapped=look(first+2), dielectric=look(first+3), notBaked=look(first+4);
            std::printf("      the box drawn forward in a mask map's finish: roughness (of 255) %u metal, %u smoothness from blue, %u remapped, %u dielectric, %u not baked; "
                        "albedo %.4f metal, %.4f dielectric, %.4f not baked; reflections %.4f metal, %.4f dielectric\n", metal.roughness, blue.roughness, remapped.roughness,
                        dielectric.roughness, notBaked.roughness, metal.albedo, dielectric.albedo, notBaked.albedo, metal.specular, dielectric.specular);
            expect(metal.forward && metal.roughness<=2 && std::abs(int(blue.roughness)-127)<=2 && std::abs(int(remapped.roughness)-102)<=2,
                   "a surface drawn forward takes its roughness from its mask map: read linear, from its shader's channel, remapped");
            expect(dielectric.albedo>0.05 && metal.albedo<0.02*dielectric.albedo, "a metal drawn forward has no diffuse color");
            expect(dielectric.specular>0 && metal.specular>4.0*dielectric.specular, "a metal drawn forward reflects with its base color as its specular color");
            // Its low ends are not the material's constants (smoothness 0.3, metallic 0): roughness 0.6, three quarters of its color.
            expect(std::abs(int(notBaked.roughness)-153)<=2 && std::fabs(notBaked.albedo-0.749*dielectric.albedo)<0.03*dielectric.albedo,
                   "a mask map not baked yet reads as its remaps' low ends: the least metal at its roughest");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene set");
        }
        expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "materials set");
    }
    // Characters: a box whose vertices live in a structured buffer laid out like the game's skinning output (a
    // float3 position and a packed normal and tangent, 20 bytes a vertex, at an offset in a shared buffer) and are
    // written again every frame. Traced, it casts a shadow in the sun, and the shadow follows the vertices.
    {
        struct SkinVertex { float x, y, z; uint32_t packed[2]; };
        static_assert(sizeof(SkinVertex)==20, "the game's compressed vertex");
        const uint32_t first=16, count=uint32_t(cube.size());
        std::vector<SkinVertex> verts(first+count, SkinVertex{});
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=UINT(verts.size()*sizeof(SkinVertex)); bd.Usage=D3D11_USAGE_DEFAULT;
        bd.BindFlags=D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride=sizeof(SkinVertex);
        ComPtr<ID3D11Buffer> skinning; device->CreateBuffer(&bd, nullptr, &skinning);
        expect(skinning!=nullptr, "a skinning output buffer");
        auto pose=[&](V3 c) {
            for(uint32_t i=0;i<count;++i) verts[first+i]=SkinVertex{c.x+cube[i].px*0.8f, c.y+cube[i].py*0.8f, c.z+cube[i].pz*0.8f, {0, 0}};
            context->UpdateSubresource(skinning.Get(), 0, nullptr, verts.data(), 0, 0);
        };
        std::vector<uint32_t> indices(sides.begin(), sides.end()); indices.insert(indices.end(), top.begin(), top.end());
        const uint32_t starts[2]={0, uint32_t(sides.size())};
        TGPT_CpuMeshDesc md{sizeof(TGPT_CpuMeshDesc), TGPT_ABI}; md.key=77; md.indices=indices.data(); md.vertexCount=count; md.indexCount=uint32_t(indices.size());
        const int handle=RegisterSkinnedMesh ? RegisterSkinnedMesh(&md, starts, 2) : -100;
        expect(handle>0, "a character's triangles registered");
        TGPT_SkinnedInstance si{}; si.transform[0]=si.transform[5]=si.transform[10]=1.f;
        si.mesh=handle; si.firstVertex=first; si.vertexCount=count; si.submeshCount=2; si.materials[0]=si.materials[1]=0;
        // Measured with the Bounces setting's paths alone: the sum is of every pixel the character darkens, bounced light
        // included, and a green cube with holes is a cavity that traps and absorbs light, more with every bounce followed
        // (four bounces without the radiance cache darken the room 0.94 as much as a solid green cube, two 0.72; the cache,
        // every bounce, 0.98), which is not what these checks are about: rays through its cut-outs.
        auto frames=[&](bool character, uint32_t base) {
            std::vector<float> px;
            for(uint32_t i=0;i<40;++i) {
                if(character) SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
                else SetSkinned(0, 0, nullptr, 0);
                run(TGPT_VIEW_NONE, cam, cam, i ? base+i : 0, TGPT_OPTION_NO_RADIANCE_CACHE);
            }
            return readback();
        };
        const V3 at{-1.5f, 1.2f, 4.5f}, moved{-0.2f, 1.2f, 4.5f};
        pose(at); frames(true, 3000); // the mesh is converted and built over the first frames
        pose(at); const auto with=frames(true, 3100);
        const auto without=frames(false, 3200);
        pose(moved); const auto shifted=frames(true, 3300);
        save(with, "27-character-shadow.png", true);
        auto shadow=[&](const std::vector<float>& a, double& centroidX) {
            double sum=0, sx=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const size_t i=(size_t(y)*W+x)*3;
                const double d=(0.2126*without[i]+0.7152*without[i+1]+0.0722*without[i+2])-(0.2126*a[i]+0.7152*a[i+1]+0.0722*a[i+2]);
                if(d>0.01) { sum+=d; sx+=d*x; }
            }
            centroidX=sum>0 ? sx/sum : 0; return sum;
        };
        double cx1=0, cx2=0;
        const double s1=shadow(with, cx1), s2=shadow(shifted, cx2);
        ReadStatus(&status);
        std::printf("      character shadow: %.1f (centroid x %.0f), moved 1.3 m: %.1f (centroid x %.0f)\n", s1, cx1, s2, cx2);
        expect(s1>20.0, "a traced character casts a shadow");
        expect(cx2>cx1+20.0, "the character's shadow follows its vertices from one frame to the next");
        // Its parts are alpha-tested like the world's: with the plugin's invisible material it casts no shadow (the
        // instance is traced non-opaque), and registered with texture coordinates and the checkered leaf material
        // (half its texels transparent) it casts part of one, through the cut-outs.
        pose(at);
        si.materials[0]=si.materials[1]=7; const auto invisible=frames(true, 3400);
        si.materials[0]=si.materials[1]=8; const auto cameraOnly=frames(true, 3450);
        si.materials[0]=si.materials[1]=9; const auto greyGlass=frames(true, 3470);
        si.materials[0]=si.materials[1]=10; const auto clearGlass=frames(true, 3490);
        std::vector<float> uvs(size_t(count)*2); for(uint32_t i=0;i<count;++i) { uvs[i*2]=cube[i].u; uvs[i*2+1]=cube[i].v; }
        TGPT_CpuMeshDesc cut=md; cut.key=78; cut.uvs=uvs.data();
        const int cutHandle=RegisterSkinnedMesh ? RegisterSkinnedMesh(&cut, starts, 2) : -100;
        expect(cutHandle>0, "a character's triangles registered with texture coordinates");
        si.mesh=cutHandle; si.materials[0]=si.materials[1]=6;
        frames(true, 3500); const auto cutOut=frames(true, 3600); // converted and built over the first frames
        si.mesh=handle; si.materials[0]=si.materials[1]=0;
        const auto again=frames(false, 3700); // no character again: how much two runs differ by noise alone
        double cx0=0, cx3=0, cx4=0;
        const double s0=shadow(again, cx0), s3=shadow(invisible, cx3), s4=shadow(cutOut, cx4);
        save(cutOut, "30-character-cut-out-shadow.png", true);
        std::printf("      character with the invisible material: shadow %.1f (noise between runs %.1f); cut out by a checkered texture: %.1f (solid %.1f)\n", s3, s0, s4, s1);
        expect(s3<s0*1.5+s1*0.03, "a character's invisible parts cast no shadow");
        double cx5=0; const double s5=shadow(cameraOnly, cx5);
        std::printf("      character seen by camera rays only (the eyes): shadow %.1f\n", s5);
        expect(s5<s0*1.5+s1*0.03, "a surface seen by camera rays only casts no shadow");
        double cx6=0, cx7=0; const double s6=shadow(greyGlass, cx6), s7=shadow(clearGlass, cx7);
        std::printf("      glass: grey (luminance 0.36) shadow %.1f, %.2f of a solid one's (0.64 expected); clear %.1f\n", s6, s6/std::max(s1, 1e-6), s7);
        expect(s6>s1*0.45 && s6<s1*0.85, "grey glass lets light through as bright as its color (its shadow about 0.64 of a solid one's)");
        expect(s7<s0*1.5+s1*0.03, "clear glass casts no shadow");
        expect(s4>s1*0.15 && s4<s1*0.85, "a character's cut-outs let light through, texel by texel");
        SetSkinned(0, 0, nullptr, 0);
    }
    // Dark crevices: sand under a canvas stretched close over it against a boat's hull, as under the lining of a beached boat
    // where the gap looked black in daylight. The canvas (0.4 m over the sand, 1.8 m deep to the hull, 6 m long, open at the
    // front and both ends; with a skirt, only a 12 cm slit is left at the front's foot) is in the traced scene only, so the
    // camera looks down at the sand under it: bands at the front edge, the middle and by the hull (1.35 to 1.7 m in), against
    // the sunlit sand in front. TGPT_CREVICES=1 runs the study: the bounced light against an honest reference (the plain
    // path tracer, no reuse, clamps or boiling filter, 8 rays a pixel over many frames), each of ReSTIR's changes to what it
    // estimates undone in turn, 1 to 4 bounces, the sky alone, and the denoisers.
    {
        const bool study=std::getenv("TGPT_CREVICES")!=nullptr;
        // Traced sand, canvas and hull wood (luminance about 0.4 each), drawn white: the bands are measured in the light views
        // (the light a white surface gets), where the drawn color doesn't enter.
        auto pocketMaterials=materials;
        auto add=[&](float r, float g, float b) { TGPT_Material m=materials[0]; m.baseColor[0]=r; m.baseColor[1]=g; m.baseColor[2]=b; pocketMaterials.push_back(m); return int(pocketMaterials.size()-1); };
        const int sand=add(0.5f, 0.42f, 0.28f), cloth=add(0.45f, 0.4f, 0.33f), wood=add(0.5f, 0.38f, 0.22f);
        expect(SetMaterials(0, pocketMaterials.data(), uint32_t(pocketMaterials.size()))==0, "sand, canvas and hull materials set");
        const M4 groundWorld=translate_scale({0, 0, 0}, {40, 1, 40});
        const M4 hullWorld=mul(translate_scale({0, 0.6f, 1.f}, {8, 1.2f, 1}), rot_x(-pi/2));        // facing -z, the pocket
        const M4 canvasWorld=mul(translate_scale({0, 0.4f, 0.1f}, {6, 1, 1.8f}), rot_x(pi));        // facing down, z -0.8..1
        const M4 skirtWorld=mul(translate_scale({0, 0.26f, -0.8f}, {6, 0.28f, 1}), rot_x(pi/2));    // facing +z, y 0.12..0.4
        const auto keepObjects=objects;
        objects={Object{0, groundWorld, {sand}, false, false, 0, false, 0}, Object{0, hullWorld, {wood}, false, false, 0, false, 0}};
        auto pocketScene=[&](bool skirt) {
            std::vector<TGPT_Instance> list; std::vector<uint32_t> listIndices;
            auto place=[&](const M4& w, int material) {
                TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=w.m[r][c];
                in.mesh=quadHandle; in.materialBase=uint32_t(listIndices.size()); in.mask=0xFF;
                listIndices.push_back(uint32_t(material)); list.push_back(in);
            };
            place(groundWorld, sand); place(hullWorld, wood); place(canvasWorld, cloth);
            if(skirt) place(skirtWorld, cloth);
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, skirt ? "the canvas with a skirt set" : "the canvas set");
        };
        expect(SetLights(nullptr, 0)==0, "no lamps: the sun and the sky alone");
        const Camera pcam=make_camera({0, 2.4f, -3.2f}, {0, 0, 0.1f}, 60.f, float(W)/float(H));
        // Each pixel's point on the sand (its ray from the camera to the ground plane) decides its band.
        enum { kOpen, kEdge, kMiddle, kDeep, kBands };
        const char* bandNames[kBands]={"sunlit sand", "front edge", "middle", "by the hull"};
        const float bandZ[kBands][2]={{-2.f, -1.3f}, {-0.75f, -0.45f}, {-0.15f, 0.15f}, {0.55f, 0.9f}};
        std::vector<int8_t> band(size_t(W)*H, -1); int bandCount[kBands]{};
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const float nx=(float(x)+0.5f)/float(W)*2.f-1.f, ny=1.f-(float(y)+0.5f)/float(H)*2.f;
            auto at=[&](float depth) { const float v[4]={nx, ny, depth, 1.f}; float r[4]{}; for(int i=0;i<4;++i) for(int k=0;k<4;++k) r[i]+=pcam.invViewProj.m[i][k]*v[k]; return V3{r[0]/r[3], r[1]/r[3], r[2]/r[3]}; };
            const V3 a=at(1.f), d=normalize(at(0.5f)-a);
            if(d.y>=-1e-4f) continue;
            const float t=-(pcam.position.y+a.y)/d.y, gx=pcam.position.x+a.x+d.x*t, gz=pcam.position.z+a.z+d.z*t;
            if(std::fabs(gx)>1.2f) continue;
            for(int b=0;b<kBands;++b) if(gz>=bandZ[b][0] && gz<=bandZ[b][1]) { band[size_t(y)*W+x]=int8_t(b); ++bandCount[b]; }
        }
        std::printf("      crevice bands (pixels): sunlit sand %d, front edge %d, middle %d, by the hull %d\n", bandCount[kOpen], bandCount[kEdge], bandCount[kMiddle], bandCount[kDeep]);
        expect(bandCount[kOpen]>2000 && bandCount[kEdge]>2000 && bandCount[kMiddle]>2000 && bandCount[kDeep]>2000, "each band of the pocket has its pixels");
        using Bands=std::array<double, kBands>;
        auto bandsOf=[&](const std::vector<float>& px) {
            Bands s{};
            for(size_t j=0;j<band.size();++j) if(band[j]>=0) s[size_t(band[j])]+=0.2126*px[j*3]+0.7152*px[j*3+1]+0.0722*px[j*3+2];
            for(int b=0;b<kBands;++b) s[size_t(b)]/=std::max(1, bandCount[b]);
            return s;
        };
        uint32_t seed=20000;
        // The bands' mean light in a view over frames (after 16 to settle: histories, and the composite's one frame of
        // delay), with its standard error from eight batches of frames. Without the radiance cache unless asked for: ReSTIR's
        // own biases are measured on the Bounces setting's paths, the reference's and ReSTIR's alike.
        uint32_t pocketCache=TGPT_OPTION_NO_RADIANCE_CACHE;
        auto measure=[&](uint32_t view, uint32_t options, uint32_t raysPerPixel, uint32_t frames, Bands& standardError, std::vector<double>* image=nullptr) {
            options|=pocketCache;
            const uint32_t keepRays=rays; rays=raysPerPixel;
            const uint32_t batches=8, per=std::max(1u, frames/batches);
            Bands sum{}, sum2{};
            for(uint32_t i=0;i<16;++i) run(view, pcam, pcam, i ? seed++ : 0, options);
            if(image) image->assign(size_t(W)*H*3, 0.0);
            for(uint32_t batch=0;batch<batches;++batch) {
                Bands mean{};
                for(uint32_t i=0;i<per;++i) {
                    run(view, pcam, pcam, seed++, options);
                    const auto px=readback();
                    const Bands b=bandsOf(px);
                    for(int k=0;k<kBands;++k) mean[size_t(k)]+=b[size_t(k)]/per;
                    if(image) for(size_t j=0;j<px.size();++j) (*image)[j]+=px[j]/double(per*batches);
                }
                for(int k=0;k<kBands;++k) { sum[size_t(k)]+=mean[size_t(k)]; sum2[size_t(k)]+=mean[size_t(k)]*mean[size_t(k)]; }
            }
            rays=keepRays;
            Bands mean{};
            for(int k=0;k<kBands;++k) {
                mean[size_t(k)]=sum[size_t(k)]/batches;
                standardError[size_t(k)]=std::sqrt(std::max(0.0, sum2[size_t(k)]/batches-mean[size_t(k)]*mean[size_t(k)])/(batches-1));
            }
            return mean;
        };
        auto saveScaled=[&](const std::vector<double>& v, const char* name, float gain) {
            std::vector<float> f(v.size()); for(size_t j=0;j<v.size();++j) f[j]=float(v[j]*gain);
            save(f, name, true);
        };
        const uint32_t plain=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL;
        const uint32_t honest=plain|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS;
        giHistory=8; // the plugin's (RTXDI's) bounced light history
        // Under the skirted canvas, ReSTIR's bounced light undenoised against the honest reference (2 bounces both), as it
        // ships and with the Crevice Light row (TGPT_OPTION_GI_VISIBILITY). As it ships, the front edge is 14 % short (RTXDI's
        // Basic normalization counts pixels further in for samples they don't see) and the middle 9 % too bright (histories
        // dropped at 30 frames restart where they were dark). With the row, measured with each of its three changes undone:
        // Basic normalization left the front edge 19 % short, the 30-frame limit the middle 16 % too bright, and dropping
        // outliers (without that limit) the middle 12 % and the sand by the hull 22 % short.
        {
            pocketScene(true);
            Bands referenceError{}, shippedError{}, rowError{};
            const Bands reference=measure(TGPT_VIEW_INDIRECT, honest, 8, 48, referenceError);
            const Bands shipped=measure(TGPT_VIEW_INDIRECT, TGPT_OPTION_NO_DENOISER, 1, 128, shippedError);
            std::vector<double> image;
            const Bands row=measure(TGPT_VIEW_INDIRECT, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_GI_VISIBILITY, 1, 128, rowError, &image);
            saveScaled(image, "crevices-light-indirect.png", 8.f);
            Bands ratio{}, before{};
            for(int k=0;k<kBands;++k) {
                ratio[size_t(k)]=reference[size_t(k)]>0 ? row[size_t(k)]/reference[size_t(k)] : 0.0;
                before[size_t(k)]=reference[size_t(k)]>0 ? shipped[size_t(k)]/reference[size_t(k)] : 0.0;
            }
            std::printf("      under a canvas close over sunlit sand, ReSTIR's bounced light against the plain path tracer's: sunlit sand %.3f, front edge %.3f, middle %.3f, by the hull %.3f with Crevice Light (%.3f, %.3f, %.3f, %.3f without)\n",
                ratio[kOpen], ratio[kEdge], ratio[kMiddle], ratio[kDeep], before[kOpen], before[kEdge], before[kMiddle], before[kDeep]);
            expect(std::fabs(ratio[kOpen]-1.0)<0.02 && std::fabs(ratio[kEdge]-1.0)<0.05,
                   "with Crevice Light, ReSTIR's bounced light reaches the opening of a dark pocket (reuse counts a pixel only for samples it sees)");
            expect(ratio[kMiddle]>0.92 && ratio[kMiddle]<1.05 && ratio[kDeep]>0.85 && ratio[kDeep]<1.05,
                   "with Crevice Light, ReSTIR's bounced light deep in a dark pocket is neither lost to the boiling filter nor inflated by restarted histories");
            // As it ships, with the radiance cache: two bounces ending on it against the honest reference's four (the study:
            // two bounces hold 53 to 57 % of the light deep under the skirted canvas, four 90 to 99 %).
            Bands fourError{}, cachedError{};
            bounces=4; const Bands four=measure(TGPT_VIEW_INDIRECT, honest, 8, 48, fourError); bounces=2;
            pocketCache=0; const Bands cached=measure(TGPT_VIEW_INDIRECT, TGPT_OPTION_NO_DENOISER, 1, 128, cachedError); pocketCache=TGPT_OPTION_NO_RADIANCE_CACHE;
            Bands withCache{}, withoutCache{};
            for(int k=0;k<kBands;++k) {
                withCache[size_t(k)]=four[size_t(k)]>0 ? cached[size_t(k)]/four[size_t(k)] : 0.0;
                withoutCache[size_t(k)]=four[size_t(k)]>0 ? shipped[size_t(k)]/four[size_t(k)] : 0.0;
            }
            std::printf("      under the canvas as it ships, against the honest reference with 4 bounces: front edge %.3f, middle %.3f, by the hull %.3f with the radiance cache (%.3f, %.3f, %.3f without)\n",
                withCache[kEdge], withCache[kMiddle], withCache[kDeep], withoutCache[kEdge], withoutCache[kMiddle], withoutCache[kDeep]);
            expect(withCache[kMiddle]>withoutCache[kMiddle]+0.1 && withCache[kDeep]>withoutCache[kDeep]+0.1 && withCache[kMiddle]>0.8 && withCache[kDeep]>0.8,
                   "the radiance cache brings the light deep in a dark pocket most of the way to four bounces'");
        }
        if(study) {
            struct Variant { const char* name; uint32_t options; };
            const Variant variants[]={
                {"ReSTIR as it ships", TGPT_OPTION_NO_DENOISER},
                {"  without the boiling filter", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_GI_BOILING},
                {"  without the clamps", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS},
                {"  with the exact Jacobian", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_EXACT_JACOBIAN},
                {"  with RTXDI's surface where rays start", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_OFFSET_RECEIVER},
                {"  without passing through near back faces", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_PASS_THROUGH},
                {"  without spatial reuse", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPATIAL},
                {"  without temporal reuse", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_TEMPORAL},
                {"  none of the three (clamps, filters, Jacobian limits)", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS|TGPT_OPTION_NO_BOILING|TGPT_OPTION_EXACT_JACOBIAN},
                {"no reuse, with the clamps and the boiling filter", plain},
                {"with Crevice Light", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_GI_VISIBILITY},
                {"  and without spatial reuse", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_GI_VISIBILITY|TGPT_OPTION_NO_SPATIAL},
                {"  and without temporal reuse", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_GI_VISIBILITY|TGPT_OPTION_NO_TEMPORAL},
                {"  and without the boiling filter", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_GI_VISIBILITY|TGPT_OPTION_NO_GI_BOILING},
                {"  and without the clamps", TGPT_OPTION_NO_DENOISER|TGPT_OPTION_GI_VISIBILITY|TGPT_OPTION_NO_CLAMPS}};
            auto line=[&](const char* scene, const char* name, const Bands& m, const Bands& e, const Bands& r, const Bands& re) {
                std::printf("      CREVICES %s: %-56s", scene, name);
                for(int k=0;k<kBands;++k) {
                    const double ratio=r[size_t(k)]>0 ? m[size_t(k)]/r[size_t(k)] : 0.0;
                    const double err=ratio*std::sqrt(std::pow(e[size_t(k)]/std::max(m[size_t(k)], 1e-12), 2)+std::pow(re[size_t(k)]/std::max(r[size_t(k)], 1e-12), 2));
                    std::printf(" %s %.3f+-%.3f", bandNames[k], ratio, err);
                }
                std::printf("\n");
            };
            for(int skirt=0;skirt<2;++skirt) {
                const char* scene=skirt ? "tunnel" : "roof";
                pocketScene(skirt!=0);
                Bands directError{}; bounces=2;
                const Bands direct=measure(TGPT_VIEW_DIRECT, honest, 8, 64, directError);
                // The honest reference at 1 to 4 bounces: the light the pocket gets against the sunlit sand's (its direct and
                // bounced light), in stops, and how much each bounce adds.
                Bands reference[5]{}, referenceError[5]{};
                std::vector<double> referenceImage, shippedImage;
                for(uint32_t n=1;n<=4;++n) { bounces=n; reference[n]=measure(TGPT_VIEW_INDIRECT, honest, 8, 128, referenceError[n], n==2 ? &referenceImage : nullptr); }
                const double sunlit=direct[kOpen]+reference[4][kOpen];
                for(uint32_t n=1;n<=4;++n) {
                    std::printf("      CREVICES %s: honest reference, %u bounce%s, direct and bounced light:", scene, n, n>1 ? "s" : " ");
                    for(int k=0;k<kBands;++k) {
                        const double total=reference[n][size_t(k)]+direct[size_t(k)];
                        std::printf(" %s %.5f (%+.2f EV, +-%.1f%%)", bandNames[k], total, std::log2(std::max(total, 1e-12)/sunlit),
                            100.0*referenceError[n][size_t(k)]/std::max(reference[n][size_t(k)], 1e-12));
                    }
                    std::printf("\n");
                }
                // The rest of the bounces, as a geometric series from the last two a bounce added.
                std::printf("      CREVICES %s: 2 bounces hold, of the series to infinity:", scene);
                for(int k=0;k<kBands;++k) {
                    const double d3=reference[3][size_t(k)]-reference[2][size_t(k)], d4=reference[4][size_t(k)]-reference[3][size_t(k)];
                    const double q=d3>0 ? std::clamp(d4/d3, 0.0, 0.95) : 0.0, all=reference[4][size_t(k)]+d4*q/(1.0-q);
                    std::printf(" %s %.3f (4 bounces %.3f, ratio %.2f)", bandNames[k], reference[2][size_t(k)]/std::max(all, 1e-12), reference[4][size_t(k)]/std::max(all, 1e-12), q);
                }
                std::printf("\n");
                std::printf("      CREVICES %s: direct light (sun), honest: sunlit sand %.5f, front edge %.5f, middle %.5f, by the hull %.5f\n", scene,
                    direct[kOpen], direct[kEdge], direct[kMiddle], direct[kDeep]);
                bounces=2;
                for(const auto& v:variants) {
                    Bands e{}; const Bands m=measure(TGPT_VIEW_INDIRECT, v.options, 1, 256, e, &v==variants ? &shippedImage : nullptr);
                    line(scene, v.name, m, e, reference[2], referenceError[2]);
                }
                // The sky alone (no sun): the light it gives straight into the pocket (1 bounce: every path vertex is dark), against
                // what it gives the open sand.
                sunOn=false;
                Bands skyError{}; bounces=1; const Bands sky=measure(TGPT_VIEW_INDIRECT, honest, 8, 64, skyError);
                sunOn=true; bounces=2;
                std::printf("      CREVICES %s: the sky seen straight from the sand, against the open sand's: front edge %.4f, middle %.4f, by the hull %.4f (open sand %.5f)\n",
                    scene, sky[kEdge]/std::max(sky[kOpen], 1e-12), sky[kMiddle]/std::max(sky[kOpen], 1e-12), sky[kDeep]/std::max(sky[kOpen], 1e-12), sky[kOpen]);
                // The denoisers: the image as shown, ReSTIR as it ships, undenoised against NRD and the built-in denoiser.
                Bands rawError{}, nrdError{}, ownError{};
                std::vector<double> nrdImage;
                const Bands raw=measure(TGPT_VIEW_NONE, TGPT_OPTION_NO_DENOISER, 1, 128, rawError);
                const Bands nrd=measure(TGPT_VIEW_NONE, TGPT_OPTION_NRD, 1, 128, nrdError, &nrdImage);
                const Bands own=measure(TGPT_VIEW_NONE, 0, 1, 128, ownError);
                line(scene, "image with NRD, against undenoised", nrd, nrdError, raw, rawError);
                line(scene, "image with the built-in denoiser, against undenoised", own, ownError, raw, rawError);
                saveScaled(nrdImage, skirt ? "crevices-tunnel-nrd.png" : "crevices-roof-nrd.png", 1.f);
                saveScaled(referenceImage, skirt ? "crevices-tunnel-reference.png" : "crevices-roof-reference.png", 4.f);
                saveScaled(shippedImage, skirt ? "crevices-tunnel-restir.png" : "crevices-roof-restir.png", 4.f);
            }
        }
        objects=keepObjects;
        expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room's scene again");
        expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "the room's materials again");
        expect(SetLights(&light, 1)==0, "one light again");
        bounces=2; sunOn=true; giHistory=0;
    }
    // Hair's shadow lets light through between its strands (TGPT_MATERIAL_STRANDS): a traced-only card in the sun whose
    // texture its strands cover half of (every other column of texels, so every alpha-tile texel is half covered) casts
    // about half a shadow, where cut out by the cutoff (a fifth of a texel covered) it cast a full one, as dark as a solid
    // card's. A mesh of its own, which the hair material is first given to, of 4x4 quads, so that micromaps for it would
    // settle every micro-triangle by the cutoff (a card of two triangles spans too many texels for any to be settled).
    {
        ComPtr<ID3D11Texture2D> strands;
        {
            const int size=512; std::vector<uint32_t> px(size*size);
            for(int y=0;y<size;++y) for(int x=0;x<size;++x) px[y*size+x]=(x&1) ? 0x00304060u : 0xFF304060u;
            D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.SampleDesc.Count=1;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init{px.data(), UINT(size*4), 0};
            device->CreateTexture2D(&d, &init, &strands);
        }
        const int strandsTile=RegisterTexture(46, uint64_t(uintptr_t(strands.Get()))), strandsAlpha=RegisterAlphaTexture(46, uint64_t(uintptr_t(strands.Get())), 0.5f);
        std::vector<Vertex> grid; std::vector<uint16_t> gridIndices;
        for(int j=0;j<=4;++j) for(int i=0;i<=4;++i) grid.push_back({float(i)/4.f-0.5f, 0, float(j)/4.f-0.5f, 0, 1, 0, float(i)/4.f, float(j)/4.f});
        for(int j=0;j<4;++j) for(int i=0;i<4;++i) {
            const uint16_t a=uint16_t(j*5+i), b=uint16_t(a+1), c=uint16_t(a+6), d=uint16_t(a+5);
            gridIndices.insert(gridIndices.end(), {a, c, b, a, d, c}); // wound as the card
        }
        MeshBuffers hairCard=make_mesh(grid, {gridIndices});
        const int hairHandle=register_mesh(hairCard, 6);
        expect(strandsTile>=0 && strandsAlpha>=0 && hairHandle>0, "a hair texture and card registered");
        // 12: hair as the plugin makes it (coverage in its alpha tile, a cutoff of 0.2, HDRP's for camera rays); 13: the same
        // texture as any alpha-tested material with an alpha tile (straw, a fringe: its coverage stops rays by chance too);
        // 14: the same cut out by the cutoff, without an alpha tile (its color tile's alpha, averaged over the columns).
        auto hair=materials; hair.resize(15);
        hair[12]=materials[0]; hair[12].albedoTile=strandsTile; hair[12].alphaTile=strandsAlpha; hair[12].alphaCutoff=0.2f;
        hair[12].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_ALPHA_TILE|TGPT_MATERIAL_HDRP_SHADED|TGPT_MATERIAL_STRANDS;
        hair[13]=hair[12]; hair[13].flags&=~(TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HDRP_SHADED); hair[13].alphaCutoff=0.5f;
        hair[14]=hair[12]; hair[14].flags&=~(TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_ALPHA_TILE); hair[14].alphaTile=-1;
        expect(SetMaterials(0, hair.data(), uint32_t(hair.size()))==0, "hair materials set");
        auto floorWith=[&](int material, uint32_t first) {
            auto list=instances; auto listIndices=indices;
            if(material>=0) {
                TGPT_Instance c{}; const M4 w=translate_scale({-4.2f, 3.f, 4.2f}, {1.2f, 1, 1.2f});
                for(int r=0;r<3;++r) for(int k=0;k<4;++k) c.transform[r*4+k]=w.m[r][k];
                c.mesh=hairHandle; c.materialBase=uint32_t(listIndices.size()); c.mask=0xFF; c.flags=TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(uint32_t(material)); list.push_back(c);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with a hair card");
            std::vector<double> acc(size_t(W)*H*3, 0.0);
            for(uint32_t i=0;i<40;++i) {
                run(TGPT_VIEW_DIRECT, cam, cam, i ? first+i : 0, TGPT_OPTION_NO_DENOISER);
                if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
            }
            return acc;
        };
        floorWith(12, 5000); // the card's mesh is read back and built over the first frames
        const auto none=floorWith(-1, 5100), soft=floorWith(12, 5100), tiled=floorWith(13, 5100), cut=floorWith(14, 5100);
        expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
        auto lum=[](const std::vector<double>& v, size_t j) { return 0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2]; };
        double full=0, half=0, tiledHalf=0; int shaded=0;
        for(size_t j=0;j+2<none.size();j+=3) {
            const double d=lum(none, j)-lum(cut, j);
            if(d<0.25*lum(none, j) || d<=0.0) continue; // the card's full shadow
            full+=d; half+=lum(none, j)-lum(soft, j); tiledHalf+=lum(none, j)-lum(tiled, j); ++shaded;
        }
        ReadStatus(&status);
        std::printf("      a hair card in the sun, its strands covering half its texels: %d pixels in its shadow cut out by the cutoff, "
                    "the strands' shadow %.3f as dark (0.5 expected), an alpha tile's %.3f; micromap meshes %u\n", shaded, full>0 ? half/full : 0.0,
                    full>0 ? tiledHalf/full : 0.0, status.micromapMeshes);
        expect(shaded>500, "a card cut out by the cutoff casts a full shadow");
        expect(full>0 && half/full>0.38 && half/full<0.62, "hair's shadow lets light through between its strands, as much as they leave uncovered");
        // What is finer than an alpha tile's texels (straw lying on a floor) lets the light between its parts through: cut out
        // at half a texel's coverage, the tavern's straw was a solid mat to rays, whose shadow blacked out the floor between it.
        expect(full>0 && tiledHalf/full>0.38 && tiledHalf/full<0.62, "an alpha tile's shadow lets light through as much as it leaves uncovered");
    }
    // Light through thin skin (TGPT_SetSkinTransmission): a quad drawn as skin (HDRP's transmission through a thick profile,
    // 1 mm thick by its GBuffer, the traced slab behind it 4 mm) with the sun behind it gets the sun's light through it, tinted
    // as the profile transmits it (red most); the same drawn quad over a traced block 30 cm thick (a head behind a face) gets
    // none; and the floor, drawn as the same thin skin, lit from the front as without any transmission. Every variant traced
    // with the same frame indices, the direct light alone, undenoised.
    {
        // Profile 7: a tint of (1, 0.5, 0.25) (HDRP's constant holds a quarter of it), no remap offset, 0 to 5 mm, Burley's
        // shape 1, 2 and 4 per millimetre. 1 mm through it lets e^-S + 3 e^-S/3 of a quarter of the tint through:
        // 0.629, 0.209, 0.051.
        float table[128]{};
        table[7*8]=0.25f; table[7*8+1]=0.125f; table[7*8+2]=0.0625f; table[7*8+3]=0.f;
        table[7*8+4]=1.f; table[7*8+5]=2.f; table[7*8+6]=4.f; table[7*8+7]=5.f;
        const V3 sunL=normalize({0.35f, 1.f, 0.55f});
        const double wrap=std::clamp((0.258819-(-sunL.z))/(1.258819*1.258819), 0.0, 1.0); // the quad faces -z
        const double through[3]={0.25*(std::exp(-1.0)+3*std::exp(-1.0/3)), 0.125*(std::exp(-2.0)+3*std::exp(-2.0/3)), 0.0625*(std::exp(-4.0)+3*std::exp(-4.0/3))};
        auto skinQuad=[&](float slab, bool transmits, bool floorSkin) {
            SetSkinTransmission(table, transmits ? 16u : 0u);
            thickProfiles=1u<<7;
            // Drawn: the quad facing the camera as skin (transmission and scattering, feature 3), profile 7 with a full mask
            // (PackFloatInt8bit: 15/255 x 1 + 16/255 x 7 = 127/255), 1 mm thick of 0 to 5 (51/255).
            Object quad{0, mul(translate_scale({-3.5f, 4.3f, 4.5f}, {1.5f, 1, 1.5f}), rot_x(-pi/2)), {0}, false, false, 3};
            quad.gb2g=51; quad.gb2b=127;
            objects.push_back(quad);
            const auto keepFloor=objects[0];
            if(floorSkin) { objects[0].feature=3; objects[0].gb2g=51; objects[0].gb2b=127; }
            // Traced: a box whose front face is the drawn quad, the given thickness deep.
            auto list=instances; auto listIndices=indices;
            TGPT_Instance box{}; const M4 w=translate_scale({-3.5f, 4.3f, 4.5f+slab*0.5f}, {1.7f, 1.2f, slab});
            for(int r=0;r<3;++r) for(int k=0;k<4;++k) box.transform[r*4+k]=w.m[r][k];
            box.mesh=cubeHandle; box.materialBase=uint32_t(listIndices.size()); box.mask=0xFF;
            listIndices.push_back(0); listIndices.push_back(0); list.push_back(box);
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with a slab behind the skin");
            std::vector<double> acc(size_t(W)*H*3, 0.0);
            for(uint32_t i=0;i<40;++i) {
                run(TGPT_VIEW_DIRECT, cam, cam, i ? 5300+i : 0, TGPT_OPTION_NO_DENOISER);
                if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]/32.0; }
            }
            objects.pop_back(); objects[0]=keepFloor; thickProfiles=0; SetSkinTransmission(nullptr, 0);
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
            return acc;
        };
        const auto solid=skinQuad(0.004f, false, true), thin=skinQuad(0.004f, true, true), block=skinQuad(0.3f, true, false), blockSolid=skinQuad(0.3f, false, false);
        // The quad's pixels: where the thin skin gained light (its whole face, lit from behind).
        double gained[3]{}, blocked=0, blockRef=0; int n=0;
        for(size_t j=0;j+2<thin.size();j+=3) {
            const double d=(0.2126*thin[j]+0.7152*thin[j+1]+0.0722*thin[j+2])-(0.2126*solid[j]+0.7152*solid[j+1]+0.0722*solid[j+2]);
            if(d<0.005) continue;
            for(int c=0;c<3;++c) gained[c]+=thin[j+c]-solid[j+c];
            blocked+=(0.2126*block[j]+0.7152*block[j+1]+0.0722*block[j+2])-(0.2126*blockSolid[j]+0.7152*blockSolid[j+1]+0.0722*blockSolid[j+2]);
            blockRef+=d; ++n;
        }
        for(double& g:gained) g/=std::max(n, 1);
        // Expected on the quad: the sun (6 lux) times the transmittance and HDRP's wrapped cosine, over pi, pre-exposed.
        const double expected=6.0*exposure*wrap/3.14159265*(0.2126*through[0]+0.7152*through[1]+0.0722*through[2]);
        const double level=0.2126*gained[0]+0.7152*gained[1]+0.0722*gained[2];
        // The floor, lit from the front: as without transmission.
        double floorThin=0, floorSolid=0;
        for(UINT y=150;y<300;++y) for(UINT x=150;x<1000;++x) {
            const size_t j=(size_t(y)*W+x)*3;
            floorThin+=0.2126*thin[j]+0.7152*thin[j+1]+0.0722*thin[j+2]; floorSolid+=0.2126*solid[j]+0.7152*solid[j+1]+0.0722*solid[j+2];
        }
        std::printf("      thin skin lit from behind: %d pixels gain light, %.4f (%.4f expected), red/green %.2f (%.2f expected); over a 30 cm block %.3f of that; "
                    "the floor lit from the front %.4f of its light without transmission\n", n, level, expected, gained[1]>0 ? gained[0]/gained[1] : 0.0,
                    through[0]/through[1], blockRef>0 ? blocked/blockRef : 0.0, floorSolid>0 ? floorThin/floorSolid : 0.0);
        expect(n>2000 && std::fabs(level/expected-1.0)<0.15, "thin skin lit from behind gets the light HDRP's transmission lets through");
        expect(gained[1]>0 && std::fabs(gained[0]/gained[1]/(through[0]/through[1])-1.0)<0.15, "the light through skin is tinted as its profile transmits it");
        expect(blockRef>0 && blocked<0.05*blockRef, "no light comes through skin with a thick body behind it");
        expect(floorSolid>0 && std::fabs(floorThin/floorSolid-1.0)<0.01, "skin lit from the front is lit as without transmission");
    }
    // Reflection Boost leaves skin as the game made it (HDRP's transmission through a solid profile): boosted, the game's
    // already glossy skin mirrored the dark chin and collar above a neck in sharp patches. Over the pixels the tracer takes
    // for skin with the floor drawn as skin (the Surfaces view's orange), the floor reflects as much with the boost at 1 as
    // at 0; drawn as it is, far more. Without the radiance cache, which carries on from one run to the next (still filling,
    // it made the second run's reflections brighter).
    {
        std::vector<size_t> skinPixels;
        {
            const auto keepFloor=objects[0];
            objects[0].feature=3; objects[0].gb2g=51; objects[0].gb2b=127; thickProfiles=1u<<7;
            // Twice: the composite shows a frame's traced output with the next frame.
            run(TGPT_VIEW_SURFACES, cam, cam, 0); run(TGPT_VIEW_SURFACES, cam, cam, 1);
            const auto v=readback();
            objects[0]=keepFloor; thickProfiles=0;
            for(size_t j=0;j+2<v.size();j+=3) if(v[j]>1.5f*v[j+1] && v[j+1]>2.f*v[j+2] && v[j]>0.02f) skinPixels.push_back(j);
        }
        auto floorSpecular=[&](bool skin, float boost) {
            const auto keepFloor=objects[0];
            if(skin) { objects[0].feature=3; objects[0].gb2g=51; objects[0].gb2b=127; thickProfiles=1u<<7; }
            reflectionBoost=boost;
            std::vector<double> acc(size_t(W)*H*3, 0.0);
            for(uint32_t i=0;i<24;++i) {
                run(TGPT_VIEW_SPECULAR, cam, cam, i ? 5400+i : 0, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE);
                if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
            }
            objects[0]=keepFloor; thickProfiles=0; reflectionBoost=0;
            double s=0;
            for(size_t j:skinPixels) s+=0.2126*acc[j]+0.7152*acc[j+1]+0.0722*acc[j+2];
            return s;
        };
        const double skin0=floorSpecular(true, 0.f), skin1=floorSpecular(true, 1.f), plain0=floorSpecular(false, 0.f), plain1=floorSpecular(false, 1.f);
        std::printf("      the floor drawn as skin (%zu pixels skin to the tracer), its reflections with Reflection Boost at 1 against 0: %.3f; "
                    "drawn as it is %.2f\n", skinPixels.size(), skin0>0 ? skin1/skin0 : 0.0, plain0>0 ? plain1/plain0 : 0.0);
        expect(skinPixels.size()>20000, "the floor drawn as skin is skin to the tracer");
        expect(skin0>0 && std::fabs(skin1/skin0-1.0)<0.01, "Reflection Boost leaves skin's reflections as the game made them");
        expect(plain0>0 && plain1>1.5*plain0, "Reflection Boost makes other surfaces reflect more");
    }
    // Traced shadows on what HDRP shades itself (pt_hdrp_shadow): a table top drawn forward, in the traced scene for camera
    // rays only and shaded by HDRP (as the eyes are), "HDRP's" color the sun's, with a lamp 1.5 m above it. A traced-only block
    // just under the lamp shadows the table: its color is darkened, to the share of its light the bounced light leaves;
    // without the block it stays as HDRP made it, and so it does with the block when HDRP shadows the lamp itself (no shadow
    // twice), with the option off, or with the table missing from the traced scene (a forward surface the tracer doesn't
    // know may be unlit: a glow card, an impostor). The sun, which the ceiling keeps off the table, darkens it the same way
    // unless HDRP shadows the sun itself.
    {
        auto hdrpMaterial=materials[8]; hdrpMaterial.flags|=TGPT_MATERIAL_HDRP_SHADED; // seen by camera rays only, as the eyes
        expect(SetMaterials(14, &hdrpMaterial, 1)==0, "a material HDRP shades");
        const Object table{0, translate_scale({-2.f, 1.f, -3.f}, {1.5f, 1, 1.5f}), {1}, false, false, 0, true}; // red: unlike the floor under it
        TGPT_Light lamp{}; lamp.position[0]=-2; lamp.position[1]=2.5f; lamp.position[2]=-3; lamp.type=0;
        lamp.color[0]=40; lamp.color[1]=22; lamp.color[2]=8; lamp.range=12; lamp.radius=0.03f; lamp.id=400;
        // The table's pixels, and "HDRP's" image of them: without the composite, the traced light changes nothing drawn forward.
        objects.push_back(table); raster(cam, cam); const auto hdrp=readback(); objects.pop_back();
        raster(cam, cam); const auto plain=readback();
        std::vector<size_t> tablePixels;
        for(size_t j=0;j+2<hdrp.size();j+=3) if(std::fabs(hdrp[j]-plain[j])+std::fabs(hdrp[j+1]-plain[j+1])>1e-3 && hdrp[j]>1e-3) tablePixels.push_back(j);
        auto share=[&](bool block, uint32_t lampFlags, bool sun, bool sunHdrp, uint32_t options, uint32_t first, bool traced=true) {
            objects.push_back(table);
            sunOn=sun; sunShadowedByHdrp=sunHdrp;
            TGPT_Light l=lamp; l.flags=lampFlags;
            expect(sun ? SetLights(nullptr, 0)==0 : SetLights(&l, 1)==0, "the lamp over the table");
            auto list=instances; auto listIndices=indices;
            if(traced) {
                TGPT_Instance top{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) top.transform[r*4+k]=table.world.m[r][k];
                top.mesh=quadHandle; top.materialBase=uint32_t(listIndices.size()); top.mask=0xFF; top.flags=TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(14); list.push_back(top);
            }
            if(block) {
                TGPT_Instance b{}; const M4 w=translate_scale({-2.f, 2.1f, -3.f}, {0.6f, 0.6f, 0.6f});
                for(int r=0;r<3;++r) for(int k=0;k<4;++k) b.transform[r*4+k]=w.m[r][k];
                b.mesh=cubeHandle; b.materialBase=uint32_t(listIndices.size()); b.mask=0xFF;
                listIndices.push_back(0); listIndices.push_back(0); list.push_back(b);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene around the table");
            for(uint32_t i=0;i<48;++i) run(TGPT_VIEW_NONE, cam, cam, i ? first+i : 0, options);
            const auto px=readback();
            objects.pop_back(); sunOn=true; sunShadowedByHdrp=false;
            expect(SetLights(&light, 1)==0 && SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
            double s=0, h=0;
            for(size_t j:tablePixels) { s+=px[j]+px[j+1]+px[j+2]; h+=hdrp[j]+hdrp[j+1]+hdrp[j+2]; }
            return h>0 ? s/h : 0.0;
        };
        const double blocked=share(true, 0, false, false, 0, 6000), open=share(false, 0, false, false, 0, 6000);
        const double off=share(true, 0, false, false, TGPT_OPTION_NO_HDRP_SHADOWS, 6000), hdrpLamp=share(true, TGPT_LIGHT_HDRP_SHADOWS, false, false, 0, 6000);
        const double sunShadow=share(false, 0, true, false, 0, 6100), sunHdrp=share(false, 0, true, true, 0, 6100);
        const double untraced=share(true, 0, false, false, 0, 6000, false);
        std::printf("      a table HDRP shades itself (%zu pixels), its color against HDRP's: %.3f under a lamp a block shadows, %.3f without the block, "
                    "%.3f with the option off, %.3f when HDRP shadows the lamp, %.3f missing from the traced scene; in the ceiling's shadow of the sun "
                    "%.3f, %.3f when HDRP shadows the sun\n", tablePixels.size(), blocked, open, off, hdrpLamp, untraced, sunShadow, sunHdrp);
        expect(tablePixels.size()>5000, "the table is drawn");
        expect(blocked<0.6, "what HDRP shades itself is darkened where the traced scene shadows a lamp it doesn't");
        expect(open>0.95 && open<=1.0001, "what HDRP shades itself stays as HDRP made it where a lamp reaches it");
        expect(std::fabs(off-1.0)<1e-3, "with the option off, what HDRP shades itself keeps HDRP's light");
        expect(hdrpLamp>0.95, "a lamp HDRP shadows itself darkens nothing twice");
        expect(std::fabs(untraced-1.0)<1e-3, "a surface drawn forward that the traced scene lacks keeps HDRP's light");
        expect(sunShadow<0.6 && sunHdrp>0.95, "the sun darkens what HDRP shades itself unless HDRP shadows the sun itself");
    }
    // What HDRP shadows itself keeps its light in the share: the same table HDRP shades, in the sun through the ceiling's
    // opening (the sun HDRP shadows, its cascades), a lamp beside it that HDRP doesn't shadow, and a traced-only block between
    // the two that shadows the table from the lamp alone. The traced shadow takes the lamp's part of the table's light, not
    // the sun's: once multiplied by A / (D + A) whatever else lit it, hair and eyes in daylight went nearly black under any
    // candle a head was between.
    {
        const Object table{0, translate_scale({-0.2f, 0.8f, 2.f}, {1.3f, 1, 1.3f}), {1}, false, false, 0, true};
        TGPT_Light lamp{}; lamp.position[0]=-2.f; lamp.position[1]=1.5f; lamp.position[2]=2.f; lamp.type=0;
        lamp.color[0]=lamp.color[1]=lamp.color[2]=80; lamp.range=12; lamp.radius=0.03f; lamp.id=401;
        objects.push_back(table); raster(cam, cam); const auto hdrp=readback(); objects.pop_back();
        raster(cam, cam); const auto plain=readback();
        std::vector<size_t> tablePixels;
        for(size_t j=0;j+2<hdrp.size();j+=3) if(std::fabs(hdrp[j]-plain[j])+std::fabs(hdrp[j+1]-plain[j+1])>1e-3 && hdrp[j]>1e-3) tablePixels.push_back(j);
        auto share=[&](bool block, bool sun, uint32_t first) {
            objects.push_back(table);
            sunOn=sun; sunShadowedByHdrp=true;
            expect(SetLights(&lamp, 1)==0, "the lamp beside the table");
            auto list=instances; auto listIndices=indices;
            TGPT_Instance top{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) top.transform[r*4+k]=table.world.m[r][k];
            top.mesh=quadHandle; top.materialBase=uint32_t(listIndices.size()); top.mask=0xFF; top.flags=TGPT_INSTANCE_ALPHA_TEST;
            listIndices.push_back(14); list.push_back(top);
            if(block) {
                TGPT_Instance b{}; const M4 w=translate_scale({-1.2f, 1.2f, 2.f}, {0.2f, 1.2f, 2.f});
                for(int r=0;r<3;++r) for(int k=0;k<4;++k) b.transform[r*4+k]=w.m[r][k];
                b.mesh=cubeHandle; b.materialBase=uint32_t(listIndices.size()); b.mask=0xFF;
                listIndices.push_back(0); listIndices.push_back(0); list.push_back(b);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene around the sunlit table");
            for(uint32_t i=0;i<48;++i) run(TGPT_VIEW_NONE, cam, cam, i ? first+i : 0, 0);
            const auto px=readback();
            objects.pop_back(); sunOn=true; sunShadowedByHdrp=false;
            expect(SetLights(&light, 1)==0 && SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
            double s=0, h=0;
            for(size_t j:tablePixels) { s+=px[j]+px[j+1]+px[j+2]; h+=hdrp[j]+hdrp[j+1]+hdrp[j+2]; }
            return h>0 ? s/h : 0.0;
        };
        const double lampAlone=share(true, false, 6200), withSun=share(true, true, 6200), open=share(false, true, 6200);
        // Where HDRP's ambient light is taken from it (a camera that sees no sky: indoors), an eye (the table's material is one
        // seen by camera rays only, as the eyes) takes the traced bounced light for it: the table gains light over HDRP's own.
        forwardKept=0.f; const double gained=share(false, true, 6200); forwardKept=1.f;
        std::printf("      a sunlit table HDRP shades (%zu pixels), a block between it and a lamp HDRP doesn't shadow: its color against HDRP's "
                    "%.3f without the sun, %.3f in the sun HDRP shadows, %.3f without the block, %.3f with HDRP's ambient light taken\n",
                    tablePixels.size(), lampAlone, withSun, open, gained);
        expect(gained>open+0.02, "an eye takes the traced bounced light where HDRP's own ambient light was taken from it");
        expect(tablePixels.size()>600, "the sunlit table is drawn");
        expect(lampAlone<0.6, "a block between a lamp and what HDRP shades darkens it");
        // Without its part kept, the sun changed nothing: the share was the lamp's alone.
        expect(withSun>lampAlone+0.12, "the sun HDRP shadows itself keeps its part of what HDRP shades when a lamp is blocked");
        expect(open>0.95 && open<=1.0001, "the sunlit table stays as HDRP made it where the lamp reaches it");
    }
    // What characters' acceleration structures cost the GPU, for the record: as many as the tracer takes (64 renderers, a
    // tavern's worth: its 55 had some 600k triangles), 8192 triangles each, posed anew every frame and rebuilt from their
    // vertices, against none. They stand under the floor, where no ray goes: what they add is their builds and the top level.
    // The GPU is shared with other work, so each figure is the median of many frames, the two cases taken in turn.
    {
        struct SkinVertex { float x, y, z; uint32_t packed[2]; };
        const uint32_t rings=64, segments=64, verticesEach=(rings+1)*(segments+1), meshCount=64;
        std::vector<uint32_t> sphere;
        for(uint32_t r=0;r<rings;++r) for(uint32_t s=0;s<segments;++s) {
            const uint32_t a=r*(segments+1)+s, b=a+segments+1;
            sphere.insert(sphere.end(), {a, b, a+1, a+1, b, b+1});
        }
        std::vector<SkinVertex> crowd(size_t(verticesEach)*meshCount);
        auto posePeople=[&](float t) {
            for(uint32_t m=0;m<meshCount;++m) for(uint32_t r=0;r<=rings;++r) for(uint32_t s=0;s<=segments;++s) {
                const float th=3.14159265f*float(r)/float(rings), ph=6.2831853f*float(s)/float(segments);
                const float wobble=0.3f*(1.f+0.1f*std::sin(3.f*th+t+float(m)));
                SkinVertex& v=crowd[size_t(m)*verticesEach+r*(segments+1)+s];
                v.x=-5.f+1.5f*float(m%8)+wobble*std::sin(th)*std::cos(ph); v.y=-50.f+wobble*std::cos(th); v.z=-5.f+1.5f*float(m/8)+wobble*std::sin(th)*std::sin(ph);
            }
        };
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=UINT(crowd.size()*sizeof(SkinVertex)); bd.Usage=D3D11_USAGE_DEFAULT;
        bd.BindFlags=D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride=sizeof(SkinVertex);
        ComPtr<ID3D11Buffer> crowdBuffer; device->CreateBuffer(&bd, nullptr, &crowdBuffer);
        std::vector<TGPT_SkinnedInstance> people;
        for(uint32_t m=0;m<meshCount;++m) {
            TGPT_CpuMeshDesc md{sizeof(TGPT_CpuMeshDesc), TGPT_ABI}; md.key=100+m; md.indices=sphere.data(); md.vertexCount=verticesEach; md.indexCount=uint32_t(sphere.size());
            const uint32_t start=0;
            const int handle=RegisterSkinnedMesh(&md, &start, 1);
            TGPT_SkinnedInstance si{}; si.transform[0]=si.transform[5]=si.transform[10]=1.f;
            si.mesh=handle; si.firstVertex=m*verticesEach; si.vertexCount=verticesEach; si.submeshCount=1;
            if(handle>0) people.push_back(si);
        }
        expect(people.size()==meshCount, "64 characters registered");
        auto frameOf=[&](bool with, uint32_t frame) {
            posePeople(float(frame)*0.1f);
            context->UpdateSubresource(crowdBuffer.Get(), 0, nullptr, crowd.data(), 0, 0);
            if(with) SetSkinned(uint64_t(uintptr_t(crowdBuffer.Get())), sizeof(SkinVertex), people.data(), uint32_t(people.size()));
            else SetSkinned(0, 0, nullptr, 0);
            run(TGPT_VIEW_NONE, cam, cam, frame, TGPT_OPTION_NO_DENOISER);
            ReadStatus(&status);
            return double(status.gpuMs);
        };
        for(uint32_t i=0;i<16;++i) frameOf(true, i ? 7000+i : 0); // converted and built over the first frames
        std::vector<double> without, with;
        uint32_t frame=7100;
        for(uint32_t round=0;round<24;++round) {
            // The GPU time a frame reports is that of the frame three before (read back when its slot comes round again): the
            // first three of each run are left out.
            for(uint32_t i=0;i<8;++i) { const double ms=frameOf(false, frame++); if(i>=3) without.push_back(ms); }
            for(uint32_t i=0;i<8;++i) { const double ms=frameOf(true, frame++); if(i>=3) with.push_back(ms); }
        }
        SetSkinned(0, 0, nullptr, 0);
        ReadStatus(&status);
        // The frames least slowed by other work on the GPU tell most: the tenth quantile, and the median.
        auto quantile=[](std::vector<double> v, double q) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[size_t(double(v.size()-1)*q)]; };
        std::printf("      characters' acceleration structures: 64 of 8192 triangles (524k), posed every frame: GPU %.2f ms a frame with them, "
                    "%.2f ms without (tenth quantile of %zu frames each): %.2f ms for them (%.2f by the medians, %.2f by the fastest frames)\n",
                    quantile(with, 0.1), quantile(without, 0.1), with.size(), quantile(with, 0.1)-quantile(without, 0.1),
                    quantile(with, 0.5)-quantile(without, 0.5), quantile(with, 0.0)-quantile(without, 0.0));
        expect(status.framesFailed==0, "frames with 64 characters trace");
    }
    // Shadow Detail (the player's row) on a finished HDR image, as HDRP hands it over before its post-processing: a ramp of
    // greys from far below a mid tone to above white, and a warm color, changed in place through a UAV and drawn over from a
    // copy (a texture without one). Nothing at or above the mid tone (0.12) changes, nor black; below it the lift follows
    // the curve, never past 3 s EV, keeps the channels' ratios and grows monotonically; the two paths agree.
    {
        expect(SetShadowDetail!=nullptr, "path tracing exports Shadow Detail");
        const UINT sw=256, sh=2;
        auto make=[&](bool uav) {
            std::vector<uint16_t> texels(size_t(sw)*sh*4);
            for(UINT y=0;y<sh;++y) for(UINT x=0;x<sw;++x) {
                const float v=x==0 ? 0.f : std::exp2(-14.f+16.f*float(x)/float(sw-1)); // black, then 2^-14 to 4
                const float rgb[3]={v, y==1 ? v*0.5f : v, y==1 ? v*0.25f : v};
                for(int k=0;k<3;++k) texels[(size_t(y)*sw+x)*4+k]=float_to_half(rgb[k]);
                texels[(size_t(y)*sw+x)*4+3]=float_to_half(1.f);
            }
            D3D11_TEXTURE2D_DESC d{}; d.Width=sw; d.Height=sh; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count=1;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET|(uav ? D3D11_BIND_UNORDERED_ACCESS : 0u);
            const D3D11_SUBRESOURCE_DATA init{texels.data(), sw*8, 0};
            ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
        };
        auto fetch=[&](ID3D11Texture2D* t) {
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
            context->CopyResource(staging.Get(), t);
            D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
            std::vector<float> v(size_t(sw)*sh*4);
            for(UINT y=0;y<sh;++y) { const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData)+size_t(y)*m.RowPitch);
                for(UINT i=0;i<sw*4;++i) v[size_t(y)*sw*4+i]=half_to_float(row[i]); }
            context->Unmap(staging.Get(), 0);
            return v;
        };
        auto apply=[&](ID3D11Texture2D* t, float strength, uint32_t request) {
            const int r=SetShadowDetail ? SetShadowDetail(request, uint64_t(uintptr_t(t)), strength) : -100;
            if(r==0) render(TGPT_EVENT_SHADOW_DETAIL, reinterpret_cast<void*>(uintptr_t(request)));
            return r;
        };
        auto viaUav=make(true), viaCopy=make(false), untouched=make(true);
        expect(apply(viaUav.Get(), 0.f, 9001)!=0, "Shadow Detail at 0 leaves the image alone");
        apply(viaUav.Get(), 0.5f, 9002); apply(viaCopy.Get(), 0.5f, 9003);
        const auto in=fetch(untouched.Get()), a=fetch(viaUav.Get()), b=fetch(viaCopy.Get());
        double worst=0, above=0, paths=0, ratio=0, lift=0; bool monotonic=true;
        float previous=-1.f;
        for(UINT x=0;x<sw;++x) {
            const size_t i=size_t(x)*4, j=(size_t(sw)+x)*4;
            for(int k=0;k<3;++k) paths=std::max(paths, double(std::fabs(a[i+k]-b[i+k]))/std::max(double(a[i+k]), 1e-6));
            if(x==0) { worst=std::max(worst, double(a[i]+b[i])); continue; } // black stays black
            const float v=in[i], d=std::max(0.f, -3.06f-std::log2(v)), h=d<0.5f ? d*d : d-0.25f;
            const float expected=v*std::exp2(0.5f*std::min(3.f, 0.4f*h));
            worst=std::max(worst, double(std::fabs(a[i]-expected))/expected);
            if(v>0.125f) above=std::max(above, double(std::fabs(a[i]-v))/v);
            lift=std::max(lift, double(std::log2(a[i]/v)));
            if(a[i]<previous) monotonic=false;
            previous=a[i];
            if(a[j]>1e-3f) ratio=std::max(ratio, std::max(std::fabs(double(a[j+1]/a[j])-0.5), std::fabs(double(a[j+2]/a[j])-0.25)));
        }
        std::printf("      shadow detail at 50%%: off the curve %.4f, above the mid tone %.5f, largest lift %.2f EV, color ratios off %.4f, UAV against copy %.4f, monotonic %s\n",
            worst, above, lift, ratio, paths, monotonic ? "yes" : "no");
        expect(worst<0.01 && above<0.001 && lift<=1.5+0.01 && lift>1.2 && ratio<0.01 && monotonic,
               "Shadow Detail lifts the dark below a mid tone along its curve and leaves the rest and black alone");
        expect(paths<0.002, "Shadow Detail through a UAV and drawn from a copy agree");
    }
    // Traced reflections on water (TGPT_SetWater, render event 4), handed over as HDRP's water system leaves them: after the
    // composite, a flat, smooth water plane 30 cm over the floor is drawn into HDRP's depth, stencil (bits 8 and 32, its
    // TraceReflectionRay and WaterSurface) and normal buffer, and the event fills HDRP's transparent SSR texture, which its
    // own SSR left empty (a weight of 0: what a reflection shows is off screen or hidden, as here). The fill must show the
    // traced scene where each pixel's mirror ray goes: a yellow light panel over the water that only the traced scene has
    // (as it has what lies off screen), the red and the green wall, the sky through the ceiling's hole, not the sky or a
    // probe everywhere. HDRP's own reflection stays where it found one, the fill takes the rest of the weight; the whole is
    // divided by HDRP's reflection lighting multiplier, pre-exposed with the traced frame's exposure; accumulated over frames.
    {
        auto SetWater=reinterpret_cast<decltype(&TGPT_SetWater)>(GetProcAddress(pt, "TGPT_SetWater"));
        expect(SetWater!=nullptr, "path tracing exports water reflections");
        const float waterY=0.3f;
        auto ssr=tex(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET);
        ComPtr<ID3D11RenderTargetView> ssrRtv; device->CreateRenderTargetView(ssr.Get(), nullptr, &ssrRtv);
        ComPtr<ID3D11DepthStencilState> waterDss;
        { D3D11_DEPTH_STENCIL_DESC d{}; d.DepthEnable=TRUE; d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc=D3D11_COMPARISON_GREATER;
          d.StencilEnable=TRUE; d.StencilReadMask=0xFF; d.StencilWriteMask=40;
          d.FrontFace={D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS}; d.BackFace=d.FrontFace;
          device->CreateDepthStencilState(&d, &waterDss); }
        const M4 waterWorld=translate_scale({0, waterY, 0}, {11, 1, 11});
        // The light panel: 2 m square, 2.2 m over the floor under the ceiling, facing down, in the traced scene only.
        auto withPanel=materials; const uint32_t panelMaterial=uint32_t(withPanel.size()); withPanel.push_back(materials[4]);
        withPanel[panelMaterial].baseColor[0]=withPanel[panelMaterial].baseColor[1]=withPanel[panelMaterial].baseColor[2]=0.2f;
        withPanel[panelMaterial].emissive[0]=6.f; withPanel[panelMaterial].emissive[1]=5.f; withPanel[panelMaterial].emissive[2]=0.3f;
        expect(SetMaterials(0, withPanel.data(), uint32_t(withPanel.size()))==0, "materials with a yellow light panel set");
        auto panelInstances=instances; auto panelIndices=indices;
        {
            const M4 w=mul(translate_scale({-1.5f, 2.2f, 1.f}, {2, 1, 2}), rot_x(pi));
            TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
            q.mesh=quadHandle; q.materialBase=uint32_t(panelIndices.size()); q.mask=0xFF;
            panelIndices.push_back(panelMaterial); panelInstances.push_back(q);
        }
        expect(SetScene(panelInstances.data(), uint32_t(panelInstances.size()), panelIndices.data(), uint32_t(panelIndices.size()))==0,
               "scene with a traced-only light panel over the water");
        // HDRP's water after the composite: its depth and stencil bits, and its normal (straight up) and roughness (0) in
        // the normal buffer, as its water pass and SSR preparation write them.
        auto drawWater=[&](const Camera& c) {
            ID3D11RenderTargetView* targets[6]={nullptr, rtv[1].Get(), nullptr, nullptr, nullptr, nullptr};
            context->OMSetRenderTargets(6, targets, dsv.Get());
            context->OMSetDepthStencilState(waterDss.Get(), 40);
            D3D11_VIEWPORT vp{0, 0, float(W), float(H), 0, 1}; context->RSSetViewports(1, &vp); context->RSSetState(rs.Get());
            context->IASetInputLayout(layout.Get()); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
            auto* cb=ocb.Get(); context->VSSetConstantBuffers(0, 1, &cb); context->PSSetConstantBuffers(0, 1, &cb);
            struct { float vp[16], mvp[16], pvp[16], world[16]; float cam[4], pcam[4], albedo[4], f0r[4], em[4], sun[4], sunc[4], misc[4]; } k{};
            std::memcpy(k.vp, c.viewProj.m, 64); std::memcpy(k.mvp, c.nonJitteredViewProj.m, 64); std::memcpy(k.pvp, c.nonJitteredViewProj.m, 64);
            std::memcpy(k.world, waterWorld.m, 64);
            k.cam[0]=k.pcam[0]=c.position.x; k.cam[1]=k.pcam[1]=c.position.y; k.cam[2]=k.pcam[2]=c.position.z;
            k.albedo[0]=k.albedo[1]=k.albedo[2]=0.5f; k.f0r[3]=0.f; k.misc[1]=exposure;
            context->UpdateSubresource(ocb.Get(), 0, nullptr, &k, 0, 0);
            UINT stride=sizeof(Vertex), offset=0; auto* vb=quadMesh.vb.Get();
            context->IASetVertexBuffers(0, 1, &vb, &stride, &offset); context->IASetIndexBuffer(quadMesh.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
            context->DrawIndexed(quadMesh.submeshes[0].indexCount, quadMesh.submeshes[0].indexStart, 0);
            ID3D11RenderTargetView* none[6]{}; context->OMSetRenderTargets(6, none, nullptr);
        };
        // HDRP's SSR texture as its transparent SSR leaves it: nothing found (0), and in marked rectangles what it found,
        // premultiplied by its weight: magenta with the full weight, and at half of it.
        struct Marker { int x0, y0, x1, y1; float value[4]; };
        const Marker markers[2]={{400, 170, 480, 210, {0.5f, 0.f, 0.5f, 1.f}}, {800, 170, 880, 210, {0.25f, 0.f, 0.25f, 0.5f}}};
        bool marked=false;
        auto prepareSsr=[&]() {
            const float zero[4]={0, 0, 0, 0}; context->ClearRenderTargetView(ssrRtv.Get(), zero);
            if(!marked) return;
            for(const auto& m:markers) {
                std::vector<uint16_t> texels(size_t(m.x1-m.x0)*(m.y1-m.y0)*4);
                for(size_t i=0;i<texels.size();++i) texels[i]=float_to_half(m.value[i%4]);
                const D3D11_BOX box{UINT(m.x0), UINT(m.y0), 0, UINT(m.x1), UINT(m.y1), 1};
                context->UpdateSubresource(ssr.Get(), 0, &box, texels.data(), UINT(m.x1-m.x0)*8, 0);
            }
        };
        float multiplier=1.f;
        // Without the radiance cache: these checks compare the fill between runs (half the weight, half the multiplier, a
        // hundred times the exposure), which needs the same traced light in each, and the cache's keeps converging from one
        // run to the next (2.017 times at half the multiplier with it).
        auto waterRun=[&](const Camera& c, const Camera& p, uint32_t frameIndex) {
            run(TGPT_VIEW_NONE, c, p, frameIndex, TGPT_OPTION_NO_RADIANCE_CACHE);
            drawWater(c);
            prepareSsr();
            const int r=SetWater ? SetWater(last.sequence, uint64_t(uintptr_t(ssr.Get())), uint64_t(uintptr_t(depth.Get())), uint64_t(uintptr_t(nb.Get())), multiplier) : -100;
            if(r==0) render(TGPT_EVENT_WATER, reinterpret_cast<void*>(uintptr_t(last.sequence)));
            else expect(false, "water reflections requested");
        };
        auto readSsr=[&]() {
            D3D11_TEXTURE2D_DESC d{}; ssr->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
            context->CopyResource(staging.Get(), ssr.Get());
            D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
            std::vector<float> v(size_t(W)*H*4);
            for(UINT y=0;y<H;++y) { const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData)+size_t(y)*m.RowPitch);
                for(UINT i=0;i<W*4;++i) v[size_t(y)*W*4+i]=half_to_float(row[i]); }
            context->Unmap(staging.Get(), 0);
            return v;
        };
        auto readStencil=[&]() {
            D3D11_TEXTURE2D_DESC d{}; depth->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
            context->CopyResource(staging.Get(), depth.Get());
            D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
            std::vector<uint8_t> s(size_t(W)*H);
            for(UINT y=0;y<H;++y) { const auto* row=static_cast<const uint8_t*>(m.pData)+size_t(y)*m.RowPitch; for(UINT x=0;x<W;++x) s[size_t(y)*W+x]=row[x*8+4]; }
            context->Unmap(staging.Get(), 0);
            return s;
        };
        // Where each water pixel's mirror ray goes in the room (the traced scene's own boxes and planes), from where the tracer
        // starts it (a little above the water: ray_offset): 1 the panel, 2 the red wall, 3 the green wall, 4 the sky through
        // the ceiling's hole, 5 the traced-only leaf card beside the panel (about as far away), 6 a sunlit card under the hole
        // (traced for the cut-out check only); 0 anything else (the box, the back wall, the ceiling, the other traced-only
        // leaf, the left wall's light, a ray that leaves by the open front).
        auto classify=[&](const Camera& c) {
            std::vector<uint8_t> cls(size_t(W)*H, 0);
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const float clip[4]={(x+0.5f)/W*2.f-1.f, 1.f-(y+0.5f)/H*2.f, 0.5f, 1.f};
                float r[4]={}; for(int i=0;i<4;++i) for(int k=0;k<4;++k) r[i]+=c.invViewProj.m[i][k]*clip[k];
                const V3 dir=normalize({r[0]/r[3], r[1]/r[3], r[2]/r[3]});
                if(dir.y>=-1e-4f) continue;
                const float toWater=(waterY-c.position.y)/dir.y;
                V3 at=c.position+dir*toWater; at.y+=0.02f+0.002f*toWater;
                const V3 d{dir.x, -dir.y, dir.z};
                float best=1e30f; uint8_t kind=0;
                auto plane=[&](int axis, float value, float lo0, float hi0, float lo1, float hi1, uint8_t k) {
                    const float o[3]={at.x, at.y, at.z}, dv[3]={d.x, d.y, d.z};
                    if(std::fabs(dv[axis])<1e-6f) return;
                    const float t=(value-o[axis])/dv[axis];
                    if(t<=1e-3f || t>=best) return;
                    const int a0=axis==0 ? 1 : 0, a1=axis==2 ? 1 : 2;
                    const float p0=o[a0]+dv[a0]*t, p1=o[a1]+dv[a1]*t;
                    if(p0<lo0 || p0>hi0 || p1<lo1 || p1>hi1) return;
                    best=t; kind=k;
                };
                auto box=[&](V3 lo, V3 hi) {
                    float t0=0, t1=1e30f; const float o[3]={at.x, at.y, at.z}, dv[3]={d.x, d.y, d.z}, l[3]={lo.x, lo.y, lo.z}, h[3]={hi.x, hi.y, hi.z};
                    for(int i=0;i<3;++i) {
                        if(std::fabs(dv[i])<1e-6f) { if(o[i]<l[i] || o[i]>h[i]) return; continue; }
                        float a=(l[i]-o[i])/dv[i], b=(h[i]-o[i])/dv[i]; if(a>b) std::swap(a, b);
                        t0=std::max(t0, a); t1=std::min(t1, b);
                    }
                    if(t0<=t1 && t0>1e-3f && t0<best) { best=t0; kind=0; }
                };
                plane(1, 2.2f, -2.5f, -0.5f, 0.f, 2.f, 1);                    // the panel (y = 2.2: x, z)
                plane(0, -6.f, 0.f, 6.f, -6.f, 6.f, 2);                       // red wall (x = -6: y, z)
                plane(0, 6.f, 0.f, 6.f, -6.f, 6.f, 3);                        // green wall
                plane(2, 6.f, -6.f, 6.f, 0.f, 6.f, 0);                        // back wall (z = 6: x, y)
                plane(1, 6.f, -6.f, 6.f, -6.f, 2.f, 0);                       // ceiling (y = 6: x, z)
                plane(1, 6.f, -6.f, 6.f, 2.f, 6.f, 4);                        // the hole: sky
                plane(1, 4.f, -2.f, 0.f, 2.5f, 4.5f, 0);                      // traced-only leaf
                plane(1, 2.f, -4.1f, -2.5f, 0.3f, 1.9f, 5);                   // traced-only leaf card
                plane(0, -5.98f, 2.4f, 3.6f, 2.f, 4.f, 0);                    // the left wall's light
                plane(1, 3.f, 3.8f, 5.4f, 2.7f, 4.3f, 6);                     // the sunlit card (in the scene for its own check)
                box({0.5f, 0.f, -1.f}, {2.5f, 2.f, 1.f});                     // the box
                cls[size_t(y)*W+x]=kind;
            }
            return cls;
        };
        const auto classes=classify(cam);
        // A still camera, 24 frames from a fresh start, no marked rectangles: the fill as traced.
        auto sequenceStill=[&](uint32_t first) {
            for(uint32_t i=0;i<24;++i) waterRun(cam, cam, first+i);
            return readSsr();
        };
        const auto traced=sequenceStill(6000);
        const auto stencil=readStencil();
        {
            std::vector<float> px(size_t(W)*H*3); for(size_t i=0;i<size_t(W)*H;++i) for(int k=0;k<3;++k) px[i*3+k]=traced[i*4+k];
            save(px, "35-water-reflections.png", true);
        }
        double sum[7][3]{}; int count[7]{}; int water=0, weightOne=0, untouched=0, dry=0;
        bool finiteWater=true;
        for(size_t i=0;i<size_t(W)*H;++i) {
            const bool wet=(stencil[i]&8)!=0;
            if(!wet) { ++dry; if(traced[i*4]==0.f && traced[i*4+1]==0.f && traced[i*4+2]==0.f && traced[i*4+3]==0.f) ++untouched; continue; }
            ++water;
            if(!std::isfinite(traced[i*4]) || !std::isfinite(traced[i*4+1]) || !std::isfinite(traced[i*4+2])) finiteWater=false;
            if(std::fabs(traced[i*4+3]-1.f)<1e-3f) ++weightOne;
            const uint8_t k=classes[i];
            for(int c=0;c<3;++c) sum[k][c]+=traced[i*4+c];
            ++count[k];
        }
        auto mean=[&](int k, int c) { return count[k] ? sum[k][c]/count[k] : 0.0; };
        std::printf("      water: %d pixels (%d filled with the full weight), %d dry pixels untouched of %d\n", water, weightOne, untouched, dry);
        const char* names[7]={"elsewhere", "the light panel", "the red wall", "the green wall", "the sky", "the leaf card", "the sunlit card's place (no card)"};
        for(int k=0;k<7;++k) std::printf("      water reflecting %s: %d pixels, %.4f %.4f %.4f\n", names[k], count[k], mean(k, 0), mean(k, 1), mean(k, 2));
        expect(water>50000 && finiteWater && weightOne>water*95/100, "the water's reflections are filled, with the full weight, and finite");
        expect(untouched==dry, "pixels without HDRP's reflection bit keep HDRP's reflections");
        expect(count[1]>500 && mean(1, 0)>3.0*mean(1, 2) && mean(1, 1)>3.0*mean(1, 2) &&
               mean(1, 0)+mean(1, 1)>4.0*(mean(0, 0)+mean(0, 1)), "the water reflects a light panel only the traced scene has, yellow and bright");
        expect(count[2]>500 && mean(2, 0)>2.0*mean(2, 1) && mean(2, 0)>2.0*mean(2, 2), "the water reflects the red wall where its mirror sends it");
        expect(count[3]>500 && mean(3, 1)>2.0*mean(3, 0) && mean(3, 1)>2.0*mean(3, 2), "the water reflects the green wall where its mirror sends it");
        if(count[4]>200) expect(mean(4, 2)>mean(4, 0), "the water reflects the sky through the ceiling's hole");
        // The fill's filter keeps the reflection's edges: the panel's reflection (its light is even) is as bright two texels in
        // from its edge, where the filter's 5x5 reaches the ceiling beyond it, as further in; blurred across, those took
        // about a seventh of the ceiling's darker light. (Right at the edge a pixel's own ray meets the panel or passes it
        // by turns, the smoothest water's reflection being a little rough: those pixels take both.)
        {
            double inside=0, ring=0; int nInside=0, nRing=0;
            for(UINT y=3;y+3<H;++y) for(UINT x=3;x+3<W;++x) {
                const size_t i=size_t(y)*W+x;
                if(classes[i]!=1 || !(stencil[i]&8)) continue;
                int nearestOther=4;
                for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx)
                    if(classes[size_t(int(y)+dy)*W+size_t(int(x)+dx)]!=1) nearestOther=std::min(nearestOther, std::max(std::abs(dx), std::abs(dy)));
                const double l=0.2126*traced[i*4]+0.7152*traced[i*4+1]+0.0722*traced[i*4+2];
                if(nearestOther>3) { inside+=l; ++nInside; }
                else if(nearestOther==2) { ring+=l; ++nRing; }
            }
            const double ratio=nInside && nRing && inside>0 ? (ring/nRing)/(inside/nInside) : 0.0;
            std::printf("      the panel's reflection two texels in from its edge against inside it: %.3f (%d and %d pixels)\n", ratio, nRing, nInside);
            expect(nInside>1000 && nRing>200 && ratio>0.93, "the water's reflections keep their edges");
        }
        // The traced reflection replaces HDRP's, where its SSR found the whole reflection (the full weight: magenta) and
        // where it found part of it (half): the same frames trace the same fill.
        {
            marked=true;
            const auto withMarkers=sequenceStill(6000);
            marked=false;
            double worstFull=0, worstHalf=0; int full=0, half=0;
            for(int m=0;m<2;++m) for(int y=markers[m].y0;y<markers[m].y1;++y) for(int x=markers[m].x0;x<markers[m].x1;++x) {
                const size_t i=size_t(y)*W+x;
                if(!(stencil[i]&8)) continue;
                for(int c=0;c<3;++c) {
                    const double expected=traced[i*4+c];
                    const double e=std::fabs(withMarkers[i*4+c]-expected)/std::max(expected, 0.01);
                    (m==0 ? worstFull : worstHalf)=std::max(m==0 ? worstFull : worstHalf, e);
                }
                ++(m==0 ? full : half);
            }
            std::printf("      HDRP's own reflection replaced: %d pixels where it had its full weight off the traced one by at most %.4f, %d with half by %.4f\n", full, worstFull, half, worstHalf);
            expect(full>1000 && worstFull<0.01, "where HDRP's SSR found the whole reflection, the traced one replaces it");
            expect(half>1000 && worstHalf<0.01, "where HDRP's SSR found part of the reflection, the traced one replaces it");
        }
        // Divided by HDRP's reflection lighting multiplier (it multiplies all of a reflection by it), and pre-exposed with the
        // traced frame's exposure (HDRP takes its SSR texture back out of its previous frame's): the same frames at half the
        // multiplier write twice as much, at 100 times the exposure 100 times as much.
        {
            auto ratioTo=[&](const std::vector<float>& a) {
                double s=0, r=0;
                for(size_t i=0;i<size_t(W)*H;++i) if(stencil[i]&8) for(int c=0;c<3;++c) { s+=traced[i*4+c]; r+=a[i*4+c]; }
                return s>0 ? r/s : 0.0;
            };
            multiplier=0.5f; const double halfMultiplier=ratioTo(sequenceStill(6000)); multiplier=1.f;
            setExposure(25.f); const double brighter=ratioTo(sequenceStill(6000)); setExposure(0.25f);
            std::printf("      the fill at half HDRP's reflection multiplier: %.4f times; at 100 times the exposure: %.3f times\n", halfMultiplier, brighter);
            expect(std::fabs(halfMultiplier-2.0)<0.01, "the fill is divided by HDRP's reflection lighting multiplier");
            expect(std::fabs(brighter-100.0)<1.0, "the fill is pre-exposed with the traced frame's exposure");
        }
        // Accumulated over frames: the fill's grain on the walls' reflections, still, against a frame traced alone (the second
        // frame from a fresh start applies the first one's, which had no history).
        {
            auto grainOf=[&](const std::vector<float>& v) {
                double s=0, level=0;
                for(UINT y=1;y+1<H;++y) for(UINT x=1;x+1<W;++x) {
                    const size_t i=size_t(y)*W+x;
                    if(classes[i]<2 || classes[i]>3) continue;
                    bool all=true; double m=0;
                    for(int dy=-1;dy<=1 && all;++dy) for(int dx=-1;dx<=1;++dx) {
                        const size_t j=size_t(int(y)+dy)*W+size_t(int(x)+dx);
                        if(!(stencil[j]&8) || classes[j]!=classes[i]) { all=false; break; }
                        m+=0.2126*v[j*4]+0.7152*v[j*4+1]+0.0722*v[j*4+2];
                    }
                    if(!all) continue;
                    m/=9; s+=std::fabs(0.2126*v[i*4]+0.7152*v[i*4+1]+0.0722*v[i*4+2]-m); level+=m;
                }
                return level>0 ? s/level : 0.0;
            };
            waterRun(cam, cam, 6100); waterRun(cam, cam, 6101);
            const auto single=readSsr();
            {
                std::vector<float> px(size_t(W)*H*3); for(size_t i=0;i<size_t(W)*H;++i) for(int k=0;k<3;++k) px[i*3+k]=single[i*4+k];
                save(px, "36-water-reflections-one-frame.png", true);
            }
            const double accumulated=grainOf(traced), alone=grainOf(single);
            std::printf("      the fill's grain on the walls' reflections: accumulated %.4f, one frame %.4f (%.2f)\n", accumulated, alone, accumulated/std::max(alone, 1e-9));
            expect(alone>0 && accumulated<0.6*alone, "the water's reflections are accumulated over frames");
            // And filtered over neighbouring texels (a reflection lit by bounced light kept 0.17 of grain accumulated alone).
            expect(accumulated<0.08, "the water's reflections are filtered over neighbouring texels");
        }
        // A moving camera: the fill follows (reprojected through the reflection's mirror image and the surface), stays finite
        // and keeps the panel's reflection where the mirror sends it.
        {
            Camera prevCam=cam;
            for(uint32_t i=0;i<24;++i) {
                const Camera c=make_camera({-0.6f+0.05f*float(i), 3.2f, -9.f+0.04f*float(i)}, {0, 2.2f, 2}, 60.f, float(W)/float(H));
                waterRun(c, i ? prevCam : c, 6200+i);
                prevCam=c;
            }
            const auto moving=readSsr();
            const auto movingStencil=readStencil();
            const auto movingClasses=classify(prevCam);
            double s[3]{}, other[3]{}; int n=0, o=0; bool finiteMoving=true;
            for(size_t i=0;i<size_t(W)*H;++i) {
                if(!(movingStencil[i]&8)) continue;
                if(!std::isfinite(moving[i*4]) || !std::isfinite(moving[i*4+1]) || !std::isfinite(moving[i*4+2])) { finiteMoving=false; continue; }
                if(movingClasses[i]==1) { for(int c=0;c<3;++c) s[c]+=moving[i*4+c]; ++n; }
                else if(movingClasses[i]==0) { for(int c=0;c<3;++c) other[c]+=moving[i*4+c]; ++o; }
            }
            std::printf("      moving camera: panel's reflection %d pixels, %.4f %.4f %.4f; elsewhere %.4f %.4f %.4f\n", n, n ? s[0]/n : 0.0, n ? s[1]/n : 0.0, n ? s[2]/n : 0.0,
                o ? other[0]/o : 0.0, o ? other[1]/o : 0.0, o ? other[2]/o : 0.0);
            expect(finiteMoving && n>500 && s[0]>3.0*s[2] && (s[0]+s[1])/n>4.0*(other[0]+other[1])/std::max(o, 1),
                   "with a moving camera the water keeps reflecting the panel where its mirror sends it");
        }
        // Translucent surfaces (leaves, grass: TGPT_MATERIAL_TRANSLUCENT, as the plugin marks the game's) are lit through from
        // behind in the reflection, as the tracer lights the pixels HDRP gives transmission and every path vertex on them: a
        // card in the sun seen from under it in the water, alpha-tested and translucent (its texture opaque, so nothing else
        // differs), is brighter than the same card alpha-tested alone, which only bounced light reaches below.
        {
            auto withCard=withPanel; const uint32_t cardMaterial=uint32_t(withCard.size()); withCard.push_back(materials[3]); // the checker, opaque texels
            auto cardInstances=panelInstances; auto cardIndices=panelIndices;
            {
                const M4 w=translate_scale({4.6f, 3.f, 3.5f}, {1.6f, 1, 1.6f});
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                q.mesh=quadHandle; q.materialBase=uint32_t(cardIndices.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
                cardIndices.push_back(cardMaterial); cardInstances.push_back(q);
            }
            expect(SetScene(cardInstances.data(), uint32_t(cardInstances.size()), cardIndices.data(), uint32_t(cardIndices.size()))==0, "scene with a sunlit card");
            auto cardReflection=[&](uint32_t flags) {
                withCard[cardMaterial].flags=flags; withCard[cardMaterial].alphaCutoff=0.5f;
                expect(SetMaterials(0, withCard.data(), uint32_t(withCard.size()))==0, "the card's material set");
                const auto v=sequenceStill(6000);
                double s=0; int n=0;
                for(size_t i=0;i<size_t(W)*H;++i) if((stencil[i]&8) && classes[i]==6) { s+=0.2126*v[i*4]+0.7152*v[i*4+1]+0.0722*v[i*4+2]; ++n; }
                return n>0 ? s/n : 0.0;
            };
            const double cutOut=cardReflection(TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_TRANSLUCENT), solid=cardReflection(TGPT_MATERIAL_ALPHA_TEST);
            std::printf("      a sunlit card seen from under it in the water: translucent %.4f, alpha-tested alone %.4f (%.2f)\n", cutOut, solid, cutOut/std::max(solid, 1e-9));
            expect(solid>0 && cutOut>1.5*solid, "translucent surfaces (leaves, grass) are lit through from behind in the water's reflections");
            expect(SetMaterials(0, withPanel.data(), uint32_t(withPanel.size()))==0, "materials with the panel again");
            expect(SetScene(panelInstances.data(), uint32_t(panelInstances.size()), panelIndices.data(), uint32_t(panelIndices.size()))==0, "scene with the panel again");
        }
        ReadStatus(&status);
        std::printf("      water reflections: %u frames traced, %u applied\n", status.waterTraced, status.waterApplied);
        expect(status.waterTraced>0 && status.waterApplied>0, "the status counts the water reflections traced and applied");
        expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "materials set");
        expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the panel");
    }
    std::printf("      totals: frames traced %llu, failed %llu, last error 0x%08X, disabled %u; images in %s\n", (unsigned long long)status.framesTraced,
        (unsigned long long)status.framesFailed, uint32_t(status.lastError), status.disabled, images.string().c_str());
    expect(status.framesFailed==0 && status.disabled==0, "no frame failed");
    std::printf("      composites applied: %u of %llu traced frames\n", status.composited, (unsigned long long)status.framesTraced);
    expect(status.composited>0, "traced light applied to the color buffer (the status counts it)");
    // What each stage of a frame cost the GPU, over the last second (the F8 report's line).
    {
        char stages[1024]{};
        const int length=GetStageTimes ? GetStageTimes(stages, sizeof(stages)) : -1;
        std::printf("      GPU time by stage: %s\n", stages);
        expect(length>0 && std::strstr(stages, "scene ") && std::strstr(stages, "bounced light ") && std::strstr(stages, "bounced light spatial reuse ") &&
               std::strstr(stages, "composite "), "the frame's stages are timed on the GPU");
    }
    if(debugLayer) {
        std::printf("      Direct3D 12 debug layer: %d errors, %d warnings\n", debugErrors, debugWarnings);
        expect(debugErrors==0, "the Direct3D 12 debug layer reports no errors");
        if(infoQueue) infoQueue->UnregisterMessageCallback(debugCookie);
    }
    std::printf(failures ? "FAILED %d checks\n" : "all path tracing checks passed\n", failures);
    return failures ? 1 : 0;
}
