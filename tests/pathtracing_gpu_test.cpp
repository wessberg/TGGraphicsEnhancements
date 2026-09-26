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
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
struct Camera { V3 position; M4 viewProj, invViewProj; };
static Camera make_camera(V3 position, V3 target, float fovDegrees, float aspect, float jitterX=0, float jitterY=0, int width=1, int height=1) {
    const V3 f=normalize(target-position), r=normalize(cross({0, 1, 0}, f)), u=cross(f, r);
    M4 view=identity();
    view.m[0][0]=r.x; view.m[0][1]=r.y; view.m[0][2]=r.z;
    view.m[1][0]=u.x; view.m[1][1]=u.y; view.m[1][2]=u.z;
    view.m[2][0]=-f.x; view.m[2][1]=-f.y; view.m[2][2]=-f.z;
    const float n=0.1f, fa=1000.f, t=1.f/std::tan(fovDegrees*3.14159265f/360.f);
    M4 gl; gl.m[0][0]=t/aspect; gl.m[1][1]=t; gl.m[2][2]=-(fa+n)/(fa-n); gl.m[2][3]=-2*fa*n/(fa-n); gl.m[3][2]=-1;
    gl.m[0][2]+=jitterX*2.f/float(width); gl.m[1][2]+=jitterY*2.f/float(height);
    M4 gpu=gl;
    for(int c=0;c<4;++c) { gpu.m[1][c]=-gl.m[1][c]; gpu.m[2][c]=-0.5f*gl.m[2][c]+0.5f*gl.m[3][c]; }
    Camera cam; cam.position=position; cam.viewProj=mul(gpu, view); cam.invViewProj=inverse(cam.viewProj);
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
struct Object { int mesh; M4 world; std::vector<int> materials; bool tracedOnly=false; bool alphaTest=false; uint32_t feature=0; }; // feature: HDRP Lit's material feature in GBuffer2.a (2: transmission)

static const char* kSceneShader=R"HLSL(
cbuffer Object : register(b0) {
    row_major float4x4 viewProj; row_major float4x4 prevViewProj; row_major float4x4 world;
    float4 cameraPos; float4 prevCameraPos; float4 albedo; float4 f0rough; float4 emissive; float4 sunDir; float4 sunColor; float4 misc;
};
Texture2D checker : register(t0); SamplerState wrap : register(s0);
struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float3 wpos : TEXCOORD0; float3 nrm : TEXCOORD1; float2 uv : TEXCOORD2; float4 cur : TEXCOORD3; float4 prev : TEXCOORD4; };
VSOut vsMain(VSIn i) {
    VSOut o;
    float3 w = mul(world, float4(i.pos, 1)).xyz;
    o.wpos = w; o.nrm = normalize(mul((float3x3)world, i.nrm)); o.uv = i.uv;
    o.pos = mul(viewProj, float4(w - cameraPos.xyz, 1)); o.cur = o.pos;
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
    o.gb0 = float4(base, 1);
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
    if(agility) {
        wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path stage=fs::path(exe).parent_path()/"tgpt-native";
        for(const char* mod:{"TGGraphicsBridge", "TGPathTracing"}) {
            fs::create_directories(stage/mod, ec);
            fs::copy(root/mod, stage/mod, fs::copy_options::recursive|fs::copy_options::overwrite_existing, ec);
            if(ec) { std::printf("FAIL: copy %s beside the test: %s\n", mod, ec.message().c_str()); return 1; }
        }
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
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"TGPathTracingTest";
    RegisterClassW(&wc);
    const HWND window=CreateWindowExW(0, wc.lpszClassName, L"TG Path Tracing test", WS_OVERLAPPEDWINDOW, 0, 0, 1296, 759, nullptr, nullptr, wc.hInstance, nullptr);
    ComPtr<IDXGIFactory2> factory;
    if(FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) { std::printf("SKIP: no DXGI factory\n"); return 77; }
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_1;
    if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) { std::printf("SKIP: no D3D11 device\n"); return 77; }
    DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=1280; sd.Height=720; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc={1, 0};
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=2; sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapchain;
    expect(SUCCEEDED(factory->CreateSwapChainForHwnd(device.Get(), window, &sd, nullptr, nullptr, &swapchain)), "game swapchain created through the bridge hooks");
    TGB_Status bs{sizeof(TGB_Status), TGB_ABI};
    expect(readBridge(&bs)==0 && bs.d3d12DeviceReady==1, "bridge created the shared D3D12 device");
    const HMODULE pt=GetModuleHandleW(L"TGPathTracing.Native.dll");
#define LOAD(name) auto name=reinterpret_cast<decltype(&TGPT_##name)>(GetProcAddress(pt, "TGPT_" #name))
    LOAD(ReadStatus); LOAD(GetUnsupportedReason); LOAD(RegisterMesh); LOAD(RegisterTexture); LOAD(SetMaterials); LOAD(SetScene);
    LOAD(SetLights); LOAD(SetSky); LOAD(SubmitFrame); LOAD(SetComposite); LOAD(GetRenderEvent); LOAD(RegisterCpuMesh); LOAD(RegisterAlphaTexture);
    LOAD(RegisterSkinnedMesh); LOAD(SetSkinned);
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
    const int leafAlpha=RegisterAlphaTexture(43, uint64_t(uintptr_t(leaf.Get())));
    expect(leafAlpha>=0, "alpha-tested texture registered for the alpha atlas");
    // Materials: white, red, green, checker, emissive, terrain (vertex color).
    struct Mat { float r, g, b; bool checker; float er, eg, eb; };
    const Mat mats[6]={{0.75f, 0.75f, 0.75f, false, 0, 0, 0}, {0.75f, 0.08f, 0.06f, false, 0, 0, 0}, {0.08f, 0.6f, 0.1f, false, 0, 0, 0},
                       {1, 1, 1, true, 0, 0, 0}, {0.2f, 0.2f, 0.2f, false, 6.f, 3.f, 1.f}, {1, 1, 1, false, 0, 0, 0}};
    std::vector<TGPT_Material> materials(8);
    for(int i=0;i<6;++i) {
        auto& m=materials[size_t(i)]; m.baseColor[0]=mats[i].r; m.baseColor[1]=mats[i].g; m.baseColor[2]=mats[i].b; m.baseColor[3]=1;
        m.uvScaleOffset[0]=m.uvScaleOffset[1]=1; m.emissive[0]=mats[i].er; m.emissive[1]=mats[i].eg; m.emissive[2]=mats[i].eb;
        m.albedoTile=mats[i].checker ? tile : -1; m.emissiveTile=-1; m.smoothness=0.3f;
        if(i==5) m.flags=TGPT_MATERIAL_VERTEX_COLOR;
    }
    // 6: an alpha-tested leaf; 7: the plugin's "invisible to rays" material (a cutoff above 1, no texture).
    materials[6]=materials[0]; materials[6].albedoTile=leafTile; materials[6].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_ALPHA_TILE; materials[6].alphaTile=leafAlpha; materials[6].alphaCutoff=0.5f;
    materials[7]=materials[0]; materials[7].flags=TGPT_MATERIAL_ALPHA_TEST; materials[7].alphaCutoff=2.f;
    expect(SetMaterials(0, materials.data(), 8)==0, "materials set");
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
    light.color[0]=40; light.color[1]=22; light.color[2]=8; light.range=12; light.radius=0.05f; light.id=0;
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
    const float exposure=0.25f;
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
    ComPtr<ID3D11Buffer> ocb; { D3D11_BUFFER_DESC d{}; d.ByteWidth=16*4*3+16*8; d.BindFlags=D3D11_BIND_CONSTANT_BUFFER; device->CreateBuffer(&d, nullptr, &ocb); }
    ComPtr<ID3D11DepthStencilState> dss;
    { D3D11_DEPTH_STENCIL_DESC d{}; d.DepthEnable=TRUE; d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc=D3D11_COMPARISON_GREATER;
      d.StencilEnable=TRUE; d.StencilReadMask=d.StencilWriteMask=0xFF;
      d.FrontFace={D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS}; d.BackFace=d.FrontFace;
      device->CreateDepthStencilState(&d, &dss); }
    ComPtr<ID3D11RasterizerState> rs; { D3D11_RASTERIZER_DESC d{}; d.FillMode=D3D11_FILL_SOLID; d.CullMode=D3D11_CULL_NONE; d.DepthClipEnable=TRUE; device->CreateRasterizerState(&d, &rs); }
    ComPtr<ID3D11SamplerState> wrap; { D3D11_SAMPLER_DESC d{}; d.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; d.AddressU=d.AddressV=d.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; d.MaxLOD=D3D11_FLOAT32_MAX; device->CreateSamplerState(&d, &wrap); }
    const V3 sunDir=normalize({0.35f, 1.f, 0.55f});
    const float sunLux=6.f;
    const float skyColor[4]={0.55f*exposure, 0.75f*exposure, 0.95f*exposure, 1};

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
            const MeshBuffers& m=o.mesh==0 ? quadMesh : cubeMesh;
            for(size_t s=0;s<m.submeshes.size();++s) {
                const Mat& mat=mats[o.materials[std::min(s, o.materials.size()-1)]];
                struct { float vp[16], pvp[16], world[16]; float cam[4], pcam[4], albedo[4], f0r[4], em[4], sun[4], sunc[4], misc[4]; } c{};
                std::memcpy(c.vp, cam.viewProj.m, 64); std::memcpy(c.pvp, prev.viewProj.m, 64); std::memcpy(c.world, o.world.m, 64);
                c.cam[0]=cam.position.x; c.cam[1]=cam.position.y; c.cam[2]=cam.position.z;
                c.pcam[0]=prev.position.x; c.pcam[1]=prev.position.y; c.pcam[2]=prev.position.z;
                c.albedo[0]=mat.r; c.albedo[1]=mat.g; c.albedo[2]=mat.b; c.f0r[0]=c.f0r[1]=c.f0r[2]=0.04f; c.f0r[3]=0.6f;
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
    uint32_t sequence=0, rays=1, history=20;
    bool sunOn=true;
    uint32_t spatialSamples=4, bounces=2;
    float rayDistance=500;
    float indirectColor=1; // Indirect Color: 1 as traced
    float reflectionBoost=0; // Reflection Boost: 0 as the materials are
    bool rayReconstruction=false; // frames that ask for Ray Reconstruction's guides and hand the light out undenoised
    TGPT_Frame last{};
    auto run=[&](uint32_t view, const Camera& cam, const Camera& prev, uint32_t frameIndex, uint32_t options=0) {
        raster(cam, prev);
        TGPT_Frame f{sizeof(TGPT_Frame), TGPT_ABI};
        f.sequence=++sequence; f.flags=frameIndex==0 ? TGPT_FRAME_RESET : 0;
        f.gbuffer0=uint64_t(uintptr_t(gb0.Get())); f.normal=uint64_t(uintptr_t(nb.Get())); f.gbuffer2=uint64_t(uintptr_t(gb2.Get()));
        f.gbuffer3=uint64_t(uintptr_t(gb3.Get())); f.depth=uint64_t(uintptr_t(depth.Get())); f.motion=uint64_t(uintptr_t(mv.Get()));
        f.exposure=uint64_t(uintptr_t(exposureTex.Get()));
        f.renderWidth=W; f.renderHeight=H; f.debugView=view;
        unity_layout(cam.invViewProj, f.invViewProj); unity_layout(cam.viewProj, f.viewProj); unity_layout(prev.viewProj, f.prevViewProj);
        f.cameraPosition[0]=cam.position.x; f.cameraPosition[1]=cam.position.y; f.cameraPosition[2]=cam.position.z;
        f.prevCameraPosition[0]=prev.position.x; f.prevCameraPosition[1]=prev.position.y; f.prevCameraPosition[2]=prev.position.z;
        f.sunDirection[0]=sunDir.x; f.sunDirection[1]=sunDir.y; f.sunDirection[2]=sunDir.z; f.sunDirection[3]=0.0047f;
        f.sunColor[0]=f.sunColor[1]=f.sunColor[2]=sunLux; f.sunColor[3]=sunOn ? 1.f : 0.f;
        f.frameIndex=frameIndex; f.bounces=bounces; f.spatialSamples=spatialSamples; f.temporalCap=history; f.options=options; f.raysPerPixel=rays;
        if(rayReconstruction) { f.flags|=TGPT_FRAME_GUIDES; f.options|=TGPT_OPTION_RAY_RECONSTRUCTION; }
        f.maxRayDistance=rayDistance; f.skyIntensity=1; f.indirectColor=indirectColor; f.reflectionBoost=reflectionBoost;
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
    // Debug views.
    const std::pair<uint32_t, const char*> views[]={{TGPT_VIEW_ALBEDO, "2-view-albedo.png"}, {TGPT_VIEW_NORMALS, "3-view-normals.png"},
        {TGPT_VIEW_COVERAGE, "4-view-coverage.png"}, {TGPT_VIEW_MATERIALS, "5-view-materials.png"}, {TGPT_VIEW_INDIRECT, "6-view-indirect.png"},
        {TGPT_VIEW_INDIRECT_RAW, "7-view-indirect-raw.png"}};
    for(const auto& v:views) {
        run(v.first, cam, cam, 100);
        const auto px=readback(); save(px, v.second, v.first==TGPT_VIEW_INDIRECT || v.first==TGPT_VIEW_INDIRECT_RAW);
        if(v.first==TGPT_VIEW_COVERAGE) {
            int covered=0, total=0;
            for(UINT y=0;y<H;y+=4) for(UINT x=0;x<W;x+=4) { const size_t i=(size_t(y)*W+x)*3; const bool geometry=base[i]!=skyColor[0] || base[i+1]!=skyColor[1];
                if(!geometry) continue; ++total; if(px[i]+px[i+1]+px[i+2]>0.3f) ++covered; }
            std::printf("      coverage: %d of %d sampled geometry pixels have traced geometry in front of or at HDRP's surface\n", covered, total);
            expect(total>0 && covered>total*95/100, "the traced scene covers what HDRP drew");
        }
    }
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
        struct Case { const char* name; uint32_t cap; bool jitter, moving, night, bumps=false; double floorLimit=0; };
        const Case cases[]={
            {"history 1, still", 1, false, false, false, false, 0.06}, {"history 20, still", 20, false, false, false},
            {"history 1, jittered", 1, true, false, false}, {"history 20, jittered", 20, true, false, false},
            {"history 1, moving", 1, true, true, false}, {"history 20, moving", 20, true, true, false},
            {"night, history 1, still", 1, false, false, true}, {"night, history 1, jittered", 1, true, false, true},
            {"night, history 1, moving", 1, true, true, true}, {"night, history 20, moving", 20, true, true, true},
            {"bumpy, history 1, moving", 1, true, true, false, true},
            {"bumpy night, history 1, moving", 1, true, true, true, true}, {"bumpy night, history 20, moving", 20, true, true, true, true}};
        double still[2]{};
        uint32_t index=800;
        for(const Case& k:cases) {
            history=k.cap; bumpy=k.bumps;
            if(k.night==sunOn) { sunOn=!k.night; expect(k.night ? SetLights(torches.data(), uint32_t(torches.size()))==0 : SetLights(&light, 1)==0, k.night ? "torches set" : "one light again"); }
            Camera prev{};
            std::vector<float> px, before;
            double flicker=0; int flickerFrames=0;
            for(uint32_t i=0;i<48;++i) {
                const float jx=k.jitter ? halton(i%8+1, 2)-0.5f : 0.f, jy=k.jitter ? halton(i%8+1, 3)-0.5f : 0.f;
                const Camera c=make_camera({k.moving ? -0.6f+0.025f*float(i) : 0.f, 3.2f, -9}, {0, 2.2f, 2}, 60.f, float(W)/float(H), jx, jy, int(W), int(H));
                run(TGPT_VIEW_NONE, c, i ? prev : c, i ? index++ : 0); // each case starts over
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
            if(!k.jitter && !k.night) still[k.cap==1 ? 0 : 1]=ceiling;
            if(k.night && k.moving && k.cap==1) save(px, k.bumps ? "14-bumpy-night-full-moving.png" : "13-night-full-moving.png", true);
        }
        history=20; bumpy=false;
        // Lamp shadows follow the light's size: the checkered leaf's shadow on the floor from the light above it,
        // alone and without the sun, converged without the denoiser, is crisper from a 2 cm light than a 10 cm one.
        // (A 10 cm minimum light size made both the same.)
        {
            sunOn=false;
            auto sharpness=[&](float radius) {
                TGPT_Light one=torches.back(); one.radius=radius;
                expect(SetLights(&one, 1)==0, "the light above the leaf alone");
                const uint32_t keep=rays; rays=8;
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_DIRECT, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                rays=keep;
                std::vector<float> avg(acc.size()); for(size_t j=0;j<acc.size();++j) avg[j]=float(acc[j]/32.0);
                save(avg, radius<0.05f ? "27-lamp-2cm.png" : "28-lamp-10cm.png", true);
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
        }
        if(!sunOn) { sunOn=true; expect(SetLights(&light, 1)==0, "one light again"); }
        expect(still[1]<=still[0]*1.25+1e-4, "a longer ReSTIR history leaves no more grain than history 1");
        // What the quality settings change, measured on the room in the sun (in game DLSS Ray Reconstruction removes
        // most of the noise they trade against, so they show little there):
        //   Bounces: light under the ceiling, converged, 1 bounce against 4;
        //   Ray Distance: the same light with rays reaching 2 m instead of 500 m (misses count as sky);
        //   Rays Per Pixel and Spatial Samples: a single frame's error against the converged frame, undenoised.
        {
            auto average=[&](uint32_t frames) {
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<frames+8;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                std::vector<float> out(acc.size()); for(size_t j=0;j<acc.size();++j) out[j]=float(acc[j]/frames);
                return out;
            };
            auto frameError=[&](const std::vector<float>& reference) {
                for(uint32_t i=0;i<12;++i) run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                const auto px=readback();
                double e=0, level=0;
                for(UINT y=2;y<H-2;++y) for(UINT x=2;x<W-2;++x) {
                    const size_t j=(size_t(y)*W+x)*3;
                    e+=std::fabs((0.2126*px[j]+0.7152*px[j+1]+0.0722*px[j+2])-(0.2126*reference[j]+0.7152*reference[j+1]+0.0722*reference[j+2]));
                    level+=0.2126*reference[j]+0.7152*reference[j+1]+0.0722*reference[j+2];
                }
                return level>0 ? e/level : 0.0;
            };
            const uint32_t keepRays=rays; rays=4;
            bounces=1; const double oneBounce=region(average(24), 500, 650, 780, 700);
            bounces=4; const double fourBounces=region(average(24), 500, 650, 780, 700);
            bounces=2; rayDistance=2; const double shortRays=region(average(24), 500, 650, 780, 700);
            rayDistance=500; const double longRays=region(average(24), 500, 650, 780, 700);
            rays=8; const auto reference=average(32);
            rays=1; const double oneRay=frameError(reference);
            rays=8; const double eightRays=frameError(reference);
            rays=1; spatialSamples=0; const double noSpatial=frameError(reference);
            spatialSamples=8; const double eightSpatial=frameError(reference);
            spatialSamples=4; rays=keepRays;
            std::printf("      settings: under the ceiling, 1 bounce %.4f, 4 bounces %.4f; rays to 2 m %.4f, to 500 m %.4f\n", oneBounce, fourBounces, shortRays, longRays);
            std::printf("      settings: one frame's error, 1 ray %.4f, 8 rays %.4f; no spatial reuse %.4f, 8 spatial samples %.4f\n", oneRay, eightRays, noSpatial, eightSpatial);
            expect(fourBounces>oneBounce*1.02, "more bounces brighten the light under the ceiling");
            expect(std::fabs(shortRays-longRays)>0.02*longRays, "Ray Distance changes the light under the ceiling");
            expect(eightRays<oneRay*0.8, "more rays per pixel leave less noise");
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
        }
        // Translucent surfaces (HDRP's transmission: grass blades, leaves) are lit through from behind: a drawn quad
        // facing the camera with the sun behind it gets direct light as a translucent surface, none as an opaque one.
        {
            auto directWith=[&](uint32_t feature) {
                objects.push_back(Object{0, mul(translate_scale({3.5f, 1.5f, 3.8f}, {1.5f, 1, 1.5f}), rot_x(-pi/2)), {0}, false, false, feature});
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<32;++i) {
                    run(TGPT_VIEW_DIRECT, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                objects.pop_back();
                return acc;
            };
            const auto opaque=directWith(0), translucent=directWith(2);
            int through=0;
            for(size_t j=0;j+2<opaque.size();j+=3) {
                const double lo=(0.2126*opaque[j]+0.7152*opaque[j+1]+0.0722*opaque[j+2])/24.0, lt=(0.2126*translucent[j]+0.7152*translucent[j+1]+0.0722*translucent[j+2])/24.0;
                if(lt>lo+0.02) ++through;
            }
            std::printf("      a quad lit from behind by the sun: %d pixels lit through as a translucent surface\n", through);
            expect(through>1000, "a translucent surface (grass, leaves) is lit through from behind");
        }
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
        if(agility) expect(dxr12, "TG Path Tracing's Agility SDK runtime brings DirectX Raytracing 1.2 (Shader Model 6.9)");
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
    // AMD FSR Ray Regeneration runs on a Radeon RX 9000 or newer only: elsewhere a frame asking for it gets
    // the built-in denoiser.
    {
        ReadStatus(&status);
        const bool available=(status.features&TGPT_FEATURE_RAY_REGENERATION)!=0;
        for(uint32_t i=0;i<8;++i) run(TGPT_VIEW_NONE, cam, cam, 900+i, TGPT_OPTION_RAY_REGENERATION);
        const auto px=readback();
        bool finiteRrg=true; for(float v:px) if(!std::isfinite(v)) { finiteRrg=false; break; }
        ReadStatus(&status);
        std::printf("      AMD Ray Regeneration: %s on this GPU; the last frame %s\n", available ? "available" : "not available",
            (status.features&TGPT_FEATURE_RAY_REGENERATION_ACTIVE) ? "was denoised by it" : "had the built-in denoiser");
        expect(finiteRrg, "frames asking for AMD Ray Regeneration are finite");
        if(!available) expect(!(status.features&TGPT_FEATURE_RAY_REGENERATION_ACTIVE), "without AMD Ray Regeneration the built-in denoiser serves");
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
        auto frames=[&](bool character, uint32_t base) {
            std::vector<float> px;
            for(uint32_t i=0;i<40;++i) {
                if(character) SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
                else SetSkinned(0, 0, nullptr, 0);
                run(TGPT_VIEW_NONE, cam, cam, i ? base+i : 0);
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
        expect(s4>s1*0.15 && s4<s1*0.85, "a character's cut-outs let light through, texel by texel");
        SetSkinned(0, 0, nullptr, 0);
    }
    std::printf("      totals: frames traced %llu, failed %llu, last error 0x%08X, disabled %u; images in %s\n", (unsigned long long)status.framesTraced,
        (unsigned long long)status.framesFailed, uint32_t(status.lastError), status.disabled, images.string().c_str());
    expect(status.framesFailed==0 && status.disabled==0, "no frame failed");
    std::printf("      composites applied: %u of %llu traced frames\n", status.composited, (unsigned long long)status.framesTraced);
    expect(status.composited>0, "traced light applied to the color buffer (the status counts it)");
    std::printf(failures ? "FAILED %d checks\n" : "all path tracing checks passed\n", failures);
    return failures ? 1 : 0;
}
