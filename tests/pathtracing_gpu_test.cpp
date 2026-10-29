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
#include <functional>
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
#include <random>
#include <string>
#include <vector>
#include <psapi.h> // the test's own memory, for its ceiling (memory_watch)

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
// TGPT_TEST_MEMORY_CEILING_MB=<megabytes>: the run ends itself once the test's own committed memory passes it, whatever it
// is doing (a thread of its own looks twice a second). Why (PT-99, docs/agents/speed-1.md, 2026-10-09): the one start of
// a build whose two reordering passes the driver had not made before held 33 GB after 9.5 minutes and grew 5 GB a minute,
// the driver still making those passes, before any frame was traced; the PC paged, and the coordinator had to stop it.
// The speed-1 mode has a ceiling of 8 GB of its own unless this says otherwise; elsewhere: none unless this is set.
static double memoryCeilingMb=0.0;
static DWORD WINAPI memory_watch(LPVOID) {
    for(;;) {
        Sleep(500);
        PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb=sizeof(counters);
        if(!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) continue;
        const double mb=double(counters.PrivateUsage)/1048576.0;
        if(mb<=memoryCeilingMb) continue;
        std::printf("STOPPED: the test's own memory is %.0f MB, over its ceiling of %.0f MB (TGPT_TEST_MEMORY_CEILING_MB)\n", mb, memoryCeilingMb);
        std::fflush(stdout);
        ExitProcess(4);
    }
}
// The isolated optical checks measure small analytical fixtures. Keep their pixel
// count low; the rest of the image regression suite still uses its original size.
static const bool opticalOnly=GetEnvironmentVariableA("TGPT_TEST_GLASS_ONLY",nullptr,0)>0;
static const bool glassShellOnly=GetEnvironmentVariableA("TGPT_TEST_GLASS_SHELL_ONLY",nullptr,0)>0;
static UINT W=(opticalOnly || glassShellOnly) ? 320 : 1280, H=(opticalOnly || glassShellOnly) ? 180 : 720;
// TGPT_TEST_STOP_AT_FAIL: the run ends at its first failed check (a mutant's run: its FAIL line is all it is for).
static int checksDone=0; // (how many checks so far: TGPT_TEST_TRACE's lines carry it, so a frame is found in the run's log)
static void expect(bool condition, const char* what) {
    ++checksDone;
    std::printf("%s: %s\n", condition ? "ok  " : "FAIL", what);
    if(condition) return;
    ++failures;
    if(GetEnvironmentVariableA("TGPT_TEST_STOP_AT_FAIL", nullptr, 0)>0) { std::printf("stopped at the first failed check\n"); std::fflush(stdout); ExitProcess(1); }
}

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
// and the transmission mask packed with the diffusion profile), in place of the specular color's. ao: the material's ambient
// occlusion, which HDRP writes into GBuffer3's green behind its tag; so: on the transmission features, the specular occlusion
// HDRP keeps in GBuffer2's red (1 unless set).
// drawn: the mesh the raster pass draws, where it is neither the quad nor the cube (a test's own, PT-04's cylinder).
// moved, before: a thing that moved since the frame before, and where it was then: its motion vectors are its own, as HDRP's
// for an object with a previous matrix (unity_MatrixPreviousM); else the camera's alone.
struct Object { int mesh; M4 world; std::vector<int> materials; bool tracedOnly=false; bool alphaTest=false; uint32_t feature=0; bool forward=false;
                int raster=-1; float roughness=0.6f; int gb2g=-1, gb2b=-1; float f0=-1.f; // f0: a metal's specular color (grey), where set
                float ao=1.f, so=-1.f; const MeshBuffers* drawn=nullptr; bool moved=false; M4 before{};
                float f0red=-1.f, f0green=-1.f, f0blue=-1.f; }; // a tinted metal's specular color, where set (PT-75)

// The game's TG/Clothes/Cloth_Wind vertex shader as its compiled code moves a vertex (docs/agents/cloth.md has the decode):
// what HDRP draws, which the tracer's sway pass (pt_sway) must match. Written from the compiled shader, not from pt_sway.
// Shader Graph's Simple Noise: Hash_Tchou_2_1 at the cell corners, a smoothstep, three octaves at the scale, its half and its
// quarter, weighted 1/8, 1/4, 1/2; the noise read at uv + time x speed; the vertex moved by intensity x noise x (its object-
// space position + the wind's world-space push brought into object space) x the color mask.
static float wind_hash(float x, float y) {
    uint32_t vx=uint32_t(int32_t(x)), vy=uint32_t(int32_t(y));
    vy^=1103515245u; vx+=vy; vx*=vy; vx^=vx>>5u; vx*=0x27d4eb2du;
    return float(vx>>8u)*(1.f/float(0x00ffffff));
}
static float wind_value_noise(float u, float v) {
    const float iu=std::floor(u), iv=std::floor(v);
    float fu=u-iu, fv=v-iv; fu=fu*fu*(3.f-2.f*fu); fv=fv*fv*(3.f-2.f*fv);
    const float r0=wind_hash(iu, iv), r1=wind_hash(iu+1.f, iv), r2=wind_hash(iu, iv+1.f), r3=wind_hash(iu+1.f, iv+1.f);
    const float bottom=r0+(r1-r0)*fu, top=r2+(r3-r2)*fu;
    return bottom+(top-bottom)*fv;
}
// toObject: the instance's world-to-object 3x3, rows.
static V3 cloth_wind_reference(V3 p, float u, float v, V3 color, const TGPT_ClothWind& w, float time, const float toObject[9]) {
    const bool positionUv=(w.flags&TGPT_WIND_POSITION_UV)!=0;
    const float su=positionUv ? p.x : u, sv=positionUv ? p.y : v;
    const float nu=su+time*w.speed, nv=sv+time*w.speed, s=w.noiseScale;
    const float n=wind_value_noise(nu*s, nv*s)*0.125f+wind_value_noise(nu*(s*0.5f), nv*(s*0.5f))*0.25f+wind_value_noise(nu*(s*0.25f), nv*(s*0.25f))*0.5f;
    const V3 push{sv*w.wind*w.direction[0], 0.f, sv*w.wind*w.direction[1]};
    const V3 local{p.x+toObject[0]*push.x+toObject[1]*push.y+toObject[2]*push.z, p.y+toObject[3]*push.x+toObject[4]*push.y+toObject[5]*push.z,
                   p.z+toObject[6]*push.x+toObject[7]*push.y+toObject[8]*push.z};
    const V3 mask=(w.flags&TGPT_WIND_VERTEX_PAINT) ? color*2.f : V3{1, 1, 1};
    return {p.x+w.intensity[0]*n*local.x*mask.x, p.y+w.intensity[1]*n*local.y*mask.y, p.z+w.intensity[2]*n*local.z*mask.z};
}

// The game's vegetation wind as its compiled vertex shaders move a vertex (docs/agents/world.md 4.1 has the decode of
// TG/Vegetation/Vegetation_Grass, Vegetation_Leaves and Vegetation_Trunk): what HDRP draws, which the tracer's pose of its
// plants (pt_sway's step 5) must match. Written from the compiled shaders' instructions, not from pt_sway.
struct PlantNoise { int width=0, height=0; std::vector<uint8_t> texels; }; // one byte a texel, the row at v = 0 first
// The map as D3D11's sampler reads it at mip 0: bilinear, repeating.
static float plant_noise(const PlantNoise& n, float u, float v) {
    if(n.texels.empty()) return 1.f;
    const float x=u*float(n.width)-0.5f, y=v*float(n.height)-0.5f;
    const float fx=std::floor(x), fy=std::floor(y), ax=x-fx, ay=y-fy;
    auto at=[&](int px, int py) {
        px=((px%n.width)+n.width)%n.width; py=((py%n.height)+n.height)%n.height;
        return float(n.texels[size_t(py)*size_t(n.width)+size_t(px)])/255.f;
    };
    const int ix=int(fx), iy=int(fy);
    const float low=at(ix, iy)+(at(ix+1, iy)-at(ix, iy))*ax, high=at(ix, iy+1)+(at(ix+1, iy+1)-at(ix, iy+1))*ax;
    return low+(high-low)*ay;
}
static float sat1(float v) { return std::min(std::max(v, 0.f), 1.f); }
static V3 world_point(const M4& w, V3 p) {
    return {w.m[0][0]*p.x+w.m[0][1]*p.y+w.m[0][2]*p.z+w.m[0][3], w.m[1][0]*p.x+w.m[1][1]*p.y+w.m[1][2]*p.z+w.m[1][3], w.m[2][0]*p.x+w.m[2][1]*p.y+w.m[2][2]*p.z+w.m[2][3]};
}
// Grass. world: object to world; toObject: its inverse's 3x3, rows; strength: the material's _Wind_Strength; speed and
// direction: the game's _VegetationWindSpeed and _VegetationWindDirection (degrees); time: HDRP's _TimeParameters.x.
static V3 grass_wind_reference(V3 p, const M4& world, const float toObject[9], float strength, float time, float speed, float direction, const PlantNoise& noise) {
    const float b=(225.f-direction)*0.017453f, a=(450.f-direction)*0.017453f;
    const float sb=std::sin(b), cb=std::cos(b), sa=std::sin(a), ca=std::cos(a);
    const V3 w=world_point(world, p);
    // The world's xz turned by b about (0.5, 0.5), a tenth of it; the two readings scroll with speed x time, one each way.
    const float cx=w.x-0.5f, cz=w.z-0.5f;
    const float rx=(cx*cb+cz*sb+0.5f)*0.1f, rz=(cz*cb-cx*sb+0.5f)*0.1f;
    const float t=speed*time;
    const float u1=(w.x*0.001f+(t*0.01f+rx))*0.2f, v1=(w.z*0.001f+(t*0.01f+rz))*0.2f;
    const float u2=(w.x*0.001f-(t*0.002f+rx))*0.2f, v2=(w.z*0.001f-(t*0.002f+rz))*0.2f;
    const float gust=std::max(plant_noise(noise, u2, v2), plant_noise(noise, u1, v1));
    const float weight=sat1(sat1(p.y*6.f)*gust);
    float k=speed*strength*p.y*0.0001f+1.f;
    k*=k;
    const float bend=k*k-k;
    // The vertex pushed along the wind in the world, brought back into its object; it moves weight of the way there.
    const V3 push{ca*bend, 0.f, sa*bend};
    const V3 local{toObject[0]*push.x+toObject[1]*push.y+toObject[2]*push.z, toObject[3]*push.x+toObject[4]*push.y+toObject[5]*push.z,
                   toObject[6]*push.x+toObject[7]*push.y+toObject[8]*push.z};
    return p+local*weight;
}
// Leaves and trunks (a trunk's wind has its _MaskHeight as bendHeight, no pivot offset, its _WindDistanceFade as fade).
// camera, forward: the camera's position and the direction it looks along.
static V3 leaves_wind_reference(V3 p, const M4& world, const float toObject[9], const TGPT_PlantWind& wind, float time, float speed, float direction,
                                V3 camera, V3 forward, const PlantNoise& noise) {
    if(!(wind.flags&TGPT_PLANT_BEND)) return p;
    // The object's own turn about the up axis, taken out of the wind's direction: the axis is the same in the world for all.
    const float theta=std::atan2(toObject[0], toObject[6]);
    const float phi=((450.f-direction)-theta*57.29578f)*0.017453f;
    float ax=std::cos(phi), az=std::sin(phi);
    const float length=std::sqrt(ax*ax+az*az); ax/=length; az/=length;
    const V3 w=world_point(world, p);
    const float u=w.x*0.001f+time*0.02f, v=w.y*0.001f+time*0.02f;
    const float n=plant_noise(noise, u, v)*plant_noise(noise, u+u, v+v);
    const float angle=n*speed*wind.strength*0.017453f, s=std::sin(angle), c=std::cos(angle), omc=1.f-c;
    const V3 turned{(ax*ax*omc+c)*p.x-az*s*p.y+ax*az*omc*p.z, az*s*p.x+c*p.y-ax*s*p.z, ax*az*omc*p.x+ax*s*p.y+(az*az*omc+c)*p.z};
    const float mask=sat1((p.y+wind.pivotOffsetY)/std::min(std::max(wind.bendHeight, 0.1f), 100.f));
    const V3 bent=p+(turned-p)*mask;
    const float viewZ=-dot(forward, w-camera);
    const float fade=sat1((viewZ+wind.fade[0])/(wind.fade[0]-wind.fade[1]));
    return bent+(p-bent)*fade;
}

// ------------------------------------------------------------------ a reference random walk (PT-40)
// Light inside a subsurface material, walked on the CPU in double precision: what the tracer's walk (pt_common.hlsli
// subsurface_walk) is checked against. Written from the method, not from the shader: a convex thing given by its faces'
// planes (inside: n.x <= d for every one), a homogeneous medium from the surface's color and its diffusion profile's
// shape by HDRP's own conversion (its path tracer's RemapSubSurfaceScatteringParameters), in at a face cosine-distributed
// about its inward normal, free flights from one channel's exponential (the channel by what the walk carries, the three
// densities weighed together), even scattering, Russian roulette, out where a flight crosses a face.
struct WalkPlane { double n[3]; double d; };
struct WalkMedium { double sigmaT[3], keeps[3]; };
// Independent double-precision solution of the isotropic transport H equation.
// Midpoint quadrature (256 directions); no use of the shader's inverse table.
static double walk_reflectance(double q) {
    constexpr int N=256; double h[N], next[N];
    std::fill_n(h, N, 1.0); const double omega=1-q*q;
    for(int it=0;it<400;++it) {
        double error=0;
        for(int i=0;i<N;++i) {
            const double mu=(i+0.5)/N; double sum=0;
            for(int j=0;j<N;++j) { double u=(j+0.5)/N; sum+=u*h[j]/(mu+u)/N; }
            next[i]=0.5*(h[i]+1/(q+0.5*omega*sum)); error=std::max(error,std::fabs(next[i]-h[i]));
        }
        std::copy_n(next,N,h); if(error<1e-11) break;
    }
    double moment=0; for(int i=0;i<N;++i) moment+=(i+0.5)/N*h[i]/N;
    return 1-2*q*moment;
}
static double walk_single_albedo(double a) {
    if(a<=0) return 0; if(a>=1) return 1;
    double lo=0,hi=1;
    for(int i=0;i<32;++i) { const double q=0.5*(lo+hi); if(walk_reflectance(q)>a) lo=q; else hi=q; }
    const double q=0.5*(lo+hi); return 1-q*q;
}
static WalkMedium walk_medium(const double albedo[3], const double shape[3]) {
    WalkMedium m{};
    for(int c=0;c<3;++c) {
        const double a=std::clamp(albedo[c], 0.0, 1.0);
        m.keeps[c]=walk_single_albedo(a);
        m.sigmaT[c]=shape[c]/(1.9-a+3.5*(a-0.8)*(a-0.8));
    }
    return m;
}
static void walk_basis(const double n[3], double t[3], double b[3]) {
    const double a[3]={std::fabs(n[0])<0.9 ? 1.0 : 0.0, std::fabs(n[0])<0.9 ? 0.0 : 1.0, 0.0};
    t[0]=a[1]*n[2]-a[2]*n[1]; t[1]=a[2]*n[0]-a[0]*n[2]; t[2]=a[0]*n[1]-a[1]*n[0];
    const double l=std::sqrt(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); for(int k=0;k<3;++k) t[k]/=l;
    b[0]=n[1]*t[2]-n[2]*t[1]; b[1]=n[2]*t[0]-n[0]*t[2]; b[2]=n[0]*t[1]-n[1]*t[0];
}
// One walk from a point of face `entry`. True: it left through face `exitFace` at `where`, carrying `carried` per channel.
static bool walk_reference(const std::vector<WalkPlane>& planes, const double start[3], int entry, const WalkMedium& m, std::mt19937_64& rng,
                           double where[3], int& exitFace, double carried[3], int& flights) {
    std::uniform_real_distribution<double> U(0.0, 1.0);
    const double pi=3.14159265358979323846;
    double p[3]={start[0], start[1], start[2]}, dir[3];
    {
        const double in[3]={-planes[size_t(entry)].n[0], -planes[size_t(entry)].n[1], -planes[size_t(entry)].n[2]};
        double t[3], b[3]; walk_basis(in, t, b);
        const double u=U(rng), r=std::sqrt(u), phi=2.0*pi*U(rng), z=std::sqrt(std::max(0.0, 1.0-u));
        for(int k=0;k<3;++k) dir[k]=t[k]*r*std::cos(phi)+b[k]*r*std::sin(phi)+in[k]*z;
    }
    carried[0]=carried[1]=carried[2]=1.0;
    for(flights=1;;++flights) {
        const double sum=carried[0]+carried[1]+carried[2];
        if(!(sum>0.0)) return false;
        const double pick[3]={carried[0]/sum, carried[1]/sum, carried[2]/sum};
        const double u=U(rng);
        const int c=u<pick[0] ? 0 : (u<pick[0]+pick[1] ? 1 : 2);
        const double t=-std::log(std::max(1.0-U(rng), 1e-300))/m.sigmaT[c];
        double out=1e300; int face=-1;
        for(size_t i=0;i<planes.size();++i) {
            const double along=planes[i].n[0]*dir[0]+planes[i].n[1]*dir[1]+planes[i].n[2]*dir[2];
            if(along<=1e-14) continue;
            const double d=(planes[i].d-(planes[i].n[0]*p[0]+planes[i].n[1]*p[1]+planes[i].n[2]*p[2]))/along;
            if(d<out) { out=std::max(d, 0.0); face=int(i); }
        }
        if(face>=0 && out<=t) {
            double tr[3], w=0; for(int k=0;k<3;++k) { tr[k]=std::exp(-m.sigmaT[k]*out); w+=pick[k]*tr[k]; }
            for(int k=0;k<3;++k) { carried[k]*=tr[k]/w; where[k]=p[k]+dir[k]*out; }
            exitFace=face;
            return true;
        }
        double tr[3], w=0; for(int k=0;k<3;++k) { tr[k]=m.sigmaT[k]*std::exp(-m.sigmaT[k]*t); w+=pick[k]*tr[k]; }
        for(int k=0;k<3;++k) { carried[k]*=m.keeps[k]*tr[k]/w; p[k]+=dir[k]*t; }
        const double z=1.0-2.0*U(rng), r=std::sqrt(std::max(0.0, 1.0-z*z)), phi=2.0*pi*U(rng);
        dir[0]=r*std::cos(phi); dir[1]=r*std::sin(phi); dir[2]=z;
        const double q=std::min(std::max(carried[0], std::max(carried[1], carried[2])), 1.0);
        if(!(q>0.0) || U(rng)>=q) return false;
        for(int k=0;k<3;++k) carried[k]/=q;
    }
}
// The Direct view's mean over a part of the screen for such a thing lit by one point light of `intensity` candela, as the
// tracer shows it: what a walk from the camera's pixel carries over the surface's color, times the light where the walk
// leaves the thing (intensity x cosine / distance^2 on a convex thing nothing else shadows), over pi, per channel and
// times `toView`. The rays: from `eye` through the screen region x0..x1, y0..y1 (in -1..1, y up or down alike for a scene
// symmetric about the view's axis). flightsMean: the walks' mean number of flights. lost: the share of the rays that
// missed the thing (none, for a region inside its outline). through: the share of what the walks carry that leaves
// through the face opposite the one entered, per channel (a slab's diffuse transmission; boxes only).
struct WalkView { double light[3]; double flightsMean; double lost; double through[3]; };
static WalkView walk_view(const std::vector<WalkPlane>& planes, const double albedo[3], const double shape[3], V3 eye, V3 target, float fovDegrees, float aspect,
                          double x0, double x1, double y0, double y1, V3 lamp, double intensity, double toView, int walks, uint64_t seed) {
    const WalkMedium m=walk_medium(albedo, shape);
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    const V3 f=normalize(target-eye), r=normalize(cross({0, 1, 0}, f)), u=cross(f, r);
    const double tanHalf=std::tan(double(fovDegrees)*3.14159265358979323846/360.0);
    WalkView v{}; double flights=0; int missed=0;
    for(int i=0;i<walks;++i) {
        const double sx=(x0+(x1-x0)*U(rng))*tanHalf*double(aspect), sy=(y0+(y1-y0)*U(rng))*tanHalf;
        double d[3]={f.x+r.x*sx+u.x*sy, f.y+r.y*sx+u.y*sy, f.z+r.z*sx+u.z*sy};
        const double dl=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); for(int k=0;k<3;++k) d[k]/=dl;
        const double o[3]={eye.x, eye.y, eye.z};
        double in=-1e300, out=1e300; int face=-1;
        for(size_t k=0;k<planes.size();++k) {
            const double along=planes[k].n[0]*d[0]+planes[k].n[1]*d[1]+planes[k].n[2]*d[2];
            const double t=(planes[k].d-(planes[k].n[0]*o[0]+planes[k].n[1]*o[1]+planes[k].n[2]*o[2]))/along;
            if(along<0.0) { if(t>in) { in=t; face=int(k); } } else if(along>0.0) out=std::min(out, t);
        }
        if(face<0 || !(in<out) || !(in>0.0)) { ++missed; continue; }
        const double start[3]={o[0]+d[0]*in, o[1]+d[1]*in, o[2]+d[2]*in};
        double where[3], carried[3]; int exitFace=-1, n=0;
        const bool left=walk_reference(planes, start, face, m, rng, where, exitFace, carried, n);
        flights+=n;
        if(!left) continue;
        // Opposite faces of a box are stored in pairs (walk_box).
        if((exitFace^1)==face) for(int k=0;k<3;++k) v.through[k]+=carried[k];
        const double l[3]={lamp.x-where[0], lamp.y-where[1], lamp.z-where[2]};
        const double d2=l[0]*l[0]+l[1]*l[1]+l[2]*l[2];
        const double c=(planes[size_t(exitFace)].n[0]*l[0]+planes[size_t(exitFace)].n[1]*l[1]+planes[size_t(exitFace)].n[2]*l[2])/std::sqrt(d2);
        if(c<=0.0) continue;
        for(int k=0;k<3;++k) v.light[k]+=carried[k]/std::max(albedo[k], 1e-3)*intensity*c/d2;
    }
    const int counted=std::max(walks-missed, 1);
    for(int k=0;k<3;++k) { v.light[k]*=toView/double(counted); v.through[k]/=double(counted); }
    v.flightsMean=flights/double(counted); v.lost=double(missed)/double(std::max(walks, 1));
    return v;
}
// A box's six planes (opposite faces in pairs), and a prism's (a regular polygon of `sides` around the y axis, its corners
// `radius` out, flat ends).
static std::vector<WalkPlane> walk_box(V3 centre, V3 size) {
    std::vector<WalkPlane> p;
    const double c[3]={centre.x, centre.y, centre.z}, h[3]={0.5*size.x, 0.5*size.y, 0.5*size.z};
    for(int a=0;a<3;++a) for(int s=-1;s<=1;s+=2) { WalkPlane q{}; q.n[a]=s; q.d=s*c[a]+h[a]; p.push_back(q); }
    return p;
}
static std::vector<WalkPlane> walk_prism(V3 centre, int sides, double radius, double height) {
    std::vector<WalkPlane> p;
    const double pi=3.14159265358979323846, apothem=radius*std::cos(pi/sides);
    for(int i=0;i<sides;++i) {
        WalkPlane q{}; const double a=2.0*pi*i/sides; q.n[0]=std::cos(a); q.n[2]=std::sin(a);
        q.d=q.n[0]*centre.x+q.n[2]*centre.z+apothem; p.push_back(q);
    }
    for(int s=-1;s<=1;s+=2) { WalkPlane q{}; q.n[1]=s; q.d=s*centre.y+0.5*height; p.push_back(q); }
    return p;
}

static const char* kSceneShader=R"HLSL(
cbuffer Object : register(b0) {
    row_major float4x4 viewProj; row_major float4x4 motionViewProj; row_major float4x4 prevViewProj; row_major float4x4 world;
    float4 cameraPos; float4 prevCameraPos; float4 albedo; float4 f0rough; float4 emissive; float4 sunDir; float4 sunColor; float4 misc;
    row_major float4x4 prevWorld; // where the object was the frame before (its world matrix when it stood still)
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
    o.prev = mul(prevViewProj, float4(mul(prevWorld, float4(i.pos, 1)).xyz - prevCameraPos.xyz, 1));
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
        b *= misc.z; // their scale (1: as above)
        n = normalize(n + b - dot(b, n) * n);
    }
    o.gb0 = albedo.a > 0.5 ? float4(0, 0, 0, 0) : float4(base, 1); // albedo.a: drawn forward
    o.nb = float4(Pack888(saturate(OctEncode(n) * 0.5 + 0.5)), f0rough.w);
    o.gb2 = float4(FastLinearToSRGB(f0rough.rgb), misc.w / 255.0);
    float exposure = misc.y;
    o.gb3 = any(emissive.rgb > 0) ? float4(emissive.rgb * exposure, 0) : float4(2048, emissive.a, 1024, 0); // HDRP's tag, its AO in green
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

// PT-45: the game's own textures opened by the tracer's D3D12 device, with no copy. The hook has been on the device since
// its swapchain was made, as in the game: textures made only for shaders to read come out shareable, and
// TGPT_RegisterSharedTexture opens one and writes its view. Every texel of every mip read through that view (a copy of the
// tracer's descriptor) must equal what D3D11 reads from the same texture: the same memory, format and mips.
//
// Phase 3: the start-up self-test shared and read back one texture of each format the game uses before the hook went in
// (its line in the native log, nativeLog), and a format it failed is never shared (TGPT_TEST_SHARE_BREAK=<DXGI format>
// makes it read one wrong byte: the ctest tgpt_share_selftest_fails_one); the video memory cap stops opening textures, the
// largest first, as the game's count nears the limit (TGPT_TEST_SHARE_LIMIT_MB sets the limit).
static void shared_texture_checks(ID3D11Device* device, ID3D11DeviceContext* context, decltype(&TGPT_RegisterSharedTexture) RegisterShared,
                                  decltype(&TGPT_RegisterTexture) RegisterTexture, decltype(&TGPT_ReadStatus) ReadStatus, decltype(&TGPT_ClearScene) ClearScene,
                                  const fs::path& nativeLog) {
    static const char* kReadMips=R"HLSL(
Texture2D<float4> tex : register(t0);
RWByteAddressBuffer result : register(u0);
cbuffer Mip : register(b0) { uint mip, width, height, offset; };
[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
    if(id.x >= width || id.y >= height) return;
    result.Store4(offset + (id.y * width + id.x) * 16, asuint(tex.Load(int3(id.xy, mip))));
}
)HLSL";
    auto ptr=[](IUnknown* p) { return uint64_t(uintptr_t(p)); };
    auto info=[] { TGPT_SharedTexture i{}; i.size=sizeof(i); i.abi=TGPT_ABI; return i; };
    // The self-test: on its own thread from the bridge's device on; the hook goes in when it ends.
    char brokenText[16]{};
    const uint32_t broken=GetEnvironmentVariableA("TGPT_TEST_SHARE_BREAK", brokenText, sizeof brokenText) ? uint32_t(std::strtoul(brokenText, nullptr, 10)) : 0u;
    {
        auto st=info(); RegisterShared(0, &st);
        const auto waited=std::chrono::steady_clock::now();
        for(int k=0;k<400 && (st.hook==0 || st.hook==3);++k) { Sleep(25); RegisterShared(0, &st); }
        const uint32_t passed=st.selfTest&0xFFu, tested=(st.selfTest>>8)&0xFFu, ms=st.selfTest>>16;
        std::string line;
        {
            std::ifstream log(nativeLog); std::string l;
            while(std::getline(log, l)) if(l.find("Texture sharing self-test on ")!=std::string::npos) line=l;
        }
        std::printf("      start-up self-test: %u of %u formats passed in %u ms (this test waited %.0f ms more for it); the hook %u\n      native log: %s\n",
                    passed, tested, ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-waited).count(), st.hook, line.c_str());
        if(!broken) expect(st.hook==1 && tested>=20 && passed==tested && line.find("FAILED")==std::string::npos,
                           "the start-up self-test reads every format the game uses back bit for bit on this card, and the hook goes in after it");
        else expect(st.hook==1 && tested>=20 && passed==tested-1 && line.find("FAILED: D3D12 read different bytes at mip 0")!=std::string::npos,
                    "a format the start-up self-test reads back wrong fails alone, and the hook goes in for the others");
        expect(!line.empty() && std::count(line.begin(), line.end(), ',')>=24, "the self-test writes one line with a verdict for each format");
    }
    struct Made { const char* name; DXGI_FORMAT format; UINT w, h; D3D11_USAGE usage; bool create2D1; ComPtr<ID3D11Texture2D> t; uint32_t mips=0; uint64_t bytes=0; };
    std::vector<Made> made={{"BC1", DXGI_FORMAT_BC1_UNORM, 512, 512, D3D11_USAGE_DEFAULT, false}, {"BC3 sRGB", DXGI_FORMAT_BC3_UNORM_SRGB, 512, 128, D3D11_USAGE_IMMUTABLE, false},
                            {"BC5", DXGI_FORMAT_BC5_UNORM, 256, 256, D3D11_USAGE_DEFAULT, false}, {"BC7 sRGB", DXGI_FORMAT_BC7_UNORM_SRGB, 1024, 1024, D3D11_USAGE_DEFAULT, true}};
    uint32_t seed=12345;
    for(auto& m:made) {
        // Random blocks, every mip, given at creation as the game gives them; BC7 through CreateTexture2D1 (its own hook).
        while((std::max(m.w, m.h)>>m.mips)>0) ++m.mips;
        const UINT block=m.format==DXGI_FORMAT_BC1_UNORM ? 8 : 16;
        std::vector<std::vector<uint8_t>> data(m.mips); std::vector<D3D11_SUBRESOURCE_DATA> init(m.mips);
        for(uint32_t i=0;i<m.mips;++i) {
            const UINT w=std::max(1u, m.w>>i), h=std::max(1u, m.h>>i), bw=(w+3)/4, bh=(h+3)/4;
            data[i].resize(size_t(bw)*bh*block); for(auto& b:data[i]) { seed=seed*1664525u+1013904223u; b=uint8_t(seed>>24); }
            init[i]={data[i].data(), bw*block, 0}; m.bytes+=uint64_t(w)*h*16;
        }
        if(m.create2D1) {
            ComPtr<ID3D11Device3> d3; ComPtr<ID3D11Texture2D1> t1;
            D3D11_TEXTURE2D_DESC1 d{m.w, m.h, m.mips, 1, m.format, {1, 0}, m.usage, D3D11_BIND_SHADER_RESOURCE, 0, 0, D3D11_TEXTURE_LAYOUT_UNDEFINED};
            if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3))) && SUCCEEDED(d3->CreateTexture2D1(&d, init.data(), &t1))) t1.As(&m.t);
        } else {
            D3D11_TEXTURE2D_DESC d{m.w, m.h, m.mips, 1, m.format, {1, 0}, m.usage, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            device->CreateTexture2D(&d, init.data(), &m.t);
        }
    }
    // D3D11's reading, every mip into one buffer.
    auto cs11=compile(kReadMips, "main", "cs_5_0"); ComPtr<ID3D11ComputeShader> shader11; ComPtr<ID3D11Buffer> constants11;
    if(cs11) device->CreateComputeShader(cs11->GetBufferPointer(), cs11->GetBufferSize(), nullptr, &shader11);
    D3D11_BUFFER_DESC cd{16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0}; device->CreateBuffer(&cd, nullptr, &constants11);
    auto read11=[&](const Made& m) {
        std::vector<uint8_t> v(m.bytes);
        D3D11_BUFFER_DESC bd{UINT(m.bytes), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0};
        ComPtr<ID3D11Buffer> out, back; ComPtr<ID3D11UnorderedAccessView> uav; ComPtr<ID3D11ShaderResourceView> srv;
        device->CreateBuffer(&bd, nullptr, &out);
        bd.Usage=D3D11_USAGE_STAGING; bd.BindFlags=0; bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ; bd.MiscFlags=0; device->CreateBuffer(&bd, nullptr, &back);
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{DXGI_FORMAT_R32_TYPELESS, D3D11_UAV_DIMENSION_BUFFER}; ud.Buffer.NumElements=UINT(m.bytes/4); ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
        if(!out || !back || !shader11 || FAILED(device->CreateUnorderedAccessView(out.Get(), &ud, &uav)) || FAILED(device->CreateShaderResourceView(m.t.Get(), nullptr, &srv))) return v;
        context->CSSetShader(shader11.Get(), nullptr, 0); context->CSSetShaderResources(0, 1, srv.GetAddressOf());
        context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr); context->CSSetConstantBuffers(0, 1, constants11.GetAddressOf());
        for(uint32_t i=0, offset=0;i<m.mips;++i) {
            const uint32_t c[4]={i, std::max(1u, m.w>>i), std::max(1u, m.h>>i), offset};
            context->UpdateSubresource(constants11.Get(), 0, nullptr, c, 0, 0); context->Dispatch((c[1]+7)/8, (c[2]+7)/8, 1); offset+=c[1]*c[2]*16;
        }
        context->ClearState(); context->CopyResource(back.Get(), out.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        if(SUCCEEDED(context->Map(back.Get(), 0, D3D11_MAP_READ, 0, &map))) { std::memcpy(v.data(), map.pData, v.size()); context->Unmap(back.Get(), 0); }
        return v;
    };
    // The same through the tracer's view, on the bridge's D3D12 device (its own queue, pipeline and heap).
    ComPtr<ID3D12Device> d12; ComPtr<ID3D12RootSignature> root; ComPtr<ID3D12PipelineState> pso; ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list; ComPtr<ID3D12Fence> fence; ComPtr<ID3D12DescriptorHeap> heap; uint64_t fenceValue=0;
    auto setup12=[&](ID3D12Resource* opened) {
        if(d12) return true;
        if(!opened || FAILED(opened->GetDevice(IID_PPV_ARGS(&d12)))) return false;
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[0].DescriptorTable={1, &range};
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[2].Constants.Num32BitValues=4;
        D3D12_ROOT_SIGNATURE_DESC rd{3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        auto serialize=reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12SerializeRootSignature"));
        ComPtr<ID3DBlob> blob, errors; auto cs12=compile(kReadMips, "main", "cs_5_1");
        if(!serialize || !cs12 || FAILED(serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
           FAILED(d12->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root.Get(); pd.CS={cs12->GetBufferPointer(), cs12->GetBufferSize()};
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        return SUCCEEDED(d12->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso))) && SUCCEEDED(d12->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) &&
               SUCCEEDED(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator))) &&
               SUCCEEDED(d12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) && SUCCEEDED(list->Close()) &&
               SUCCEEDED(d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) && SUCCEEDED(d12->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
    };
    auto read12=[&](const Made& m, const TGPT_SharedTexture& i) {
        std::vector<uint8_t> v(m.bytes);
        if(!setup12(reinterpret_cast<ID3D12Resource*>(uintptr_t(i.resource)))) return v;
        auto buffer=[&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
            D3D12_HEAP_PROPERTIES hp{}; hp.Type=type; D3D12_RESOURCE_DESC bd{}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=m.bytes; bd.Height=1;
            bd.DepthOrArraySize=1; bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; bd.Flags=flags;
            ComPtr<ID3D12Resource> r; d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(&r)); return r;
        };
        auto out=buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        auto back=buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        if(!out || !back) return v;
        d12->CopyDescriptorsSimple(1, heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(i.view)}, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        allocator->Reset(); list->Reset(allocator.Get(), pso.Get());
        auto barrier=[&](D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
            D3D12_RESOURCE_BARRIER b{}; b.Transition={out.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to}; list->ResourceBarrier(1, &b);
        };
        barrier(D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->SetComputeRootSignature(root.Get()); ID3D12DescriptorHeap* heaps[]={heap.Get()}; list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart()); list->SetComputeRootUnorderedAccessView(1, out->GetGPUVirtualAddress());
        for(uint32_t k=0, offset=0;k<m.mips;++k) {
            const uint32_t c[4]={k, std::max(1u, m.w>>k), std::max(1u, m.h>>k), offset};
            list->SetComputeRoot32BitConstants(2, 4, c, 0); list->Dispatch((c[1]+7)/8, (c[2]+7)/8, 1); offset+=c[1]*c[2]*16;
        }
        barrier(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(back.Get(), 0, out.Get(), 0, m.bytes); list->Close();
        ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1, lists); queue->Signal(fence.Get(), ++fenceValue);
        const HANDLE done=CreateEventW(nullptr, FALSE, FALSE, nullptr); fence->SetEventOnCompletion(fenceValue, done); WaitForSingleObject(done, 10000); CloseHandle(done);
        void* mapped=nullptr; if(SUCCEEDED(back->Map(0, nullptr, &mapped))) { std::memcpy(v.data(), mapped, v.size()); back->Unmap(0, nullptr); }
        return v;
    };
    std::vector<int32_t> numbers; std::vector<TGPT_SharedTexture> infos;
    for(auto& m:made) {
        auto i=info(); const int32_t n=m.t ? RegisterShared(ptr(m.t.Get()), &i) : -100;
        auto again=info(); const int32_t same=m.t ? RegisterShared(ptr(m.t.Get()), &again) : -100;
        numbers.push_back(n); infos.push_back(i);
        if(uint32_t(m.format)==broken) {
            // The format the self-test failed on purpose: born without the flag, so it keeps its atlas tile (copies).
            const int32_t tile=m.t ? RegisterTexture(9002, ptr(m.t.Get())) : -100;
            std::printf("      %s (the format the self-test failed): %d, misc 0x%X; its atlas tile %d\n", m.name, n, i.miscFlags, tile);
            expect(n==TGPT_SHARE_SELF_TEST && !(i.miscFlags&D3D11_RESOURCE_MISC_SHARED) && tile>=0,
                   "a texture in a format the start-up self-test failed is not made shareable and keeps its atlas tile");
            continue;
        }
        const auto want=read11(m), got=n>=0 ? read12(m, i) : std::vector<uint8_t>();
        size_t differ=got.size()==want.size() ? 0 : want.size();
        for(size_t k=0;k<want.size() && k<got.size();k+=16) if(std::memcmp(&want[k], &got[k], 16)) ++differ;
        bool someLight=false; for(size_t k=0;k<want.size() && !someLight;++k) someLight=want[k]!=0;
        std::printf("      %s %ux%u, %u mips (%s): number %d (again %d), misc 0x%X, %u mips in D3D11, %.2f MB on the GPU; %zu of %llu texels differ through D3D12's view\n",
                    m.name, m.w, m.h, m.mips, m.create2D1 ? "CreateTexture2D1" : "CreateTexture2D", n, same, i.miscFlags, i.mips, i.bytes/1048576.0, differ,
                    (unsigned long long)(m.bytes/16));
        char what[160]; std::snprintf(what, sizeof what, "the game's %s texture is made shareable by the hook and opened on D3D12 with all its mips", m.name);
        expect(n>=0 && same==n && (i.miscFlags&D3D11_RESOURCE_MISC_SHARED) && i.mips==m.mips && i.bytes>=m.bytes/16/4 && i.resource && i.view, what);
        std::snprintf(what, sizeof what, "every texel of every mip of the %s texture reads the same through the tracer's D3D12 view as through D3D11", m.name);
        expect(n>=0 && someLight && differ==0, what);
    }
    // The game frees a texture: its death notice lets go of the D3D12 side (no frame recorded yet: at once).
    if(numbers[0]>=0) {
        auto* opened=reinterpret_cast<ID3D12Resource*>(uintptr_t(infos[0].resource)); opened->AddRef();
        auto before=info(), after=info(); RegisterShared(0, &before);
        made[0].t.Reset(); context->Flush();
        TGPT_Status status{sizeof(TGPT_Status), TGPT_ABI};
        for(int k=0;k<40;++k) { ReadStatus(&status); RegisterShared(0, &after); if(after.open<before.open) break; Sleep(50); }
        opened->AddRef(); const ULONG references=opened->Release(); opened->Release();
        std::printf("      the BC1 texture freed by D3D11: open %u -> %u, freed by the game %u -> %u, %.2f -> %.2f MB open, D3D12 references left %lu (1: the test's own)\n",
                    before.open, after.open, before.freed, after.freed, before.openBytes/1048576.0, after.openBytes/1048576.0, references);
        expect(after.open==before.open-1 && after.freed==before.freed+1 && after.openBytes<before.openBytes && references==1,
               "a texture the game frees is let go of on the D3D12 side too");
    }
    // PT-64 (the owner: "Don't do copies"): every size is shared. A 64x64 BC1 (2 KB) and a 128x128 one (8 KB) get a number and
    // the flag as any texture does (until then they kept their 64x64 atlas tiles, since each takes a 64 KB page of its own
    // while both devices hold it). What isn't shared: a render target, an array.
    {
        std::vector<uint8_t> bytes(512*512, 0x5A);
        D3D11_TEXTURE2D_DESC d{64, 64, 1, 1, DXGI_FORMAT_BC1_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        D3D11_SUBRESOURCE_DATA init[2]={{bytes.data(), 16*8, 0}, {bytes.data(), 16*8, 0}};
        ComPtr<ID3D11Texture2D> tiny, light, target, array;
        device->CreateTexture2D(&d, init, &tiny);
        d.Width=d.Height=128; init[0].SysMemPitch=32*8; device->CreateTexture2D(&d, init, &light);
        auto li=info(); const int32_t l=light ? RegisterShared(ptr(light.Get()), &li) : -100;
        expect(l>=0 && (li.miscFlags&D3D11_RESOURCE_MISC_SHARED)!=0, "a texture under 64 KB is shared too (no copy of it is read)");
        d.Width=d.Height=128; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET; device->CreateTexture2D(&d, nullptr, &target);
        // An array big enough to be shared (64 KB or more): 512x512 BC1, two slices.
        d.Width=d.Height=512; d.Format=DXGI_FORMAT_BC1_UNORM; d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.ArraySize=2; init[0].SysMemPitch=init[1].SysMemPitch=128*8; device->CreateTexture2D(&d, init, &array);
        auto si=info(), ti=info(), ai=info();
        const int32_t s=tiny ? RegisterShared(ptr(tiny.Get()), &si) : -100, t=target ? RegisterShared(ptr(target.Get()), &ti) : -100;
        const int32_t a=array ? RegisterShared(ptr(array.Get()), &ai) : -100;
        const int32_t tile=tiny ? RegisterTexture(9001, ptr(tiny.Get())) : -100;
        std::printf("      a 128x128 BC1 %d (misc 0x%X, %llu bytes counted), a 64x64 one %d (misc 0x%X, %llu bytes counted); not shared: render target %d (misc 0x%X), array %d (misc 0x%X); the 64x64 one's atlas tile %d\n",
                    l, li.miscFlags, static_cast<unsigned long long>(li.bytes), s, si.miscFlags, static_cast<unsigned long long>(si.bytes), t, ti.miscFlags, a, ai.miscFlags, tile);
        expect(s>=0 && (si.miscFlags&D3D11_RESOURCE_MISC_SHARED)!=0 && t==TGPT_SHARE_FLAGS && !(ti.miscFlags&D3D11_RESOURCE_MISC_SHARED) &&
               a==TGPT_SHARE_SHAPE, "a 64x64 texture is shared as any other; one the hook leaves alone, or the registry can't take, gets no number and why");
        expect(tile>=0, "a texture has its atlas tile too (what reads no texture of the game's by number still reads it)");
    }
    // Phase 3, the video memory cap. The limit just above what Windows counts for this process now: a 2048x2048 BC3 texture
    // (5.6 MB, which wants 22 MB of room: a quarter of what is left at most) is not opened, a 256x256 one (87 KB) is; the
    // limit under the count: not even that one; no limit: both open when asked again. And what opening a texture adds to
    // Windows' count at once (the second count the cap is for).
    {
        ComPtr<IDXGIDevice> dxgiDevice; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIAdapter3> adapter3;
        const bool counted=SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && SUCCEEDED(adapter.As(&adapter3));
        auto memory=[&] { DXGI_QUERY_VIDEO_MEMORY_INFO m{}; if(counted) adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &m); return m; };
        auto bc3=[&](UINT size) {
            UINT mips=0; while((size>>mips)>0) ++mips;
            std::vector<std::vector<uint8_t>> data(mips); std::vector<D3D11_SUBRESOURCE_DATA> init(mips);
            for(UINT k=0;k<mips;++k) { const UINT b=(std::max(1u, size>>k)+3)/4; data[k].assign(size_t(b)*b*16, uint8_t(0x33+k)); init[k]={data[k].data(), b*16, 0}; }
            D3D11_TEXTURE2D_DESC d{size, size, mips, 1, DXGI_FORMAT_BC3_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, init.data(), &t); return t;
        };
        // (Not "small": the Windows headers define it as char.)
        auto big=bc3(2048), little=bc3(256), little2=bc3(256);
        context->Flush();
        auto limit=[](int64_t mb) { SetEnvironmentVariableA("TGPT_TEST_SHARE_LIMIT_MB", mb<0 ? nullptr : std::to_string(mb).c_str()); };
        const auto before=memory();
        limit(int64_t(before.CurrentUsage>>20)+16);
        auto li=info(), si=info(), ti=info(), ai=info(), bi=info();
        const int32_t l=big ? RegisterShared(ptr(big.Get()), &li) : -100;
        const int32_t s=little ? RegisterShared(ptr(little.Get()), &si) : -100;
        limit(std::max<int64_t>(1, int64_t(before.CurrentUsage>>20)-64));
        const int32_t t=little2 ? RegisterShared(ptr(little2.Get()), &ti) : -100;
        limit(-1);
        const auto opening=memory();
        const int32_t a=big ? RegisterShared(ptr(big.Get()), &ai) : -100;
        const auto opened=memory();
        Sleep(200);
        const auto later=memory();
        const int32_t b=little2 ? RegisterShared(ptr(little2.Get()), &bi) : -100;
        std::string line;
        {
            std::ifstream log(nativeLog); std::string l2;
            while(std::getline(log, l2)) if(l2.find("Texture sharing: video memory near this card's budget")!=std::string::npos) line=l2;
        }
        std::printf("      video memory: budget %llu MB, %llu MB counted; at a limit 16 MB above it: 2048 BC3 %d, 256 BC3 %d; at a limit under it: 256 BC3 %d; no limit: %d, %d\n"
                    "      opening the 2048 BC3 (%.2f MB) moved Windows' count by %+.2f MB at once, %+.2f MB 200 ms later\n      native log: %s\n",
                    (unsigned long long)(before.Budget>>20), (unsigned long long)(before.CurrentUsage>>20), l, s, t, a, b, ai.bytes/1048576.0,
                    (double(opened.CurrentUsage)-double(opening.CurrentUsage))/1048576.0, (double(later.CurrentUsage)-double(opening.CurrentUsage))/1048576.0, line.c_str());
        expect(counted && l==TGPT_SHARE_BUDGET && s>=0, "near the video memory limit a large texture is not opened and a small one is (the largest stop first)");
        expect(t==TGPT_SHARE_BUDGET, "over the video memory limit no texture is opened");
        expect(a>=0 && b>=0, "with room again the textures the cap kept back are opened when asked again");
        expect(!line.empty(), "the cap says so in the native log");
    }
    // The scene that follows starts from nothing: its tiles from 0, these textures let go of.
    auto last=info(); ClearScene(); RegisterShared(0, &last);
    expect(last.open==0, "a cleared scene lets go of every shared texture");
    context->ClearState();
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const bool water10Only=GetEnvironmentVariableA("TGPT_TEST_WATER10_ONLY", nullptr, 0)>0;
    const bool waterOnly=water10Only || GetEnvironmentVariableA("TGPT_TEST_WATER_SURFACE", nullptr, 0)>0;
    // (TGPT_TEST_WATER_COST=1 with it: the fixture's cost run alone, at the game's size.)
    const bool waterCost=waterOnly && GetEnvironmentVariableA("TGPT_TEST_WATER_COST", nullptr, 0)>0;
    if(waterOnly && !waterCost) { W=128; H=72; }
    // (TGPT_TEST_WATER_SEA=1 with it: the open sea as the game sends it alone, water_sea_gpu.inl, at 640 x 360.)
    const bool waterSea=waterOnly && !waterCost && GetEnvironmentVariableA("TGPT_TEST_WATER_SEA", nullptr, 0)>0;
    if(waterSea) { W=640; H=360; }
    // (TGPT_TEST_WATER_LANDED=1 with it: what the caustics land on what lies under the water alone, water_landed_gpu.inl, at 640 x 360.)
    const bool waterLanded=waterOnly && !waterCost && !waterSea && GetEnvironmentVariableA("TGPT_TEST_WATER_LANDED", nullptr, 0)>0;
    if(waterLanded) { W=640; H=360; }
    // (TGPT_TEST_SPEED1_ONLY=1: the speed switches' checks and times alone, pathtracing_speed1_checks.inl, near the game's size.)
    const bool speed1Only=GetEnvironmentVariableA("TGPT_TEST_SPEED1_ONLY", nullptr, 0)>0;
    // (TGPT_TEST_NOSUN_ONLY=1: a frame without a sun alone, pathtracing_nosun_checks.inl, PT-133; in the speed study's block,
    // whose materials and lamps it uses.)
    const bool nosunOnly=GetEnvironmentVariableA("TGPT_TEST_NOSUN_ONLY", nullptr, 0)>0;
    const bool fogFroxelOnly=GetEnvironmentVariableA("TGPT_TEST_FOG_FROXEL_ONLY", nullptr, 0)>0;
    if(fogFroxelOnly) { W=1920; H=1080; }
    if(speed1Only) { W=1920; H=1080; }
    // (TGPT_TEST_MEMORY2_1080=1 with TGPT_TEST_MEMORY2_ONLY=1: memory-2's cases at 1920 x 1080, for what of Windows' count of
    // the tracer's video memory grows with the render size; pathtracing_memory2_checks.inl, docs/agents/memory-2.md.)
    if(GetEnvironmentVariableA("TGPT_TEST_MEMORY2_ONLY", nullptr, 0)>0 && GetEnvironmentVariableA("TGPT_TEST_MEMORY2_1080", nullptr, 0)>0) { W=1920; H=1080; }
    // The run's memory ceiling (memory_watch): 8 GB in the speed-1 mode, none elsewhere, unless the environment says.
    {
        char ceiling[32]{};
        if(GetEnvironmentVariableA("TGPT_TEST_MEMORY_CEILING_MB", ceiling, sizeof ceiling)>0) memoryCeilingMb=std::atof(ceiling);
        else if(speed1Only) memoryCeilingMb=8192.0;
        if(memoryCeilingMb>0.0) {
            std::printf("      memory ceiling: the run ends itself over %.0f MB of its own memory\n", memoryCeilingMb);
            if(HANDLE watch=CreateThread(nullptr, 0, memory_watch, nullptr, 0, nullptr)) CloseHandle(watch);
        }
    }
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
    const auto swapchainStarted=std::chrono::steady_clock::now();
    expect(SUCCEEDED(factory->CreateSwapChainForHwnd(device.Get(), window, &sd, nullptr, nullptr, &swapchain)), "game swapchain created through the bridge hooks");
    std::printf("      the game's swapchain made in %.1f ms (with the bridge's D3D12 device, and texture sharing's self-test thread started)\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-swapchainStarted).count());
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
    LOAD(RegisterSkinnedMesh); LOAD(SetSkinned); LOAD(RegisterCookie);
    LOAD(GetStageTimes); LOAD(RegisterDetailTexture); LOAD(RegisterHairColor); LOAD(RegisterEyeColor);
    LOAD(SetWaterCaustics); LOAD(RegisterHairLobes); LOAD(SetProfileReflectance);
    LOAD(ReleaseHairColor); LOAD(RegisterSharedTexture); LOAD(ClearScene); LOAD(SetClothWind); LOAD(SwapSharedTexture); LOAD(GetYardstick); LOAD(SetMaterialPrograms);
    LOAD(GetWalkPixels); // PT-40: who takes the scattering walk (null with a tracer from before it: the walk checks say so)
    LOAD(SetPlantWind); LOAD(SetWindNoise); LOAD(SetMoving); LOAD(SetDecals); LOAD(SetFogBoxes);
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
    // Its passes are made on threads of their own from the device's creation on (start_passes); the harness waits for them
    // as the plugin does (a frame handed over meanwhile goes untraced).
    expect(status.starting==1 || status.startSeconds>0.f, "the passes are made on a thread of their own, from the device's creation");
    // Parallel workers can spend longer compiling cold ray-shader pipelines. Keep
    // the measured time visible; a readiness timeout must not run empty frames.
    const char* startupOverride=std::getenv("TGPT_TEST_STARTUP_SECONDS");
    const int startupSeconds=startupOverride ? std::max(1,std::atoi(startupOverride)) :
        (std::getenv("TGPT_TEST_CANDLE_ONLY") ? 600 : 180);
    for(int i=0;i<startupSeconds*10 && status.starting;++i) { Sleep(100); ReadStatus(&status); }
    expect(!status.starting && status.startSeconds>0.f, "the passes were made");
    std::printf("      passes made in %.1f s on their own threads\n", status.startSeconds);
    if(status.starting) { std::printf("FAILED: the passes were still being made after %.0f s (a cold pass cache or a busy GPU): run the test again\n", double(startupSeconds)); return 1; }
    auto ReadDiagnostics=reinterpret_cast<decltype(&TGPT_ReadDiagnostics)>(GetProcAddress(pt,"TGPT_ReadDiagnostics"));
    auto ReleaseMesh=reinterpret_cast<decltype(&TGPT_ReleaseMesh)>(GetProcAddress(pt,"TGPT_ReleaseMesh"));
    expect(ReadDiagnostics && ReleaseMesh, "memory/startup diagnostic and mesh-release exports");
    if(ReadDiagnostics) {
        TGPT_Diagnostics d{sizeof(d),TGPT_ABI}; expect(ReadDiagnostics(&d)==0,"shader progress readable");
        expect(d.passesTotal>0 && d.passesDone==d.passesTotal,"all startup passes report completion");
        std::printf("      shader cache: %u hits, %u/%u passes, %.3f seconds\n",d.cacheHits,d.passesDone,d.passesTotal,status.startSeconds);
        if(GetEnvironmentVariableA("TGPT_TEST_CACHE_WARM",nullptr,0)) {
            expect(d.cacheHits>=40,"second process reads compiled passes from disk");
            expect(status.startSeconds<3.f,"second process startup below three seconds");
        }
    }
    if(GetEnvironmentVariableA("TGPT_TEST_STARTUP_ONLY",nullptr,0)) return failures ? 1 : 0;
    expect(RegisterSharedTexture && ClearScene, "the texture-sharing export");
    if(!waterOnly && !glassShellOnly && RegisterSharedTexture && ClearScene) shared_texture_checks(device.Get(), context.Get(), RegisterSharedTexture, RegisterTexture, ReadStatus, ClearScene,
                                                                  root/"TGPathTracing"/"TGPathTracing.Native.log");
    // TGPT_TEST_SHARE_ONLY=1: stop here, after the texture-sharing checks (some seconds).
    if(GetEnvironmentVariableA("TGPT_TEST_SHARE_ONLY", nullptr, 0)>0) { std::printf(failures ? "FAILED %d checks\n" : "texture-sharing checks passed\n", failures); return failures ? 1 : 0; }
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
    auto register_mesh=[&](MeshBuffers& m, uint64_t key, bool decalAttributes=false) {
        TGPT_MeshDesc d{sizeof(TGPT_MeshDesc), TGPT_ABI};
        d.key=key; d.vertexBuffers[0]=uint64_t(uintptr_t(m.vb.Get())); d.strides[0]=sizeof(Vertex);
        d.positionStream=0; d.positionOffset=0; d.positionFormat=TGPT_VF_FLOAT32; d.positionDimension=3;
        d.uvStream=0; d.uvOffset=24; d.uvFormat=TGPT_VF_FLOAT32; d.uvDimension=2;
        d.colorStream=0xFFFFFFFFu;
        d.normalStream=0; d.normalOffset=12; d.normalFormat=TGPT_VF_FLOAT32; d.normalDimension=3;
        d.indexBuffer=uint64_t(uintptr_t(m.ib.Get())); d.indexFormat=16; d.vertexCount=m.vertexCount;
        d.submeshCount=uint32_t(m.submeshes.size()); d.submeshes=m.submeshes.data();
        ComPtr<ID3D11Buffer> attributes;
        if(decalAttributes) {
            d.reserved=1;
            std::vector<float> data(size_t(m.vertexCount)*8);
            for(uint32_t v=0;v<m.vertexCount;++v) { data[8*v]=1; data[8*v+3]=-1; for(int k=4;k<8;++k) data[8*v+k]=1; }
            D3D11_BUFFER_DESC desc{}; desc.ByteWidth=UINT(data.size()*4); desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{data.data(),0,0}; expect(SUCCEEDED(device->CreateBuffer(&desc,&initial,&attributes)),"decal tangent/colour stream");
            d.vertexBuffers[1]=uint64_t(uintptr_t(attributes.Get())); d.strides[1]=32;
            d.tangentStream=1; d.tangentOffset=0; d.tangentFormat=TGPT_VF_FLOAT32; d.tangentDimension=4;
            d.colorStream=1; d.colorOffset=16; d.colorFormat=TGPT_VF_FLOAT32; d.colorDimension=4;
        }
        return RegisterMesh(&d);
    };
    const int decalMeshHandle=register_mesh(quadMesh,0xdcca,true);
    auto decalParts=make_mesh(card,{{0,2,1},{0,3,2}}); const int decalPartsHandle=register_mesh(decalParts,0xdccb,true);
    auto layeredVertices=card; for(auto& v:layeredVertices) v.u=v.v=0.25f;
    for(auto v:card) { v.py=0.01f; v.u=v.v=0.75f; layeredVertices.push_back(v); }
    auto decalLayers=make_mesh(layeredVertices,{{0,2,1,0,3,2,4,6,5,4,7,6}});
    const int decalLayersHandle=register_mesh(decalLayers,0xdccc,true);
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
    // Its alpha at 256x256 as well, which the alpha test and the micromaps read instead of the 64x64 tile's. After 300 others
    // (PT-50: the atlas held 256 until 2026-10-07): every alpha test below reads a tile past the old atlas's end.
    // TGPT_TEST_BEFORE_PT50=1: this test against a tracer from before PT-50 (its 256 tiles), for the study's numbers of
    // the clamps as they were (nrdStudy's candles; docs/agents/caps.md): no fillers then, and the other PT-50 checks fail.
    const bool beforePt50=GetEnvironmentVariableA("TGPT_TEST_BEFORE_PT50", nullptr, 0)>0;
    int lastFiller=-1;
    for(uint64_t k=0;k<(beforePt50 ? 0u : 300u);++k) lastFiller=RegisterAlphaTexture(0xA1FA000000ull+k, uint64_t(uintptr_t(leaf.Get())), 0.5f);
    const int leafAlpha=RegisterAlphaTexture(43, uint64_t(uintptr_t(leaf.Get())), 0.5f);
    expect(beforePt50 ? leafAlpha>=0 : lastFiller==299 && leafAlpha==300, "alpha-tested texture registered for the alpha atlas, the 301st (past the old 256)");
    // Materials: white, red, green, checker, emissive, terrain (vertex color), the furnace's glowing grey (drawn only
    // where a test builds the furnace), the white with HDRP's GBuffer3 holding light (drawn only: the hair layer's test), and
    // black (drawn only: a metal's diffuse color, PT-46's mirror), and brown (drawn only: PT-75's floor).
    struct Mat { float r, g, b; bool checker; float er, eg, eb; };
    const Mat mats[10]={{0.75f, 0.75f, 0.75f, false, 0, 0, 0}, {0.75f, 0.08f, 0.06f, false, 0, 0, 0}, {0.08f, 0.6f, 0.1f, false, 0, 0, 0},
                       {1, 1, 1, true, 0, 0, 0}, {0.2f, 0.2f, 0.2f, false, 6.f, 3.f, 1.f}, {1, 1, 1, false, 0, 0, 0}, {0.5f, 0.5f, 0.5f, false, 0.5f, 0.5f, 0.5f},
                       {0.75f, 0.75f, 0.75f, false, 0.6f, 0.45f, 0.4f}, {0, 0, 0, false, 0, 0, 0}, {0.45f, 0.2f, 0.05f, false, 0, 0, 0}};
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
    // 8: legacy camera-only shell fixture, passed by other rays.
    materials[8]=materials[0]; materials[8].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_CAMERA_ONLY; materials[8].alphaCutoff=0.f;
    // 9: grey glass, tinted by its own absorption (PT-76; until 2026-10-09 by a base color of 0.36, which is paint on glass,
    // not the glass): 0.8 m of it leaves 36 % of the light (a closed object's way inside it), and as an open sheet it is a
    // pane 0.4 m thick, which leaves 60 % at a crossing (TGPT_Material.opticalReserved's second word: the thickness, a
    // float's bits); 10: clear glass (lets it all through).
    materials[9]=materials[0]; materials[9].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS; materials[9].alphaCutoff=0.f;
    materials[9].baseColor[0]=materials[9].baseColor[1]=materials[9].baseColor[2]=1.f;
    materials[10]=materials[9];
    for(float& v:materials[9].absorption) v=float(-std::log(0.36)/0.8);
    { const float pane=0.4f; std::memcpy(&materials[9].opticalReserved[1], &pane, 4); }
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
    ComPtr<ID3D11Buffer> ocb; { D3D11_BUFFER_DESC d{}; d.ByteWidth=16*4*4+16*8+16*4; /* four matrices, eight vectors, the previous world */ d.BindFlags=D3D11_BIND_CONSTANT_BUFFER; device->CreateBuffer(&d, nullptr, &ocb); }
    ComPtr<ID3D11DepthStencilState> dss;
    { D3D11_DEPTH_STENCIL_DESC d{}; d.DepthEnable=TRUE; d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc=D3D11_COMPARISON_GREATER;
      d.StencilEnable=TRUE; d.StencilReadMask=d.StencilWriteMask=0xFF;
      d.FrontFace={D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS}; d.BackFace=d.FrontFace;
      device->CreateDepthStencilState(&d, &dss); }
    ComPtr<ID3D11RasterizerState> rs; { D3D11_RASTERIZER_DESC d{}; d.FillMode=D3D11_FILL_SOLID; d.CullMode=D3D11_CULL_NONE; d.DepthClipEnable=TRUE; device->CreateRasterizerState(&d, &rs); }
    ComPtr<ID3D11SamplerState> wrap; { D3D11_SAMPLER_DESC d{}; d.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; d.AddressU=d.AddressV=d.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; d.MaxLOD=D3D11_FLOAT32_MAX; device->CreateSamplerState(&d, &wrap); }
    V3 sunDir=normalize({0.35f, 1.f, 0.55f}); // (the sea fixture's width part alone moves it, and puts it back: a low sun)
    float sunLux=6.f; // (the fog check's TGPT_TEST_FOG_SCALE makes it the game's size)
    float skyColor[4]={0.55f*exposure, 0.75f*exposure, 0.95f*exposure, 1};
    auto setExposure=[&](float e) {
        exposure=e;
        const float v[2]={e, 0}; context->UpdateSubresource(exposureTex.Get(), 0, nullptr, v, 8, 0);
        skyColor[0]=0.55f*e; skyColor[1]=0.75f*e; skyColor[2]=0.95f*e;
    };

    struct Frame { V3 camPos, camTarget; };
    bool bumpy=false; // normal-mapped surfaces
    float bumpScale=1.f; // how far they tilt: 1 the harness's own (up to about 30 degrees), 0.1 a tenth of that
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
            const MeshBuffers& m=o.drawn ? *o.drawn : o.mesh==0 ? quadMesh : cubeMesh;
            for(size_t s=0;s<m.submeshes.size();++s) {
                const Mat& mat=mats[o.raster>=0 ? o.raster : o.materials[std::min(s, o.materials.size()-1)]];
                struct { float vp[16], mvp[16], pvp[16], world[16]; float cam[4], pcam[4], albedo[4], f0r[4], em[4], sun[4], sunc[4], misc[4]; float pworld[16]; } c{};
                std::memcpy(c.vp, cam.viewProj.m, 64); std::memcpy(c.mvp, cam.nonJitteredViewProj.m, 64); std::memcpy(c.pvp, prev.nonJitteredViewProj.m, 64);
                std::memcpy(c.world, o.world.m, 64); std::memcpy(c.pworld, (o.moved ? o.before : o.world).m, 64);
                c.cam[0]=cam.position.x; c.cam[1]=cam.position.y; c.cam[2]=cam.position.z;
                c.pcam[0]=prev.position.x; c.pcam[1]=prev.position.y; c.pcam[2]=prev.position.z;
                c.albedo[0]=mat.r; c.albedo[1]=mat.g; c.albedo[2]=mat.b; c.albedo[3]=o.forward ? 1.f : 0.f; c.f0r[0]=c.f0r[1]=c.f0r[2]=0.04f; c.f0r[3]=o.roughness;
                // The value the shader's FastLinearToSRGB turns into that byte.
                if(o.gb2g>=0) c.f0r[1]=std::pow((float(o.gb2g)/255.f+0.055f)/1.055f, 2.4f);
                if(o.gb2b>=0) c.f0r[2]=std::pow((float(o.gb2b)/255.f+0.055f)/1.055f, 2.4f);
                if(o.f0>=0.f) c.f0r[0]=c.f0r[1]=c.f0r[2]=o.f0;
                if(o.f0red>=0.f) { c.f0r[0]=o.f0red; c.f0r[1]=o.f0green; c.f0r[2]=o.f0blue; }
                // The transmission features' red byte is their specular occlusion (1 unless set): the value FastLinearToSRGB
                // turns into it.
                if(o.feature==2 || o.feature==3) c.f0r[0]=o.so>=0.f ? std::pow((std::clamp(o.so, 0.f, 1.f)+0.055f)/1.055f, 2.4f) : 1.f;
                c.em[0]=mat.er; c.em[1]=mat.eg; c.em[2]=mat.eb; c.em[3]=o.ao; c.sun[0]=sunDir.x; c.sun[1]=sunDir.y; c.sun[2]=sunDir.z;
                c.sunc[0]=c.sunc[1]=c.sunc[2]=sunLux; c.misc[0]=mat.checker ? 1.f : 0.f; c.misc[1]=exposure; c.misc[2]=bumpy ? bumpScale : 0.f; c.misc[3]=float(o.feature);
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
    // One of Ray Reconstruction's guide textures (TGPT_GetGuides) read back whole: four floats a pixel, rows as readback()'s.
    auto readGuide=[&](uint64_t resource) {
        std::vector<float> v(size_t(W)*H*4, 0.f);
        auto* t=reinterpret_cast<ID3D11Texture2D*>(uintptr_t(resource));
        if(!t) return v;
        D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
        const bool half=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
        if(!staging) return v;
        context->CopyResource(staging.Get(), t);
        D3D11_MAPPED_SUBRESOURCE m{};
        if(FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return v;
        for(UINT y=0;y<H && y<d.Height;++y) {
            const uint8_t* row=static_cast<const uint8_t*>(m.pData)+size_t(y)*m.RowPitch;
            for(UINT x=0;x<W*4 && x<d.Width*4;++x) v[size_t(y)*W*4+x]=half ? half_to_float(reinterpret_cast<const uint16_t*>(row)[x]) : row[x]/255.f;
        }
        context->Unmap(staging.Get(), 0);
        return v;
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
    float maxFogDistance=0; // HDRP's maximum fog distance (TGPT_Frame.forward's z): Fog Adaptation on the fog in front of the sky
    float fogExtinction=0, fogBase=0, fogRcpScale=0; // HDRP's height fog (TGPT_Frame.fog): 0 extinction, no fog
    // HDRP's fog color (TGPT_Frame.fogColor: what it scatters, w 1 in the Sky colour mode; 0: none sent) and mip fog (fogMip).
    float fogColorSent[4]={0, 0, 0, 0}, fogMipSent[4]={0, 0, 0, 0};
    // The tracer's own camera rays for every pixel (PT-59). The checks written before them keep the route they were written
    // for, each pixel's surface from the drawn G-buffer (TGPT_OPTION_GBUFFER_SURFACES): many of their scenes hold things only
    // the traced scene has, to cast a shadow or be seen through something, which the camera's own rays would show.
    bool ownRays=false;
    // The camera's matrices handed over WITHOUT the frame's sub-pixel shift, the jitter told all the same, as the plugin
    // handed them in the game until 2026-10-08 (it read HDRP's matrices after HDRP had taken the shift out again; PT-84).
    // HDRP's own drawing keeps the shift: the G-buffer is rasterised as ever.
    bool lateMatrices=false;
    // PT-85 on trial (TGPT_Frame.jitter's z): 1, the tracer's own histories of the frame before are fetched in that frame's
    // own pixel grid; 2, ReSTIR's reservoirs as well; 0, by the motion alone, as the game runs unless its hidden setting says so.
    float reuseTrial=0.f;
    // PT-13, PT-35 on trial (TGPT_Frame.cameraPosition's w, docs/agents/glass-3.md): 1, the view through see-through glass
    // crosses the mesh's smooth face; 2, its paint only on the faces the game draws; 4, glass has an inside where its piece
    // of the mesh closes a volume; 8, light through glass counted twice, as before 2026-10-09; 0, glass as it is.
    float glassTrial=0.f;
    float waterTrial=0.f; // water10: cameraPosition.w, unused by the prior ABI
    // PT-95, PT-19 on trial (TGPT_Frame.prevCameraPosition's w, docs/agents/nrd-relief.md): the reflections' own filter, 1 by
    // the lobe, 2 by the reflectance, 4 without its feedback into the history (added up); 0, as before.
    float reliefTrial=0.f;
    // PT-99 on trial (TGPT_Frame.prevCameraPosition's w, docs/agents/speed-1.md): speed that leaves the picture as it is, a
    // bit each (1: a shadow ray asks which water it starts in only under the water's highest point); 16 and up, a
    // measuring build's switches; 0, everything as before.
    float speedTrial=0.f;
    // PT-109 on trial (TGPT_Frame.flags' bits 2 to 5, docs/agents/megageo-1.md): NVIDIA RTX Mega Geometry. 1, 2, 3, the
    // frame's top level partitioned in cells of 64, 128, 256 m (where the tracer was built with it and the driver
    // reports it); 0, the plain structures.
    uint32_t megaTrial=0;
    // PT-110 (TGPT_Frame.flags' bits 6 to 13, docs/agents/omm-1.md): the hidden "Micromaps trial". 1, 2, 4: the plants',
    // the characters', every other cut-out solid (for measuring); 8: no micromap used; 16: the plain submeshes of
    // characters and plant groups tested by the shader again (M3 undone); 32: plants with their meshes' micromaps (on
    // trial); 0, as it ships.
    uint32_t ommTrial=0;
    // TGPT_TEST_SPEED_TRIAL=<number> (docs/agents/perf-1.md): every frame of the run carries that number, so that a fixture's
    // own times and numbers can be taken with a switch on and off (the sea's parts: TGPT_TEST_SEA_PART).
    { char asked[16]{}; if(GetEnvironmentVariableA("TGPT_TEST_SPEED_TRIAL", asked, sizeof asked)>0) { speedTrial=float(std::atoi(asked)); std::printf("      (TGPT_TEST_SPEED_TRIAL: every frame carries the speed trial %d)\n", int(speedTrial)); } }
    uint32_t fogFrameFlags=0;
    float fogAnisotropy=0.f; // the fog check's: HDRP's Fog volume anisotropy (TGPT_Frame.fogMedium's fourth)
    float fogMediumAlbedo[3]={0.58f, 0.97f, 0.99f}; // the fog check's: what the fog scatters (TGPT_Frame.fogMedium's first three)
    TGPT_Frame last{};
    // PT-133 (docs/agents/nosun.md), two switches for ANY mode of this test:
    // TGPT_TEST_NOSUN=1: every frame of the run goes without a sun, whatever the check set (TGPT_Frame.sunColor's w 0, the
    // sun's direction and colour left as they are); =2: as the plugin sends a frame of an interior or a night, the sun's
    // direction, angular radius and colour all zero too (HdrpPasses.cs fills them only when the game's sun is valid).
    // The checks of a mode then fail where they count on the sun: such a run is for its numbers alone.
    // TGPT_TEST_TRACE=<file>: after every frame the composite is read back and one line of numbers is written (trace_frame).
    // Two runs of the same mode, one with the tracer's TGPT_TEST_SM66=1 (its Shader Model 6.6 passes), laid side by side by
    // tools/agents/nosun_compare.py, show a pass whose 6.9 build the driver gets wrong.
    int noSunForced=0;
    { char asked[8]{}; if(GetEnvironmentVariableA("TGPT_TEST_NOSUN", asked, sizeof asked)>0) { noSunForced=asked[0]=='2' ? 2 : 1;
        std::printf("      (TGPT_TEST_NOSUN=%d: every frame of this run goes without a sun%s)\n", noSunForced, noSunForced==2 ? ", its direction and colour zero as the plugin sends them" : ""); } }
    FILE* traceFile=nullptr; uint32_t traceCount=0; ComPtr<ID3D11Texture2D> traceStaging;
    { wchar_t path[MAX_PATH]{}; if(GetEnvironmentVariableW(L"TGPT_TEST_TRACE", path, MAX_PATH)>0) { traceFile=_wfopen(path, L"w");
        std::printf("      (TGPT_TEST_TRACE: every frame's numbers %s %ls)\n", traceFile ? "go to" : "could NOT be written to", path); } }
    // One line a frame: its number in the run, the checks done before it, the view, the frame's index, options and flags,
    // the sun's w, the camera's place; of the composite's finite colour values the mean of each channel and the largest and
    // the smallest; how many values are not finite, how many pixels have a channel above 1,000 and above 30,000 (the half
    // float's largest is 65,504: no light of these scenes is near either) and how many one below -0.001; then the mean
    // luminance of each block of a 16 x 9 grid.
    auto trace_frame=[&](uint32_t view, const TGPT_Frame& f) {
        if(!traceStaging) {
            D3D11_TEXTURE2D_DESC d{}; color->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
            device->CreateTexture2D(&d, nullptr, &traceStaging);
            if(!traceStaging) return;
        }
        context->CopyResource(traceStaging.Get(), color.Get());
        D3D11_MAPPED_SUBRESOURCE m{}; if(FAILED(context->Map(traceStaging.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
        double sum[3]{}, block[144]{}; uint64_t finite=0, notFinite=0, above1k=0, above30k=0, below=0; float largest=-3.4e38f, smallest=3.4e38f;
        for(UINT y=0;y<H;++y) {
            const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData)+size_t(y)*m.RowPitch);
            const UINT by=y*9u/H;
            for(UINT x=0;x<W;++x) {
                float c[3]; bool ok=true, big=false, huge=false, neg=false;
                for(int k=0;k<3;++k) {
                    c[k]=half_to_float(row[x*4+k]);
                    if(!std::isfinite(c[k])) { ++notFinite; ok=false; continue; }
                    sum[k]+=c[k]; ++finite; largest=std::max(largest, c[k]); smallest=std::min(smallest, c[k]);
                    big|=c[k]>1e3f; huge|=c[k]>3e4f; neg|=c[k]<-1e-3f;
                }
                above1k+=big; above30k+=huge; below+=neg;
                if(ok) block[by*16u+x*16u/W]+=0.2126*c[0]+0.7152*c[1]+0.0722*c[2];
            }
        }
        context->Unmap(traceStaging.Get(), 0);
        const double pixels=double(W)*double(H);
        std::fprintf(traceFile, "%u %d %u %u %x %x %g %.3f %.3f %.3f | %.9g %.9g %.9g %.9g %.9g %llu %llu %llu %llu |", traceCount, checksDone, view, f.frameIndex, f.options, f.flags,
                     double(f.sunColor[3]), double(f.cameraPosition[0]), double(f.cameraPosition[1]), double(f.cameraPosition[2]),
                     sum[0]/pixels, sum[1]/pixels, sum[2]/pixels, finite ? double(largest) : 0.0, finite ? double(smallest) : 0.0,
                     (unsigned long long)notFinite, (unsigned long long)above1k, (unsigned long long)above30k, (unsigned long long)below);
        const double blockPixels=pixels/144.0;
        for(double b:block) std::fprintf(traceFile, " %.7g", b/blockPixels);
        std::fputc('\n', traceFile);
        if((traceCount&7u)==7u) std::fflush(traceFile);
    };
    auto run=[&](uint32_t view, const Camera& cam, const Camera& prev, uint32_t frameIndex, uint32_t options=0) {
        raster(cam, prev);
        TGPT_Frame f{sizeof(TGPT_Frame), TGPT_ABI};
        f.sequence=++sequence; f.flags=(frameIndex==0 || cut) ? TGPT_FRAME_RESET : 0;
        f.flags|=(megaTrial&TGPT_FRAME_MEGA_MASK)<<TGPT_FRAME_MEGA_SHIFT;
        f.flags|=(ommTrial&TGPT_FRAME_OMM_MASK)<<TGPT_FRAME_OMM_SHIFT;
        f.flags|=fogFrameFlags;
        if(fogFroxelOnly) { for(int c=0;c<3;++c) f.fogMedium[c]=fogMediumAlbedo[c]; f.fogMedium[3]=fogAnisotropy; }
        f.gbuffer0=uint64_t(uintptr_t(gb0.Get())); f.normal=uint64_t(uintptr_t(nb.Get())); f.gbuffer2=uint64_t(uintptr_t(gb2.Get()));
        f.gbuffer3=uint64_t(uintptr_t(gb3.Get())); f.depth=uint64_t(uintptr_t(depth.Get())); f.motion=uint64_t(uintptr_t(mv.Get()));
        f.exposure=uint64_t(uintptr_t(exposureTex.Get()));
        f.renderWidth=W; f.renderHeight=H; f.debugView=view;
        unity_layout(cam.invViewProj, f.invViewProj); unity_layout(cam.viewProj, f.viewProj);
        if(lateMatrices) { unity_layout(inverse(cam.nonJitteredViewProj), f.invViewProj); unity_layout(cam.nonJitteredViewProj, f.viewProj); }
        unity_layout(mul(prev.nonJitteredViewProj, translate_scale(cam.position-prev.position, {1, 1, 1})), f.prevViewProj); // as HDRP's
        unity_layout(cam.view, f.view); unity_layout(cam.nonJitteredProj, f.nonJitteredProj); f.jitter[0]=cam.jitter[0]; f.jitter[1]=cam.jitter[1];
        f.cameraPosition[0]=cam.position.x; f.cameraPosition[1]=cam.position.y; f.cameraPosition[2]=cam.position.z;
        f.cameraPosition[3]=glassTrial+16.f*waterTrial; // glass in the low 4 bits, the water's times 16 (tgpt_api.h)
        f.prevCameraPosition[0]=prev.position.x; f.prevCameraPosition[1]=prev.position.y; f.prevCameraPosition[2]=prev.position.z;
        f.prevCameraPosition[3]=reliefTrial+8.f*speedTrial; // the relief trial's bits 0 to 2, PT-99's switches times 8 (tgpt_api.h)
        f.sunDirection[0]=sunDir.x; f.sunDirection[1]=sunDir.y; f.sunDirection[2]=sunDir.z; f.sunDirection[3]=0.0047f;
        f.sunColor[0]=f.sunColor[1]=f.sunColor[2]=sunLux; f.sunColor[3]=sunOn ? (sunShadowedByHdrp ? 2.f : 1.f) : 0.f;
        if(noSunForced) f.sunColor[3]=0.f; // (TGPT_TEST_NOSUN, above)
        if(noSunForced==2) for(int k=0;k<4;++k) f.sunDirection[k]=f.sunColor[k]=0.f;
        f.frameIndex=frameIndex; f.bounces=bounces; f.spatialSamples=spatialSamples; f.diHistory=history; f.giHistory=giHistory ? giHistory : history; f.thickProfiles=thickProfiles; f.tracedGain=tracedGain; f.options=options; f.raysPerPixel=rays;
        // A sequence's first frame runs without the radiance cache, which empties it for the next: each case starts from
        // nothing, as a new scene does in game (where a camera cut keeps the cache's cells, the world's).
        if(frameIndex==0) f.options|=TGPT_OPTION_NO_RADIANCE_CACHE;
        if(!ownRays) f.options|=TGPT_OPTION_GBUFFER_SURFACES;
        if(rayReconstruction) { f.flags|=TGPT_FRAME_GUIDES; f.options|=TGPT_OPTION_RAY_RECONSTRUCTION; }
        f.maxRayDistance=rayDistance; f.skyIntensity=skyIntensity; f.indirectColor=indirectColor; f.reflectionBoost=reflectionBoost;
        f.forward[0]=1.f; f.forward[2]=maxFogDistance;
        f.jitter[2]=reuseTrial;
        f.fog[0]=fogExtinction; f.fog[1]=fogBase; f.fog[2]=fogRcpScale;
        for(int k=0;k<4;++k) { f.fogColor[k]=fogColorSent[k]; f.fogMip[k]=fogMipSent[k]; }
        if(SubmitFrame(&f)!=0) { expect(false, "frame submitted"); return; }
        render(TGPT_EVENT_TRACE, reinterpret_cast<void*>(uintptr_t(f.sequence)));
        SetComposite(f.sequence, uint64_t(uintptr_t(color.Get())));
        render(TGPT_EVENT_COMPOSITE, reinterpret_cast<void*>(uintptr_t(f.sequence)));
        last=f;
        if(traceFile) { trace_frame(view, f); ++traceCount; }
    };
    // The yardstick's numbers of the frame with this index (TGPT_GetYardstick; they are read back a few frames later): frames of
    // its view from the same camera until they arrive. each: what a frame needs first (a character's pose).
    auto yardstickOf=[&](uint32_t frameIndex, const Camera& c, uint32_t options, TGPT_Yardstick& y, const std::function<void()>& each) {
        for(uint32_t i=1;i<=6;++i) {
            if(each) each();
            run(TGPT_VIEW_YARDSTICK, c, c, frameIndex+i, options);
            y=TGPT_Yardstick{sizeof(TGPT_Yardstick), TGPT_ABI};
            if(GetYardstick && GetYardstick(&y)==0 && y.frameIndex==frameIndex) return true;
        }
        return false;
    };
#include "pathtracing_glass_shell_checks.inl"
    const Camera cam=make_camera({0, 3.2f, -9}, {0, 2.2f, 2}, 60.f, float(W)/float(H));
    // Warm-up: meshes are read back and built over the first frames.
    for(uint32_t i=0;i<8;++i) { run(TGPT_VIEW_NONE, cam, cam, i); context->Flush(); Sleep(20); }
    ReadStatus(&status);
    std::printf("      after warm-up: meshes ready %u queued %u failed %u, instances traced %u of %u, triangles %llu, textures %u, frames traced %llu failed %llu\n",
        status.meshesReady, status.meshesQueued, status.meshesFailed, status.instancesTraced, status.instances, (unsigned long long)status.triangles,
        status.textures, (unsigned long long)status.framesTraced, (unsigned long long)status.framesFailed);
    expect(status.meshesReady==7 && status.instancesTraced==uint32_t(instances.size()), "every mesh read back, built and traced");
    // TGPT_TEST_PSR_ONLY=1 (PT-127, docs/agents/psr-2.md): what NRD is told of a pixel that shows a still pure mirror
    // (primary surface replacement), against the scene's own planes, and what it costs. Alone, right after the warm-up (a
    // mode further down runs every check above it first); the whole test does not take it.
    if(GetEnvironmentVariableA("TGPT_TEST_PSR_ONLY", nullptr, 0)>0)
#include "pathtracing_psr_checks.inl"
    // TGPT_TEST_PLANT_SHIFT_ONLY=1 (PT-125, docs/agents/fixt.md): a group of plants in the wind keeps its pose and its
    // picture when another group comes and goes before it in the frame's order (the kept range number that went stale).
    // Alone, right after the warm-up (about a minute); the whole test does not take it.
    if(GetEnvironmentVariableA("TGPT_TEST_PLANT_SHIFT_ONLY", nullptr, 0)>0)
#include "pathtracing_plant_shift_checks.inl"
    // TGPT_TEST_DECAL_MOVE_ONLY=1 (PT-134, docs/agents/fixt.md): a mesh decal still shows, and the device still stands,
    // after its mesh's structure was compacted and its old memory given back in quiet frames (the kept address that went
    // stale). Alone, right after the warm-up (about a minute); the whole test does not take it.
    if(GetEnvironmentVariableA("TGPT_TEST_DECAL_MOVE_ONLY", nullptr, 0)>0)
#include "pathtracing_decal_move_checks.inl"
#include "pathtracing_sky_checks.inl"
#include "pathtracing_fog_froxel_checks.inl"
    // PT-87 (docs/agents/memory-1.md): what a setting or a scene does not need is not held, and the picture does not know.
    // Its own mode alone (TGPT_TEST_MEMORY1_ONLY=1): the whole run does not take it.
#include "pathtracing_memory1_checks.inl"
    // PT-73: a frozen camera sees the original cutout, not the 256px coverage copy. Its source has alternating 0.2/0.6
    // alpha stripes, while the fallback is deliberately a different mask. An authored cutoff of 0.8 hides all of it;
    // 0.4 resolves half. Repeat frames must have identical geometry, including with opacity micromaps available.
    if(RegisterSharedTexture) {
        const UINT side=512;
        std::vector<uint32_t> texels(size_t(side)*side);
        for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) texels[size_t(y)*side+x]=(((x/2)&1u) ? 0x9900FF00u : 0x3300FF00u);
        D3D11_TEXTURE2D_DESC td{side, side, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        D3D11_SUBRESOURCE_DATA init{texels.data(), side*4, 0};
        ComPtr<ID3D11Texture2D> cutout;
        expect(SUCCEEDED(device->CreateTexture2D(&td, &init, &cutout)), "original fine cutout created");
        TGPT_SharedTexture shared{sizeof(TGPT_SharedTexture), TGPT_ABI};
        const int32_t registered=RegisterSharedTexture(uint64_t(uintptr_t(cutout.Get())), &shared);
        expect(registered>=0 && shared.handle>=0, "original fine cutout shared");
        if(registered>=0 && shared.handle>=0) {
            auto ms=materials;
            TGPT_Material foreground=materials[6];
            foreground.colorTexture=shared.handle; // the native resolver supplies MATERIAL_SHARED, as for the game
            foreground.baseColor[0]=foreground.baseColor[1]=foreground.baseColor[2]=foreground.baseColor[3]=1.f;
            foreground.alphaCutoff=0.8f;
            const uint32_t fi=uint32_t(ms.size()); ms.push_back(foreground);
            TGPT_Material background=materials[0]; background.albedoTile=-1;
            background.baseColor[0]=0.8f; background.baseColor[1]=background.baseColor[2]=0.f;
            ms.push_back(background);
            TGPT_Instance cards[2]{}; const uint32_t ids[2]={fi, fi+1};
            for(int k=0;k<2;++k) {
                const M4 m=translate_scale({0, k==0 ? 1.f : 0.f, 0}, {k==0 ? 4.f : 12.f, 1.f, k==0 ? 4.f : 12.f});
                cards[k].mesh=cardHandle; cards[k].materialBase=uint32_t(k); cards[k].mask=0xFF;
                cards[k].flags=k==0 ? TGPT_INSTANCE_ALPHA_TEST : 0u;
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) cards[k].transform[r*4+c]=m.m[r][c];
            }
            expect(SetMaterials(0, ms.data(), uint32_t(ms.size()))==0 && SetScene(cards, 2, ids, 2)==0, "fine cutout scene set");
            const Camera eye=make_camera({0, 4, -4}, {0, 1, 0}, 60.f, float(W)/float(H));
            const uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_SPATIAL;
            ownRays=true;
            for(uint32_t i=0;i<5;++i) run(TGPT_VIEW_ALBEDO, eye, eye, i, raw);
            auto closed=readback(); size_t closedGreen=0;
            for(size_t p=0;p<closed.size();p+=3) closedGreen+=closed[p+1]>0.5f && closed[p]<0.1f;
            ms[fi].alphaCutoff=0.4f;
            expect(SetMaterials(fi, &ms[fi], 1)==0, "original cutout's authored threshold changed");
            for(uint32_t i=5;i<10;++i) run(TGPT_VIEW_ALBEDO, eye, eye, i, raw);
            auto open=readback(); size_t openGreen=0;
            for(size_t p=0;p<open.size();p+=3) openGreen+=open[p+1]>0.5f && open[p]<0.1f;
            run(TGPT_VIEW_ALBEDO, eye, eye, 10, raw); auto repeated=readback();
            size_t changed=0;
            for(size_t p=0;p<open.size();p+=3) changed+=std::fabs(open[p+1]-repeated[p+1])>0.01f;
            std::printf("      original cutout: green pixels cutoff 0.8 %zu, cutoff 0.4 %zu; changed frozen pixels %zu\n", closedGreen, openGreen, changed);
            // More than 1,000 pixels of the whole test's 1280 x 720: the same share of whatever size this run has (the water's
            // fixture runs at 128 x 72, where 919 of its 9,216 pixels are green).
            expect(closedGreen==0 && openGreen>size_t(1000)*W*H/(1280u*720u), "camera rays use the source alpha and its authored cutoff, not the coverage copy");
            expect(changed==0, "frozen cutout geometry is stable across random seeds");
            save(open, "73-original-cutout.png", false);
            ownRays=false;
            SetMaterials(0, materials.data(), uint32_t(materials.size()));
            SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()));
        }
    }
    if(GetEnvironmentVariableA("TGPT_TEST_LIGHT_FLICKER_ONLY", nullptr, 0)>0) {
        std::printf(failures ? "FAILED %d checks\n" : "light/flicker checks passed\n", failures); return failures ? 1 : 0;
    }
    // PT-08 (docs/agents/speed-2.md): opacity micromaps for cut-outs whose alpha test reads the game's own texture, as every
    // cut-out's in the game does (the texture opened with no copy, the cutoff the material's own). Their micromaps are baked
    // from that texture, so the meshes get them, trace with them, and every pixel comes out as the alpha test alone makes
    // it. (Until 2026-10-08 the micromap check further down saw only cut-outs that read the atlas's tiles: it passed while
    // the game, whose cut-outs all read their own textures, had no micromaps at all.)
    // Three meshes of their own, released at the end: a grid of 4 x 4 quads, whose micro-triangles span some 32 texels (the
    // texture's alpha bounds settle them); one of 12 x 12, some 11 texels, a leaf that lets light through (its covered
    // micro-triangles stay with the test); one of 48 x 48, under 3 texels (settled texel by texel). The texture, tiled twice
    // and shifted: discs with soft edges across the cutoff, one disc of alpha exactly the cutoff, a patch of stripes two
    // texels wide. Then another cutoff, and another texture in the material (what mip streaming does): the micromaps
    // follow. Alone: TGPT_TEST_MICROMAPS_ONLY=1.
    if(RegisterSharedTexture && ReleaseMesh) {
        ReadStatus(&status);
        const bool dxr12=(status.features&TGPT_FEATURE_DXR12)!=0;
        const UINT side=1024;
        std::vector<ComPtr<ID3D11Texture2D>> keep;
        auto cutoutTexture=[&](bool inverted) {
            std::vector<uint32_t> texels(size_t(side)*side);
            for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) {
                const float cx=float(x%256u)-127.5f, cy=float(y%256u)-127.5f, r=std::sqrt(cx*cx+cy*cy);
                float a=std::clamp((90.f-r)/24.f+0.5f, 0.f, 1.f);
                const UINT cell=(y/256u)*4u+x/256u;
                if(cell==5u && r<70.f) a=128.f/255.f;          // exactly the material's first cutoff
                if(cell==10u) a=((x/2u)&1u) ? 0.9f : 0.1f;     // stripes two texels wide
                if(inverted) a=1.f-a;
                texels[size_t(y)*side+x]=0x0020C020u|(uint32_t(std::lround(a*255.f))<<24);
            }
            D3D11_TEXTURE2D_DESC td{side, side, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            D3D11_SUBRESOURCE_DATA init{texels.data(), side*4, 0};
            ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&td, &init, &t); keep.push_back(t);
            TGPT_SharedTexture shared{sizeof(TGPT_SharedTexture), TGPT_ABI};
            return t && RegisterSharedTexture(uint64_t(uintptr_t(t.Get())), &shared)>=0 ? int32_t(shared.handle) : -1;
        };
        const int32_t discs=cutoutTexture(false), holes=cutoutTexture(true);
        auto gridMesh=[&](int n) {
            std::vector<Vertex> v; std::vector<uint16_t> ix;
            for(int j=0;j<=n;++j) for(int i=0;i<=n;++i) v.push_back({float(i)/float(n)-0.5f, 0, float(j)/float(n)-0.5f, 0, 1, 0, float(i)/float(n), float(j)/float(n)});
            for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
                const uint16_t a=uint16_t(j*(n+1)+i), b=uint16_t(a+1), c=uint16_t(a+n+2), d=uint16_t(a+n+1);
                ix.insert(ix.end(), {a, c, b, a, d, c}); // wound as the card
            }
            return make_mesh(v, {ix});
        };
        MeshBuffers coarseMesh=gridMesh(4), middleMesh=gridMesh(12), fineMesh=gridMesh(48);
        const int coarseHandle=register_mesh(coarseMesh, 0x5d21), middleHandle=register_mesh(middleMesh, 0x5d22), fineHandle=register_mesh(fineMesh, 0x5d23);
        expect(discs>=0 && holes>=0 && coarseHandle>0 && middleHandle>0 && fineHandle>0, "two cut-out textures shared, three cut-out meshes registered");
        if(discs>=0 && holes>=0 && coarseHandle>0 && middleHandle>0 && fineHandle>0) {
            auto ms=materials;
            const uint32_t fi=uint32_t(ms.size());
            TGPT_Material leaf=materials[0];
            leaf.flags=TGPT_MATERIAL_ALPHA_TEST; leaf.albedoTile=leaf.alphaTile=-1;
            leaf.colorTexture=discs; leaf.normalTexture=leaf.maskTexture=leaf.emissiveTexture=-1; // the native resolver supplies MATERIAL_SHARED, as for the game
            leaf.baseColor[0]=leaf.baseColor[1]=leaf.baseColor[2]=leaf.baseColor[3]=1.f;
            leaf.alphaCutoff=128.f/255.f;
            leaf.uvScaleOffset[0]=leaf.uvScaleOffset[1]=2.f; leaf.uvScaleOffset[2]=0.25f; leaf.uvScaleOffset[3]=0.125f;
            ms.push_back(leaf);
            TGPT_Material thin=leaf; thin.flags|=TGPT_MATERIAL_TRANSLUCENT; ms.push_back(thin);
            std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
            auto put=[&](int mesh, const M4& world, uint32_t material, uint32_t flags) {
                TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
                in.mesh=mesh; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=flags;
                ids.push_back(material); ids.push_back(material); list.push_back(in);
            };
            put(quadHandle, translate_scale({0, 0, 0}, {14, 1, 14}), 0u, 0u);
            put(coarseHandle, translate_scale({-3.2f, 1, 0}, {3, 1, 3}), fi, TGPT_INSTANCE_ALPHA_TEST);
            put(fineHandle, translate_scale({0, 1, 0}, {3, 1, 3}), fi, TGPT_INSTANCE_ALPHA_TEST);
            put(middleHandle, translate_scale({3.2f, 1, 0}, {3, 1, 3}), fi+1, TGPT_INSTANCE_ALPHA_TEST);
            expect(SetMaterials(0, ms.data(), uint32_t(ms.size()))==0 && SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "the cut-outs' scene set");
            const Camera eye=make_camera({0, 5.5f, -6.5f}, {0, 0.6f, 0}, 60.f, float(W)/float(H));
            const uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE;
            const bool ownBefore=ownRays; ownRays=true;
            uint32_t frame=20000;
            // The picture with micromaps turned off and with them, of the same frames (the same rays): lit, sixteen frames of
            // light and shadow through the cut-outs; and what the camera's own rays see of their color.
            struct Pair { double most=0, mean=0; uint32_t used[2]{}; size_t green=0; };
            auto compare=[&](const char* name) {
                Pair p; std::vector<float> lit[2], seen[2];
                for(int k=0;k<2;++k) {
                    const uint32_t options=raw|(k==0 ? TGPT_OPTION_NO_MICROMAPS : 0u);
                    for(uint32_t i=0;i<16;++i) run(TGPT_VIEW_NONE, eye, eye, frame+i, options);
                    lit[k]=readback(); ReadStatus(&status); p.used[k]=status.micromapInstances;
                    for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_ALBEDO, eye, eye, frame+100+i, options|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_SPATIAL);
                    seen[k]=readback();
                }
                frame+=200;
                double sum=0;
                for(int which=0;which<2;++which) {
                    const auto& a=which ? seen[0] : lit[0]; const auto& b=which ? seen[1] : lit[1];
                    for(size_t i=0;i<a.size();++i) { const double d=std::fabs(double(a[i])-double(b[i])); sum+=d; p.most=std::max(p.most, d); }
                }
                p.mean=sum/double(lit[0].size()+seen[0].size());
                for(size_t i=0;i<seen[1].size();i+=3) p.green+=seen[1][i+1]>0.5f && seen[1][i]<0.3f;
                save(lit[0], (std::string("74-cutout-")+name+"-off.png").c_str(), true); save(lit[1], (std::string("74-cutout-")+name+"-on.png").c_str(), true);
                save(seen[1], (std::string("74-cutout-")+name+"-seen.png").c_str(), false);
                return p;
            };
            // The meshes are read back and built, and their micromaps baked: some frames.
            auto settle=[&](uint32_t frames) { for(uint32_t i=0;i<frames;++i) { run(TGPT_VIEW_NONE, eye, eye, frame++, raw); context->Flush(); Sleep(20); } frame+=10; ReadStatus(&status); };
            settle(24);
            std::printf("      cut-outs of the game's own texture: micromaps on %u meshes in all, %u waiting, %u instances tracing with them\n",
                        status.micromapMeshes, status.micromapsWaiting, status.micromapInstances);
            if(dxr12) expect(status.micromapInstances==3 && status.micromapsWaiting==0, "a cut-out that reads the game's own texture has opacity micromaps and traces with them");
            const Pair first=compare("discs");
            // Another cutoff (the micromaps were made for the first), then another texture (a new object, as mip streaming makes).
            ms[fi].alphaCutoff=ms[fi+1].alphaCutoff=0.3f;
            expect(SetMaterials(fi, &ms[fi], 2)==0, "the cut-outs' cutoff changed");
            settle(12);
            const uint32_t afterCutoff=status.micromapInstances;
            const Pair second=compare("cutoff");
            ms[fi].colorTexture=ms[fi+1].colorTexture=holes;
            expect(SetMaterials(fi, &ms[fi], 2)==0, "the cut-outs' texture replaced");
            settle(12);
            const uint32_t afterTexture=status.micromapInstances;
            const Pair third=compare("holes");
            std::printf("      micromaps off against on, largest difference of any pixel (lit frames and the camera's view), and the cut-outs' pixels seen: "
                        "discs %.5f (%zu pixels; instances with micromaps %u off, %u on); cutoff 0.3 %.5f (%zu; %u after the change); the other texture %.5f (%zu; %u after the change)\n",
                        first.most, first.green, first.used[0], first.used[1], second.most, second.green, afterCutoff, third.most, third.green, afterTexture);
            if(dxr12) expect(first.used[0]==0 && first.used[1]==3, "the three cut-outs trace with micromaps, and without them when they are turned off");
            expect(first.most<1e-3 && second.most<1e-3 && third.most<1e-3, "micromaps baked from the game's own texture leave every pixel as the alpha test alone makes it");
            if(dxr12) expect(afterCutoff==3 && afterTexture==3, "micromaps are made anew when the cutoff changes and when the texture is replaced");
            // The pictures are of cut-outs: part covered, part bare, more of it covered at the lower cutoff, and the other
            // texture's covers where the first's is bare.
            const size_t some=size_t(2000)*W*H/(1280u*720u);
            expect(first.green>some && second.green>first.green+some/4 && third.green>some && third.green!=second.green, "the cut-outs show their texture's shapes, by the material's cutoff");
            // What a bake costs, for the record (printed, never judged): a mesh of 125,000 alpha-tested triangles that reads the
            // other texture, its micro-triangles some 2 texels wide (settled texel by texel) and then, with another tiling,
            // some 16 (settled by the bounds): the slowest frame's GPU time while it is read back, built and baked, against
            // the frames after it. (A frame's budget is 262,144 triangles: scene.cpp kOmmTrianglesPerFrame.)
            {
                MeshBuffers bigMesh=gridMesh(250);
                const int bigHandle=register_mesh(bigMesh, 0x5d24);
                TGPT_Material big=ms[fi]; big.colorTexture=holes; big.alphaCutoff=0.5f;
                double slowest[2]{}, usual[2]{}; uint32_t baked[2]{};
                for(int k=0;k<2 && bigHandle>0;++k) {
                    big.uvScaleOffset[0]=big.uvScaleOffset[1]=k==0 ? 8.f : 64.f;
                    ms.resize(fi+3); ms[fi+2]=big;
                    auto bigList=list; auto bigIds=ids;
                    bigList.resize(1); bigIds.resize(2); // the floor
                    TGPT_Instance in{}; const M4 world=translate_scale({0, 1, 0}, {8, 1, 8});
                    for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
                    in.mesh=bigHandle; in.materialBase=uint32_t(bigIds.size()); in.mask=0xFF; in.flags=TGPT_INSTANCE_ALPHA_TEST;
                    bigIds.push_back(fi+2); bigIds.push_back(fi+2); bigList.push_back(in);
                    SetMaterials(fi+2, &ms[fi+2], 1); SetScene(bigList.data(), uint32_t(bigList.size()), bigIds.data(), uint32_t(bigIds.size()));
                    std::vector<double> times;
                    for(uint32_t i=0;i<40;++i) { run(TGPT_VIEW_NONE, eye, eye, frame++, raw); context->Flush(); Sleep(20); ReadStatus(&status); times.push_back(double(status.gpuMs)); }
                    baked[k]=status.micromapInstances;
                    slowest[k]=*std::max_element(times.begin(), times.end());
                    std::sort(times.begin(), times.end()); usual[k]=times[times.size()/2];
                    frame+=10;
                }
                std::printf("      a bake of 125,000 triangles from the game's own texture: micro-triangles 2 texels wide, the slowest frame %.1f ms of the GPU "
                            "(a frame after it %.1f ms; traces with micromaps: %u); 16 texels wide, %.1f ms (%.1f ms; %u)\n",
                            slowest[0], usual[0], baked[0], slowest[1], usual[1], baked[1]);
                if(bigHandle>0) ReleaseMesh(bigHandle);
            }
            ownRays=ownBefore;
            SetMaterials(0, materials.data(), uint32_t(materials.size()));
            SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()));
            expect(ReleaseMesh(coarseHandle)==0 && ReleaseMesh(middleHandle)==0 && ReleaseMesh(fineHandle)==0, "the cut-out meshes released");
            for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, cam, cam, i);
        }
    }
    if(GetEnvironmentVariableA("TGPT_TEST_MICROMAPS_ONLY", nullptr, 0)>0) {
        std::printf(failures ? "FAILED %d checks\n" : "micromap checks passed\n", failures); return failures ? 1 : 0;
    }
    if(GetEnvironmentVariableA("TGPT_TEST_MEMORY_ONLY",nullptr,0)) {
        TGPT_Diagnostics before{sizeof(before),TGPT_ABI}, after{sizeof(after),TGPT_ABI};
        Sleep(1100); render(7,nullptr); ReadDiagnostics(&before);
        expect(before.meshGeometry>0 && before.acceleration>0,"memory accounting sees uploaded geometry and acceleration");
        // (One mesh fewer than before the release: the count was pinned at 3, from before the decals' three meshes were
        // registered beside the room's four, and failed on its own tree since.)
        ReadStatus(&status); const uint32_t readyBefore=status.meshesReady;
        auto remaining=instances;
        remaining.erase(std::remove_if(remaining.begin(),remaining.end(),[&](const TGPT_Instance& i) { return i.mesh==cubeHandle; }),remaining.end());
        expect(SetScene(remaining.data(),uint32_t(remaining.size()),indices.data(),uint32_t(indices.size()))==0,"scene without a retired mesh submitted");
        expect(ReleaseMesh(cubeHandle)==0,"unused mesh retirement requested");
        for(uint32_t i=8;i<14;++i) run(TGPT_VIEW_NONE,cam,cam,i);
        ReadStatus(&status);
        std::printf("      meshes ready: %u before the cube's release, %u after\n", readyBefore, status.meshesReady);
        expect(readyBefore>1 && status.meshesReady==readyBefore-1,"unused mesh retired while the other meshes stay ready");
        expect(ClearScene()==0,"scene clear requested");
        context->Flush(); Sleep(300); render(7,nullptr); Sleep(1100); render(7,nullptr); ReadDiagnostics(&after);
        std::printf("      memory clear bytes: total %llu -> %llu, geometry %llu -> %llu, acceleration %llu -> %llu, records %llu -> %llu, plants %llu -> %llu, scratch %llu -> %llu\n",
          before.total,after.total,before.meshGeometry,after.meshGeometry,before.acceleration,after.acceleration,before.records,after.records,before.plants,after.plants,before.scratch,after.scratch);
        expect(after.meshGeometry==0 && after.acceleration==0 && after.records==0 && after.plants==0 && after.cloth==0 && after.scratch==0,"scene-owned allocations returned after clear and fence completion");
        expect(after.total<before.total,"accounted memory comes back down after a scene unload");
        return failures ? 1 : 0;
    }
    // Things the game moves (PT-71 item 4; TGPT_SetMoving): every frame each is placed where HDRP draws it then, with where it
    // was a frame before, and the scene list's copy of it from the last scan is not traced. Judged by the yardstick with the
    // tracer's own camera rays and no denoiser: a red box HDRP draws at one place while the scene's list still holds it 2.3 m
    // to the left. Run first and alone with TGPT_TEST_MOVING_ONLY=1.
    auto movingChecks=[&]() {
        const uint32_t raw=TGPT_OPTION_NO_DENOISER;
        const bool ownBefore=ownRays;
        ownRays=true;
        const M4 scanned=translate_scale({-3.5f, 0.5f, 2.5f}, {1, 1, 1}); // where the last scan saw the box
        const M4 here=translate_scale({-1.2f, 0.5f, 2.5f}, {1, 1, 1});    // where HDRP draws it now
        const M4 nudged=translate_scale({-1.1f, 0.5f, 2.5f}, {1, 1, 1});  // 10 cm on, the frame after
        const uint32_t red[2]={1u, 1u};
        auto place=[](TGPT_Moving& t, const M4& now, const M4& before) {
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) { t.transform[r*4+c]=now.m[r][c]; t.previous[r*4+c]=before.m[r][c]; }
        };
        // The scene's list with the box where the scan saw it (its last instance), or without it.
        auto list=instances; auto ids=indices;
        TGPT_Instance stale{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) stale.transform[r*4+c]=scanned.m[r][c];
        stale.mesh=cubeHandle; stale.materialBase=uint32_t(ids.size()); stale.mask=0xFF;
        ids.push_back(1u); ids.push_back(1u); list.push_back(stale);
        const uint32_t staleIndex=uint32_t(list.size()-1);
        auto counted=[&](uint32_t base, const char* what, TGPT_Yardstick& y) {
            for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_YARDSTICK, cam, cam, i ? base+i : 0, raw);
            const bool have=yardstickOf(base+2, cam, raw, y, nullptr);
            uint32_t moving=0, movingSame=0; for(int i=1;i<6;++i) moving+=y.kinds[3][i];
            movingSame=y.kinds[3][1];
            std::printf("      %s: the box %u pixels (the same surface as drawn %u), traced nearer than drawn %u, farther %u, nothing traced %u; own motion off the drawn %u\n",
                        what, moving, movingSame, y.where[2], y.where[3], y.where[4], y.motion);
            return have;
        };
        // The room alone: what the yardstick counts without the box (traced-only quads and the like).
        expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0 &&
               SetMoving(nullptr, 0, nullptr, 0, nullptr, 0)==0, "the room, nothing moving");
        TGPT_Yardstick room{sizeof(TGPT_Yardstick), TGPT_ABI};
        const bool haveRoom=counted(12000, "the room alone", room);
        // 1. Where it is this frame: the scan's copy is stood in for, the moving box traced where HDRP draws it.
        objects.push_back(Object{1, here, {1, 1}});
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "the scene's list with the box where the scan saw it");
        TGPT_Moving box{}; place(box, here, here); box.mesh=cubeHandle; box.materialBase=0; box.mask=0xFF;
        expect(SetMoving(&box, 1, red, 2, &staleIndex, 1)==0, "the box the game moved, standing in for the scan's copy");
        TGPT_Yardstick placed{sizeof(TGPT_Yardstick), TGPT_ABI};
        const bool havePlaced=counted(12020, "a box the game moved 2.3 m since the scan", placed);
        uint32_t boxPixels=0; for(int i=1;i<6;++i) boxPixels+=placed.kinds[3][i];
        const int64_t nearerMore=int64_t(placed.where[2])-int64_t(room.where[2]);
        expect(haveRoom && havePlaced && boxPixels>2000, "the yardstick counts the moving box's pixels as a moving thing's");
        expect(placed.kinds[3][1]>boxPixels*95/100, "a moving thing is traced where HDRP draws it this frame");
        expect(nearerMore<int64_t(boxPixels/20), "the scan's copy of a moving thing is not traced where it was");
        // 2. Its own motion: the frame it moves 10 cm on, HDRP's motion vectors carry the box's own (its previous matrix), and
        // so does the tracer's hit (the transform it had a frame before).
        objects.back().world=nudged; objects.back().moved=true; objects.back().before=here;
        place(box, nudged, here);
        expect(SetMoving(&box, 1, red, 2, &staleIndex, 1)==0, "the box 10 cm on");
        TGPT_Yardstick went{sizeof(TGPT_Yardstick), TGPT_ABI};
        run(TGPT_VIEW_YARDSTICK, cam, cam, 12040, raw);
        const bool haveWent=yardstickOf(12040, cam, raw, went, nullptr);
        uint32_t wentPixels=0; for(int i=1;i<6;++i) wentPixels+=went.kinds[3][i];
        const int64_t motionMore=int64_t(went.motion)-int64_t(room.motion);
        std::printf("      the box 10 cm on in a frame: %u pixels, own motion off the drawn %u (the room alone %u)\n", wentPixels, went.motion, room.motion);
        expect(haveWent && wentPixels>2000 && motionMore<int64_t(wentPixels/50), "a moving thing's pixels move by its own previous transform, as HDRP's motion vectors");
        // 3. Gone the same frame: hidden (a sheathed weapon, a picked-up item), neither it nor the scan's copy is traced.
        objects.pop_back();
        box.flags=TGPT_INSTANCE_HIDDEN; place(box, nudged, nudged);
        expect(SetMoving(&box, 1, red, 2, &staleIndex, 1)==0, "the box hidden");
        TGPT_Yardstick gone{sizeof(TGPT_Yardstick), TGPT_ABI};
        counted(12060, "the box hidden", gone);
        uint32_t gonePixels=0; for(int i=1;i<6;++i) gonePixels+=gone.kinds[3][i];
        expect(gonePixels==0 && int64_t(gone.where[2])-int64_t(room.where[2])<int64_t(boxPixels/20), "a hidden moving thing and its scan's copy are traced nowhere");
        box.flags=0;
        // 4. What it costs: 2,000 and 20,000 cubes the game moves, each placed every frame 2 cm on from the frame before, behind
        // the room (printed for docs/agents/moving.md, judged only for being traced: other programs may share the GPU).
        expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
        for(uint32_t count:{2000u, 20000u}) {
            std::vector<TGPT_Moving> many(count);
            const uint32_t side=uint32_t(std::ceil(std::sqrt(double(count))));
            char stages[1024]{};
            TGPT_Status after{sizeof(TGPT_Status), TGPT_ABI};
            const auto t0=std::chrono::steady_clock::now();
            double setMs=0;
            for(uint32_t f=0;f<60;++f) {
                for(uint32_t i=0;i<count;++i) {
                    const float x=-40.f+float(i%side)*80.f/float(side), z=20.f+float(i/side)*60.f/float(side);
                    place(many[i], translate_scale({x+0.02f*float(f), 0.3f, z}, {0.3f, 0.3f, 0.3f}), translate_scale({x+0.02f*float(f ? f-1 : 0), 0.3f, z}, {0.3f, 0.3f, 0.3f}));
                    many[i].mesh=cubeHandle; many[i].materialBase=0; many[i].mask=0xFF;
                }
                const auto s0=std::chrono::steady_clock::now();
                SetMoving(many.data(), count, red, 2, nullptr, 0);
                setMs+=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-s0).count();
                run(TGPT_VIEW_NONE, cam, cam, 12100+f, raw); context->Flush();
            }
            const double frameMs=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-t0).count()/60.0;
            if(GetStageTimes) GetStageTimes(stages, sizeof(stages));
            ReadStatus(&after);
            const char* at=std::strstr(stages, "scene ");
            std::printf("      %u things the game moves, placed every frame: %u instances traced, the scene's stage %.2f ms of the GPU, %.2f ms of the render "
                        "thread recording, TGPT_SetMoving %.3f ms a frame, %.1f ms a frame in all\n", count, after.instancesTraced, at ? std::atof(at+6) : -1.0,
                        double(after.recordMs), setMs/60.0, frameMs);
            expect(after.instancesTraced>=uint32_t(instances.size())+count, "every thing the game moves is traced, however many");
        }
        expect(SetMoving(nullptr, 0, nullptr, 0, nullptr, 0)==0, "nothing moving");
        ownRays=ownBefore;
    };
    // TGPT_TEST_SPEED_ONLY=1 (docs/agents/speed.md, PT-73 c): what a frame costs the GPU by stage in a closed room, as the
    // tavern is one, whose every surface has one material: plain; read HDRP Lit's way from three of the game's own textures;
    // those with HDRP Lit's detail map; the game's architecture, terrain and triplanar shaders; and the textured room with
    // three glass or three wax cubes in it, or with its floor a subsurface material of the game's snow profile (as the plugin
    // marks the game's ground where its material names that profile). The tracer's own camera rays, no denoiser, no sun, eight lamps, one bounce (the plugin's default). Printed,
    // never judged (other programs may share the GPU: builds are run back to back). Each case's 48th frame is saved
    // (speed-<case>.png) with its floats (speed-<case>.bin), for "the same picture" between two builds. Then 300 frames' GPU
    // time each: the least, the tenth quantile and the median (with other tests on the GPU a frame's time holds their turns
    // too, and only the fastest frames are this build's own), and the stage times' means over about the last second of them.
    if(speed1Only || nosunOnly || GetEnvironmentVariableA("TGPT_TEST_SPEED_ONLY", nullptr, 0)>0) {
        if(!RegisterSharedTexture || !SetMaterialPrograms || !GetStageTimes) { std::printf("FAILED: the speed study needs shared textures, material programs and stage times\n"); return 1; }
        // A 1024x1024 map with all its mips: a value with a fine pattern over it.
        std::vector<ComPtr<ID3D11Texture2D>> keep;
        auto map=[&](uint32_t seed, float r, float g, float b, float a, float spread, bool srgb) {
            const UINT size=1024; std::vector<std::vector<uint32_t>> levels; std::vector<D3D11_SUBRESOURCE_DATA> init;
            levels.emplace_back(size_t(size)*size);
            uint32_t s=seed*747796405u+2891336453u;
            const float base[4]={r, g, b, a};
            for(uint32_t& p:levels[0]) {
                p=0;
                for(int c=0;c<4;++c) { s=s*1664525u+1013904223u; const float v=std::clamp(base[c]+spread*(float(s>>8)/16777215.f-0.5f), 0.f, 1.f); p|=uint32_t(std::lround(v*255.f))<<(8*c); }
            }
            for(UINT n=size/2;n>=1;n/=2) {
                std::vector<uint32_t> down(size_t(n)*n);
                const std::vector<uint32_t>& up=levels.back();
                for(UINT y=0;y<n;++y) for(UINT x=0;x<n;++x) {
                    uint32_t p=0;
                    for(int c=0;c<4;++c) { uint32_t sum=0; for(UINT k=0;k<4;++k) sum+=(up[size_t(y*2+(k>>1))*(n*2)+x*2+(k&1u)]>>(8*c))&0xFFu; p|=((sum+2)/4)<<(8*c); }
                    down[size_t(y)*n+x]=p;
                }
                levels.push_back(std::move(down));
            }
            UINT n=size; for(const auto& l:levels) { init.push_back({l.data(), n*4, 0}); n/=2; }
            D3D11_TEXTURE2D_DESC d{size, size, UINT(levels.size()), 1, srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, init.data(), &t); keep.push_back(t);
            TGPT_SharedTexture i{}; i.size=sizeof(i); i.abi=TGPT_ABI;
            return t && RegisterSharedTexture(uint64_t(uintptr_t(t.Get())), &i)>=0 ? int32_t(i.handle) : -1;
        };
        const int32_t colorA=map(1, 0.72f, 0.60f, 0.48f, 1, 0.3f, true), normalA=map(2, 0.5f, 0.5f, 1, 1, 0.2f, false), maskA=map(3, 0, 0.8f, 0.5f, 0.3f, 0.2f, false);
        const int32_t colorB=map(4, 0.5f, 0.55f, 0.4f, 1, 0.3f, true), normalB=map(5, 0.5f, 0.5f, 1, 1, 0.2f, false), maskB=map(6, 0, 0.8f, 0.5f, 0.3f, 0.2f, false);
        const int32_t microN=map(7, 0.5f, 0.5f, 1, 1, 0.1f, false), noiseMap=map(8, 0.5f, 0.5f, 0.5f, 1, 0.6f, false), detailMap=map(9, 0.5f, 0.5f, 0.5f, 0.5f, 0.3f, false);
        if(colorA<0 || normalA<0 || maskA<0 || colorB<0 || normalB<0 || maskB<0 || microN<0 || noiseMap<0 || detailMap<0) { std::printf("FAILED: the speed study's maps were not shared\n"); return 1; }
        std::vector<uint32_t> rows;
        auto bits=[](float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; };
        auto record=[&](uint32_t program, const std::vector<int32_t>& textures, const std::vector<std::array<float, 4>>& values) {
            const uint32_t row=uint32_t(rows.size()/4);
            uint32_t present=0; for(size_t i=0;i<textures.size();++i) if(textures[i]!=-1) present|=1u<<i;
            rows.insert(rows.end(), {program, uint32_t(textures.size()), uint32_t(values.size()), present});
            for(size_t i=0;i<((textures.size()+3)/4)*4;++i) rows.push_back(i<textures.size() ? uint32_t(textures[i]) : 0xFFFFFFFFu);
            for(const auto& v:values) for(int k=0;k<4;++k) rows.push_back(bits(v[size_t(k)]));
            return row;
        };
        // 100 plain, 101 HDRP Lit with its three maps, 102 with a detail map too, 103 the architecture shader (overlay and mask
        // noise on: every one of its reads counts), 104 clear glass, 105 wax, 106 the terrain shader (two of its eight layers
        // painted, as most of a ground is), 107 the triplanar shader, 108 the architecture shader with its overlay and its
        // mask noise off, 109 HDRP Lit with its three maps and the game's snow profile (scattering 27.72, 30.57, 57.76 mm).
        TGPT_Material six[10];
        six[0]=materials[0]; six[0].albedoTile=-1; six[0].colorTexture=six[0].normalTexture=six[0].maskTexture=six[0].emissiveTexture=-1;
        six[0].baseColor[0]=six[0].baseColor[1]=six[0].baseColor[2]=0.5f;
        six[1]=six[0]; six[1].baseColor[0]=six[1].baseColor[1]=six[1].baseColor[2]=1.f; six[1].colorTexture=colorA; six[1].normalTexture=normalA; six[1].maskTexture=maskA;
        six[1].normalScale=1.f; six[1].uvScaleOffset[0]=six[1].uvScaleOffset[1]=4.f; six[1].maskRemap=0xFF00FF00u;
        six[2]=six[1]; six[2].program=TGPT_PROGRAM_DETAIL;
        six[2].programRow=record(TGPT_PROGRAM_DETAIL, {detailMap, colorA, -1, -1}, {{8.f, 8.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 0.f}});
        six[3]=six[1]; six[3].program=TGPT_PROGRAM_ARCHITECTURE;
        six[3].programRow=record(TGPT_PROGRAM_ARCHITECTURE, {colorB, normalB, maskB, microN, noiseMap, colorA, normalA, maskA},
            {{0.9f, 0.9f, 0.9f, 1.f}, {1.f, 1.f, 3.f, 6.f}, {0.2f, 0.8f, -100.f, 0.5f}, {0.5f, 0.f, 0.f, 0.8f}, {1.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}});
        six[4]=six[0]; six[4].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS; six[4].alphaCutoff=0.f; six[4].baseColor[0]=six[4].baseColor[1]=six[4].baseColor[2]=1.f;
        six[4].smoothness=0.95f; six[4].ior=1.5f;
        six[5]=six[0]; six[5].flags=TGPT_MATERIAL_SUBSURFACE; six[5].subsurfaceMask=1.f; six[5].subsurface[0]=100.f; six[5].subsurface[1]=143.f; six[5].subsurface[2]=200.f;
        six[5].baseColor[0]=0.8f; six[5].baseColor[1]=0.7f; six[5].baseColor[2]=0.55f;
        const int32_t splat0=map(10, 0.6f, 0.4f, 0, 0, 0, false), splat1=map(11, 0, 0, 0, 0, 0, false);
        six[6]=six[0]; six[6].baseColor[0]=six[6].baseColor[1]=six[6].baseColor[2]=1.f; six[6].program=TGPT_PROGRAM_TERRAIN;
        {
            std::vector<int32_t> textures={splat0, splat1};
            std::vector<std::array<float, 4>> values={{0.f, 0.f, 1.f, 0.2f}};
            for(int i=0;i<8;++i) {
                textures.push_back(i%2 ? colorB : colorA); textures.push_back(i%2 ? normalB : normalA); textures.push_back(i%2 ? maskB : maskA);
                values.push_back({0.9f, 0.9f, 0.9f, 2.f+float(i)}); values.push_back({1.f, 1.f, 0.8f, 0.f});
            }
            values.push_back({0.f, 0.f, 0.f, 0.f});
            six[6].programRow=record(TGPT_PROGRAM_TERRAIN, textures, values);
        }
        six[7]=six[6]; six[7].program=TGPT_PROGRAM_TRIPLANAR;
        six[7].programRow=record(TGPT_PROGRAM_TRIPLANAR, {colorA, normalA, maskA, colorB, normalB, maskB, noiseMap, microN},
            {{0.9f, 0.9f, 0.9f, 0.5f}, {0.8f, 0.8f, 0.8f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f}, {1.f, 1.f, 0.5f, 0.5f}, {0.f, 1.f, 0.1f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.2f}, {0.1f, 0.1f, 0.1f, 0.f}});
        six[8]=six[1]; six[8].program=TGPT_PROGRAM_ARCHITECTURE;
        six[8].programRow=record(TGPT_PROGRAM_ARCHITECTURE, {colorB, normalB, maskB, microN, noiseMap, colorA, normalA, maskA},
            {{0.9f, 0.9f, 0.9f, 1.f}, {1.f, 1.f, 3.f, 6.f}, {0.2f, 0.8f, -100.f, 0.5f}, {0.5f, 0.f, 0.f, 0.8f}, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}});
        six[9]=six[1]; six[9].flags=TGPT_MATERIAL_SUBSURFACE; six[9].subsurfaceMask=1.f; six[9].subsurface[0]=1000.f/27.72f; six[9].subsurface[1]=1000.f/30.57f; six[9].subsurface[2]=1000.f/57.76f;
        if(splat0<0 || splat1<0 || SetMaterialPrograms(0, rows.data(), uint32_t(rows.size()/4))!=0 || SetMaterials(100, six, 10)!=0) { std::printf("FAILED: the speed study's materials\n"); return 1; }
        // TGPT_TEST_SPEED1_ONLY=1 (PT-99, docs/agents/speed-1.md): the speed switches' own checks and times, on a town
        // of these materials, in place of the study's rooms below.
        if(speed1Only)
#include "pathtracing_speed1_checks.inl"
        // The room closed all round (12 m square, 6 m tall), twelve boxes standing in it, and a case's three cubes in front.
        auto room=[&](uint32_t wall, uint32_t extra, uint32_t extras, uint32_t floor) {
            std::vector<TGPT_Instance> list; std::vector<uint32_t> index;
            auto put=[&](int mesh, const M4& world, uint32_t material, uint32_t flags) {
                TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
                in.mesh=mesh; in.materialBase=uint32_t(index.size()); in.mask=0xFF; in.flags=flags;
                index.push_back(material); index.push_back(material); list.push_back(in);
            };
            put(quadHandle, translate_scale({0, 0, 0}, {12, 1, 12}), floor ? floor : wall, 0);
            put(quadHandle, mul(translate_scale({0, 3, 6}, {12, 6, 1}), rot_x(-pi/2)), wall, 0);
            put(quadHandle, mul(translate_scale({-6, 3, 0}, {1, 6, 12}), rot_z(-pi/2)), wall, 0);
            put(quadHandle, mul(translate_scale({6, 3, 0}, {1, 6, 12}), rot_z(pi/2)), wall, 0);
            put(quadHandle, mul(translate_scale({0, 6, 0}, {12, 1, 12}), rot_x(pi)), wall, 0);
            put(quadHandle, mul(translate_scale({0, 3, -6}, {12, 6, 1}), rot_x(pi/2)), wall, 0);
            for(int i=0;i<12;++i) put(cubeHandle, translate_scale({-4.5f+3.f*float(i%4), 0.5f+0.25f*float(i%3), -1.5f+2.5f*float(i/4)}, {1.f+0.2f*float(i%3), 1.f+0.5f*float(i%3), 1.f}), wall, 0);
            for(uint32_t i=0;i<extras;++i) put(cubeHandle, translate_scale({-3.f+3.f*float(i), 0.4f, -3.2f}, {0.8f, 0.8f, 0.8f}), extra, extra==104u ? uint32_t(TGPT_INSTANCE_ALPHA_TEST) : 0u);
            return SetScene(list.data(), uint32_t(list.size()), index.data(), uint32_t(index.size()))==0;
        };
        std::vector<TGPT_Light> lamps(8);
        for(int i=0;i<8;++i) { auto& l=lamps[size_t(i)]; l.position[0]=-4.5f+3.f*float(i%4); l.position[1]=4.5f; l.position[2]=-3.f+5.f*float(i/4); l.type=0; l.color[0]=30; l.color[1]=20; l.color[2]=12; l.range=14; l.radius=0.05f; l.id=uint32_t(100+i); }
        if(SetLights(lamps.data(), 8)!=0) { std::printf("FAILED: the speed study's lamps\n"); return 1; }
        sunOn=false; skyIntensity=1e-6f; ownRays=true; bounces=1;
        // TGPT_TEST_NOSUN_ONLY=1 (PT-133, docs/agents/nosun.md): a frame without a sun, in place of the study's rooms below.
        if(nosunOnly)
#include "pathtracing_nosun_checks.inl"
        struct Case { const char* name; uint32_t wall, extra, extras, floor; };
        const Case cases[]={{"plain", 100, 0, 0, 0}, {"lit", 101, 0, 0, 0}, {"detail", 102, 0, 0, 0}, {"architecture", 103, 0, 0, 0}, {"arch-plain", 108, 0, 0, 0},
                            {"terrain", 106, 0, 0, 0}, {"triplanar", 107, 0, 0, 0}, {"lit-glass", 101, 104, 3, 0}, {"lit-wax", 101, 105, 3, 0}, {"lit-snow-floor", 101, 0, 0, 109}};
        const Camera scam=make_camera({0, 2.5f, -5.5f}, {0, 1.5f, 3}, 60.f, float(W)/float(H));
        for(const Case& c:cases) {
            if(!room(c.wall, c.extra, c.extras, c.floor)) { std::printf("FAILED: the speed study's scene %s\n", c.name); return 1; }
            uint32_t frame=0;
            for(;frame<48;++frame) run(TGPT_VIEW_NONE, scam, scam, frame, TGPT_OPTION_NO_DENOISER);
            const auto px=readback();
            save(px, (std::string("speed-")+c.name+".png").c_str(), true);
            if(FILE* out=_wfopen((images/(std::string("speed-")+c.name+".bin")).c_str(), L"wb")) { std::fwrite(px.data(), sizeof(float), px.size(), out); std::fclose(out); }
            std::vector<float> ms;
            for(;frame<348;++frame) {
                run(TGPT_VIEW_NONE, scam, scam, frame, TGPT_OPTION_NO_DENOISER); context->Flush();
                ReadStatus(&status); ms.push_back(status.gpuMs);
            }
            std::sort(ms.begin(), ms.end());
            char stages[1024]{}; GetStageTimes(stages, sizeof(stages));
            std::printf("      speed %-13s GPU least %.2f ms, tenth quantile %.2f, median %.2f (%u instances) | %s\n", c.name, ms.front(), ms[ms.size()/10], ms[ms.size()/2],
                        status.instancesTraced, stages);
            std::fflush(stdout);
        }
        std::printf("speed study done\n");
        return 0;
    }
    // TGPT_TEST_MEGA_ONLY=1 (PT-109, docs/agents/megageo-1.md): the partitioned top level against the plain one, alone,
    // right after the warm-up (about a minute); the whole test does not take it.
    if(GetEnvironmentVariableA("TGPT_TEST_MEGA_ONLY", nullptr, 0)>0)
#include "pathtracing_mega_checks.inl"
    // TGPT_TEST_MEGAPLANTS_ONLY=1 (PT-109, docs/agents/megageo-4.md): the small plants' groups built from clusters (the
    // hidden "Mega Geometry trial" 5) against the plain groups, alone, right after the warm-up; the whole test does not take it.
    if(GetEnvironmentVariableA("TGPT_TEST_MEGAPLANTS_ONLY", nullptr, 0)>0)
#include "pathtracing_megaplants_checks.inl"
    // TGPT_TEST_OMM_ONLY=1 (PT-110, docs/agents/omm-1.md): a character's and a plant group's submeshes that are no cut-out,
    // solid in their own structures, against all of them tested by the shader: the same hits. Alone, right after the
    // warm-up (under a minute); the whole test does not take it.
    if(GetEnvironmentVariableA("TGPT_TEST_OMM_ONLY", nullptr, 0)>0)
#include "pathtracing_omm_checks.inl"
    // TGPT_TEST_MOVING_ONLY=1: the checks of the things the game moves alone, right after the warm-up (some seconds).
    if(GetEnvironmentVariableA("TGPT_TEST_MOVING_ONLY", nullptr, 0)>0) {
        movingChecks();
        std::printf(failures ? "FAILED %d checks\n" : "moving checks passed\n", failures); return failures ? 1 : 0;
    }
    // The water's fixture first (TGPT_TEST_WATER_SURFACE=1: it runs alone, at its own small size, and ends the test).
    #include "water_surface_gpu.inl"
#include "pathtracing_edges_checks.inl"
#include "pathtracing_reuse_checks.inl"
#include "pathtracing_walk_checks.inl"
#include "pathtracing_candle_checks.inl"
#include "pathtracing_skin_material_checks.inl"
#include "pathtracing_glass_checks.inl"
    // TGPT_TEST_GLASS_ONLY: the room's coloured-glass checks too (further down in the whole test), then the run ends.
    if(opticalOnly) {
        uint32_t index=60000;
#include "pathtracing_glass_tint_checks.inl"
        std::printf(failures ? "FAILED %d optical checks\n" : "optical checks passed\n", failures);
        return failures ? 1 : 0;
    }

    // PT-63 decals: the real camera-ray path, no denoiser, live shared maps and a real mirror.
    // TGPT_TEST_DECALS_ONLY is a bounded worker/mutation run; the normal full test also runs these checks.
    {
        std::printf("\nDecals at every ray hit (Denoising None)\n");
        expect(SetDecals!=nullptr, "decal export exists");
        const auto savedObjects=objects;
        const bool savedOwn=ownRays; ownRays=true;
        const uint32_t savedBounces=bounces; bounces=1;
        const float savedSky=skyIntensity;
        const auto savedMaterials=materials;
        auto dm=materials[0]; dm.albedoTile=-1; dm.flags=TGPT_MATERIAL_DOUBLE_SIDED;
        dm.baseColor[0]=dm.baseColor[1]=dm.baseColor[2]=0.5f; dm.metallic=0; dm.smoothness=0.1f;
        auto mirror=dm; mirror.metallic=1; mirror.smoothness=1;
        mirror.baseColor[0]=mirror.baseColor[1]=mirror.baseColor[2]=0.98f;
        TGPT_Material mm[2]={dm,mirror}; SetMaterials(0,mm,2);
        objects={ {0,translate_scale({0,0,0},{10,1,12}),{0}},
                  {0,mul(translate_scale({0,2,4},{8,4,1}),rot_x(-pi/2)),{1}} };
        std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
        for(const auto& o:objects) {
            TGPT_Instance inst{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) inst.transform[4*r+c]=o.world.m[r][c];
            inst.mesh=quadHandle; inst.materialBase=uint32_t(ids.size()); inst.mask=0xFF; inst.renderingLayerMask=1;
            ids.push_back(uint32_t(o.materials[0])); list.push_back(inst);
        }
        SetScene(list.data(),uint32_t(list.size()),ids.data(),uint32_t(ids.size()));
        const Camera dc=make_camera({0,3,-7},{0,1,2},55.f,float(W)/float(H));
        auto decal=[] {
            TGPT_Decal d{};
            d.worldToDecal[0]=1.f/8; d.worldToDecal[5]=1; d.worldToDecal[10]=1.f/8;
            // Tangent x, bitangent z, normal +y.
            d.normalToWorld[0]=1; d.normalToWorld[6]=1; d.normalToWorld[9]=1;
            d.boundsMin[0]=d.boundsMin[2]=-4; d.boundsMin[1]=-0.5f;
            d.boundsMax[0]=d.boundsMax[2]=4; d.boundsMax[1]=0.5f;
            d.layerMask=1; d.flags=1;
            d.baseColor[0]=0.8f; d.baseColor[1]=0.04f; d.baseColor[2]=0.02f; d.baseColor[3]=1;
            d.emissiveBlend[3]=1; d.uvScaleOffset[0]=d.uvScaleOffset[1]=1;
            d.fade[0]=d.fade[3]=1;
            d.remapMin[1]=d.remapMax[1]=1; d.remapMin[3]=1;
            for(auto& t:d.textures) t=-1;
            return d;
        };
        auto d=decal();
        const uint32_t options=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_CLAMPS|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_GI_BOILING;
        auto shot=[&](uint32_t view,int frames=5) {
            std::vector<float> sum(size_t(W)*H*3,0);
            for(int f=0;f<frames+2;++f) {
                run(view,dc,dc,f ? uint32_t(16000+f) : 0,options);
                auto img=readback();
                if(f>=2) for(size_t i=0;i<img.size();++i) sum[i]+=img[i]/frames;
            }
            return sum;
        };
        std::vector<size_t> floorPixels,mirrorPixels,wallPixels,floorRight,floorLeft;
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const float v[4]={(x+0.5f)/W*2-1,1-(y+0.5f)/H*2,1,1}; float p[4]{};
            for(int r=0;r<4;++r) for(int k=0;k<4;++k) p[r]+=dc.invViewProj.m[r][k]*v[k];
            const V3 dir=normalize(V3{p[0]/p[3],p[1]/p[3],p[2]/p[3]});
            if(dir.y>=-0.01f || dir.z<=0) continue;
            const V3 floor=dc.position+dir*(-dc.position.y/dir.y);
            const V3 wall=dc.position+dir*((4-dc.position.z)/dir.z);
            const size_t pixel=(size_t(y)*W+x)*3;
            if(floor.z<3.8f && std::fabs(floor.x)<3.3f && std::fabs(floor.z)<3.3f) {
                floorPixels.push_back(pixel);
                if(floor.x-floor.z>0.2f) floorRight.push_back(pixel); if(floor.z-floor.x>0.2f) floorLeft.push_back(pixel);
            }
            if(wall.y>0.25f && wall.y<3.7f && std::fabs(wall.x)<3.6f) {
                wallPixels.push_back(pixel);
                const V3 reflected={dir.x,dir.y,-dir.z};
                const V3 f=wall+reflected*(-wall.y/reflected.y);
                if(std::fabs(f.x)<2.5f && f.z>-2.5f && f.z<0.5f) mirrorPixels.push_back(pixel);
            }
        }
        expect(floorPixels.size()>1000 && mirrorPixels.size()>1000,"decal floor and its mirror both have measured pixels");
        auto mean=[](const std::vector<float>& img,const std::vector<size_t>& region,int channel) {
            double sum=0; for(size_t i:region) sum+=img[i+channel]; return sum/std::max(size_t(1),region.size());
        };
        auto difference=[](const std::vector<float>& a,const std::vector<float>& b,const std::vector<size_t>& region) {
            double sum=0; for(size_t i:region) for(int k=0;k<3;++k) sum+=std::fabs(a[i+k]-b[i+k]); return sum/std::max(size_t(1),region.size()*3);
        };
        SetDecals(nullptr,0); const auto off=shot(TGPT_VIEW_ALBEDO);
        SetDecals(&d,1); const auto on=shot(TGPT_VIEW_ALBEDO);
        save(off,"decals-off.png",true); save(on,"decals-on.png",true);
        ReadStatus(&status);
        std::printf("      decal scene: instances %u, traced %u, frames %llu failed %llu, GPU %.3f ms\n",status.instances,status.instancesTraced,(unsigned long long)status.framesTraced,(unsigned long long)status.framesFailed,status.gpuMs);
        const double red=mean(on,floorPixels,0),green=mean(on,floorPixels,1);
        std::printf("      camera colour: red %.6f green %.6f, off %.6f\n",red,green,mean(off,floorPixels,0));
        expect(red>green*2 && difference(on,off,floorPixels)>0.1,"camera ray sees decal colour on the receiving floor");
        SetDecals(nullptr,0); const auto removed=shot(TGPT_VIEW_ALBEDO);
        expect(difference(removed,off,floorPixels)<0.001,"removing a decal restores the receiving material");
        d.layerMask=2; SetDecals(&d,1); const auto wrongLayer=shot(TGPT_VIEW_ALBEDO);
        expect(difference(wrongLayer,off,floorPixels)<0.001,"decal respects the receiver rendering layer");
        d.flags|=512; SetDecals(&d,1); const auto noLayerFilter=shot(TGPT_VIEW_ALBEDO);
        expect(difference(noLayerFilter,on,floorPixels)<0.001,"disabled decal-layer filtering does not reject the receiver");
        d.flags&=~512u; d.layerMask=1;
        // Merge coverage: receiver layers must not replace the previous-transform field of moving things.
        TGPT_Moving movingFloor{};
        for(int k=0;k<12;++k) movingFloor.transform[k]=movingFloor.previous[k]=list[0].transform[k];
        movingFloor.mesh=list[0].mesh; movingFloor.mask=0xFF; movingFloor.renderingLayerMask=1;
        const uint32_t replacedFloor=0, floorMaterial=0;
        SetMoving(&movingFloor,1,&floorMaterial,1,&replacedFloor,1); SetDecals(&d,1);
        const auto movingPaint=shot(TGPT_VIEW_ALBEDO);
        expect(difference(movingPaint,on,floorPixels)<0.001,"moving receiver keeps decal layers beside its previous transform");
        movingFloor.renderingLayerMask=2; SetMoving(&movingFloor,1,&floorMaterial,1,&replacedFloor,1);
        const auto movingWrongLayer=shot(TGPT_VIEW_ALBEDO);
        expect(difference(movingWrongLayer,off,floorPixels)<0.001,"changing only a moving receiver layer rejects the decal");
        SetMoving(nullptr,0,nullptr,0,nullptr,0);
        auto blue=d; blue.baseColor[0]=0.02f; blue.baseColor[1]=0.04f; blue.baseColor[2]=0.8f;
        blue.baseColor[3]=d.baseColor[3]=0.5f;
        TGPT_Decal overlap[2]={d,blue}; SetDecals(overlap,2); const auto redBlue=shot(TGPT_VIEW_ALBEDO);
        std::swap(overlap[0],overlap[1]); SetDecals(overlap,2); const auto blueRed=shot(TGPT_VIEW_ALBEDO);
        expect(mean(redBlue,floorPixels,2)>mean(redBlue,floorPixels,0)+0.1 && mean(blueRed,floorPixels,0)>mean(blueRed,floorPixels,2)+0.1,
               "overlapping decals blend in draw order");
        // 257 overlapping entries, where only the last one has colour: no fixed overlap count can hide it.
        auto clear=d; clear.baseColor[3]=0;
        std::vector<TGPT_Decal> many(257,clear); many.back()=decal(); SetDecals(many.data(),uint32_t(many.size()));
        const auto deep=shot(TGPT_VIEW_ALBEDO);
        expect(difference(deep,on,floorPixels)<0.001,"all 257 overlapping decals are visited without a layer cap");
        d=decal(); SetDecals(&d,1); const auto mirrorOn=shot(TGPT_VIEW_SPECULAR,16);
        SetDecals(nullptr,0); const auto mirrorOff=shot(TGPT_VIEW_SPECULAR,16);
        std::printf("      mirror: red %.6f green %.6f, difference %.6f\n",mean(mirrorOn,mirrorPixels,0),mean(mirrorOn,mirrorPixels,1),difference(mirrorOn,mirrorOff,mirrorPixels));
        expect(mean(mirrorOn,mirrorPixels,0)>mean(mirrorOn,mirrorPixels,1)*2 && difference(mirrorOn,mirrorOff,mirrorPixels)>0.01,
               "mirror ray sees the floor decal in its colour");
        save(on,"decals-camera.png",true); save(mirrorOn,"decals-mirror.png",true);
        auto texture=[&](uint32_t colour) {
            D3D11_TEXTURE2D_DESC td{}; td.Width=td.Height=256; td.MipLevels=td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count=1; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            std::vector<uint32_t> pixels(256*256,colour); D3D11_SUBRESOURCE_DATA data{pixels.data(),256*4,0};
            ComPtr<ID3D11Texture2D> tex; device->CreateTexture2D(&td,&data,&tex); return tex;
        };
        auto painted=texture(0xFF1040FFu); TGPT_SharedTexture paint{sizeof(TGPT_SharedTexture),TGPT_ABI};
        expect(RegisterSharedTexture(uint64_t(uintptr_t(painted.Get())),&paint)>=0,"decal colour map is the shared original texture");
        d=decal(); d.baseColor[0]=d.baseColor[1]=d.baseColor[2]=1; d.textures[0]=paint.handle;
        SetDecals(&d,1); const auto paintedRed=shot(TGPT_VIEW_ALBEDO);
        std::vector<uint32_t> changedPaint(256*256,0xFFFF4010u);
        context->UpdateSubresource(painted.Get(),0,nullptr,changedPaint.data(),256*4,0); context->Flush();
        const auto paintedBlue=shot(TGPT_VIEW_ALBEDO);
        expect(mean(paintedRed,floorPixels,0)>mean(paintedRed,floorPixels,2)*4 &&
               mean(paintedBlue,floorPixels,2)>mean(paintedBlue,floorPixels,0)*4,
               "decal reads changes to the original colour texture without a copy");
        // Same live texture, deliberately outside 0..1: sampler address modes belong to the material too.
        for(uint32_t y=0;y<256;++y) for(uint32_t x=0;x<256;++x) changedPaint[y*256+x]=x<128 ? 0xFF1010FFu : 0xFFFF1010u;
        context->UpdateSubresource(painted.Get(),0,nullptr,changedPaint.data(),256*4,0); context->Flush();
        d.uvScaleOffset[0]=2; d.samplers[0]=0; SetDecals(&d,1); const auto repeated=shot(TGPT_VIEW_ALBEDO);
        d.samplers[0]=5; SetDecals(&d,1); const auto clamped=shot(TGPT_VIEW_ALBEDO);
        expect(mean(repeated,floorPixels,0)>mean(clamped,floorPixels,0)+0.03 && mean(clamped,floorPixels,2)>mean(repeated,floorPixels,2)+0.03,
            "decal uses the original texture clamp or repeat sampler");
        auto bumps=texture(0xFFFF80CCu); TGPT_SharedTexture info{sizeof(TGPT_SharedTexture),TGPT_ABI};
        expect(RegisterSharedTexture(uint64_t(uintptr_t(bumps.Get())),&info)>=0,"decal normal map is the shared original texture");
        d=decal(); d.flags=2; d.textures[1]=info.handle;
        SetDecals(nullptr,0); const auto flat=shot(TGPT_VIEW_NORMALS);
        SetDecals(&d,1); const auto normal=shot(TGPT_VIEW_NORMALS);
        expect(difference(flat,normal,floorPixels)>0.04,"camera decal uses its normal map");
        SetDecals(nullptr,0); const auto mirrorFlat=shot(TGPT_VIEW_SPECULAR,16);
        SetDecals(&d,1); const auto mirrorNormal=shot(TGPT_VIEW_SPECULAR,16);
        std::printf("      mirror normal difference %.8f\n",difference(mirrorFlat,mirrorNormal,mirrorPixels));
        save(mirrorFlat,"decals-mirror-flat.png",true); save(mirrorNormal,"decals-mirror-normal.png",true);
        std::vector<float> regionMask(size_t(W)*H*3,0); for(size_t i:mirrorPixels) regionMask[i+1]=1;
        save(regionMask,"decals-mirror-region.png",false);
        expect(difference(mirrorFlat,mirrorNormal,mirrorPixels)>0.002,"normal-mapped floor decal changes the light seen in a mirror");
        d=decal(); d.flags=16; d.remapMin[2]=d.remapMax[2]=1;
        SetDecals(&d,1); const auto smooth=shot(TGPT_VIEW_SPECULAR,16);
        SetDecals(nullptr,0); const auto rough=shot(TGPT_VIEW_SPECULAR,16);
        std::printf("      smoothness: specular difference %.6f\n",difference(smooth,rough,floorPixels));
        expect(difference(smooth,rough,floorPixels)>0.002,"decal smoothness changes the camera reflection");
        std::printf("      mirror smoothness difference %.8f\n",difference(smooth,rough,mirrorPixels));
        expect(difference(smooth,rough,mirrorPixels)>0.002,"decal smoothness changes the floor in a mirror");
        auto meshDecal=[&] {
            auto mesh=decal(); mesh.flags|=1024|2048; mesh.reserved[0]=uint32_t(decalMeshHandle); mesh.reserved[1]=0;
            mesh.graphWorld[0]=mesh.graphWorld[10]=8; mesh.graphWorld[5]=1; mesh.graphWorld[7]=0.01f;
            mesh.worldToDecal[7]=-0.01f; mesh.boundsMin[1]=mesh.boundsMax[1]=0.01f;
            return mesh;
        };
        auto md=meshDecal(); SetDecals(&md,1); const auto meshColor=shot(TGPT_VIEW_ALBEDO);
        std::printf("      mesh camera colour difference %.8f\n",difference(meshColor,on,floorPixels));
        expect(difference(meshColor,on,floorPixels)<0.002,"actual mesh decal triangles paint the receiving floor");
        const auto meshMirror=shot(TGPT_VIEW_SPECULAR,16);
        expect(mean(meshMirror,mirrorPixels,0)>mean(meshMirror,mirrorPixels,1)*2 && difference(meshMirror,mirrorOff,mirrorPixels)>0.01,
            "a mirror sees the mesh decal along its reflected ray");
        save(meshColor,"decals-mesh-camera.png",true); save(meshMirror,"decals-mesh-mirror.png",true);
        md.graphWorld[7]=-0.25f; md.worldToDecal[7]=0.25f; md.boundsMin[1]=md.boundsMax[1]=-0.25f;
        SetDecals(&md,1); const auto behind=shot(TGPT_VIEW_ALBEDO);
        expect(difference(behind,off,floorPixels)<0.001,"mesh decals behind the receiver do not project through it");
        md=meshDecal(); md.flags=1024|2048|2; md.textures[1]=info.handle;
        SetDecals(&md,1); const auto meshNormal=shot(TGPT_VIEW_NORMALS);
        expect(difference(meshNormal,normal,floorPixels)<0.003,"mesh decal normal uses its actual tangent stream");
        md=meshDecal(); md.flags=1024|2048|16; md.remapMin[2]=md.remapMax[2]=1;
        SetDecals(&md,1); const auto meshSmooth=shot(TGPT_VIEW_SPECULAR,16);
        expect(difference(meshSmooth,rough,mirrorPixels)>0.002,"mesh decal smoothness changes its mirror image");
        md=meshDecal(); md.baseColor[3]=0.5f; blue=decal(); blue.baseColor[0]=0.02f; blue.baseColor[2]=0.8f; blue.baseColor[3]=0.5f;
        TGPT_Decal mixed[2]={md,blue}; SetDecals(mixed,2); const auto mixedOrder=shot(TGPT_VIEW_ALBEDO);
        expect(mean(mixedOrder,floorPixels,2)>mean(mixedOrder,floorPixels,0)+0.1,"projector draw order follows mesh decal draws");
        md=meshDecal(); md.reserved[0]=uint32_t(decalPartsHandle);
        auto otherHalf=md; otherHalf.reserved[1]=1; otherHalf.baseColor[0]=0.02f; otherHalf.baseColor[2]=0.8f;
        TGPT_Decal parts[2]={md,otherHalf}; SetDecals(parts,2); const auto partColors=shot(TGPT_VIEW_ALBEDO);
        expect(mean(partColors,floorRight,0)>mean(partColors,floorRight,2)*2 && mean(partColors,floorLeft,2)>mean(partColors,floorLeft,0)*2,
            "mesh decal submeshes keep their distinct materials");
        md=meshDecal(); md.reserved[0]=uint32_t(decalLayersHandle); md.boundsMax[1]=0.02f;
        md.baseColor[0]=md.baseColor[1]=md.baseColor[2]=1; md.baseColor[3]=0.5f; md.textures[0]=paint.handle;
        SetDecals(&md,1); const auto allPrimitives=shot(TGPT_VIEW_ALBEDO);
        expect(mean(allPrimitives,floorPixels,2)>mean(allPrimitives,floorPixels,0)+0.1,
            "all intersected mesh primitives blend in primitive draw order");
        // Shipped BFX graph goes through the same native payload and shared hit decoder.
        auto graph=decal(); graph.kind=7; for(auto& t:graph.graphTextures) t=-1;
        graph.graph[0][0]=0.8f; graph.graph[0][1]=0.04f; graph.graph[0][2]=0.02f; graph.graph[0][3]=1;
        graph.baseColor[0]=graph.baseColor[2]=0.05f; graph.baseColor[1]=0.8f; // green if its shader program is ignored
        graph.graphTextures[0]=graph.graphTextures[1]=-2; // explicit white, distinct from the shader's default black
        graph.graph[2][0]=0; graph.graph[4][1]=1; graph.graph[4][3]=0;
        SetDecals(&graph,1); const auto graphColor=shot(TGPT_VIEW_ALBEDO);
        std::printf("      BFX graph colour red %.8f green %.8f\n",mean(graphColor,floorPixels,0),mean(graphColor,floorPixels,1));
        expect(mean(graphColor,floorPixels,0)>mean(graphColor,floorPixels,1)*2 && difference(graphColor,off,floorPixels)>0.05,
            "compiled BFX decal graph paints the native traced surface");
        // A glowing decal on a non-emissive floor sends real bounced light to a diffuse wall.
        mm[1]=dm; SetMaterials(0,mm,2); sunOn=false; skyIntensity=1e-6f; SetLights(nullptr,0);
        d=decal(); d.flags=32; d.emissiveBlend[0]=20; d.emissiveBlend[1]=2; d.emissiveBlend[2]=1;
        SetDecals(nullptr,0); const auto darkWall=shot(TGPT_VIEW_INDIRECT_RAW,24);
        SetDecals(&d,1); const auto litWall=shot(TGPT_VIEW_INDIRECT_RAW,24);
        std::printf("      glowing decal bounced light on wall: off %.6f on %.6f\n",mean(darkWall,wallPixels,0),mean(litWall,wallPixels,0));
        expect(mean(litWall,wallPixels,0)>mean(darkWall,wallPixels,0)+0.02 && mean(litWall,wallPixels,0)>mean(litWall,wallPixels,1)*2,
               "a glowing floor decal lights another wall by traced paths");
        save(litWall,"decals-glow-bounce.png",true);
        md=meshDecal(); md.flags=1024|2048|32; md.emissiveBlend[0]=20; md.emissiveBlend[1]=2; md.emissiveBlend[2]=1;
        SetDecals(&md,1); const auto meshGlow=shot(TGPT_VIEW_INDIRECT_RAW,24);
        expect(mean(meshGlow,wallPixels,0)>mean(darkWall,wallPixels,0)+0.02 && mean(meshGlow,wallPixels,0)>mean(meshGlow,wallPixels,1)*2,
            "glowing mesh decal lights another wall by traced paths");
        if(GetEnvironmentVariableA("TGPT_TEST_DECAL_COST",nullptr,0)>0) {
            mm[1]=mirror; SetMaterials(0,mm,2); skyIntensity=savedSky; sunOn=true;
            auto measure=[&](const char* name,const TGPT_Decal* data,uint32_t count) {
                SetDecals(data,count); double total=0,minimum=1e30,maximum=0;
                for(int f=0;f<40;++f) {
                    run(TGPT_VIEW_NONE,dc,dc,f ? uint32_t(18000+f) : 0,options); (void)readback(); ReadStatus(&status);
                    if(f>=8) { total+=status.gpuMs; minimum=std::min(minimum,double(status.gpuMs)); maximum=std::max(maximum,double(status.gpuMs)); }
                }
                std::printf("      decal cost 1280x720 None %s: mean %.4f min %.4f max %.4f ms\n",name,total/32,minimum,maximum);
            };
            measure("off",nullptr,0); d=decal(); measure("one projector",&d,1);
            measure("257 overlapping projectors",many.data(),uint32_t(many.size())); md=meshDecal(); measure("one mesh",&md,1);
            measure("BFX graph",&graph,1); measure("off again",nullptr,0);
        }
        SetDecals(nullptr,0); SetMaterials(0,savedMaterials.data(),uint32_t(savedMaterials.size()));
        objects=savedObjects; SetScene(instances.data(),uint32_t(instances.size()),indices.data(),uint32_t(indices.size()));
        SetLights(&light,1); ownRays=savedOwn; bounces=savedBounces; sunOn=true; skyIntensity=savedSky;
        run(TGPT_VIEW_NONE,cam,cam,0);
        if(GetEnvironmentVariableA("TGPT_TEST_DECALS_ONLY",nullptr,0)>0) {
            std::printf("decal checks: %s (%d failures)\n",failures ? "FAILED" : "passed",failures); return failures ? 1 : 0;
        }
    }

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
    // NRD's look (PT-19), measured as the in-game harness measures a frozen world (tools/harness/analyze.py), every number
    // in 8-bit levels of the picture as save() shows it, the camera still (without jitter: in game the upscaler resolves
    // it, and here it would count as change). Boil: how much the mean of each 8x8-pixel block changes from frame to frame
    // (its standard deviation over 16 frames), the mean and the 95th percentile over the blocks; flicker: the same for
    // single pixels; error: the 16 frames' mean picture against an undenoised reference (2 x 80 frames of 8 rays a pixel,
    // no denoiser); detail kept: how much of the reference's fine detail (its Laplacian) the mean picture keeps, 1 all of
    // it, 0 a flat picture, measured against the two halves of the reference so that their noise cancels (the Laplacian of
    // the mean picture times the halves' mean, over the product of the two halves'). Over the whole picture and under the
    // ceiling, which the bounced light alone lights. Bump-mapped surfaces, as the game's are, and the game's defaults
    // (1 bounce, 1 ray a pixel, 4 spatial samples, histories 4 and 8). Alone: TGPT_TEST_NRD_ONLY=1.
    // light: the frames' mean light (linear, the whole picture) against the undenoised reference's, 1 when none is lost.
    struct NrdLook { double boil=0, boilP95=0, flicker=0, error=0, detail=0, detailCeiling=0, gpuMs=0, light=0; bool nrd=false; };
    auto nrdStudy=[&]() {
        auto shown=[&](const std::vector<float>& px) {
            std::vector<float> d(size_t(W)*H);
            for(size_t i=0;i<d.size();++i) {
                float v=0.2126f*px[i*3]+0.7152f*px[i*3+1]+0.0722f*px[i*3+2];
                if(!std::isfinite(v)) v=1e3f;
                v=std::max(v, 0.f); d[i]=255.f*std::pow(v/(1.f+v), 1.f/2.2f);
            }
            return d;
        };
        auto laplacian=[&](const std::vector<float>& d, size_t i) { return 4.0*d[i]-d[i-1]-d[i+1]-d[i-W]-d[i+W]; };
        // Detail kept in a region (read-back rows are bottom-up: 600-710 is under the ceiling).
        auto kept=[&](const std::vector<float>& mean, const std::vector<float>& a, const std::vector<float>& b, UINT x0, UINT y0, UINT x1, UINT y1) {
            double num=0, den=0;
            for(UINT y=y0;y<y1;++y) for(UINT x=x0;x<x1;++x) {
                const size_t i=size_t(y)*W+x; const double la=laplacian(a, i), lb=laplacian(b, i);
                num+=laplacian(mean, i)*0.5*(la+lb); den+=la*lb;
            }
            return den>0 ? num/den : 0.0;
        };
        const Camera still=make_camera({0, 3.2f, -9}, {0, 2.2f, 2}, 60.f, float(W)/float(H));
        uint32_t index=30000;
        // The undenoised reference in two halves: 8 rays a pixel, no denoiser.
        struct Reference { std::vector<float> a, b, both; };
        auto reference=[&](uint32_t more=0u) {
            const uint32_t keep=rays; rays=8;
            std::vector<double> sum[2]={std::vector<double>(size_t(W)*H*3, 0.0), std::vector<double>(size_t(W)*H*3, 0.0)};
            for(uint32_t i=0;i<168;++i) {
                run(TGPT_VIEW_NONE, still, still, i ? index++ : 0, TGPT_OPTION_NO_DENOISER|more);
                if(i<8) continue;
                const auto r=readback();
                auto& s=sum[i%2];
                for(size_t j=0;j<s.size();++j) s[j]+=r[j];
            }
            rays=keep;
            Reference ref; std::vector<float> a(sum[0].size()), b(a.size()), both(a.size());
            for(size_t j=0;j<a.size();++j) { a[j]=float(sum[0][j]/80.0); b[j]=float(sum[1][j]/80.0); both[j]=0.5f*(a[j]+b[j]); }
            ref.a=shown(a); ref.b=shown(b); ref.both=both;
            return ref;
        };
        auto look=[&](uint32_t options, const Reference& ref, const char* image) {
            constexpr uint32_t warm=48, frames=16;
            const UINT bw=W/8, bh=H/8;
            std::vector<double> sum(size_t(W)*H, 0.0), sum2(size_t(W)*H, 0.0), bsum(size_t(bw)*bh, 0.0), bsum2(size_t(bw)*bh, 0.0);
            std::vector<double> rgb(size_t(W)*H*3, 0.0);
            NrdLook l;
            for(uint32_t i=0;i<warm+frames;++i) {
                run(TGPT_VIEW_NONE, still, still, i ? index++ : 0, options);
                if(i<warm) continue;
                ReadStatus(&status); l.gpuMs+=status.gpuMs/frames;
                const auto px=readback();
                for(size_t j=0;j<rgb.size();++j) rgb[j]+=px[j]/frames;
                const auto d=shown(px);
                for(size_t j=0;j<d.size();++j) { sum[j]+=d[j]; sum2[j]+=double(d[j])*d[j]; }
                for(UINT by=0;by<bh;++by) for(UINT bx=0;bx<bw;++bx) {
                    double b=0;
                    for(UINT y=0;y<8;++y) for(UINT x=0;x<8;++x) b+=d[size_t(by*8+y)*W+bx*8+x];
                    b/=64.0; bsum[size_t(by)*bw+bx]+=b; bsum2[size_t(by)*bw+bx]+=b*b;
                }
            }
            ReadStatus(&status); l.nrd=(status.features&TGPT_FEATURE_NRD_ACTIVE)!=0;
            std::vector<double> boils;
            for(UINT by=1;by+1<bh;++by) for(UINT bx=1;bx+1<bw;++bx) {
                const size_t j=size_t(by)*bw+bx; const double m=bsum[j]/frames;
                boils.push_back(std::sqrt(std::max(bsum2[j]/frames-m*m, 0.0)));
            }
            for(double b:boils) l.boil+=b/double(boils.size());
            std::sort(boils.begin(), boils.end()); l.boilP95=boils[size_t(0.95*double(boils.size()))];
            std::vector<float> mean(size_t(W)*H);
            const auto refShown=shown(ref.both);
            double flicker=0, error=0; size_t n=0;
            for(UINT y=8;y+8<H;++y) for(UINT x=8;x+8<W;++x) {
                const size_t j=size_t(y)*W+x; const double m=sum[j]/frames;
                flicker+=std::sqrt(std::max(sum2[j]/frames-m*m, 0.0)); error+=std::fabs(m-refShown[j]); ++n;
            }
            for(size_t j=0;j<mean.size();++j) mean[j]=float(sum[j]/frames);
            l.flicker=flicker/double(n); l.error=error/double(n);
            l.detail=kept(mean, ref.a, ref.b, 9, 9, W-9, H-9); l.detailCeiling=kept(mean, ref.a, ref.b, 300, 600, 980, 710);
            std::vector<float> img(rgb.size()); for(size_t j=0;j<img.size();++j) img[j]=float(rgb[j]);
            const double refLight=region(ref.both, 0, 0, int(W), int(H));
            l.light=refLight>0 ? region(img, 0, 0, int(W), int(H))/refLight : 0.0;
            if(image) save(img, image, true);
            return l;
        };
        // The lights: the harness's lamp in the sun; at night 24 torches along the walls and one high over the checkered leaf;
        // and the same night with a light far away, out of the room's reach (a bonfire across the valley, the sun's disc in an
        // interior), 40 and 40,000 times the brightest: it changes nothing in the room but the radiance reference the tracer
        // stores its light against, so what NRD makes of the room must not change with it.
        std::vector<TGPT_Light> night;
        for(uint32_t i=0;i<24;++i) {
            TGPT_Light t{}; const float s=float(i%8)/7.f;
            if(i<8) { t.position[0]=-5.6f; t.position[2]=-5+10*s; } else if(i<16) { t.position[0]=5.6f; t.position[2]=-5+10*s; } else { t.position[0]=-5+10*s; t.position[2]=5.6f; }
            t.position[1]=1.6f+0.4f*float(i%3); t.type=0; t.color[0]=12; t.color[1]=6.5f; t.color[2]=2.4f; t.range=7; t.radius=0.04f; t.id=100+i;
            night.push_back(t);
        }
        { TGPT_Light t{}; t.position[0]=-1; t.position[1]=7; t.position[2]=3.5f; t.type=0; t.color[0]=90; t.color[1]=60; t.color[2]=30; t.range=14; t.radius=0.03f; t.id=200; night.push_back(t); }
        // TGPT_TEST_FAR_LIGHTS=1: the torches fall off with distance alone (PT-50), each listed near within its 7 m and drawn
        // from the far lists past that: for the GPU time of the far candidate against a run without (the study's checks
        // are of the windowed lights' picture, and may fail then).
        if(GetEnvironmentVariableA("TGPT_TEST_FAR_LIGHTS", nullptr, 0)>0) for(TGPT_Light& t:night) t.flags|=TGPT_LIGHT_NO_RANGE;
        std::vector<TGPT_Light> far40=night, far40k=night;
        { TGPT_Light t{}; t.position[0]=220; t.position[1]=5; t.position[2]=0; t.type=0; t.color[0]=t.color[1]=t.color[2]=2600; t.range=10; t.radius=0.5f; t.id=201;
          far40.push_back(t); t.color[0]=t.color[1]=t.color[2]=2.6e6f; far40k.push_back(t); }
        // PT-50, the radiance clamps' own case: a night lit by eight candles standing on the floor, each flame 5 cm over it.
        // Half of a candle's light lands on the floor within a few centimetres of it and bounces from there: a path that ends
        // by a flame is rare and very bright (I / d^2 at 5 cm is 400 times the light at 1 m). Until 2026-10-07 the tracer
        // took that light as if 25 cm away and cut what was still brighter than 4 times the brightest light's at 25 cm, and
        // NRD's input at 500 times the median: the room came out darker than its candles light it. The numbers this prints
        // with a tracer from before (TGPT_TEST_BEFORE_PT50=1 and its DLLs) against today's are in docs/agents/caps.md.
        std::vector<TGPT_Light> candles;
        for(uint32_t i=0;i<8;++i) {
            static const float at[8][2]={{-3, -2}, {-1, 0}, {1, 1.5f}, {3, -1}, {-2.5f, 3}, {2, 3.5f}, {0, -4}, {4, 2}};
            TGPT_Light t{}; t.position[0]=at[i][0]; t.position[1]=0.05f; t.position[2]=at[i][1]; t.type=0;
            t.color[0]=12; t.color[1]=6.5f; t.color[2]=2.4f; t.range=7; t.radius=0.01f; t.id=300+i;
            candles.push_back(t);
        }
        const uint32_t keep[5]={history, giHistory, bounces, spatialSamples, rays};
        history=4; giHistory=8; bounces=1; spatialSamples=4; rays=1; bumpy=true;
        struct Scene { const char* name; bool sun; const std::vector<TGPT_Light>* lights; };
        const Scene scenes[]={{"sun", true, nullptr}, {"night", false, &night}, {"night, a light 40x far away", false, &far40},
                              {"night, a light 40000x far away", false, &far40k}, {"night, candles on the floor", false, &candles}};
        std::vector<std::pair<std::string, NrdLook>> looks;
        double candleLight[2]={0, 0};
        for(const Scene& sc:scenes) {
            sunOn=sc.sun;
            expect(sc.lights ? SetLights(sc.lights->data(), uint32_t(sc.lights->size()))==0 : SetLights(&light, 1)==0, "the study's lights set");
            const Reference ref=reference();
            const bool first=&sc==scenes, more=&sc-scenes<2;
            if(first) save(ref.both, "80-nrd-reference.png", true);
            if(sc.lights==&candles) {
                // The same picture with the old clamps' option: no clamp is left for it to turn off.
                const Reference open=reference(TGPT_OPTION_NO_CLAMPS);
                candleLight[0]=region(ref.both, 0, 0, int(W), int(H)); candleLight[1]=region(open.both, 0, 0, int(W), int(H));
                std::printf("      NRD study, %-32s the undenoised reference's light %.5f, with TGPT_OPTION_NO_CLAMPS %.5f (%.3f of it)\n", sc.name,
                    candleLight[0], candleLight[1], candleLight[1]>0 ? candleLight[0]/candleLight[1] : 0.0);
                save(ref.both, "83-candles-reference.png", true);
                // For the record, what ReSTIR's own filters still take here (RTXDI's boiling filter, and the reconnection
                // Jacobian's limits): the reference against the same paths with no reuse at all.
                const double plain=region(reference(TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_BOILING).both, 0, 0, int(W), int(H));
                const double unfiltered=region(reference(TGPT_OPTION_NO_BOILING).both, 0, 0, int(W), int(H));
                const double exact=region(reference(TGPT_OPTION_NO_BOILING|TGPT_OPTION_EXACT_JACOBIAN).both, 0, 0, int(W), int(H));
                std::printf("      NRD study, %-32s against no reuse at all (%.5f): the game's reuse %.3f, without the boiling filters %.3f, and with the exact Jacobian %.3f\n",
                    sc.name, plain, plain>0 ? candleLight[0]/plain : 0.0, plain>0 ? unfiltered/plain : 0.0, plain>0 ? exact/plain : 0.0);
            }
            struct Variant { const char* name; uint32_t options, rays; const char* image; };
            const bool candle=sc.lights==&candles;
            const Variant variants[]={{"built-in", 0, 1, first ? "81-builtin.png" : nullptr}, {"NRD", TGPT_OPTION_NRD, 1, first ? "82-nrd.png" : candle ? "84-candles-nrd.png" : nullptr},
                {"NRD, 4 rays", TGPT_OPTION_NRD, 4, nullptr}, {"undenoised", TGPT_OPTION_NO_DENOISER, 1, nullptr}};
            for(const Variant& v:variants) {
                if(v.rays>1 && !more) continue;
                rays=v.rays;
                const NrdLook l=look(v.options, ref, v.image);
                rays=1;
                std::printf("      NRD study, %-32s %-12s boil %.3f (95th %.3f) flicker %.3f | error %.3f | detail kept %.3f, under the ceiling %.3f | light %.3f of the reference's | GPU %.2f ms%s\n", sc.name, v.name,
                    l.boil, l.boilP95, l.flicker, l.error, l.detail, l.detailCeiling, l.light, l.gpuMs, (v.options&TGPT_OPTION_NRD) && !l.nrd ? " (NRD did not run)" : "");
                looks.emplace_back(std::string(sc.name)+"/"+v.name, l);
            }
        }
        history=keep[0]; giHistory=keep[1]; bounces=keep[2]; spatialSamples=keep[3]; rays=keep[4]; bumpy=false; sunOn=true;
        expect(SetLights(&light, 1)==0, "one light again");
        // PT-50: no clamp takes light away (the old option changes nothing: with the clamps it took the room's light down to
        // 0.85). One ray a pixel over 16 frames holds the reference's light but for the rare bright paths so few frames
        // miss (0.95; 0.995 with the clamps); the denoisers hold back more of it (the built-in 0.83, NRD 0.81; 0.87 and 0.85
        // with the clamps, and 0.94 and 0.93 in the sun's and the torches' rooms): the filters' own way with a pixel far
        // brighter than its neighbours, not a cap. Measured 2026-10-07 (docs/agents/caps.md "Clamps").
        {
            auto of=[&](const char* key) { for(auto& l:looks) if(l.first==key) return l.second.light; return 0.0; };
            const double own=of("night, candles on the floor/built-in"), nrd=of("night, candles on the floor/NRD"), raw=of("night, candles on the floor/undenoised");
            expect(candleLight[1]>0 && std::fabs(candleLight[0]/candleLight[1]-1.0)<0.05, "no radiance clamp is left: the candle-lit room holds the same light with and without the old clamps' option");
            expect(raw>0.9 && raw<1.1, "ReSTIR's reuse keeps the candle-lit room's light (one ray a pixel against the reference's eight)");
            expect(own>0.75 && nrd>0.9*own, "NRD's picture of the candle-lit room holds the light the built-in denoiser's does");
        }
        // Measured 2026-10-07 (docs/agents/nrd.md). Before: under the ceiling NRD kept 0.1 to 1 % of the normal map's detail
        // (0.37 and 0.35 since), and with the far light its boil went from 0.28 to 0.49 levels (0.30 since).
        auto find=[&](const char* key) -> const NrdLook* { for(auto& l:looks) if(l.first==key) return &l.second; return nullptr; };
        const NrdLook *sunNrd=find("sun/NRD"), *sunOwn=find("sun/built-in"), *nightNrd=find("night/NRD"), *nightOwn=find("night/built-in"),
            *farNrd=find("night, a light 40000x far away/NRD");
        if(sunNrd && sunOwn && nightNrd && nightOwn && farNrd && sunNrd->nrd && nightNrd->nrd && farNrd->nrd) {
            expect(sunNrd->detailCeiling>0.2 && nightNrd->detailCeiling>0.2, "NRD keeps the normal map's shading in the bounced light (under the ceiling)");
            expect(farNrd->boil<=nightNrd->boil*1.25 && farNrd->boilP95<=nightNrd->boilP95*1.5, "NRD denoises the room the same with a light 40,000 times brighter far away");
            expect(sunNrd->boil<=sunOwn->boil*1.25 && nightNrd->boil<=nightOwn->boil*1.25, "NRD boils no more than a quarter above the built-in denoiser");
            expect(sunNrd->error<=sunOwn->error*1.15 && nightNrd->error<=nightOwn->error*1.15, "NRD's picture is as close to the undenoised reference as the built-in denoiser's");
        } else expect(false, "NRD ran in the study's cases");
        return looks;
    };
    // PT-75: a denoiser may take noise away; it may not move what the picture converges to. The room closed (a front wall,
    // the ceiling's gap covered but for a slot 1.5 m wide: the sky through a small opening), its floor brown (0.45, 0.2,
    // 0.05), the box a tinted metal, normal-mapped as the game's surfaces are, the game's defaults (1 bounce, 1 ray a pixel,
    // 4 spatial samples, histories 4 and 8), the camera inside and still, the automatic gain 1 (the highlights' hold then
    // does nothing). No lamp and no sun: every surface is lit by bounced light alone, the sky's through the slot (rare,
    // bright, blue) and the glowing panel's (rare, bright, warm) against what the walls and the floor pass on (common, dim,
    // their colors). And the same room at night with eight candles on its floor, where the lamps' direct light, which the
    // tracer's own filter takes, is in the picture too.
    // Measured: the mean of 64 frames after 48 to settle, in linear light, a channel, over the brown floor (all of it, and
    // its dim end by the camera), the walls and the metal box: with NRD and with the built-in denoiser against two means of
    // undenoised frames (A and B, other random numbers: how far they are apart is the spread a mean of 64 frames has).
    // First without the reflections (TGPT_OPTION_NO_SPECULAR: the picture is the bounced light times the surface's color,
    // NRD's path alone), then the whole picture (the metal's light is all reflections: the tracer's own filter), then the
    // direct light's and the reflections' own filters on their debug views. Blocks: as tools/harness/mean_colour.py counts
    // them in game, 64-pixel blocks whose light differs by more than 10 % in a channel. Alone: TGPT_TEST_NRD_MEAN_ONLY=1.
    // In the whole test (brief): three of the four scenes (the slot, the grille, the candles), without the reflections for
    // each and the whole picture for the slot's and the candles' room, no debug views. Every part's regions are checked, for
    // NRD and for the built-in denoiser: within three times what the two undenoised means differ by, and 3 % at least.
    // The second undenoised mean follows the first without starting over (the same settled room, the next 64 frames).
    auto nrdMean=[&](bool brief) {
        struct Rgb { double c[3]{}; };
        const uint32_t keep[5]={history, giHistory, bounces, spatialSamples, rays};
        const bool keepBumpy=bumpy, keepSun=sunOn; const float keepSky=skyIntensity;
        const auto keepObjects=objects;
        history=4; giHistory=8; bounces=1; spatialSamples=4; rays=1; sunOn=false;
        bumpy=GetEnvironmentVariableA("TGPT_TEST_NRD_MEAN_FLAT", nullptr, 0)==0;
        char only[64]{}; GetEnvironmentVariableA("TGPT_TEST_NRD_MEAN_SCENE", only, sizeof only);
        char framesText[16]{}; const uint32_t frames=GetEnvironmentVariableA("TGPT_TEST_NRD_MEAN_FRAMES", framesText, sizeof framesText) ? uint32_t(std::strtoul(framesText, nullptr, 10)) : 64u;
        const bool quick=GetEnvironmentVariableA("TGPT_TEST_NRD_MEAN_QUICK", nullptr, 0)>0; // the first part alone, for the settings' study
        // A build without NRD (or TGPT_TEST_NRD_MEAN_OWN=1): the built-in denoiser in NRD's place too.
        ReadStatus(&status);
        const bool noNrd=GetEnvironmentVariableA("TGPT_TEST_NRD_MEAN_OWN", nullptr, 0)>0 || (status.features&TGPT_FEATURE_NRD)==0;
        char warmText[16]{}; const uint32_t warm=GetEnvironmentVariableA("TGPT_TEST_NRD_MEAN_WARM", warmText, sizeof warmText) ? uint32_t(std::strtoul(warmText, nullptr, 10)) : 200u;
        auto list=materials;
        { TGPT_Material m=materials[0]; m.baseColor[0]=0.45f; m.baseColor[1]=0.2f; m.baseColor[2]=0.05f; list.push_back(m); }                                    // 12: the brown floor
        { TGPT_Material m=materials[0]; m.baseColor[0]=0.9f; m.baseColor[1]=0.75f; m.baseColor[2]=0.55f; m.metallic=1.f; m.smoothness=0.6f; list.push_back(m); } // 13: the tinted metal
        expect(SetMaterials(0, list.data(), uint32_t(list.size()))==0, "the brown floor's and the tinted metal's materials set");
        objects.resize(8); // without the leaves that only rays see
        objects[0].materials={12}; objects[0].raster=9;
        objects[5].materials={13, 13}; objects[5].raster=8; objects[5].roughness=0.4f;
        objects[5].f0red=0.9f; objects[5].f0green=0.75f; objects[5].f0blue=0.55f;
        objects.push_back(Object{0, mul(translate_scale({0, 3, -6}, {12, 6, 1}), rot_x(pi/2)), {0}});            // a front wall, facing +z
        objects.push_back(Object{0, mul(translate_scale({-3.375f, 6, 4}, {5.25f, 1, 4}), rot_x(pi)), {0}});      // the ceiling's gap covered left
        objects.push_back(Object{0, mul(translate_scale({3.375f, 6, 4}, {5.25f, 1, 4}), rot_x(pi)), {0}});       // and right of a slot 1.5 m wide
        auto place=[&]() {
            std::vector<TGPT_Instance> in; std::vector<uint32_t> idx;
            for(const auto& o:objects) {
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=o.world.m[r][c];
                q.mesh=handles[o.mesh]; q.materialBase=uint32_t(idx.size()); q.mask=0xFF; q.flags=o.alphaTest ? TGPT_INSTANCE_ALPHA_TEST : 0u;
                for(int m:o.materials) idx.push_back(uint32_t(m));
                in.push_back(q);
            }
            return SetScene(in.data(), uint32_t(in.size()), idx.data(), uint32_t(idx.size()))==0;
        };
        expect(place(), "the closed room with the brown floor and the metal box set");
        const Camera cam=make_camera({0, 2.6f, -5.6f}, {0, 0.9f, 1.5f}, 70.f, float(W)/float(H));
        uint32_t index=40000;
        const size_t pixels=size_t(W)*H;
        // The regions, from what the tracer reads of each pixel: the brown floor by its color, the metal by a valid normal
        // and no diffuse color, the walls by their white.
        expect(SetLights(nullptr, 0)==0, "no lamp");
        auto shot=[&](uint32_t view) { for(uint32_t i=0;i<4;++i) run(view, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER); return readback(); };
        const auto albedo=shot(TGPT_VIEW_ALBEDO), normals=shot(TGPT_VIEW_NORMALS);
        std::vector<uint8_t> floorMask(pixels, 0), dimMask(pixels, 0), metalMask(pixels, 0), wallMask(pixels, 0);
        double rows=0; size_t floorCount=0, dimCount=0, metalCount=0, wallCount=0;
        for(size_t p=0;p<pixels;++p) {
            const float r=albedo[p*3], g=albedo[p*3+1], b=albedo[p*3+2];
            const bool valid=normals[p*3]+normals[p*3+1]+normals[p*3+2]>0.2f;
            if(std::fabs(r-0.45f)<0.03f && std::fabs(g-0.2f)<0.03f && std::fabs(b-0.05f)<0.03f) { floorMask[p]=1; ++floorCount; rows+=double(p/W); }
            else if(valid && r<0.01f && g<0.01f && b<0.01f) { metalMask[p]=1; ++metalCount; }
            else if(std::fabs(r-0.75f)<0.03f && std::fabs(g-0.75f)<0.03f && std::fabs(b-0.75f)<0.03f) { wallMask[p]=1; ++wallCount; }
        }
        // The floor's dim end: the quarter of the picture's rows by the camera, far from the slot and the panel (the floor is in
        // the picture's lower half, whichever way its rows run).
        const bool lowRowsNear=floorCount && rows/double(floorCount)<0.5*double(H);
        for(size_t p=0;p<pixels;++p) if(floorMask[p] && (lowRowsNear ? p/W<H/4 : p/W>=H-H/4)) { dimMask[p]=1; ++dimCount; }
        std::printf("      NRD mean: the regions' pixels: floor %zu (its dim end %zu), metal %zu, walls %zu of %zu\n", floorCount, dimCount, metalCount, wallCount, pixels);
        expect(floorCount>pixels/10 && dimCount>pixels/50 && metalCount>pixels/100 && wallCount>pixels/20, "the brown floor, the metal box and the walls are in the picture");
        struct Region { const char* name; const std::vector<uint8_t>* mask; };
        const Region floorR{"the brown floor", &floorMask}, dimR{"the floor's dim end", &dimMask}, metalR{"the metal box", &metalMask}, wallR{"the walls", &wallMask};
        bool ranNrd=true; uint32_t notFinite=0;
        // drift: the whole picture's light in the second half of the last mean's frames over that in the first half, a channel
        // (1 when the picture has settled; above 1 while the room still fills with light).
        double drift[3]={1, 1, 1};
        auto mean=[&](uint32_t view, uint32_t options, bool resume=false) {
            std::vector<double> sum(pixels*3, 0.0); double halves[2][3]={};
            for(uint32_t i=resume ? warm : 0u;i<warm+frames;++i) {
                run(view, cam, cam, i ? index++ : 0, options);
                if(i<warm) continue;
                const auto px=readback(); double* half=halves[(i-warm)*2/frames];
                for(size_t j=0;j<sum.size();++j) { if(std::isfinite(px[j])) { sum[j]+=px[j]; half[j%3]+=px[j]; } else ++notFinite; }
            }
            for(int k=0;k<3;++k) drift[k]=halves[1][k]/std::max(halves[0][k], 1e-30);
            if(options&TGPT_OPTION_NRD) { ReadStatus(&status); ranNrd=ranNrd && (status.features&TGPT_FEATURE_NRD_ACTIVE)!=0; }
            for(double& v:sum) v/=double(frames);
            return sum;
        };
        auto over=[&](const std::vector<double>& img, const std::vector<uint8_t>& mask) {
            Rgb m; size_t n=0;
            for(size_t p=0;p<pixels;++p) if(mask[p]) { for(int k=0;k<3;++k) m.c[k]+=img[p*3+k]; ++n; }
            for(int k=0;k<3;++k) m.c[k]/=double(std::max<size_t>(n, 1));
            return m;
        };
        // The share of 64-pixel blocks, among those lit at all, whose light differs by more than 10 % in a channel.
        auto blocks=[&](const std::vector<double>& ref, const std::vector<double>& img) {
            const UINT bs=64, bw=W/bs, bh=H/bs; std::vector<Rgb> a(size_t(bw)*bh), b(size_t(bw)*bh); double whole=0;
            for(UINT y=0;y<bh*bs;++y) for(UINT x=0;x<bw*bs;++x) {
                const size_t p=size_t(y)*W+x, q=size_t(y/bs)*bw+x/bs;
                for(int k=0;k<3;++k) { a[q].c[k]+=ref[p*3+k]; b[q].c[k]+=img[p*3+k]; whole+=ref[p*3+k]; }
            }
            whole/=3.0*double(a.size()); uint32_t lit=0, off=0;
            for(size_t q=0;q<a.size();++q) {
                if(std::max({a[q].c[0], a[q].c[1], a[q].c[2]})<0.05*whole) continue;
                ++lit; bool differs=false;
                for(int k=0;k<3;++k) differs=differs || std::fabs((b[q].c[k]+1e-6*whole)/(a[q].c[k]+1e-6*whole)-1.0)>0.10;
                off+=differs ? 1u : 0u;
            }
            return lit ? 100.0*double(off)/double(lit) : 0.0;
        };
        // One region's line: the undenoised mean (B), how far the other undenoised mean (A) is from it, and each denoiser's
        // light as a share of B's, a channel. Returns the largest distance from 1 of the named picture's shares, and the spread's.
        struct Shift { double worst=0, worstOwn=0, spread=0; };
        auto report=[&](const char* scene, const char* part, const Region& r, const std::vector<double>& a, const std::vector<double>& b,
                        const std::vector<double>* nrd, const std::vector<double>* own) {
            const Rgb mb=over(b, *r.mask), ma=over(a, *r.mask);
            Shift s; char text[2][96]={"", ""};
            for(int k=0;k<3;++k) s.spread=std::max(s.spread, std::fabs(ma.c[k]/std::max(mb.c[k], 1e-12)-1.0));
            const std::vector<double>* imgs[2]={nrd, own};
            for(int v=0;v<2;++v) if(imgs[v]) {
                const Rgb m=over(*imgs[v], *r.mask); double q[3];
                for(int k=0;k<3;++k) { q[k]=m.c[k]/std::max(mb.c[k], 1e-12); double& w=v==0 ? s.worst : s.worstOwn; w=std::max(w, std::fabs(q[k]-1.0)); }
                std::snprintf(text[v], sizeof text[v], " | %s R %.3f G %.3f B %.3f", v==0 ? "NRD" : "the tracer's own filters", q[0], q[1], q[2]);
            }
            std::printf("      NRD mean, %s, %s, %s: undenoised R %.5f G %.5f B %.5f (the other mean R %.3f G %.3f B %.3f of it)%s%s\n", scene, part, r.name,
                mb.c[0], mb.c[1], mb.c[2], ma.c[0]/std::max(mb.c[0], 1e-12), ma.c[1]/std::max(mb.c[1], 1e-12), ma.c[2]/std::max(mb.c[2], 1e-12), text[0], text[1]);
            return s;
        };
        std::vector<TGPT_Light> candles;
        for(uint32_t i=0;i<8;++i) {
            static const float at[8][2]={{-3, -2}, {-1, 0}, {1, 1.5f}, {3, -1}, {-2.5f, 3}, {2, 3.5f}, {0, -4}, {4, 2}};
            TGPT_Light t{}; t.position[0]=at[i][0]; t.position[1]=0.05f; t.position[2]=at[i][1]; t.type=0;
            t.color[0]=12; t.color[1]=6.5f; t.color[2]=2.4f; t.range=7; t.radius=0.01f; t.id=300+i;
            candles.push_back(t);
        }
        // panel: the glowing panel on the left wall is there (without it the sky's blue is the only rare, bright light: the
        // other way round from the warm panel's, as a place in the shade outdoors against a candle-lit room).
        // grille: nine slats across the slot, a quarter of it left open in slivers 10 cm wide: the sky seen through small
        // holes, which few paths find (a place in the shade of a wheel's spokes or under leaves).
        struct Scene { const char* name; float sky; const std::vector<TGPT_Light>* lights; bool panel, grille; };
        const Scene scenes[]={{"the sky through a slot", 1.f, nullptr, true, false}, {"the sky alone", 1.f, nullptr, false, false},
                              {"the sky through a grille", 1.f, nullptr, false, true}, {"candles at night", 1e-6f, &candles, true, false}};
        const auto withPanel=objects;
        // One part's regions against the tolerance, for NRD and for the built-in denoiser.
        auto judge=[&](const Scene& sc, const char* part, const Region& r, const Shift& s, bool nrdRan, bool ownRan) {
            const double tolerance=std::max(0.03, 3.0*s.spread);
            std::printf("      NRD mean, %s, %s, %s: the largest shift of a channel: NRD %.3f, the built-in denoiser %.3f, against a tolerance of %.3f\n", sc.name, part, r.name, s.worst, s.worstOwn, tolerance);
            const std::string where=std::string(sc.name)+", "+part+", "+r.name;
            if(nrdRan) expect(s.worst<=tolerance, ("NRD's mean is the undenoised mean's, a channel: "+where).c_str());
            if(ownRan) expect(s.worstOwn<=tolerance, ("the built-in denoiser's mean is the undenoised mean's, a channel: "+where).c_str());
        };
        const uint32_t nrdOption=noNrd ? 0u : TGPT_OPTION_NRD;
        for(const Scene& sc:scenes) {
            if(only[0] && !std::strstr(sc.name, only)) continue;
            if(brief && !sc.panel && !sc.grille) continue; // the whole test leaves "the sky alone" out
            skyIntensity=sc.sky;
            objects=withPanel; if(!sc.panel) objects.erase(objects.begin()+6);
            if(sc.grille) for(int i=0;i<9;++i) objects.push_back(Object{0, mul(translate_scale({0, 5.99f, 2.17f+0.44f*float(i)}, {1.5f, 1, 0.34f}), rot_x(pi)), {0}});
            expect(place(), "the scene's room set");
            expect(sc.lights ? SetLights(sc.lights->data(), uint32_t(sc.lights->size()))==0 : SetLights(nullptr, 0)==0, "the scene's lights set");
            const bool first=&sc==scenes;
            // 1. Without the reflections: the bounced light (and the lamps' direct light) times the surface's color.
            {
                const uint32_t base=TGPT_OPTION_NO_SPECULAR;
                const auto a=mean(TGPT_VIEW_NONE, base|TGPT_OPTION_NO_DENOISER);
                std::printf("      NRD mean, %s, settled? the undenoised picture's light in the second half of its %u frames over the first half (after %u frames): R %.3f G %.3f B %.3f\n", sc.name, frames, warm, drift[0], drift[1], drift[2]);
                const auto b=mean(TGPT_VIEW_NONE, base|TGPT_OPTION_NO_DENOISER, true);
                const auto nrd=mean(TGPT_VIEW_NONE, base|nrdOption);
                std::printf("      NRD mean, %s, settled? NRD's picture, the same: R %.3f G %.3f B %.3f\n", sc.name, drift[0], drift[1], drift[2]);
                const auto own=quick ? std::vector<double>() : mean(TGPT_VIEW_NONE, base);
                std::printf("      NRD mean, %s, without reflections: 64-pixel blocks differing by more than 10 %% in a channel: the two undenoised means %.1f %%, NRD %.1f %%, the built-in denoiser %.1f %%\n",
                    sc.name, blocks(b, a), blocks(b, nrd), quick ? 0.0 : blocks(b, own));
                for(const Region* r:{&floorR, &dimR, &wallR}) judge(sc, "without reflections", *r, report(sc.name, "without reflections", *r, a, b, &nrd, quick ? nullptr : &own), true, !quick);
                if(first) {
                    auto asFloat=[](const std::vector<double>& d) { std::vector<float> f(d.size()); for(size_t i=0;i<d.size();++i) f[i]=float(d[i]); return f; };
                    save(asFloat(b), "85-mean-undenoised.png", true); save(asFloat(nrd), "86-mean-nrd.png", true);
                }
            }
            if(quick) continue;
            // 2. The whole picture: the metal's light is all reflections, which the tracer's own filter takes with NRD too.
            if(!brief || !sc.grille) {
                const auto a=mean(TGPT_VIEW_NONE, TGPT_OPTION_NO_DENOISER), b=mean(TGPT_VIEW_NONE, TGPT_OPTION_NO_DENOISER, true);
                const auto nrd=mean(TGPT_VIEW_NONE, nrdOption), own=mean(TGPT_VIEW_NONE, 0u);
                std::printf("      NRD mean, %s, the whole picture: 64-pixel blocks differing by more than 10 %% in a channel: the two undenoised means %.1f %%, NRD %.1f %%, the built-in denoiser %.1f %%\n",
                    sc.name, blocks(b, a), blocks(b, nrd), blocks(b, own));
                for(const Region* r:{&metalR, &floorR, &dimR, &wallR}) judge(sc, "the whole picture", *r, report(sc.name, "the whole picture", *r, a, b, &nrd, &own), true, true);
                // As Ray Reconstruction is handed it: the tracer's samples with no filter of its own (it must be the undenoised picture).
                rayReconstruction=true;
                const auto handed=mean(TGPT_VIEW_NONE, 0u);
                rayReconstruction=false;
                for(const Region* r:{&metalR, &floorR, &wallR}) {
                    const Shift s=report(sc.name, "as Ray Reconstruction is handed it (in NRD's column)", *r, a, b, &handed, nullptr);
                    expect(s.worst<=std::max(0.03, 3.0*s.spread), (std::string("Ray Reconstruction is handed the undenoised mean, a channel: ")+sc.name+", "+r->name).c_str());
                }
            }
            if(brief) continue;
            // 3. The tracer's own filters, each on its debug view (they serve with NRD as with the built-in denoiser).
            struct View { const char* name; uint32_t view; };
            for(const View& v:{View{"the bounced light's view (built-in denoiser)", TGPT_VIEW_INDIRECT}, View{"the direct light's view", TGPT_VIEW_DIRECT}, View{"the reflections' view", TGPT_VIEW_SPECULAR}}) {
                if(v.view==TGPT_VIEW_DIRECT && !sc.lights) continue; // no lamp, no sun: no direct light
                const auto a=mean(v.view, TGPT_OPTION_NO_DENOISER), b=mean(v.view, TGPT_OPTION_NO_DENOISER, true), own=mean(v.view, 0u);
                for(const Region* r:{&floorR, &dimR, &metalR, &wallR}) report(sc.name, v.name, *r, a, b, nullptr, &own);
            }
        }
        if(!noNrd) expect(ranNrd, "NRD denoised the frames of the mean's check");
        expect(notFinite==0, "every value of the mean's frames is finite");
        objects=keepObjects; bumpy=keepBumpy; sunOn=keepSun; skyIntensity=keepSky;
        history=keep[0]; giHistory=keep[1]; bounces=keep[2]; spatialSamples=keep[3]; rays=keep[4];
        expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0 && SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0 &&
               SetLights(&light, 1)==0, "the harness's room again");
    };
    if(GetEnvironmentVariableA("TGPT_TEST_NRD_MEAN_ONLY", nullptr, 0)>0) {
        nrdMean(GetEnvironmentVariableA("TGPT_TEST_NRD_MEAN_BRIEF", nullptr, 0)>0);
        std::printf(failures ? "FAILED %d checks\n" : "NRD mean checks passed\n", failures); return failures ? 1 : 0;
    }
    // PT-80: what stands between the tracer's samples and the picture leaves a mirror's picture as it is traced (the owner:
    // "we need our tracer to be able to perfectly render the world in a mirror"; in game, 2026-10-08, a tavern's mirror kept
    // 1.24 of the reference's detail with no denoiser and 0.09 with NRD). The floor a perfect mirror (roughness 0, a metal of
    // reflectance 1, no diffuse color), the camera low over it, and a panel standing on it with a fine texture (a checker of
    // 8-texel squares, then one of coarse cells), lit by six small lamps of two colors: what the mirror shows is lit by a
    // lamp drawn by chance each frame, so its picture is noisy, as a room's is in a mirror in game. The game's defaults (1
    // bounce, 1 ray a pixel, 4 spatial samples, histories 4 and 8), the camera still.
    // Measured over the panel's middle as the mirror shows it: the picture with no denoiser twice (two means, other random
    // numbers), with NRD, with the built-in denoiser, and as Ray Reconstruction is handed it: each the mean of 32 frames
    // after 64, all alike (the room's light is still settling then, and pictures warmed differently differ by that, not by
    // their filters). Detail kept: the picture's fine detail (each pixel less the mean of the 5x5 around it) times the two
    // undenoised means' mean detail, over the product of the two means' (their noise cancels): 1 all of it, 0 a smear.
    // Light: the region's mean against the undenoised means'. Flicker: a pixel's spread over the frames, over its mean.
    // Then the same floor rougher (0.25, 0.5): a rough surface's reflection is a blur of what it reflects, which the filter
    // may take the noise out of (printed: its flicker against the undenoised picture's). Alone: TGPT_TEST_MIRROR_FILTERS_ONLY=1.
    auto mirrorFilters=[&]() {
        const auto keepFloor=objects[0]; const bool keepSun=sunOn, keepBumpy=bumpy;
        const uint32_t keep[5]={history, giHistory, bounces, spatialSamples, rays};
        history=4; giHistory=8; bounces=1; spatialSamples=4; rays=1; sunOn=false; bumpy=false;
        objects[0].roughness=0.f; objects[0].f0=1.f; objects[0].raster=8;
        const UINT size=1024, mips=11;
        ComPtr<ID3D11Texture2D> checkerTexture, cellsTexture;
        {
            std::vector<std::vector<uint32_t>> fine(mips), coarse(mips); std::vector<D3D11_SUBRESOURCE_DATA> fi(mips), ci(mips);
            auto grey=[](float l) { const uint32_t b=uint32_t(std::lround(std::clamp(l, 0.f, 1.f)*255.f)); return b|(b<<8)|(b<<16)|0xFF000000u; };
            for(UINT m=0;m<mips;++m) {
                const UINT w=std::max(1u, size>>m), span=1u<<m; // a texel's side in mip 0's texels
                fine[m].resize(size_t(w)*w); coarse[m].resize(size_t(w)*w);
                for(UINT y=0;y<w;++y) for(UINT x=0;x<w;++x) {
                    uint32_t lit=0, cell=0;
                    for(UINT j=0;j<span;++j) for(UINT i=0;i<span;++i) { lit+=(((x*span+i)/8u+(y*span+j)/8u)&1u); cell+=(((x*span+i)/48u+(y*span+j)/32u)&1u); }
                    fine[m][size_t(y)*w+x]=grey(0.05f+0.75f*float(lit)/float(span*span));
                    // The coarse cells in two colors (a brick red and a pale stone), so a color is in the check too.
                    const float t=float(cell)/float(span*span);
                    const uint32_t r=uint32_t(std::lround((0.55f+0.25f*t)*255.f)), g=uint32_t(std::lround((0.12f+0.62f*t)*255.f)), b=uint32_t(std::lround((0.08f+0.55f*t)*255.f));
                    coarse[m][size_t(y)*w+x]=r|(g<<8)|(b<<16)|0xFF000000u;
                }
                fi[m]={fine[m].data(), w*4, 0}; ci[m]={coarse[m].data(), w*4, 0};
            }
            D3D11_TEXTURE2D_DESC d{size, size, mips, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            device->CreateTexture2D(&d, fi.data(), &checkerTexture); device->CreateTexture2D(&d, ci.data(), &cellsTexture);
        }
        auto sharedHandle=[&](const ComPtr<ID3D11Texture2D>& t) { TGPT_SharedTexture i{}; i.size=sizeof(i); i.abi=TGPT_ABI; const int32_t n=t ? RegisterSharedTexture(uint64_t(uintptr_t(t.Get())), &i) : -100; return n>=0 ? i.handle : -1; };
        const int32_t checkerHandle=sharedHandle(checkerTexture), cellsHandle=sharedHandle(cellsTexture);
        expect(checkerHandle>=0 && cellsHandle>=0, "the mirror check's two textures shared");
        auto list=materials;
        TGPT_Material white=materials[0]; white.baseColor[0]=white.baseColor[1]=white.baseColor[2]=1.f; white.smoothness=0.f;
        white.colorTexture=white.normalTexture=white.maskTexture=-1;
        const uint32_t firstMaterial=uint32_t(list.size());
        { TGPT_Material t=white; t.colorTexture=checkerHandle; list.push_back(t); t.colorTexture=cellsHandle; list.push_back(t); }
        expect(SetMaterials(0, list.data(), uint32_t(list.size()))==0, "materials for the mirror's filters check");
        const Camera mcam=make_camera({0, 0.8f, 0.2f}, {0, -1.f, 2.f}, 60.f, float(W)/float(H));
        // The panel: x -1 to 1, y 0.02 to 2.02 at z 2, facing -z: u along +x, v along +y.
        const M4 panelWorld=mul(translate_scale({0.f, 1.02f, 2.f}, {2.f, 2.f, 1.f}), rot_x(-pi/2));
        auto withPanel=[&](uint32_t material) {
            auto l=instances; auto li=indices;
            TGPT_Instance p{}; for(int row=0;row<3;++row) for(int col=0;col<4;++col) p.transform[row*4+col]=panelWorld.m[row][col];
            p.mesh=cardHandle; p.materialBase=uint32_t(li.size()); p.mask=0xFF; li.push_back(material); l.push_back(p);
            expect(SetScene(l.data(), uint32_t(l.size()), li.data(), uint32_t(li.size()))==0, "the scene with the textured panel over the mirror");
        };
        std::vector<TGPT_Light> lamps;
        for(uint32_t i=0;i<6;++i) {
            static const float at[6][3]={{0, 1.6f, 0.2f}, {-1.5f, 0.6f, 0.9f}, {1.5f, 0.6f, 0.9f}, {-0.8f, 2.4f, 1.2f}, {0.8f, 2.4f, 1.2f}, {0, 0.3f, 1.1f}};
            TGPT_Light t{}; t.position[0]=at[i][0]; t.position[1]=at[i][1]; t.position[2]=at[i][2]; t.type=0;
            t.color[0]=i%2 ? 16.f : 8.f; t.color[1]=9.f; t.color[2]=i%2 ? 4.f : 12.f; t.range=12; t.radius=0.05f; t.id=720+i;
            lamps.push_back(t);
        }
        expect(SetLights(lamps.data(), uint32_t(lamps.size()))==0, "six lamps for the panel");
        // The panel's middle as the mirror shows it: each pixel's reflected ray to the panel, its texture coordinates within
        // 0.2 to 0.8 both ways (as PT-46's check finds them).
        std::vector<uint8_t> middle(size_t(W)*H, 0); size_t middleCount=0;
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const float nx=(x+0.5f)/W*2-1, ny=1-(y+0.5f)/H*2, in[4]={nx, ny, 0.5f, 1.f};
            float o[4]={0, 0, 0, 0}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) o[r]+=mcam.invViewProj.m[r][k]*in[k];
            const V3 d=normalize({o[0]/o[3], o[1]/o[3], o[2]/o[3]});
            if(d.y>=-1e-4f) continue;
            V3 f=mcam.position+d*(-mcam.position.y/d.y); // on the floor
            const V3 r{d.x, -d.y, d.z};
            if(r.z<=1e-4f || std::fabs(f.x)>6 || f.z>6) continue;
            f.y+=0.02f+0.002f*std::sqrt(dot(f-mcam.position, f-mcam.position));
            const V3 q=f+r*((2.f-f.z)/r.z);
            const float u=(q.x+1.f)/2.f, v=(q.y-0.02f)/2.f;
            if(u>0.2f && u<0.8f && v>0.2f && v<0.8f && x>=2 && x+2<W && y>=2 && y+2<H) { middle[size_t(y)*W+x]=1; ++middleCount; }
        }
        expect(middleCount>20000, "the panel's middle fills a good part of the mirror");
        struct Seen { std::vector<double> mean; double flicker=0, light=0; bool nrd=false; };
        uint32_t index=50000;
        auto seen=[&](uint32_t options, uint32_t warm, uint32_t frames, bool reconstruction=false) {
            Seen s; s.mean.assign(size_t(W)*H*3, 0.0); std::vector<double> l1(size_t(W)*H, 0.0), l2(size_t(W)*H, 0.0);
            rayReconstruction=reconstruction;
            for(uint32_t i=0;i<warm+frames;++i) {
                run(TGPT_VIEW_NONE, mcam, mcam, i ? index++ : 0, options);
                if(i<warm) continue;
                const auto px=readback();
                for(size_t j=0;j<s.mean.size();++j) s.mean[j]+=double(px[j])/double(frames);
                for(size_t j=0;j<l1.size();++j) if(middle[j]) { const double l=0.2126*px[j*3]+0.7152*px[j*3+1]+0.0722*px[j*3+2]; l1[j]+=l; l2[j]+=l*l; }
            }
            rayReconstruction=false;
            ReadStatus(&status); s.nrd=(status.features&TGPT_FEATURE_NRD_ACTIVE)!=0;
            double spread=0, light=0;
            for(size_t j=0;j<l1.size();++j) if(middle[j]) { const double m=l1[j]/frames; spread+=std::sqrt(std::max(l2[j]/frames-m*m, 0.0)); light+=m; }
            s.flicker=light>0 ? spread/light : 0.0; s.light=light/double(std::max<size_t>(middleCount, 1));
            return s;
        };
        // A picture's fine detail over the panel's middle: each pixel's light less the mean of the 5x5 pixels around it.
        auto fineDetail=[&](const std::vector<double>& img) {
            std::vector<double> l(size_t(W)*H), h(size_t(W)*H, 0.0);
            for(size_t j=0;j<l.size();++j) l[j]=0.2126*img[j*3]+0.7152*img[j*3+1]+0.0722*img[j*3+2];
            for(UINT y=2;y+2<H;++y) for(UINT x=2;x+2<W;++x) {
                const size_t i=size_t(y)*W+x; if(!middle[i]) continue;
                double box=0; for(int j=-2;j<=2;++j) for(int q=-2;q<=2;++q) box+=l[size_t(int(y)+j)*W+size_t(int(x)+q)];
                h[i]=l[i]-box/25.0;
            }
            return h;
        };
        auto toFloat=[](const std::vector<double>& v) { return std::vector<float>(v.begin(), v.end()); };
        struct Shown { const char* name; uint32_t material; const char* images[3]; };
        const Shown textures[]={{"a checker of 8-texel squares", firstMaterial, {"87-mirror-undenoised.png", "88-mirror-nrd.png", "89-mirror-builtin.png"}},
                                  {"coarse cells of two colors", firstMaterial+1, {nullptr, nullptr, nullptr}}};
        bool ranNrd=true;
        for(const Shown& t:textures) {
            withPanel(t.material);
            const Seen a=seen(TGPT_OPTION_NO_DENOISER, 64, 32), b=seen(TGPT_OPTION_NO_DENOISER, 64, 32);
            const Seen nrd=seen(TGPT_OPTION_NRD, 64, 32), own=seen(0u, 64, 32), handed=seen(0u, 64, 32, true);
            ranNrd=ranNrd && nrd.nrd;
            const auto ha=fineDetail(a.mean), hb=fineDetail(b.mean);
            double den=0; for(size_t j=0;j<ha.size();++j) den+=ha[j]*hb[j];
            auto kept=[&](const Seen& s) { const auto h=fineDetail(s.mean); double num=0; for(size_t j=0;j<h.size();++j) num+=h[j]*0.5*(ha[j]+hb[j]); return den>0 ? num/den : 0.0; };
            const double reference=0.5*(a.light+b.light), spread=std::fabs(a.light/std::max(b.light, 1e-12)-1.0), tolerance=std::max(0.03, 3.0*spread);
            const double keptNrd=kept(nrd), keptOwn=kept(own), keptHanded=kept(handed);
            std::printf("      mirror filters, %s (%zu pixels): detail kept with NRD %.3f, the built-in denoiser %.3f, as Ray Reconstruction takes it %.3f | light %.3f, %.3f, %.3f of the "
                        "undenoised picture's (its two means %.3f apart) | flicker %.3f, %.3f, %.3f against %.3f undenoised%s\n", t.name, middleCount, keptNrd, keptOwn, keptHanded,
                        nrd.light/reference, own.light/reference, handed.light/reference, spread, nrd.flicker, own.flicker, handed.flicker, a.flicker, nrd.nrd ? "" : " (NRD did not run)");
            if(t.images[0]) { save(toFloat(a.mean), t.images[0], true); save(toFloat(nrd.mean), t.images[1], true); save(toFloat(own.mean), t.images[2], true); }
            const bool first=&t==textures;
            expect(std::fabs(keptNrd-1.0)<=0.10, first ? "with NRD a perfect mirror keeps the fine detail of the checker it shows, within 10 percent" : "with NRD a perfect mirror keeps the fine detail of the colored cells it shows, within 10 percent");
            expect(std::fabs(keptOwn-1.0)<=0.10, first ? "with the built-in denoiser a perfect mirror keeps the fine detail of the checker it shows, within 10 percent" : "with the built-in denoiser a perfect mirror keeps the fine detail of the colored cells it shows, within 10 percent");
            expect(std::fabs(keptHanded-1.0)<=0.10, first ? "Ray Reconstruction is handed a perfect mirror's picture of the checker with its fine detail" : "Ray Reconstruction is handed a perfect mirror's picture of the colored cells with its fine detail");
            expect(std::fabs(nrd.light/reference-1.0)<=tolerance && std::fabs(own.light/reference-1.0)<=tolerance && std::fabs(handed.light/reference-1.0)<=tolerance,
                   first ? "a perfect mirror's picture of the checker holds the undenoised picture's light with every denoiser" : "a perfect mirror's picture of the colored cells holds the undenoised picture's light with every denoiser");
        }
        // A moving camera: it slides 40 cm sideways over 40 frames and stops where the still camera stood. The mirror's picture
        // moves across the floor as the panel's mirror image does, not with the floor: a history fetched by the floor's own
        // motion trails the picture over its 16 frames. The last frame's detail against the still camera's undenoised means.
        {
            withPanel(firstMaterial);
            const Seen a=seen(TGPT_OPTION_NO_DENOISER, 64, 32), b=seen(TGPT_OPTION_NO_DENOISER, 64, 32);
            const auto ha=fineDetail(a.mean), hb=fineDetail(b.mean);
            double den=0; for(size_t j=0;j<ha.size();++j) den+=ha[j]*hb[j];
            auto moved=[&](uint32_t options) {
                const uint32_t steps=40;
                Camera before=mcam;
                for(uint32_t i=0;i<=steps;++i) {
                    const float x=-0.4f+0.4f*float(i)/float(steps);
                    const Camera c=i==steps ? mcam : make_camera({x, 0.8f, 0.2f}, {x, -1.f, 2.f}, 60.f, float(W)/float(H));
                    run(TGPT_VIEW_NONE, c, i ? before : c, i ? index++ : 0, options);
                    before=c;
                }
                const auto px=readback(); const std::vector<double> img(px.begin(), px.end());
                const auto h=fineDetail(img);
                double num=0; for(size_t j=0;j<h.size();++j) num+=h[j]*0.5*(ha[j]+hb[j]);
                return den>0 ? num/den : 0.0;
            };
            const double movedRaw=moved(TGPT_OPTION_NO_DENOISER), movedNrd=moved(TGPT_OPTION_NRD), movedOwn=moved(0u);
            std::printf("      mirror filters, the camera moving 1 cm a frame: detail kept in its last frame with no denoiser %.3f, with NRD %.3f, with the built-in denoiser %.3f\n",
                        movedRaw, movedNrd, movedOwn);
            expect(movedRaw>0.8 && movedRaw<1.2, "a moving camera's undenoised frame shows the still camera's picture of the mirror (the check's own measure)");
            expect(movedNrd>0.7*movedRaw && movedOwn>0.7*movedRaw, "a mirror's picture keeps its fine detail while the camera moves, with NRD and the built-in denoiser");
        }
        // Rougher floors: what the filters take out of a glossy reflection's noise, and that its light stays.
        withPanel(firstMaterial);
        for(const float roughness:{0.25f, 0.5f}) {
            objects[0].roughness=roughness;
            const Seen a=seen(TGPT_OPTION_NO_DENOISER, 64, 32), b=seen(TGPT_OPTION_NO_DENOISER, 64, 32), nrd=seen(TGPT_OPTION_NRD, 64, 32), own=seen(0u, 64, 32);
            const double reference=0.5*(a.light+b.light), spread=std::fabs(a.light/std::max(b.light, 1e-12)-1.0), tolerance=std::max(0.03, 3.0*spread);
            std::printf("      mirror filters, the floor at roughness %.2f: light with NRD %.3f, the built-in denoiser %.3f of the undenoised picture's (its two means %.3f apart) | "
                        "flicker %.3f, %.3f against %.3f undenoised\n", roughness, nrd.light/reference, own.light/reference, spread, nrd.flicker, own.flicker, a.flicker);
            expect(std::fabs(nrd.light/reference-1.0)<=tolerance && std::fabs(own.light/reference-1.0)<=tolerance,
                   roughness<0.4f ? "a glossy floor's reflection holds the undenoised picture's light with NRD and the built-in denoiser" : "a rough floor's reflection holds the undenoised picture's light with NRD and the built-in denoiser");
            expect(nrd.flicker<0.5*a.flicker && own.flicker<0.5*a.flicker,
                   roughness<0.4f ? "a glossy floor's reflection is filtered: less than half the undenoised picture's flicker" : "a rough floor's reflection is filtered: less than half the undenoised picture's flicker");
        }
        ReadStatus(&status);
        if(status.features&TGPT_FEATURE_NRD) expect(ranNrd, "NRD denoised the frames of the mirror's check");
        objects[0]=keepFloor; sunOn=keepSun; bumpy=keepBumpy;
        history=keep[0]; giHistory=keep[1]; bounces=keep[2]; spatialSamples=keep[3]; rays=keep[4];
        expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0 && SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0 &&
               SetLights(&light, 1)==0, "the harness's room again after the mirror's check");
    };
    if(GetEnvironmentVariableA("TGPT_TEST_MIRROR_FILTERS_ONLY", nullptr, 0)>0) {
        mirrorFilters();
        std::printf(failures ? "FAILED %d checks\n" : "mirror filter checks passed\n", failures); return failures ? 1 : 0;
    }
    // PT-95, PT-19 ON TRIAL (docs/agents/nrd-relief.md; TGPT_Frame.prevCameraPosition's w): the reflections' own filter on a
    // bumpy metal. In the game a guard's hammered iron helmet came out smooth with NRD (NRD takes the bounced diffuse light
    // alone; a metal's light is all in the reflections' signal, which the tracer's own filter takes with NRD as with the
    // built-in denoiser). Here: the room's floor as a metal (no diffuse colour, reflectance 0.6, roughness 0.4) with the
    // harness's bumps (3 cm and 15 cm) at a tenth of their tilt (up to about 3 degrees, as the helmet's by the run's
    // Normals view), six small lamps of two colours over it, the camera low and still, the game's defaults. Over the
    // floor in front of the camera: the relief kept (the picture's detail, each pixel less the mean of the 9x9 around it,
    // against two undenoised means', as the mirror check measures it), the light against the undenoised picture's and
    // the flicker, with the trial off, at 4 (no feedback), 1 (the lobe), 5 (both) and 7 (and the reflectance); then the
    // bumps at their full tilt (up to about 30 degrees), and none: what the trial costs where there is nothing to keep.
    // Alone, and only so (on trial: not part of the whole run): TGPT_TEST_RELIEF_ONLY=1. Its pictures: 9x-relief-*.png.
    if(GetEnvironmentVariableA("TGPT_TEST_RELIEF_ONLY", nullptr, 0)>0) {
        history=4; giHistory=8; bounces=1; spatialSamples=4; rays=1; sunOn=false;
        objects[0].roughness=0.4f; objects[0].f0=0.6f; objects[0].raster=8;
        const Camera mcam=make_camera({0, 0.8f, 0.2f}, {0, -1.f, 2.f}, 60.f, float(W)/float(H));
        std::vector<TGPT_Light> lamps;
        for(uint32_t i=0;i<6;++i) {
            static const float at[6][3]={{0, 1.6f, 0.2f}, {-1.5f, 0.6f, 0.9f}, {1.5f, 0.6f, 0.9f}, {-0.8f, 2.4f, 1.2f}, {0.8f, 2.4f, 1.2f}, {0, 0.3f, 1.1f}};
            TGPT_Light t{}; t.position[0]=at[i][0]; t.position[1]=at[i][1]; t.position[2]=at[i][2]; t.type=0;
            t.color[0]=i%2 ? 16.f : 8.f; t.color[1]=9.f; t.color[2]=i%2 ? 4.f : 12.f; t.range=12; t.radius=0.05f; t.id=760+i;
            lamps.push_back(t);
        }
        expect(SetLights(lamps.data(), uint32_t(lamps.size()))==0, "six lamps over the metal floor");
        const int reach=4;
        std::vector<uint8_t> onFloor(size_t(W)*H, 0); size_t floorCount=0;
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const float nx=(x+0.5f)/W*2-1, ny=1-(y+0.5f)/H*2, in[4]={nx, ny, 0.5f, 1.f};
            float o[4]={0, 0, 0, 0}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) o[r]+=mcam.invViewProj.m[r][k]*in[k];
            const V3 d=normalize({o[0]/o[3], o[1]/o[3], o[2]/o[3]});
            if(d.y>=-1e-4f) continue;
            const V3 f=mcam.position+d*(-mcam.position.y/d.y); // on the floor
            if(std::fabs(f.x)<1.2f && f.z>0.6f && f.z<1.7f && int(x)>=reach && x+reach<W && int(y)>=reach && y+reach<H) { onFloor[size_t(y)*W+x]=1; ++floorCount; }
        }
        expect(floorCount>20000, "the metal floor fills a good part of the view");
        struct Seen { std::vector<double> mean; double flicker=0, light=0; };
        uint32_t index=60000;
        auto seen=[&](uint32_t options, float trial) {
            const uint32_t warm=64, frames=32;
            Seen s; s.mean.assign(size_t(W)*H*3, 0.0); std::vector<double> l1(size_t(W)*H, 0.0), l2(size_t(W)*H, 0.0);
            reliefTrial=trial;
            for(uint32_t i=0;i<warm+frames;++i) {
                run(TGPT_VIEW_NONE, mcam, mcam, i ? index++ : 0, options);
                if(i<warm) continue;
                const auto px=readback();
                for(size_t j=0;j<s.mean.size();++j) s.mean[j]+=double(px[j])/double(frames);
                for(size_t j=0;j<l1.size();++j) if(onFloor[j]) { const double l=0.2126*px[j*3]+0.7152*px[j*3+1]+0.0722*px[j*3+2]; l1[j]+=l; l2[j]+=l*l; }
            }
            reliefTrial=0.f;
            double spread=0, light=0;
            for(size_t j=0;j<l1.size();++j) if(onFloor[j]) { const double m=l1[j]/frames; spread+=std::sqrt(std::max(l2[j]/frames-m*m, 0.0)); light+=m; }
            s.flicker=light>0 ? spread/light : 0.0; s.light=light/double(std::max<size_t>(floorCount, 1));
            return s;
        };
        auto detail=[&](const std::vector<double>& img) {
            std::vector<double> l(size_t(W)*H), h(size_t(W)*H, 0.0);
            for(size_t j=0;j<l.size();++j) l[j]=0.2126*img[j*3]+0.7152*img[j*3+1]+0.0722*img[j*3+2];
            for(UINT y=reach;y+reach<H;++y) for(UINT x=reach;x+reach<W;++x) {
                const size_t i=size_t(y)*W+x; if(!onFloor[i]) continue;
                double box=0; for(int j=-reach;j<=reach;++j) for(int q=-reach;q<=reach;++q) box+=l[size_t(int(y)+j)*W+size_t(int(x)+q)];
                h[i]=l[i]-box/double((2*reach+1)*(2*reach+1));
            }
            return h;
        };
        auto toFloat=[](const std::vector<double>& v) { return std::vector<float>(v.begin(), v.end()); };
        struct Floor { const char* name; float tilt; };
        static const float trials[5]={0.f, 4.f, 1.f, 5.f, 7.f};
        for(const Floor& fl:{Floor{"gently hammered (bumps up to about 3 degrees)", 0.1f}, Floor{"with the harness's bumps (up to about 30 degrees)", 1.f}, Floor{"without bumps", 0.f}}) {
            bumpy=fl.tilt>0.f; bumpScale=bumpy ? fl.tilt : 1.f;
            const Seen a=seen(TGPT_OPTION_NO_DENOISER, 0.f), b=seen(TGPT_OPTION_NO_DENOISER, 0.f);
            const auto ha=detail(a.mean), hb=detail(b.mean);
            double den=0; for(size_t j=0;j<ha.size();++j) den+=ha[j]*hb[j];
            const double reference=0.5*(a.light+b.light), spread=std::fabs(a.light/std::max(b.light, 1e-12)-1.0);
            std::printf("      relief filters, a metal floor %s (%zu pixels; the undenoised picture's two means %.3f apart, its flicker %.3f):\n", fl.name, floorCount, spread, a.flicker);
            double keptAt[5]={}, flickerAt[5]={};
            for(int t=0;t<5;++t) {
                const Seen s=seen(0u, trials[t]);
                const auto h=detail(s.mean); double num=0; for(size_t j=0;j<h.size();++j) num+=h[j]*0.5*(ha[j]+hb[j]);
                keptAt[t]=den>0 ? num/den : 0.0; flickerAt[t]=s.flicker;
                std::printf("        the trial at %.0f: relief kept %.3f | light %.3f of the undenoised picture's | flicker %.3f\n", trials[t], keptAt[t], s.light/reference, s.flicker);
                if(fl.tilt>0.f && fl.tilt<1.f) {
                    static const char* const names[5]={"91-relief-trial-0.png", "92-relief-trial-4.png", "93-relief-trial-1.png", "94-relief-trial-5.png", "95-relief-trial-7.png"};
                    if(t==0) save(toFloat(a.mean), "90-relief-undenoised.png", true);
                    save(toFloat(s.mean), names[t], true);
                }
            }
            if(fl.tilt>0.f && fl.tilt<1.f) {
                expect(keptAt[1]>keptAt[0], "on trial: a gently hammered metal's reflections keep more of their relief without the filter's feedback than with the filter as it is");
                expect(keptAt[3]>=keptAt[2], "on trial: and more by the lobe without the feedback than by the lobe with it");
            } else if(fl.tilt>0.f)
                expect(keptAt[2]>keptAt[0], "on trial: a strongly bumpy metal's reflections keep more of their relief by the lobe than with the filter as it is");
            else
                expect(flickerAt[2]<1.1*flickerAt[0]+1e-4, "on trial: on a metal without bumps the reflections' filter by the lobe leaves no more flicker than as it is");
        }
        bumpScale=1.f;
        std::printf(failures ? "FAILED %d checks\n" : "relief filter checks passed\n", failures); return failures ? 1 : 0;
    }
    if(GetEnvironmentVariableA("TGPT_TEST_NRD_ONLY", nullptr, 0)>0) {
        nrdStudy();
        std::printf(failures ? "FAILED %d checks\n" : "NRD study passed\n", failures); return failures ? 1 : 0;
    }
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
    // bounced light), and the meter reports it as it would be shown without the gain. In a dark picture (the exposure
    // 1000 times lower): the gain is whole only where the picture is dark (the next block).
    {
        auto at=[&](float gain) {
            tracedGain=gain;
            for(uint32_t i=0;i<24;++i) run(TGPT_VIEW_NONE, cam, cam, 1700+i);
            return readback();
        };
        setExposure(0.25f/1000.f);
        const auto one=at(1.f), four=at(4.f);
        tracedGain=0; setExposure(0.25f);
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
    // The gain acts by each pixel's own brightness (pt_common highlight_hold, PT-73 e: under one gain for the whole picture
    // a lantern burned the wall beside it out to white while the gain lifted the room). The picture at a gain of 5 against
    // the same frames at 1, pixel by pixel, at an exposure that puts the ceiling's underside at the kept brightness W
    // (HIGHLIGHT_KEPT): the sunlit floor is then far above W and the shade below it. With x = L / W each pixel's luminance
    // is W - (W - L) / (1 + 4 x (1 - x)) below W and L from W up: a pixel at W as it was, none above W darker or brighter,
    // none anywhere darker than at 1, the order of brightness kept, the colour's hue kept. What a surface emits itself (the
    // panel on the left wall, found with the emissive view) is outside the curve, at the game's exposure: the traced light
    // on it takes the gain by its own brightness, on top of the glow. In a dark picture (the exposure 2000 times lower)
    // everything is 5 times brighter. The meter reports the light without the gain at 5 as at 1. On both
    // routes: each pixel's surface from the G-buffer (its light stored divided by its colour), and the tracer's own camera
    // rays (stored complete).
    for(int route=0;route<2;++route) {
        ownRays=route==1;
        const double kept=1.0; // pt_common.hlsli HIGHLIGHT_KEPT
        auto at=[&](float gain, float e) {
            setExposure(e); tracedGain=gain;
            for(uint32_t i=0;i<24;++i) run(TGPT_VIEW_NONE, cam, cam, 1700+i);
            return readback();
        };
        auto lum=[](const std::vector<float>& px, size_t j) { return 0.2126*px[j*3]+0.7152*px[j*3+1]+0.0722*px[j*3+2]; };
        const auto plain=at(1.f, 0.25f);
        // TGPT_TEST_GAIN_DUMP=1: the picture at a gain of 1 as raw floats beside the images, to compare two builds of the
        // tracer bit for bit (at 1 nothing may change, whatever the gain does elsewhere).
        if(GetEnvironmentVariableA("TGPT_TEST_GAIN_DUMP", nullptr, 0)>0) {
            std::ofstream f(images/(route ? "gain-1-own-rays.bin" : "gain-1.bin"), std::ios::binary);
            f.write(reinterpret_cast<const char*>(plain.data()), std::streamsize(plain.size()*sizeof(float)));
        }
        const double ceiling=region(plain, 500, 650, 780, 700);
        const float atKept=float(0.25*kept/std::max(ceiling, 1e-6));
        const auto one=at(1.f, atKept);
        ReadStatus(&status); const float midOne=status.tracedLog2Mid, highOne=status.tracedLog2High;
        const auto five=at(5.f, atKept);
        ReadStatus(&status); const float midFive=status.tracedLog2Mid, highFive=status.tracedLog2High;
        const double sky[3]={0.55*atKept, 0.75*atKept, 0.95*atKept}; // HDRP's own (raster's clear colour): no traced light, no gain
        // The emissive view colours what emits (green, blue or red by how the tracer takes it), grey everything else.
        for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_EMISSIVE, cam, cam, 1730+i);
        const auto emitters=readback();
        const double glow[3]={double(mats[4].er)*atKept, double(mats[4].eg)*atKept, double(mats[4].eb)*atKept}; // the panel's, pre-exposed
        size_t surfaces=0, offCurve=0, darker=0, atW=0, above=0, aboveOff=0, below=0, hueOff=0, glowing=0, glowOff=0;
        double atWOne=0, atWFive=0, aboveWorst=0, belowLeast=1e9;
        std::vector<std::pair<float, float>> pairs; pairs.reserve(size_t(W)*H);
        for(size_t j=0;j<size_t(W)*H;++j) {
            bool isSky=true; for(int k=0;k<3;++k) isSky=isSky && std::fabs(one[j*3+k]-sky[k])<0.01*sky[k];
            const double a=lum(one, j), b=lum(five, j);
            if(isSky || !std::isfinite(a) || !std::isfinite(b) || !(a>1e-5)) continue;
            const float* v=&emitters[j*3];
            const bool emits=std::max({v[0], v[1], v[2]})-std::min({v[0], v[1], v[2]})>0.2f;
            // The traced light alone (the pixel less its glow), the gain it takes by its own luminance, the pixel that makes.
            double traced[3], want[3];
            for(int k=0;k<3;++k) traced[k]=std::max(double(one[j*3+k])-(emits ? glow[k] : 0.0), 0.0);
            const double x=(0.2126*traced[0]+0.7152*traced[1]+0.0722*traced[2])/kept;
            const double gain=x<1.0 ? 1.0+4.0*(1.0-x)*(1.0-x)/(1.0+4.0*x*(1.0-x)) : 1.0;
            for(int k=0;k<3;++k) want[k]=(emits ? glow[k] : 0.0)+traced[k]*gain;
            const double expected=0.2126*want[0]+0.7152*want[1]+0.0722*want[2];
            ++surfaces;
            const bool off=std::fabs(b-expected)>0.02*expected+1e-4;
            if(off) ++offCurve;
            if(b<a*0.995-1e-5) ++darker;
            { bool other=false; for(int k=0;k<3;++k) other=other || std::fabs(five[j*3+k]-want[k])>0.02*expected+1e-4; if(other) ++hueOff; }
            if(emits) { ++glowing; if(off) ++glowOff; continue; }
            pairs.emplace_back(float(a), float(b));
            if(std::fabs(x-1.0)<0.02) { ++atW; atWOne+=a; atWFive+=b; }
            else if(x>1.0) { ++above; aboveWorst=std::max(aboveWorst, std::fabs(b/a-1.0)); if(std::fabs(b/a-1.0)>0.01) ++aboveOff; }
            else if(x<0.5) { ++below; belowLeast=std::min(belowLeast, b/a); }
        }
        std::sort(pairs.begin(), pairs.end());
        size_t outOfOrder=0; float highest=0;
        for(const auto& p: pairs) { if(p.second<highest*0.98f-1e-4f) ++outOfOrder; highest=std::max(highest, p.second); }
        const auto oneDark=at(1.f, atKept/2000.f), fiveDark=at(5.f, atKept/2000.f);
        // TGPT_TEST_GAIN_DUMP=1, a measurement and no check: without a denoiser before the composite (None, Ray
        // Reconstruction) the curve bends each raw sample, and the mean of noisy samples so bent lies under the curve on
        // their mean. 48 frames' mean at 5 against the curve on the same frames' mean at 1, over the pixels under W.
        if(GetEnvironmentVariableA("TGPT_TEST_GAIN_DUMP", nullptr, 0)>0) {
            auto meanOf=[&](float gain) {
                setExposure(atKept); tracedGain=gain;
                std::vector<double> sum(size_t(W)*H, 0.0);
                for(uint32_t i=0;i<56;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, 1700+i, TGPT_OPTION_NO_DENOISER);
                    if(i<8) continue;
                    const auto px=readback();
                    for(size_t j=0;j<sum.size();++j) sum[j]+=lum(px, j)/48.0;
                }
                return sum;
            };
            const auto rawOne=meanOf(1.f), rawFive=meanOf(5.f);
            double bent=0, curve=0, bentDark=0, curveDark=0; size_t n=0, nDark=0;
            for(size_t j=0;j<rawOne.size();++j) {
                bool isSky=true; for(int k=0;k<3;++k) isSky=isSky && std::fabs(one[j*3+k]-sky[k])<0.01*sky[k];
                const double a=rawOne[j], x=a/kept;
                if(isSky || !(a>1e-5) || !(x<1.0) || !std::isfinite(rawFive[j])) continue;
                const double expected=kept-(kept-a)/(1.0+4.0*x*(1.0-x));
                bent+=rawFive[j]; curve+=expected; ++n;
                if(x<0.25) { bentDark+=rawFive[j]; curveDark+=expected; ++nDark; }
            }
            std::printf("      raw samples (no denoiser before the composite), 48 frames' mean at 5 over the curve on their mean at 1: %.4f under W (%zu pixels), %.4f under a quarter of W (%zu)\n",
                        curve>0 ? bent/curve : 0.0, n, curveDark>0 ? bentDark/curveDark : 0.0, nDark);
        }
        tracedGain=0; setExposure(0.25f); ownRays=false;
        const double darkOne=region(oneDark, 500, 650, 780, 700), darkFive=region(fiveDark, 500, 650, 780, 700);
        std::printf("      gain 5 against 1 (%s) with the ceiling at W (exposure %.3f): %zu surface pixels, %zu off the curve, %zu darker than at 1; "
                    "%zu at W: x%.4f; %zu above W: %zu over 1 %% off x1 (the most %.4f); %zu under half of W: at least x%.3f; %zu out of order; %zu of another colour; %zu emitting: %zu off\n",
                    route ? "the tracer's own camera rays" : "the G-buffer's surfaces", atKept, surfaces, offCurve, darker, atW, atWOne>0 ? atWFive/atWOne : 0.0, above, aboveOff, aboveWorst, below, belowLeast, outOfOrder, hueOff, glowing, glowOff);
        std::printf("      in the dark (exposure %.6f) under the ceiling: x%.3f; the meter at 1: median log2 %.2f, 98th percentile %.2f; at 5: %.2f, %.2f\n",
                    atKept/2000.f, darkOne>0 ? darkFive/darkOne : 0.0, midOne, highOne, midFive, highFive);
        expect(surfaces>size_t(W)*H/2 && offCurve<surfaces/100 && darker<surfaces/1000 && atW>100 && atWFive>=atWOne*0.995 && atWFive<=atWOne*1.01 &&
               above>surfaces/50 && aboveOff<above/200+1 && below>surfaces/50 && belowLeast>1.1 && outOfOrder<surfaces/200 && hueOff<surfaces/100 &&
               glowing>1000 && glowOff<glowing/50 &&
               darkOne>0 && darkFive/darkOne>4.9 && darkFive/darkOne<5.02,
               "the traced light's gain is all there in the dark, none from the kept brightness up, and keeps the order of brightness");
        expect(std::isfinite(midOne) && std::fabs(midFive-midOne)<0.4f && std::fabs(highFive-highOne)<0.4f,
               "the meter reports the traced light without its gain where the gain holds back too");
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

    }
    // Debug views.
    const std::pair<uint32_t, const char*> views[]={{TGPT_VIEW_ALBEDO, "2-view-albedo.png"}, {TGPT_VIEW_NORMALS, "3-view-normals.png"},
        {TGPT_VIEW_COVERAGE, "4-view-coverage.png"}, {TGPT_VIEW_MATERIALS, "5-view-materials.png"}, {TGPT_VIEW_INDIRECT, "6-view-indirect.png"},
        {TGPT_VIEW_INDIRECT_RAW, "7-view-indirect-raw.png"}, {TGPT_VIEW_SURFACES, "8-view-surfaces.png"},
        {TGPT_VIEW_SPECULAR, "9-view-specular.png"}};
    for(const auto& v:views) {
        // Twice, as when the composite showed the frame before's traced output (until 2026-10-08; a frame shows its own now,
        // PT-84, and one would do).
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
    // Round things traced as round (PT-04, the owner, 2026-10-06: "cloth, jars, and other things can look a little 'stepped'/
    // low-poly ... around corners, shoulders, cloth folds"): a cylinder of 12 flat sides with smooth vertex normals, 1.2 m
    // across, 3.5 m in front of a camera of its own, the room's lamp 0.9 m from its left side. Each check reads a profile
    // across it: per column, the mean of the 41 rows at its middle over the last 16 frames (its faces are vertical, so their
    // edges stay sharp while the noise averages out), from 6 columns inside its outline on either side (the face beside the
    // outline is seen edge on). Its measure is a step: the largest jump between neighbouring columns beyond the profile's
    // own slope there (the median jump of the 13 around it), since the light falls fastest near the outline.
    // TGPT_OPTION_FACETED (the hidden "Smooth round surfaces" off) shades it as before: its steps must stand out there, or
    // the check could not see them. The face edges lie at columns 538, 576, 639, 702 and 740.
    {
        const int sides=12; const float radius=0.6f, height=1.6f;
        std::vector<Vertex> cyl; std::vector<uint16_t> cylIndices;
        for(int k=0;k<=sides;++k) {
            const float a=2.f*pi*float(k)/float(sides), c=std::cos(a), s=std::sin(a);
            cyl.push_back({radius*c, 0.f, radius*s, c, 0.f, s, float(k)/float(sides), 0.f});
            cyl.push_back({radius*c, height, radius*s, c, 0.f, s, float(k)/float(sides), 1.f});
        }
        for(int k=0;k<sides;++k) { // wound as Unity winds them: cross(v1 - v0, v2 - v0) out of the cylinder
            const uint16_t b0=uint16_t(2*k), t0=uint16_t(2*k+1), b1=uint16_t(2*k+2), t1=uint16_t(2*k+3);
            cylIndices.insert(cylIndices.end(), {b0, t0, b1, b1, t0, t1});
        }
        const uint16_t centre=uint16_t(cyl.size()); // its top, facing up
        cyl.push_back({0.f, height, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f});
        for(int k=0;k<=sides;++k) {
            const float a=2.f*pi*float(k)/float(sides);
            cyl.push_back({radius*std::cos(a), height, radius*std::sin(a), 0.f, 1.f, 0.f, 0.5f+0.5f*std::cos(a), 0.5f+0.5f*std::sin(a)});
        }
        for(int k=0;k<sides;++k) cylIndices.insert(cylIndices.end(), {centre, uint16_t(centre+2+k), uint16_t(centre+1+k)});
        MeshBuffers cylMesh=make_mesh(cyl, {cylIndices});
        const int cylHandle=register_mesh(cylMesh, 60);
        expect(cylHandle>0, "a 12-sided cylinder registered with its vertex normals");
        const M4 cw=translate_scale({-2.5f, 0.f, -3.f}, {1, 1, 1});
        const Camera cylCam=make_camera({-2.5f, 1.f, -6.5f}, {-2.5f, 0.8f, -3.f}, 60.f, float(W)/float(H));
        // Its materials: red (the drawn color mats[1]), the same tested per ray (alpha-tested), and clear see-through glass.
        const uint32_t redIndex=uint32_t(materials.size())+12, cutIndex=redIndex+1, glassIndex=redIndex+2;
        auto cutRed=materials[1]; cutRed.flags=TGPT_MATERIAL_ALPHA_TEST; cutRed.alphaCutoff=0.f;
        // Clear as HDRP blends it: none of its color covers (alpha 0; at alpha 1 HDRP draws the color whole, PT-35).
        auto seeThrough=materials[0]; seeThrough.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS|TGPT_MATERIAL_SEE_THROUGH; seeThrough.alphaCutoff=0.f;
        seeThrough.albedoTile=-1; seeThrough.smoothness=0.95f; for(int c=0;c<3;++c) seeThrough.baseColor[c]=1.f;
        seeThrough.baseColor[3]=0.f;
        expect(SetMaterials(redIndex, &materials[1], 1)==0 && SetMaterials(cutIndex, &cutRed, 1)==0 && SetMaterials(glassIndex, &seeThrough, 1)==0,
               "the cylinder's materials");
        // How HDRP draws it: 0 not at all (see-through glass, the tracer's alone), 1 into the GBuffer (with GBuffer2's feature
        // and blue byte), 2 forward.
        struct Look { int drawn; uint32_t material; uint32_t feature=0; int gb2b=-1; };
        // Where it stands, the camera that looks at it and how many rows each side of the middle a profile takes (the jar's check moves it).
        const M4* at=&cw; const Camera* eye=&cylCam; UINT band=20;
        auto place=[&](const Look& look, bool on) {
            if(on) {
                auto list=instances; auto listIndices=indices;
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=at->m[r][c];
                q.mesh=uint32_t(cylHandle); q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=look.material==redIndex ? 0u : TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(look.material); list.push_back(q);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with the cylinder");
                if(look.drawn>0) { Object o{0, *at, {1}, false, false, look.feature, look.drawn==2}; o.gb2b=look.gb2b; o.drawn=&cylMesh; objects.push_back(o); }
            } else {
                if(look.drawn>0) objects.pop_back();
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
            }
        };
        // channel: 0 to 2 one of the view's, 3 its luminance.
        auto profile=[&](const Look& look, uint32_t view, uint32_t options, int channel, uint32_t frames, uint32_t seed, const char* png=nullptr) {
            if(look.drawn>=0) place(look, true);
            std::vector<double> p(W, 0.0);
            for(uint32_t i=0;i<frames;++i) {
                run(view, *eye, *eye, i ? seed+i : 0, options);
                if(i+16<frames) continue;
                const auto r=readback();
                if(png && i+1==frames) save(r, png, view!=TGPT_VIEW_NORMALS);
                for(UINT y=H/2-band;y<=H/2+band;++y) for(UINT x=0;x<W;++x) {
                    const size_t j=(size_t(y)*W+x)*3;
                    p[x]+=channel<3 ? double(r[j+size_t(channel)]) : 0.2126*r[j]+0.7152*r[j+1]+0.0722*r[j+2];
                }
            }
            if(look.drawn>=0) place(look, false);
            for(double& v:p) v/=16.0*double(2*band+1);
            return p;
        };
        const Look tracedRed{0, redIndex};
        place(tracedRed, true); // its mesh read back and built
        for(uint32_t i=0;i<12;++i) { run(TGPT_VIEW_NONE, cylCam, cylCam, i ? 9000+i : 0); context->Flush(); Sleep(10); }
        place(tracedRed, false);
        // Its outline on those rows: where camera rays meet another instance with it than without.
        const auto seenWith=profile(tracedRed, TGPT_VIEW_COVERAGE, 0, 3, 18, 9020), seenWithout=profile(Look{-1, 0}, TGPT_VIEW_COVERAGE, 0, 3, 18, 9020);
        int first=-1, last=-1;
        for(UINT x=0;x<W;++x) if(std::fabs(seenWith[x]-seenWithout[x])>0.02) { if(first<0) first=int(x); last=int(x); }
        const int from=first+6, to=last-6;
        std::printf("      the cylinder spans columns %d to %d of the middle rows\n", first, last);
        expect(first>0 && last-first>150 && last-first<300, "the cylinder stands in front of its camera");
        auto largestStep=[&](const std::vector<double>& p, bool relative) {
            double peak=0, step=0;
            for(int x=from;x<=to;++x) peak=std::max(peak, std::fabs(p[size_t(x)]));
            std::vector<double> jump;
            for(int x=from;x<to;++x) jump.push_back(std::fabs(p[size_t(x)+1]-p[size_t(x)]));
            for(size_t i=0;i<jump.size();++i) {
                std::vector<double> around(jump.begin()+std::ptrdiff_t(i>6 ? i-6 : 0), jump.begin()+std::ptrdiff_t(std::min(jump.size(), i+7)));
                std::nth_element(around.begin(), around.begin()+std::ptrdiff_t(around.size()/2), around.end());
                step=std::max(step, jump[i]-around[around.size()/2]);
            }
            return relative ? (peak>0 ? step/peak : 0.0) : step;
        };
        // 1. See-through glass (a jar, a bottle HDRP draws as a transparent surface) is drawn by the tracer from its own mesh:
        // its normal must turn with the mesh's vertex normals, not jump from face to face (a round jar shaded as a prism).
        // In the Normals view: n.x across it, as 0.5 + 0.5 n.x. A step between two faces of 12 is up to 0.5 in n.x.
        {
            const bool previousOwn=ownRays; ownRays=true;
            const Look glass{0, glassIndex};
            const double smooth=2.0*largestStep(profile(glass, TGPT_VIEW_NORMALS, 0, 0, 20, 9100, "48-round-glass-normals.png"), false);
            const double faceted=2.0*largestStep(profile(glass, TGPT_VIEW_NORMALS, TGPT_OPTION_FACETED, 0, 20, 9100, "48-round-glass-normals-faceted.png"), false);
            std::printf("      see-through glass cylinder: largest step of its normal's x between neighbouring columns %.3f (by its faces as before %.3f)\n", smooth, faceted);
            expect(faceted>0.2, "the faceted glass cylinder's normal steps from face to face (the check sees faces)");
            expect(smooth<0.05, "see-through glass is shaded by its mesh's smooth normals, not by its flat faces");
                    ownRays=previousOwn;
        }
        // 2. A prop HDRP draws forward (outside its GBuffer), found by the camera ray: the same.
        {
            const Look forward{2, redIndex};
            const double smooth=2.0*largestStep(profile(forward, TGPT_VIEW_NORMALS, 0, 0, 20, 9200), false);
            const double faceted=2.0*largestStep(profile(forward, TGPT_VIEW_NORMALS, TGPT_OPTION_FACETED, 0, 20, 9200), false);
            std::printf("      cylinder drawn forward: largest step of its normal's x %.3f (as before %.3f)\n", smooth, faceted);
            expect(faceted>0.2 && smooth<0.05, "a prop HDRP draws forward is shaded by its mesh's smooth normals");
        }
        // 3. Drawn into the GBuffer (HDRP's smooth normals; its faces in HDRP's depth), lit by the lamp alone: its direct light
        // must fall off smoothly to the shadow line, with no step where two faces meet. The fade towards each face's terminator
        // (terminator_softening) left the whole face before the shadow line dark and half of each lit face its own flat light
        // (docs/PT-SURFACES.md A.2). Skin (HDRP's transmission through a solid profile) keeps the fade. A translucent surface
        // (HDRP's transmission through a thin profile: a leaf, and the game's candle wax) gets no fade, but its shadow rays
        // started on the light's side of every face that turned from the light: inside a closed alpha-tested mesh, which then
        // stopped them, so the face before the shadow line went black the same way.
        sunOn=false;
        {
            const Look lit{1, redIndex};
            const double smooth=largestStep(profile(lit, TGPT_VIEW_DIRECT, TGPT_OPTION_NO_DENOISER, 3, 24, 9300, "49-round-direct.png"), true);
            const double faceted=largestStep(profile(lit, TGPT_VIEW_DIRECT, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_FACETED, 3, 24, 9300, "49-round-direct-faceted.png"), true);
            thickProfiles=1u<<7;
            const double skin=largestStep(profile(Look{1, redIndex, 2, 127}, TGPT_VIEW_DIRECT, TGPT_OPTION_NO_DENOISER, 3, 24, 9300), true);
            thickProfiles=0;
            const Look wax{1, cutIndex, 2, 127};
            const double waxSmooth=largestStep(profile(wax, TGPT_VIEW_DIRECT, TGPT_OPTION_NO_DENOISER, 3, 24, 9300, "49-round-direct-translucent.png"), true);
            const double waxFaceted=largestStep(profile(wax, TGPT_VIEW_DIRECT, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_FACETED, 3, 24, 9300, "49-round-direct-translucent-faceted.png"), true);
            std::printf("      cylinder in the GBuffer lit by a lamp: largest step of its direct light %.3f of its brightest (as before %.3f), skin %.3f; "
                        "closed translucent %.3f (as before %.3f)\n", smooth, faceted, skin, waxSmooth, waxFaceted);
            expect(faceted>0.06, "the fade per face leaves steps near the shadow line (the check sees them)");
            expect(smooth<0.04, "a round surface's direct light falls off smoothly to the shadow line, without a step at each face");
            expect(skin>0.06, "skin keeps its fade towards the terminator");
            expect(waxFaceted>0.06 && waxSmooth<0.04, "a closed translucent surface (candle wax) is lit to its shadow line, without a black face before it");
        }
        sunOn=true;
        // 4. The built-in denoiser (and the direct light's and reflections' filters beside NRD) across the faces of one round
        // thing: steered by each face's own orientation alone, it filtered every face on its own into one tone. The bounced
        // light in the sun, denoised.
        {
            const Look lit{1, redIndex};
            const double smooth=largestStep(profile(lit, TGPT_VIEW_INDIRECT, 0, 3, 40, 9400, "50-round-indirect.png"), true);
            const double faceted=largestStep(profile(lit, TGPT_VIEW_INDIRECT, TGPT_OPTION_FACETED, 3, 40, 9400, "50-round-indirect-faceted.png"), true);
            std::printf("      cylinder's denoised bounced light: largest step %.3f of its brightest (as before %.3f)\n", smooth, faceted);
            expect(faceted>0.015 && smooth<0.4*faceted, "the built-in denoiser filters a round thing across its faces");
        }
    }
    // TGPT_TEST_SURFACES_ONLY=1: stop here, after the round-things checks (some 20 s), while a look is being worked on.
    if(GetEnvironmentVariableA("TGPT_TEST_SURFACES_ONLY", nullptr, 0)>0) { std::printf(failures ? "FAILED %d checks\n" : "surfaces checks passed\n", failures); return failures ? 1 : 0; }
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
        // PT-50: no light stops at a reach. A light that falls off with distance alone (TGPT_LIGHT_NO_RANGE) lights the whole
        // room the same whether the light grid lists it near everywhere (range 30 m) or only in its own 8 m cell (range
        // 0.6 m: elsewhere the far lists sample it); a light without the flag keeps its window and lights nothing past it.
        // And more lights than the old cap of 1,024: the 1,101st (the room's lamp, after 1,100 dark ones) lights the room.
        {
            const bool sunWas=sunOn; sunOn=false;
            auto image=[&](const std::vector<TGPT_Light>& ls, uint32_t first) {
                expect(SetLights(ls.empty() ? nullptr : ls.data(), uint32_t(ls.size()))==0, "lights set");
                std::vector<double> sum(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<40;++i) {
                    cut=i==0; run(TGPT_VIEW_NONE, cam, cam, first+i, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE); cut=false;
                    if(i>=8) { const auto px=readback(); for(size_t k=0;k<sum.size();++k) sum[k]+=px[k]; }
                }
                std::vector<float> out(sum.size()); for(size_t k=0;k<sum.size();++k) out[k]=float(sum[k]/32.0);
                return out;
            };
            TGPT_Light lamp=light; lamp.color[0]=400; lamp.color[1]=220; lamp.color[2]=80; lamp.flags=TGPT_LIGHT_NO_RANGE;
            TGPT_Light nearAll=lamp; nearAll.range=30.f;
            TGPT_Light ownCell=lamp; ownCell.range=0.6f;
            TGPT_Light windowed=ownCell; windowed.flags=0;
            const auto none=image({}, 9000), wide=image({nearAll}, 9100), narrow=image({ownCell}, 9200), cut=image({windowed}, 9300);
            // And the same light given the flag back: the far lists, kept from one light grid to the next, are made anew
            // when a light changes its kind (a second, dark light first, so that they were made without the lamp in them).
            TGPT_Light spare=windowed; spare.color[0]=spare.color[1]=spare.color[2]=0; spare.position[1]=5.f; spare.id=2;
            image({windowed, spare}, 9800);
            const auto again=image({ownCell, spare}, 9900);
            std::vector<TGPT_Light> crowd;
            for(uint32_t i=0;i<1100;++i) { TGPT_Light d{}; d.position[0]=-4.f+float(i%10)*0.01f; d.position[1]=1.2f; d.position[2]=-3.f; d.type=0; d.range=1.f; d.radius=0.05f; d.id=5000+i; crowd.push_back(d); }
            crowd.push_back(nearAll); crowd.back().id=9999;
            const auto many=image(crowd, 9400);
            ReadStatus(&status);
            const uint32_t sent=status.lights;
            // For the record, what the light grid costs the render thread when it is made anew (a light moved a quarter of
            // a metre): 1,000 dim lamps that fall off with distance alone, spread over 200 m, one of them moving half a metre
            // every frame (the grid every frame, its far lists every eighth: they are kept until a light has moved 4 m),
            // against the same lamps standing still (the grid kept). Medians and the slowest of 20 frames.
            double gridMs[2]={0, 0}, gridMost[2]={0, 0};
            {
                std::vector<TGPT_Light> town;
                for(uint32_t i=0;i<1000;++i) {
                    TGPT_Light d=nearAll; d.position[0]=-100.f+float(i%32)*6.4f; d.position[1]=2.f+float(i%5); d.position[2]=-100.f+float(i/32)*6.4f;
                    d.range=20.f; d.color[0]=d.color[1]=d.color[2]=0.01f; d.id=7000+i; town.push_back(d);
                }
                bool set=true;
                for(int moving=0;moving<2;++moving) {
                    std::vector<double> ms;
                    for(uint32_t i=0;i<24;++i) {
                        if(moving) town[500].position[0]+=0.5f;
                        set=set && SetLights(town.data(), uint32_t(town.size()))==0;
                        run(TGPT_VIEW_NONE, cam, cam, 9500+uint32_t(moving)*100+i, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE);
                        ReadStatus(&status);
                        if(i>=4) ms.push_back(status.recordMs);
                    }
                    std::sort(ms.begin(), ms.end()); gridMs[moving]=ms[ms.size()/2]; gridMost[moving]=ms.back();
                }
                expect(set, "a town's 1,000 lamps set");
                std::printf("      PT-50 the light grid with 1,000 lamps that fall off with distance alone: the render thread records a frame in %.2f ms when one of them "
                            "moves (%.2f ms the slowest, with the far lists made anew), %.2f ms when none does (%.2f)\n", gridMs[1], gridMost[1], gridMs[0], gridMost[0]);
            }
            // The room right of x = 0 and behind z = 0, outside the lamp's own cell: the right half of the picture, all of it.
            auto added=[&](const std::vector<float>& px) { return region(px, W/2, 0, W, H)-region(none, W/2, 0, W, H); };
            const double aWide=added(wide), aNarrow=added(narrow), aCut=added(cut), aMany=added(many), aAgain=added(again);
            std::printf("      PT-50 a lamp's light right of its cell: listed near everywhere %.4f, near only in its own cell %.4f, windowed at 0.6 m %.4f; "
                        "with 1,100 dark lights before it %.4f (%u lights taken)\n", aWide, aNarrow, aCut, aMany, sent);
            save(narrow, "11b-lamp-sampled-far.png", true);
            expect(aWide>0.01, "the lamp lights the room's right half");
            expect(std::fabs(aNarrow-aWide)<0.08*aWide, "a lamp's light goes on past the light grid's reach for it (the far lists): the same light as listed near everywhere");
            expect(aCut<0.1*aWide, "a light without TGPT_LIGHT_NO_RANGE keeps its window: nothing past its range");
            std::printf("      PT-50 the windowed lamp given TGPT_LIGHT_NO_RANGE again: %.4f\n", aAgain);
            expect(std::fabs(aAgain-aWide)<0.08*aWide, "the far lists follow a light that changes its kind (kept from grid to grid otherwise)");
            expect(sent==1101 && std::fabs(aMany-aWide)<0.08*aWide, "the 1,101st light is traced (no cap of 1,024)");
            sunOn=sunWas;
            expect(SetLights(&light, 1)==0, "one light again");
        }
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
                // Not on the bump-mapped case: there NRD shows the normal map's shading in the bounced light (PT-19), which the
                // built-in denoiser blurs away, and with the camera's jitter that shading changes from frame to frame as the game's
                // own normal-mapped lighting does before its upscaler resolves it: measured 0.030 moving and 0.038 with the camera
                // still and jittered, the same at 8 rays a pixel (so not noise), and 0.003 without the shading put back. Its noise
                // is checked without jitter in nrdStudy.
                if(std::string(pr.first).find("bumpy")==std::string::npos)
                    expect(a->flicker<=b->flicker*1.5+1e-4, "NRD flickers no more than half again as much as the built-in denoiser");
                expect(a->ceiling<=b->ceiling*1.25, "NRD's error in the bounced light alone (under the ceiling) is within a quarter of the built-in denoiser's");
            }
        }
        // NRD's look, as the in-game harness measures it (nrdStudy, PT-19).
        if(haveNrd) nrdStudy();
        // PT-75: what a denoiser's picture converges to is the undenoised picture's (nrdMean's brief form: NRD and the built-in
        // denoiser; in a build without NRD the built-in one alone). PT-80: a mirror's picture stays as traced (mirrorFilters).
        nrdMean(true);
        mirrorFilters();
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
        // (Fog Adaptation's checks went with it on 2026-10-11: the fog is the tracer's, HDRP's fog is never laid over the picture.)
        // HDRP's fog dims the sun before it lights a surface (LightEvaluation.hlsl: the fog's optical depth from the surface up
        // along the sun's direction), and the tracer's sun with it (pt_common directional_fog): a fog with its base 100 m up, a
        // scale height of 100 m and a mean free path of 200 m leaves e^-((100 - h + 100) / 200 / the sun's height) of the sun on
        // a floor at height h. The Direct view's sunlit pixels against the same without fog.
        {
            auto direct=[&](uint32_t base) {
                std::vector<float> px;
                for(uint32_t i=0;i<12;++i) { run(TGPT_VIEW_DIRECT, cam, cam, i ? base+i : 0, TGPT_OPTION_NO_DENOISER); px=readback(); }
                return px;
            };
            // The sun's own light: the view with the sun less the view without it (the room's lamps are in both, and the fog
            // dims them over their few metres only).
            const bool keptSun=sunOn;
            sunOn=true; const auto clearSun=direct(6300); sunOn=false; const auto clearLamps=direct(6320);
            fogExtinction=1.f/200.f; fogBase=100.f; fogRcpScale=1.f/100.f;
            sunOn=true; const auto foggySun=direct(6340); sunOn=false; const auto foggyLamps=direct(6360);
            fogExtinction=fogBase=fogRcpScale=0.f; sunOn=keptSun;
            float peak=0;
            for(size_t j=0;j+2<clearSun.size();j+=3) if(std::isfinite(clearSun[j+1]) && std::isfinite(clearLamps[j+1])) peak=std::max(peak, clearSun[j+1]-clearLamps[j+1]);
            double lit=0, dimmed=0, lamps=0, lampsDimmed=0; int n=0;
            for(size_t j=0;j+2<clearSun.size();j+=3) {
                if(!std::isfinite(foggySun[j+1]) || !std::isfinite(foggyLamps[j+1]) || !std::isfinite(clearSun[j+1]) || !std::isfinite(clearLamps[j+1])) continue;
                lamps+=clearLamps[j+1]; lampsDimmed+=foggyLamps[j+1];
                const float sun=clearSun[j+1]-clearLamps[j+1];
                if(!(sun>0.5f*peak)) continue;
                lit+=sun; dimmed+=foggySun[j+1]-foggyLamps[j+1]; ++n;
            }
            const double share=lit>0 ? dimmed/lit : 0.0, expected=std::exp(-(200.0-1.0)/200.0/std::max(double(sunDir.y), 0.001));
            const double lampShare=lamps>0 ? lampsDimmed/lamps : 0.0;
            std::printf("      the sun through HDRP's fog (%d sunlit pixels): %.3f of it arrives, HDRP's rule says %.3f (the sun %.0f degrees up); of the lamps' light %.3f\n",
                n, share, expected, std::asin(std::min(1.0, double(sunDir.y)))*57.2958, lampShare);
            expect(n>2000 && share>expected*0.85 && share<expected*1.15, "the traced sun is dimmed by HDRP's fog above the surface, as HDRP dims its own");
            expect(lampShare>0.85 && lampShare<0.999, "a lamp's light is dimmed by HDRP's fog over the way to the surface");
        }
        // Caustics through glass (manifold next event estimation): a clear glass ball above the floor, in the traced scene only,
        // under the sun. With its vertex normals the sun's light through it is bent and focused onto the floor: a spot brighter
        // than the open sun and a darker ring, and about the light an opaque ball's shadow takes back on the floor (less the
        // surfaces' reflections). Tinted along straight lines (TGPT_OPTION_NO_CAUSTICS), clear glass casts no shadow at all.
        {
            std::vector<Vertex> ball; std::vector<uint16_t> ballIndices;
            const int stacks=32, slices=64;
            for(int i=0;i<=stacks;++i) for(int j=0;j<=slices;++j) {
                const float th=pi*float(i)/stacks, ph=2.f*pi*float(j)/slices;
                const float x=std::sin(th)*std::cos(ph), y=std::cos(th), z=std::sin(th)*std::sin(ph);
                ball.push_back({x*0.5f, y*0.5f, z*0.5f, x, y, z, float(j)/slices, float(i)/stacks});
            }
            for(int i=0;i<stacks;++i) for(int j=0;j<slices;++j) {
                const uint16_t a=uint16_t(i*(slices+1)+j), b=uint16_t(a+slices+1);
                ballIndices.insert(ballIndices.end(), {a, b, uint16_t(a+1), uint16_t(a+1), b, uint16_t(b+1)});
            }
            MeshBuffers ballMesh=make_mesh(ball, {ballIndices});
            const int ballHandle=register_mesh(ballMesh, 50);
            expect(ballHandle>0, "a glass ball registered with its normals");
            auto clear=materials[0]; clear.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS; clear.alphaCutoff=0.f; clear.albedoTile=-1; clear.smoothness=0.95f;
            clear.baseColor[0]=clear.baseColor[1]=clear.baseColor[2]=clear.baseColor[3]=1.f;
            auto solid=materials[0];
            const uint32_t clearIndex=uint32_t(materials.size())+4, solidIndex=clearIndex+1;
            expect(SetMaterials(clearIndex, &clear, 1)==0 && SetMaterials(solidIndex, &solid, 1)==0, "the ball's materials");
            const M4 bw=translate_scale({-0.8f, 0.8f, -0.3f}, {1, 1, 1}); // its shadow on the sunlit floor in view, its sunlight clear of the leaves and the box
            auto direct=[&](int material, uint32_t options, uint32_t base) {
                auto list=instances; auto listIndices=indices;
                if(material>=0) {
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=bw.m[r][c];
                    q.mesh=ballHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
                    listIndices.push_back(uint32_t(material)); list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with the ball");
                std::vector<double> acc(size_t(W)*H, 0.0);
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_DIRECT, cam, cam, i ? base+i : 0, TGPT_OPTION_NO_DENOISER|options);
                    if(i>=16) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=0.2126*r[j*3]+0.7152*r[j*3+1]+0.0722*r[j*3+2]; }
                }
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return acc;
            };
            direct(int(clearIndex), 0, 6400); // the ball's mesh is read back and built over the first frames
            const auto open=direct(-1, 0, 6500), opaque=direct(int(solidIndex), 0, 6600);
            const auto focused=direct(int(clearIndex), 0, 6700), straight=direct(int(clearIndex), TGPT_OPTION_NO_CAUSTICS, 6800);
            {
                std::vector<float> img(size_t(W)*H*3);
                for(size_t j=0;j<focused.size();++j) img[j*3]=img[j*3+1]=img[j*3+2]=float(focused[j]/24.0);
                save(img, "41-glass-ball-caustic.png", true);
            }
            double shadow=0, back=0, straightChange=0, peak=0, darkest=1e30, brightest=0;
            for(double v:open) brightest=std::max(brightest, v);
            for(size_t j=0;j<open.size();++j) {
                const double blocked=open[j]-opaque[j];
                if(blocked>0.05*brightest) {
                    shadow+=blocked; back+=focused[j]-opaque[j]; straightChange+=straight[j]-open[j];
                    if(open[j]>0.2*brightest) { peak=std::max(peak, focused[j]/open[j]); darkest=std::min(darkest, focused[j]/open[j]); }
                }
            }
            const double kept=shadow>0 ? back/shadow : 0.0, straightShadow=shadow>0 ? std::fabs(straightChange)/shadow : 1.0;
            std::printf("      a clear glass ball in the sun: %.2f of the light its opaque twin blocks reaches the floor focused, a spot x%.2f the open sun, a ring x%.2f; straight tint changes %.3f\n",
                        kept, peak, darkest, straightShadow);
            expect(straightShadow<0.1, "clear glass tinted along straight lines casts no shadow"); // a shadow would be about 1
            expect(peak>2.0 && darkest<0.5, "glass focuses the sun into a bright spot, with a darker ring round it (caustics)");
            expect(kept>0.5 && kept<1.05, "the light through glass reaches the floor focused, less its surfaces' reflections");
            // PT-64: glass bends light by its material's own index of refraction (TGPT_Material.ior, HDRP Lit's _Ior; ordinary
            // glass's 1.5 where the material names none, as the ball above). The same frames (the same seeds) with the ball at an
            // index of 1.5 named, at 1.2 and at 1: 1.5 named is the ball above; 1.2 lays its light elsewhere on the floor (its focus
            // twice as far: the floor lies before it), still letting it through; 1 bends nothing, so the floor under it is lit as
            // by the straight tint. Measured over the ball's shadow, against the light it blocks (a single pixel's peak is no
            // measure: a lens's caustic curve is as bright as the pixel is small).
            {
                auto named=clear; named.ior=1.5f;
                auto weak=clear; weak.ior=1.2f;
                auto unbending=clear; unbending.ior=1.f;
                const uint32_t namedIndex=solidIndex+1, weakIndex=solidIndex+2, unbendingIndex=solidIndex+3;
                expect(SetMaterials(namedIndex, &named, 1)==0 && SetMaterials(weakIndex, &weak, 1)==0 && SetMaterials(unbendingIndex, &unbending, 1)==0,
                       "balls of glass with indices of refraction of their own");
                const auto at15=direct(int(namedIndex), 0, 6700), weaker=direct(int(weakIndex), 0, 6700), flat=direct(int(unbendingIndex), 0, 6700);
                double toNamed=0, toWeak=0, weakBack=0, toStraight=0;
                for(size_t j=0;j<open.size();++j) {
                    if(open[j]-opaque[j]<=0.05*brightest) continue;
                    toNamed+=std::fabs(at15[j]-focused[j]); toWeak+=std::fabs(weaker[j]-focused[j]); weakBack+=weaker[j]-opaque[j]; toStraight+=std::fabs(flat[j]-straight[j]);
                }
                const double s=std::max(shadow, 1e-12);
                std::printf("      the ball by its material's own index of refraction, over its shadow against the light it blocks: 1.5 named off the unnamed by %.3f; 1.2 off 1.5 by %.3f, "
                            "%.2f of the light through; 1 off the straight tint by %.3f\n", toNamed/s, toWeak/s, weakBack/s, toStraight/s);
                expect(toNamed/s<0.05, "glass naming no index of refraction bends light as ordinary glass (1.5)");
                expect(toWeak/s>0.3 && weakBack/s>0.5 && weakBack/s<1.1, "glass bends light by its material's own index of refraction (another index lays the light elsewhere)");
                expect(toStraight/s<0.1, "glass with an index of refraction of 1 bends nothing: its light comes straight through");
            }
            // The F8 report's count of the crosshair's glass crossings (pt_probe glass_crossings, bits 24-27 of pickSurface): the
            // ball on the camera's centre ray is crossed twice (its caustics solved); a hollow one, one mesh with an inner wall
            // (the kind a shadow ray tints along straight lines), four times.
            {
                std::vector<Vertex> hollow=ball; std::vector<uint16_t> hollowIndices=ballIndices;
                const uint16_t inner=uint16_t(ball.size());
                for(const auto& v:ball) hollow.push_back({v.px*0.9f, v.py*0.9f, v.pz*0.9f, -v.nx, -v.ny, -v.nz, v.u, v.v});
                for(size_t k=0;k+2<ballIndices.size();k+=3)
                    hollowIndices.insert(hollowIndices.end(), {uint16_t(inner+ballIndices[k]), uint16_t(inner+ballIndices[k+2]), uint16_t(inner+ballIndices[k+1])});
                MeshBuffers hollowMesh=make_mesh(hollow, {hollowIndices});
                const int hollowHandle=register_mesh(hollowMesh, 51);
                expect(hollowHandle>0, "a hollow glass ball registered");
                auto crossed=[&](int handle) {
                    auto list=instances; auto listIndices=indices;
                    const M4 at=translate_scale({0.f, 3.2f-9.f/11.f, 0.f}, {1, 1, 1}); // on the camera's centre ray
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=at.m[r][c];
                    q.mesh=handle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
                    listIndices.push_back(clearIndex); list.push_back(q);
                    expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "a glass ball under the crosshair");
                    for(uint32_t i=0;i<12;++i) { run(TGPT_VIEW_NONE, cam, cam, i ? 6900+i : 0); context->Flush(); Sleep(10); }
                    ReadStatus(&status);
                    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                    return (status.pickSurface>>24)&0xFu;
                };
                const uint32_t solidCount=crossed(ballHandle), hollowCount=crossed(hollowHandle);
                std::printf("      the crosshair's glass: a clear ball crossed %u times, a hollow one %u times\n", solidCount, hollowCount);
                expect(solidCount==2 && hollowCount==4, "the F8 report counts the crosshair's glass crossings (2 closed, 4 hollow)");
            }
        }
        // Caustics (TGPT_SetWaterCaustics, pt_caustics.hlsl): the sun's and the lamps' light traced onto HDRP's water as its
        // waves shape it, reflected and refracted there, and its light per area where it lands. A water quad (HDRP's water,
        // TGPT_INSTANCE_WATER: met by the water query alone) 0.4 m over the sunlit floor beside the box, the room's lamp off. Still
        // water first: the floor under it takes the sun through the surface (its straight shadow ray stops at the water), the
        // water's Fresnel transmittance of it (a horizontal beam's irradiance on a level bed is the water's), and HDRP's wave
        // buffers for one band next: a sine wave along x (a 3 cm height over a 0.4 m period, one period over the texture), whose
        // crests focus the refracted sun into lines on the floor where a ray traced on the CPU through the same surface puts them,
        // its mean the still water's (waves only move light).
        {
            const M4 pool=translate_scale({-0.3f, 0.4f, -0.4f}, {2.4f, 1.f, 1.6f}); // x -1.5..0.9, z -1.2..0.4
            const double period=0.4, amplitude=0.03; const int texels=128;
            auto fresnelExact=[](double c, double eta) {
                c=std::clamp(std::fabs(c), 0.0, 1.0); const double s2=(1.0-c*c)/(eta*eta); if(s2>=1.0) return 1.0;
                const double t=std::sqrt(1.0-s2), rs=(c-eta*t)/(c+eta*t), rp=(eta*c-t)/(eta*c+t); return 0.5*(rs*rs+rp*rp); };
            const double eta=1.3333;
            ComPtr<ID3D11Texture2D> waveDisplacement, waveGradient;
            {
                std::vector<float> disp(size_t(texels)*texels*4, 0.f), grad(size_t(texels)*texels*4, 0.f);
                for(int y=0;y<texels;++y) for(int x=0;x<texels;++x) {
                    const double phase=2.0*3.14159265358979*(double(x)+0.5)/texels;
                    disp[(size_t(y)*texels+x)*4]=float(amplitude*std::sin(phase)); // HDRP's x: the height
                    grad[(size_t(y)*texels+x)*4]=float(amplitude*2.0*3.14159265358979/period*std::cos(phase)); // its xy: dh/dx, dh/dz
                }
                D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=texels; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; d.SampleDesc.Count=1;
                d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA a{disp.data(), UINT(texels*16), 0}, b{grad.data(), UINT(texels*16), 0};
                expect(SUCCEEDED(device->CreateTexture2D(&d, &a, &waveDisplacement)) && SUCCEEDED(device->CreateTexture2D(&d, &b, &waveGradient)), "HDRP's wave buffers for the test");
            }
            // The slot: still water, or the sine wave (shifted along x by a share of its period).
            auto water=[&](bool on, bool waves, float shift=0.f, float top=0.4f) {
                TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI;
                c.worldToWater[0]=c.worldToWater[5]=c.worldToWater[10]=1.f;
                if(waves) {
                    c.displacement=uint64_t(uintptr_t(waveDisplacement.Get())); c.normals=uint64_t(uintptr_t(waveGradient.Get()));
                    c.bands[0][0]=float(1.0/period); c.bands[0][1]=shift; c.bands[0][3]=1.f; c.fades[0][0]=0.f; c.fades[0][1]=1.f; c.bandCount=1;
                    c.maxDisplacement=float(amplitude);
                }
                c.top=top; c.bottom=std::min(top, 0.4f);
                c.bounds[0]=-1.5f; c.bounds[1]=-1.2f; c.bounds[2]=0.9f; c.bounds[3]=0.4f;
                expect(SetWaterCaustics(on ? &c : nullptr, on ? 1u : 0u)==0, "the water's slot set, or cleared");
            };
            // The water's mesh in the scene, at its level: the water's surface is that mesh's triangles under the waves (PT-71
            // item 5), so where the water stands is where its mesh is, and the slot's top and bottom only say the same.
            auto water_scene=[&](bool on, float level=0.4f) {
                auto list=instances; auto listIndices=indices;
                if(on) {
                    const M4 at=level==0.4f ? pool : translate_scale({-0.3f, level, -0.4f}, {2.4f, 1.f, 1.6f});
                    TGPT_Instance w{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) w.transform[r*4+c]=at.m[r][c];
                    w.mesh=quadHandle; w.materialBase=uint32_t(listIndices.size()); w.mask=0xFF; w.flags=TGPT_INSTANCE_WATER; // slot 0
                    listIndices.push_back(0); list.push_back(w);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with water in it, or without");
            };
            const Camera poolCam=make_camera({-0.9f, 1.6f, -2.2f}, {-0.9f, 0.f, -0.5f}, 60.f, float(W)/float(H));
            // Undenoised, the same frames each time: what differs between two images is what the change made. The caustics' grid
            // moves every frame (its random offset): 24 frames' mean.
            auto image=[&](const Camera& c, uint32_t view, uint32_t options, uint32_t frames=40) {
                std::vector<double> acc(size_t(W)*H, 0.0);
                const uint32_t kept=frames-16;
                for(uint32_t i=0;i<frames;++i) {
                    run(view, c, c, i ? 7000+i : 0, TGPT_OPTION_NO_DENOISER|options);
                    if(i>=16) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=(0.2126*r[j*3]+0.7152*r[j*3+1]+0.0722*r[j*3+2])/double(kept); }
                }
                return acc;
            };
            auto stageMs=[&](const char* name) {
                char stages[1024]{};
                if(!GetStageTimes || GetStageTimes(stages, sizeof(stages))<=0) return -1.0;
                const char* at=std::strstr(stages, name);
                return at ? std::atof(at+std::strlen(name)) : -1.0;
            };
            // Each pixel's point on a level plane along its camera ray (pixel centres, as the tracer's rays).
            auto plane_point=[&](const Camera& c, UINT x, UINT y, float level) {
                const float cl[4]={(float(x)+0.5f)/float(W)*2.f-1.f, 1.f-(float(y)+0.5f)/float(H)*2.f, 1.f, 1.f};
                float w[4]{}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) w[r]+=c.invViewProj.m[r][k]*cl[k];
                const V3 dir=normalize(V3{w[0]/w[3], w[1]/w[3], w[2]/w[3]});
                return (dir.y<-1e-3f) == (c.position.y>level) ? c.position+dir*((level-c.position.y)/dir.y) : V3{1e9f, 0, 1e9f};
            };
            expect(SetLights(nullptr, 0)==0, "the sun alone");
            const auto open=image(poolCam, TGPT_VIEW_DIRECT, 0);
            water_scene(true); water(true, false);
            image(poolCam, TGPT_VIEW_DIRECT, 0); // the scene's new instance is built over the first frames
            const auto still=image(poolCam, TGPT_VIEW_DIRECT, 0), stillOff=image(poolCam, TGPT_VIEW_DIRECT, TGPT_OPTION_NO_CAUSTICS);
            water(true, true);
            const auto waved=image(poolCam, TGPT_VIEW_DIRECT, 0, 64);
            const double msCaustics=stageMs("caustics "), msComposite=stageMs("composite ");
            water(true, true, 0.25f); // a quarter period on
            const auto moved=image(poolCam, TGPT_VIEW_DIRECT, 0, 64);
            // Water that tops out below the floor: dry land, nothing changes there. Its mesh is moved with what its slot says (the
            // surface a ray meets is the mesh's triangles: left 0.4 m up under a slot that said -0.1, they stopped the sun's ray
            // over a floor no caustic reached, and the floor was black).
            water_scene(true, -0.1f); water(true, true, 0.f, -0.1f);
            image(poolCam, TGPT_VIEW_DIRECT, 0); // the scene's moved instance is built over the first frames
            const auto below=image(poolCam, TGPT_VIEW_DIRECT, 0);
            // What it costs over open water: a 120 m sea with the sine wave under the camera, the sun on it (every grid level
            // holds water), the caustics stage over 100 frames, the least of three (other work on the GPU only adds).
            double seaMs=1e9;
            {
                const M4 sea=translate_scale({-0.3f, 0.4f, -0.4f}, {120.f, 1.f, 120.f});
                auto list=instances; auto listIndices=indices;
                TGPT_Instance w{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) w.transform[r*4+c]=sea.m[r][c];
                w.mesh=quadHandle; w.materialBase=uint32_t(listIndices.size()); w.mask=0xFF; w.flags=TGPT_INSTANCE_WATER;
                listIndices.push_back(0); list.push_back(w);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "a sea under the room");
                TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI; c.worldToWater[0]=c.worldToWater[5]=c.worldToWater[10]=1.f;
                c.displacement=uint64_t(uintptr_t(waveDisplacement.Get())); c.normals=uint64_t(uintptr_t(waveGradient.Get()));
                c.bands[0][0]=float(1.0/period); c.bands[0][3]=1.f; c.fades[0][1]=1.f; c.bandCount=1; c.maxDisplacement=float(amplitude);
                c.top=c.bottom=0.4f; c.bounds[0]=-60.3f; c.bounds[1]=-60.4f; c.bounds[2]=59.7f; c.bounds[3]=59.6f;
                expect(SetWaterCaustics(&c, 1u)==0, "the sea's slot");
                // And the wet ground's store over it (pt_wet.hlsl: every level's cells hold water), in the prepare stage, against the
                // same frames with the ground kept dry.
                double seaPrepare=1e9, seaPrepareDry=1e9;
                for(int rep=0;rep<3;++rep) {
                    for(uint32_t i=0;i<100;++i) run(TGPT_VIEW_NONE, poolCam, poolCam, i ? 8000+i : 0, TGPT_OPTION_NO_DENOISER);
                    seaMs=std::min(seaMs, stageMs("caustics ")); seaPrepare=std::min(seaPrepare, stageMs("prepare "));
                    for(uint32_t i=0;i<100;++i) run(TGPT_VIEW_NONE, poolCam, poolCam, 8000+i, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_DRY_GROUND);
                    seaPrepareDry=std::min(seaPrepareDry, stageMs("prepare "));
                }
                std::printf("      a 120 m sea with waves under the sun: the caustics stage %.3f ms (%ux%u); the prepare stage %.3f ms with the wet ground's store, %.3f ms without\n",
                            seaMs, unsigned(W), unsigned(H), seaPrepare, seaPrepareDry);
                water_scene(true); water(true, false);
            }
            // What it costs at a cave lake's scale (the harness's lantern-lake, where the caustics' lookup took 40 to 50 ms of the
            // composite): water over the whole room's floor (11.8 m square, 0.4 m up) under the sine wave, the sun off, a lamp 0.35 m
            // over it 3 m before the back wall, the camera 7 m off. The caustics' and the composite's GPU time over 150 frames, with
            // the Caustics row and without, the least of three each.
            double lakeCaustics=1e9, lakeOn=1e9, lakeOff=1e9;
            {
                const M4 lake=translate_scale({0.f, 0.4f, 0.f}, {11.8f, 1.f, 11.8f});
                auto list=instances; auto listIndices=indices;
                TGPT_Instance w{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) w.transform[r*4+c]=lake.m[r][c];
                w.mesh=quadHandle; w.materialBase=uint32_t(listIndices.size()); w.mask=0xFF; w.flags=TGPT_INSTANCE_WATER;
                listIndices.push_back(0); list.push_back(w);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "a lake over the room's floor");
                TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI; c.worldToWater[0]=c.worldToWater[5]=c.worldToWater[10]=1.f;
                c.displacement=uint64_t(uintptr_t(waveDisplacement.Get())); c.normals=uint64_t(uintptr_t(waveGradient.Get()));
                c.bands[0][0]=float(1.0/period); c.bands[0][3]=1.f; c.fades[0][1]=1.f; c.bandCount=1; c.maxDisplacement=float(amplitude);
                c.top=c.bottom=0.4f; c.bounds[0]=-5.9f; c.bounds[1]=-5.9f; c.bounds[2]=5.9f; c.bounds[3]=5.9f;
                expect(SetWaterCaustics(&c, 1u)==0, "the lake's slot");
                // The lamp, and 24 torches round the lake's edge 0.5 to 1.5 m up (their light meets the water at a slant: its
                // reflection lands on the far walls in narrow bands, many triangles to a spot).
                std::vector<TGPT_Light> lamps;
                TGPT_Light lamp{}; lamp.position[0]=0.f; lamp.position[1]=0.75f; lamp.position[2]=3.f; lamp.type=0;
                lamp.color[0]=40.f; lamp.color[1]=26.f; lamp.color[2]=12.f; lamp.range=12.f; lamp.radius=0.04f; lamp.id=760;
                lamps.push_back(lamp);
                for(int i=0;i<24;++i) {
                    TGPT_Light t=lamp; const int side=i/6; const float s=-4.5f+1.8f*float(i%6);
                    t.position[0]=side==0 ? -5.7f : side==1 ? 5.7f : s; t.position[2]=side==2 ? 5.7f : side==3 ? -5.7f : s;
                    t.position[1]=0.5f+0.2f*float(i%6); t.color[0]=8.f; t.color[1]=5.f; t.color[2]=2.f; t.id=761+uint32_t(i);
                    lamps.push_back(t);
                }
                expect(SetLights(lamps.data(), uint32_t(lamps.size()))==0, "a lamp over the lake, torches round it");
                const bool sunWas=sunOn; sunOn=false;
                const Camera lakeCam=make_camera({0.5f, 1.0f, -5.f}, {0.f, 1.0f, 5.5f}, 60.f, float(W)/float(H));
                for(int rep=0;rep<3;++rep) {
                    for(uint32_t i=0;i<150;++i) run(TGPT_VIEW_NONE, lakeCam, lakeCam, i ? 8200+i : 0, TGPT_OPTION_NO_DENOISER);
                    const double cm=stageMs("caustics "); lakeCaustics=std::min(lakeCaustics, cm); lakeOn=std::min(lakeOn, cm+stageMs("composite "));
                    for(uint32_t i=0;i<150;++i) run(TGPT_VIEW_NONE, lakeCam, lakeCam, i ? 8400+i : 0, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CAUSTICS);
                    lakeOff=std::min(lakeOff, stageMs("caustics ")+stageMs("composite "));
                }
                std::printf("      a lamp over a lake: the caustics stage %.3f ms, with the composite %.3f ms; with the option off %.3f ms (%ux%u)\n",
                            lakeCaustics, lakeOn, lakeOff, unsigned(W), unsigned(H));
                sunOn=sunWas;
                // The world-space hash this lookup replaces took 33.7 ms here with the composite (and 40 to 50 ms of the composite
                // in the game's cave lake, run 20261007-140305); the acceleration structure's, a millisecond or two.
                expect(lakeOn<10.0, "the caustics and their lookup stay affordable where 25 lamps light a lake (under 10 ms here; the hash took 33.7 ms)");
                expect(SetLights(nullptr, 0)==0, "the sun alone again");
                water_scene(true); water(true, false);
            }
            // HDRP's real waves, end to end (the review's "production handoff": until 2026-10-07 the plugin read HDRP's wave buffers
            // with the wrong type, no slot ever got its waves, and no check saw it: every check above hands over a plain 2D
            // texture with one band). The buffers as the plugin hands HDRP's over (WaterCaustics.cs: the texture behind each
            // RTHandle): texture arrays of half floats, a slice a band, with a mip chain (whose lower mips hold a wrong value here:
            // the top one is the waves), three bands with their own patch sizes, offsets, amplitudes and distance fades, the
            // surface rotated and moved in the world. The tracer says in its log where its waves put the surface at one point of
            // its grid a frame (TGPT_LOG_WATER_SAMPLES), and there HDRP's formula worked out here must give the same point and
            // the same normal: per band, the surface-space position times the band's scale less its offset samples the
            // displacement (bilinear: x the height, yz the horizontal move times -2.25) and the surface gradient (a cubic
            // B-spline), times the amplitude and the fade (the displacement) or the fade (the gradient).
            {
                const int size=64, bandCount=3, mips=7;
                struct Band { int k, l; double height, phase, moveX, moveZ, scale, offsetX, offsetZ, amplitude, fadeA, fadeB; };
                const Band band[bandCount]={{1, 0, 0.020, 0.3, 0.012, 0.008, 1.0/3.2, 0.11, 0.37, 1.0, 0.0, 1.0},
                                            {2, 1, 0.008, 1.1, 0.004, 0.006, 1.0/0.9, 0.52, 0.05, 0.8, 0.0, 1.0},
                                            {3, -2, 0.003, 2.2, 0.002, 0.001, 1.0/0.35, 0.71, 0.23, 1.2, -0.25, 1.2}};
                // The texels as the GPU holds them (half floats): [band][texel][channel].
                std::vector<float> disp(size_t(bandCount)*size*size*4, 0.f), grad(size_t(bandCount)*size*size*4, 0.f);
                std::vector<uint16_t> dispHalf(disp.size()), gradHalf(grad.size());
                for(int b=0;b<bandCount;++b) for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
                    const double arg=2.0*3.14159265358979*(band[b].k*(x+0.5)/size+band[b].l*(y+0.5)/size)+band[b].phase;
                    const size_t at=((size_t(b)*size+y)*size+x)*4;
                    const double v[4]={band[b].height*std::sin(arg), band[b].moveX*std::cos(arg), band[b].moveZ*std::sin(arg+0.7), 0.0};
                    const double slope=band[b].height*2.0*3.14159265358979*band[b].scale*std::cos(arg);
                    const double gv[4]={slope*band[b].k, slope*band[b].l, 0.0, 0.0};
                    for(int c=0;c<4;++c) {
                        dispHalf[at+c]=float_to_half(float(v[c])); disp[at+c]=half_to_float(dispHalf[at+c]);
                        gradHalf[at+c]=float_to_half(float(gv[c])); grad[at+c]=half_to_float(gradHalf[at+c]);
                    }
                }
                ComPtr<ID3D11Texture2D> dispArray, gradArray;
                {
                    D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=mips; d.ArraySize=bandCount; d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count=1;
                    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS; d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
                    expect(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &dispArray)) && SUCCEEDED(device->CreateTexture2D(&d, nullptr, &gradArray)), "HDRP's wave buffers as it makes them: texture arrays with mips");
                    std::vector<uint16_t> wrong(size_t(size)*size*4, float_to_half(7.f));
                    for(int b=0;b<bandCount && dispArray && gradArray;++b) for(int m=0;m<mips;++m) {
                        const UINT sub=UINT(m+b*mips);
                        context->UpdateSubresource(dispArray.Get(), sub, nullptr, m ? wrong.data() : &dispHalf[size_t(b)*size*size*4], UINT((size>>m)*8), 0);
                        context->UpdateSubresource(gradArray.Get(), sub, nullptr, m ? wrong.data() : &gradHalf[size_t(b)*size*size*4], UINT((size>>m)*8), 0);
                    }
                }
                auto texel=[&](const std::vector<float>& t, int b, int x, int y, int c) {
                    x=((x%size)+size)%size; y=((y%size)+size)%size;
                    return double(t[((size_t(b)*size+y)*size+x)*4+c]);
                };
                auto bilinear=[&](const std::vector<float>& t, int b, double u, double v, int c) {
                    const double x=u*size-0.5, y=v*size-0.5; const int ix=int(std::floor(x)), iy=int(std::floor(y)); const double fx=x-ix, fy=y-iy;
                    return (texel(t, b, ix, iy, c)*(1-fx)+texel(t, b, ix+1, iy, c)*fx)*(1-fy)+(texel(t, b, ix, iy+1, c)*(1-fx)+texel(t, b, ix+1, iy+1, c)*fx)*fy;
                };
                auto bspline=[&](const std::vector<float>& t, int b, double u, double v, int c) {
                    const double x=u*size-0.5, y=v*size-0.5; const int ix=int(std::floor(x)), iy=int(std::floor(y)); const double fx=x-ix, fy=y-iy;
                    auto weights=[](double f, double w[4]) { const double f2=f*f, f3=f2*f; w[0]=(1-3*f+3*f2-f3)/6; w[1]=(4-6*f2+3*f3)/6; w[2]=(1+3*f+3*f2-3*f3)/6; w[3]=f3/6; };
                    double wx[4], wy[4]; weights(fx, wx); weights(fy, wy);
                    double sum=0; for(int j=0;j<4;++j) for(int i=0;i<4;++i) sum+=wx[i]*wy[j]*texel(t, b, ix-1+i, iy-1+j, c);
                    return sum;
                };
                const float top=0.4f;
                struct Surface { double p[3], n[3], moved, slope; };
                // HDRP's formula at an undisplaced point of the level water (world x, z), for a surface turned by `turn` about the
                // world's up and moved by (tx, tz), its bands' offsets moved on by `time`, `bandsOn` of the bands.
                auto hdrp=[&](double x, double z, double turn, double tx, double tz, double time, int bandsOn) {
                    const double cs=std::cos(turn), sn=std::sin(turn);
                    const double r[3][3]={{cs, 0, sn}, {0, 1, 0}, {-sn, 0, cs}}; // world to water: its rows
                    const double sx=r[0][0]*x+r[0][1]*top+r[0][2]*z+tx, sz=r[2][0]*x+r[2][1]*top+r[2][2]*z+tz;
                    const double dx=x-poolCam.position.x, dy=double(top)-poolCam.position.y, dz=z-poolCam.position.z, distance=std::sqrt(dx*dx+dy*dy+dz*dz);
                    double move[3]={0, 0, 0}, slope[2]={0, 0};
                    for(int b=0;b<bandsOn;++b) {
                        const double u=sx*band[b].scale-(band[b].offsetX+time*(0.31+0.17*b)), v=sz*band[b].scale-(band[b].offsetZ-time*(0.23+0.11*b));
                        double fade=std::clamp(distance*band[b].fadeA+band[b].fadeB, 0.0, 1.0); fade*=fade; fade=fade*fade*(3.0-2.0*fade);
                        const double k=band[b].amplitude*fade;
                        move[0]+=-2.25*bilinear(disp, b, u, v, 1)*k; move[1]+=bilinear(disp, b, u, v, 0)*k; move[2]+=-2.25*bilinear(disp, b, u, v, 2)*k;
                        slope[0]+=bspline(grad, b, u, v, 0)*fade; slope[1]+=bspline(grad, b, u, v, 1)*fade;
                    }
                    double n[3]={-slope[0], 1.0, -slope[1]}; const double nl=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                    Surface s{};
                    const double base[3]={x, double(top), z};
                    for(int a=0;a<3;++a) {
                        s.p[a]=base[a]+r[0][a]*move[0]+r[1][a]*move[1]+r[2][a]*move[2];
                        s.n[a]=(r[0][a]*n[0]+r[1][a]*n[1]+r[2][a]*n[2])/nl;
                    }
                    s.moved=std::sqrt(move[0]*move[0]+move[1]*move[1]+move[2]*move[2]); s.slope=std::sqrt(slope[0]*slope[0]+slope[1]*slope[1]);
                    return s;
                };
                auto slot=[&](double turn, double tx, double tz, double time, int bandsOn) {
                    TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI;
                    const float cs=float(std::cos(turn)), sn=float(std::sin(turn));
                    c.worldToWater[0]=cs; c.worldToWater[2]=sn; c.worldToWater[3]=float(tx); c.worldToWater[5]=1.f; c.worldToWater[8]=-sn; c.worldToWater[10]=cs; c.worldToWater[11]=float(tz);
                    if(bandsOn) { c.displacement=uint64_t(uintptr_t(dispArray.Get())); c.normals=uint64_t(uintptr_t(gradArray.Get())); }
                    for(int b=0;b<bandsOn;++b) {
                        c.bands[b][0]=float(band[b].scale); c.bands[b][1]=float(band[b].offsetX+time*(0.31+0.17*b)); c.bands[b][2]=float(band[b].offsetZ-time*(0.23+0.11*b));
                        c.bands[b][3]=float(band[b].amplitude); c.fades[b][0]=float(band[b].fadeA); c.fades[b][1]=float(band[b].fadeB);
                    }
                    c.bandCount=uint32_t(bandsOn); c.maxDisplacement=0.12f; c.top=c.bottom=top;
                    c.bounds[0]=-1.5f; c.bounds[1]=-1.2f; c.bounds[2]=0.9f; c.bounds[3]=0.4f;
                    return SetWaterCaustics(&c, 1u)==0;
                };
                // The tracer's last sample in its log: a grid point (world x, z), the surface there, its normal.
                const fs::path nativeLog=root/"TGPathTracing"/"TGPathTracing.Native.log";
                auto lastSample=[&](double v[8]) {
                    std::ifstream in(nativeLog); std::string line, found;
                    while(std::getline(in, line)) if(line.find("Water slot 0 sample:")!=std::string::npos) found=line;
                    const size_t at=found.find("sample: at x ");
                    return at!=std::string::npos && std::sscanf(found.c_str()+at, "sample: at x %lf z %lf the surface is at %lf %lf %lf, its normal %lf %lf %lf",
                                                               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7])==8;
                };
                SetEnvironmentVariableA("TGPT_LOG_WATER_SAMPLES", "1");
                int states=0, sampled=0, slotsSet=0; double worstPoint=0, worstNormal=0, leastMoved=1e9, mostMoved=0, mostSlope=0, stillOff=1e9; uint32_t frame=8600;
                double worst[8]{}; Surface worstWant{};
                for(int state=0;state<14;++state) {
                    const bool still=state==13; // the last: a slot without wave buffers is still water
                    const double turn=0.5+0.37*state, tx=0.4-0.3*state, tz=-0.3+0.2*state, time=0.13*state; const int bandsOn=still ? 0 : state<2 ? 1+state : 3;
                    slotsSet+=slot(turn, tx, tz, time, bandsOn) ? 1 : 0;
                    for(int i=0;i<10;++i) run(TGPT_VIEW_NONE, poolCam, poolCam, frame++, TGPT_OPTION_NO_DENOISER);
                    ++states;
                    double v[8];
                    if(!lastSample(v)) continue;
                    ++sampled;
                    const Surface want=hdrp(v[0], v[1], turn, tx, tz, time, bandsOn);
                    const double off=std::sqrt((v[2]-want.p[0])*(v[2]-want.p[0])+(v[3]-want.p[1])*(v[3]-want.p[1])+(v[4]-want.p[2])*(v[4]-want.p[2]));
                    const double cosine=std::clamp(v[5]*want.n[0]+v[6]*want.n[1]+v[7]*want.n[2], -1.0, 1.0), angle=std::acos(cosine)*180.0/3.14159265358979;
                    if(still) { stillOff=std::max(std::fabs(v[3]-double(top)), 1.0-v[6]); continue; }
                    if(off>worstPoint || angle>worstNormal) { std::copy(v, v+8, worst); worstWant=want; }
                    worstPoint=std::max(worstPoint, off); worstNormal=std::max(worstNormal, angle);
                    leastMoved=std::min(leastMoved, want.moved); mostMoved=std::max(mostMoved, want.moved); mostSlope=std::max(mostSlope, want.slope);
                }
                SetEnvironmentVariableA("TGPT_LOG_WATER_SAMPLES", nullptr);
                std::printf("      HDRP's waves end to end: %d wave states (three bands in texture arrays with mips, the surface turned and moved, the bands' offsets moving, a distance fade), %d sampled by the tracer;"
                            " its surface is off HDRP's formula by %.4f mm at most, its normal by %.4f degrees (the waves there move the surface %.1f to %.1f mm, slopes to %.3f);"
                            " without wave buffers the surface is %.6f off its level\n", states, sampled, worstPoint*1000.0, worstNormal, leastMoved*1000.0, mostMoved*1000.0, mostSlope, stillOff);
                std::printf("      the worst: at x %.4f z %.4f the tracer has %.5f %.5f %.5f normal %.4f %.4f %.4f, HDRP's formula %.5f %.5f %.5f normal %.4f %.4f %.4f\n",
                            worst[0], worst[1], worst[2], worst[3], worst[4], worst[5], worst[6], worst[7], worstWant.p[0], worstWant.p[1], worstWant.p[2], worstWant.n[0], worstWant.n[1], worstWant.n[2]);
                expect(slotsSet==states && sampled==states && mostMoved>0.01 && mostSlope>0.1, "the waves' check has waves: every state sampled, the surface moved by centimetres");
                expect(sampled==states && worstPoint<0.0003 && worstNormal<0.2,
                       "HDRP's waves reach the tracer as HDRP makes them: at a grid point the tracer's surface and normal are HDRP's formula's (three bands, a turned surface, the horizontal move, the fade)");
                expect(stillOff<1e-5, "a slot without wave buffers is still water at its level");
                water(true, false);
            }
            {
                std::vector<float> img(size_t(W)*H*3);
                double top=0; for(double v:open) top=std::max(top, v);
                for(size_t j=0;j<waved.size();++j) img[j*3]=img[j*3+1]=img[j*3+2]=float(waved[j]/std::max(top, 1e-9));
                save(img, "42-water-caustics.png", true);
                // What the waves changed against still water: red more light, green less (x10).
                for(size_t j=0;j<waved.size();++j) { const double d=(waved[j]-still[j])/std::max(top, 1e-9)*10.0; img[j*3]=float(std::max(d, 0.0)); img[j*3+1]=float(std::max(-d, 0.0)); img[j*3+2]=0.f; }
                save(img, "42b-water-caustics-waves.png", true);
            }
            // The water's transmittance of the sun, and the CPU's caustic: the sine surface traced a million rays a period, its
            // refracted light binned along x on the floor (1 cm bins, folded over one period), in the direct view's units.
            const double sunCos=double(sunDir.y), transmitted=1.0-fresnelExact(sunCos, eta);
            const int bins=40; const double binWidth=period/bins;
            std::vector<double> reference(bins, 0.0);
            {
                const int rays=1000000;
                for(int i=0;i<rays;++i) {
                    const double x=(double(i)+0.5)/rays*period, k=2.0*3.14159265358979/period;
                    const double h=amplitude*std::sin(k*x), slope=amplitude*k*std::cos(k*x);
                    const double nl=std::sqrt(slope*slope+1.0), nx=-slope/nl, ny=1.0/nl;
                    const double cosIn=nx*sunDir.x+ny*sunDir.y;
                    if(cosIn<=0.0) continue;
                    // refract(-s, n, 1 / eta)
                    const double r=1.0/eta, k2=1.0-r*r*(1.0-cosIn*cosIn);
                    if(k2<0.0) continue;
                    const double tx=-r*sunDir.x+(r*cosIn-std::sqrt(k2))*nx, ty=-r*sunDir.y+(r*cosIn-std::sqrt(k2))*ny;
                    const double land=x+tx*(0.4+h)/(-ty);
                    const double flux=double(sunLux)*(1.0-fresnelExact(cosIn, eta))*(-slope*sunDir.x+sunDir.y)*(period/rays);
                    double f=std::fmod(land, period); if(f<0) f+=period;
                    reference[std::min(int(f/binWidth), bins-1)]+=flux;
                }
                for(double& v:reference) v=v/binWidth/3.14159265*double(exposure);
            }
            // The floor in full sun, in the direct view's units: what a pixel the sun lights fully shows if it is the floor (the
            // water's waves throw reflected light onto the walls and the box beside the pool, which is right; never onto the floor),
            // on the near side (the far floor lies in a checkered card's shadow, which lets the sun through by chance: noise).
            const double sunFloor=double(sunLux)*double(sunDir.y)/3.14159265*double(exposure);
            int under=0, onT=0, outside=0; double ratioSum=0, offWorst=0, outsideWorst=0, belowWorst=0, worstX=0, worstZ=0, worstOpen=0, worstStill=0, outsideOpen=0, outsideWaved=0;
            std::vector<double> profile(bins, 0.0), profileStill(bins, 0.0), profileMoved(bins, 0.0); std::vector<int> profileCount(bins, 0);
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const size_t j=size_t(y)*W+x;
                const V3 f=plane_point(poolCam, x, y, 0.f);
                if(f.x>=-1.3f && f.x<=-0.5f && f.z>=-0.9f && f.z<=-0.1f && open[j]>0.0) {
                    ++under;
                    const double ratio=still[j]/open[j];
                    ratioSum+=ratio; if(std::fabs(ratio-transmitted)<0.03*transmitted) ++onT;
                    offWorst=std::max(offWorst, std::fabs(stillOff[j]/open[j]-1.0));
                    belowWorst=std::max(belowWorst, std::fabs(below[j]/open[j]-1.0));
                    double m=std::fmod(double(f.x), period); if(m<0) m+=period;
                    const int bin=std::min(int(m/binWidth), bins-1);
                    profile[bin]+=waved[j]; profileStill[bin]+=still[j]; profileMoved[bin]+=moved[j]; ++profileCount[bin];
                } else if((f.x<-2.4f || f.z<-2.4f) && f.z<0.f && std::fabs(f.x)<5.8f && std::fabs(f.z)<5.8f && std::fabs(open[j]-sunFloor)<0.03*sunFloor) {
                    ++outside; outsideOpen+=open[j]; outsideWaved+=waved[j];
                    if(std::fabs(waved[j]/open[j]-1.0)>outsideWorst) { outsideWorst=std::fabs(waved[j]/open[j]-1.0); worstX=f.x; worstZ=f.z; worstOpen=open[j]; worstStill=still[j]; }
                }
            }
            double mean=0, meanStill=0, meanRef=0, peak=0, peakRef=0, meanMoved=0; int peakAt=0, peakRefAt=0, peakMovedAt=0; double peakMoved=0; bool filled=true;
            for(int i=0;i<bins;++i) {
                if(profileCount[i]==0) { filled=false; continue; }
                profile[i]/=profileCount[i]; profileStill[i]/=profileCount[i]; profileMoved[i]/=profileCount[i];
                mean+=profile[i]/bins; meanStill+=profileStill[i]/bins; meanRef+=reference[i]/bins; meanMoved+=profileMoved[i]/bins;
                if(profile[i]>peak) { peak=profile[i]; peakAt=i; }
                if(reference[i]>peakRef) { peakRef=reference[i]; peakRefAt=i; }
                if(profileMoved[i]>peakMoved) { peakMoved=profileMoved[i]; peakMovedAt=i; }
            }
            auto circular=[&](int a, int b) { const int d=std::abs(a-b)%bins; return std::min(d, bins-d); };
            const int shiftExpected=bins/4; // the wave a quarter period on: its lines too
            std::printf("      caustics under water: %d floor pixels under the still water, %.4f of the open floor's sun on average (the water's transmittance %.4f), %d within 3%%;"
                        " with the option off %.5f at most; the sine wave: its mean %.4f of the still water's (the CPU's %.4f), its brightest bin %.2f x the mean at %.3f m (the CPU's %.2f x at %.3f m),"
                        " a quarter period on at %.3f m; dry land: %d pixels change %.5f on average (one %.4f: the sun's own noise there), %.5f with the water topping out below the floor; caustics %.3f ms, composite %.3f ms\n",
                        under, under ? ratioSum/under : 0.0, transmitted, onT, offWorst, meanStill>0 ? mean/meanStill : 0.0, meanStill>0 ? meanRef/meanStill : 0.0,
                        mean>0 ? peak/mean : 0.0, (peakAt+0.5)*binWidth, meanRef>0 ? peakRef/meanRef : 0.0, (peakRefAt+0.5)*binWidth, (peakMovedAt+0.5)*binWidth,
                        outside, outsideOpen>0 ? outsideWaved/outsideOpen-1.0 : 0.0, outsideWorst, belowWorst, msCaustics, msComposite);
            {
                std::printf("      dry land's worst: floor x %.2f z %.2f, open %.4f, still water %.4f\n", worstX, worstZ, worstOpen, worstStill);
                std::printf("      profile (GPU / CPU / still, x 1000):");
                for(int i=0;i<bins;i+=2) std::printf(" %.0f/%.0f/%.0f", profile[i]*1000, reference[i]*1000, profileStill[i]*1000);
                std::printf("\n");
            }
            expect(under>5000 && std::fabs(ratioSum/under-transmitted)<0.02*transmitted && onT>0.95*under,
                   "under still water the floor takes the sun through the surface: the water's Fresnel transmittance of it, no straight light");
            expect(offWorst<0.002, "with TGPT_OPTION_NO_CAUSTICS the water stops no ray and the light under it is the straight light");
            expect(filled && std::fabs(mean/meanStill-1.0)<0.02 && std::fabs(meanRef/meanStill-1.0)<0.02, "waves only move the light: the floor's mean under them is the still water's");
            expect(filled && peak/mean>2.5 && circular(peakAt, peakRefAt)<=1 && peak/mean>0.7*peakRef/meanRef && peak/mean<1.3*peakRef/meanRef,
                   "the sine wave's crests focus the sun into lines where the CPU's rays put them, as bright");
            expect(filled && circular(peakMovedAt, (peakAt+shiftExpected)%bins)<=1, "the lines move with the waves");
            expect(outside>20000 && std::fabs(outsideWaved/outsideOpen-1.0)<0.002, "no change where the light crosses no water");
            expect(belowWorst<0.002, "no water below the floor's level: nothing changes on it");
            // Sunlight reflected off the water: a wall standing at the pool's edge, facing it and a camera beside the pool. Still
            // water is the mirror's limit: the wall takes the sun's mirror image in the water where the mirrored path reaches the
            // lit pool, the water's Fresnel reflectance at the sun's angle (exact, an index of 1.333) of the sun on the wall's
            // cosine towards the water. None where the path misses the water, none from water the sun doesn't reach, none with the
            // option off. The sine wave splits it into bright lines and dark gaps.
            {
                const M4 wall=mul(translate_scale({-1.6f, 1.3f, -1.f}, {1.f, 1.8f, 2.f}), rot_z(-pi/2)); // x = -1.6, y 0.4..2.2, z -2..0, facing +x
                const Camera wallCam=make_camera({0.2f, 1.3f, -1.f}, {-1.6f, 1.3f, -1.f}, 60.f, float(W)/float(H));
                objects.push_back(Object{0, wall, {0}});
                auto scene=[&](bool on) {
                    auto list=instances; auto listIndices=indices;
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=wall.m[r][c];
                    q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; listIndices.push_back(0); list.push_back(q);
                    if(on) {
                        TGPT_Instance w{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) w.transform[r*4+c]=pool.m[r][c];
                        w.mesh=quadHandle; w.materialBase=uint32_t(listIndices.size()); w.mask=0xFF; w.flags=TGPT_INSTANCE_WATER;
                        listIndices.push_back(0); list.push_back(w);
                    }
                    expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the wall by the pool, with the water or without");
                };
                scene(false); water(false, false);
                image(wallCam, TGPT_VIEW_DIRECT, 0); // the wall's instance is built over the first frames
                const auto dry=image(wallCam, TGPT_VIEW_DIRECT, 0);
                scene(true); water(true, false);
                image(wallCam, TGPT_VIEW_DIRECT, 0);
                const auto mirror=image(wallCam, TGPT_VIEW_DIRECT, 0), mirrorOff=image(wallCam, TGPT_VIEW_DIRECT, TGPT_OPTION_NO_CAUSTICS);
                water(true, true);
                const auto rippled=image(wallCam, TGPT_VIEW_DIRECT, 0);
                water(false, false); scene(false); objects.pop_back();
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                {
                    std::vector<float> img(size_t(W)*H*3);
                    for(size_t j=0;j<rippled.size();++j) img[j*3]=img[j*3+1]=img[j*3+2]=float(rippled[j]);
                    save(img, "43-water-reflected-sun.png", true);
                }
                const V3 m={sunDir.x, -sunDir.y, sunDir.z};
                const double expected=double(sunLux)*fresnelExact(sunCos, eta)*double(m.x)/3.14159265*double(exposure);
                int lit=0, onValue=0, missed=0, bright=0, dark=0, shadedWater=0; double brightest=0, missedMost=0, offMost=0, shadedMost=0, rippledSum=0, mirrorSum=0;
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const size_t j=size_t(y)*W+x;
                    offMost=std::max(offMost, std::fabs(mirrorOff[j]-dry[j]));
                    const float c[4]={(float(x)+0.5f)/float(W)*2.f-1.f, 1.f-(float(y)+0.5f)/float(H)*2.f, 1.f, 1.f};
                    float wv[4]{}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) wv[r]+=wallCam.invViewProj.m[r][k]*c[k];
                    const V3 dir=normalize(V3{wv[0]/wv[3], wv[1]/wv[3], wv[2]/wv[3]});
                    if(dir.x>-1e-3f) continue;
                    const V3 hit=wallCam.position+dir*((-1.6f-wallCam.position.x)/dir.x);
                    if(hit.y<0.5f || hit.y>2.1f || hit.z<-1.9f || hit.z>-0.1f) continue;
                    const float t=(hit.y-0.4f)/sunDir.y;
                    const float wx=hit.x+m.x*t, wz=hit.z+m.z*t;
                    const double added=mirror[j]-dry[j];
                    if(wx>=-1.45f && wx<=0.85f && wz>=-1.f && wz<=0.35f) {
                        ++lit; brightest=std::max(brightest, added);
                        if(std::fabs(added-expected)<0.03*expected) ++onValue;
                        const double band=rippled[j]-dry[j];
                        rippledSum+=band; mirrorSum+=added;
                        if(band>1.5*expected) ++bright; else if(band<0.5*expected) ++dark;
                    } else if(wx<-1.55f || wz>0.45f || wz<-1.25f) { ++missed; missedMost=std::max(missedMost, std::fabs(added)); }
                    // The pool's strip the ceiling shades from the sun (its light reaches the sky past the ceiling's back edge
                    // only from z -1.08 on): the mirror there holds no sun, 5 cm from that edge on (the sun's own penumbra there,
                    // 1.2 cm, and the grid's: a cell of the water half in the sun ramps its light across its landed triangle, 2 cm).
                    else if(wx>=-1.45f && wx<=0.85f && wz>=-1.19f && wz<=-1.13f) { ++shadedWater; shadedMost=std::max(shadedMost, std::fabs(added)); }
                }
                std::printf("      sunlight reflected off the water onto a wall: %d pixels see the lit pool, %d of them get the still mirror's %.5f within 3%% (brightest %.5f);"
                            " under the sine wave %d bright and %d dark, %.3f of the still mirror's light; %d see no water, %.6f at most; %d see water the sun doesn't reach, %.6f at most;"
                            " with the option off %.6f\n", lit, onValue, expected, brightest, bright, dark, mirrorSum>0 ? rippledSum/mirrorSum : 0.0, missed, missedMost, shadedWater, shadedMost, offMost);
                expect(lit>5000 && onValue>0.9*lit && brightest<1.05*expected, "a wall facing still water takes the sun's mirror image in it, by the water's Fresnel reflectance");
                expect(bright>0.1*lit && dark>0.1*lit, "the waves split the reflected sun into bright lines and dark gaps");
                expect(missed>2000 && missedMost<0.02*expected, "no reflected light where the mirrored path misses the water");
                expect(shadedWater>500 && shadedMost<0.02*expected, "no reflected light from water the sun doesn't reach");
                expect(offMost<0.02*expected, "no reflected water light with TGPT_OPTION_NO_CAUSTICS");
            }
            // Lamplight reflected off the water: the same wall at the pool's edge, the sun off, and a spot 0.6 m over the pool
            // pointing down (its cone, 60 degrees, keeps its own light off the wall). Still water is the mirror's limit: the wall
            // takes the lamp's mirror image in the water, worked out for every pixel (what the lamp sends to the wall point's mirror
            // image under the water, I / d^2 over the whole path, its cone along the way to the water, the water's exact Fresnel
            // reflectance at the lamp's angle on it, the wall's cosine towards the reflection point). None where the mirrored path
            // misses the pool, none with the option off, none behind a card standing in the pool before half the wall (the wall to
            // the water), none under a card just below the lamp (the water to the lamp), none from the pool moved behind the wall.
            {
                const M4 wall=mul(translate_scale({-1.6f, 1.3f, -1.f}, {1.f, 1.8f, 2.f}), rot_z(-pi/2)); // x = -1.6, y 0.4..2.2, z -2..0, facing +x
                const Camera wallCam=make_camera({0.2f, 1.3f, -1.f}, {-1.6f, 1.3f, -1.f}, 60.f, float(W)/float(H));
                const V3 lampAt={-0.3f, 1.0f, -0.6f};
                const float lampI=60.f, lampRange=10.f, lampRadius=0.001f, cosOuter=0.5f, cosInner=0.6427876f; // a point light: the mirror's limit
                const float angleScale=1.f/(cosInner-cosOuter), angleOffset=-cosOuter*angleScale;
                TGPT_Light spot{}; spot.position[0]=lampAt.x; spot.position[1]=lampAt.y; spot.position[2]=lampAt.z; spot.type=1;
                spot.color[0]=spot.color[1]=spot.color[2]=lampI; spot.range=lampRange; spot.radius=lampRadius;
                spot.direction[1]=-1.f; spot.up[2]=1.f; spot.angleScale=angleScale; spot.angleOffset=angleOffset; spot.id=620;
                expect(SetLights(&spot, 1)==0, "a spot over the pool alone");
                sunOn=false;
                objects.push_back(Object{0, wall, {0}});
                // Traced only (the camera sees the wall behind them): a card standing in the pool 0.4 m before the wall's half
                // with z below -1, and a 0.5 m card 10 cm under the lamp, facing down (every path to the lamp inside its cone
                // crosses it within 17 cm of its middle).
                const M4 nearCard=mul(translate_scale({-1.2f, 1.15f, -1.6f}, {1.f, 1.7f, 1.2f}), rot_z(-pi/2)); // x -1.2, y 0.3..2.0, z -2.2..-1.0
                const M4 lampCard=mul(translate_scale({lampAt.x, lampAt.y-0.1f, lampAt.z}, {0.5f, 1.f, 0.5f}), rot_x(pi));
                const M4 behindPool=translate_scale({-3.0f, 0.4f, -0.4f}, {2.4f, 1.f, 1.6f}); // x -4.2..-1.8: behind the wall
                auto scene=[&](const M4* waterAt, const M4* card) {
                    auto list=instances; auto listIndices=indices;
                    auto add=[&](const M4& mm, uint32_t flags) {
                        TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=mm.m[r][c];
                        q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=flags; listIndices.push_back(0); list.push_back(q);
                    };
                    add(wall, 0u);
                    if(card) add(*card, 0u);
                    if(waterAt) add(*waterAt, TGPT_INSTANCE_WATER);
                    expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the wall by the pool with the lamp");
                };
                // Still water at a quad's place (its extent the slot's bounds).
                auto stillAt=[&](float x0, float x1) {
                    TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI; c.worldToWater[0]=c.worldToWater[5]=c.worldToWater[10]=1.f;
                    c.top=c.bottom=0.4f; c.bounds[0]=x0; c.bounds[1]=-1.2f; c.bounds[2]=x1; c.bounds[3]=0.4f;
                    expect(SetWaterCaustics(&c, 1u)==0, "still water for the lamp's reflected light");
                };
                auto wallImage=[&](uint32_t options, uint32_t frames=40) { return image(wallCam, TGPT_VIEW_DIRECT, options, frames); };
                scene(nullptr, nullptr); water(false, false);
                wallImage(0); // the wall's instance is built over the first frames
                const auto dry=wallImage(0);
                scene(&pool, nullptr); stillAt(-1.5f, 0.9f);
                wallImage(0);
                const auto wet=wallImage(0);
                const double msOn=stageMs("caustics ")+stageMs("composite ");
                const auto wetOff=wallImage(TGPT_OPTION_NO_CAUSTICS);
                const double msOff=stageMs("caustics ")+stageMs("composite ");
                water(true, true);
                const auto rippled=wallImage(0);
                stillAt(-1.5f, 0.9f);
                scene(&pool, &nearCard); wallImage(0);
                const auto nearShaded=wallImage(0);
                scene(&pool, &lampCard); wallImage(0);
                const auto lampShaded=wallImage(0);
                scene(&behindPool, nullptr); stillAt(-4.2f, -1.8f); wallImage(0);
                const auto behind=wallImage(0);
                stillAt(-1.5f, 0.9f);
                // What it costs where many lamps stand over the water (a cave's floor of candles: 32 lamps 0.3 to 0.9 m over the
                // pool, every one reaching the wall), the caustics' and the composite's GPU time over 200 frames with it and
                // without, three times each in turn, the least of each (other work on the GPU only adds).
                double manyOn=1e9, manyOff=1e9, manyCaustics=1e9;
                {
                    std::vector<TGPT_Light> many;
                    for(int i=0;i<32;++i) {
                        TGPT_Light l{}; l.position[0]=-1.3f+0.3f*float(i%8); l.position[1]=0.7f+0.2f*float(i%3); l.position[2]=-1.0f+0.4f*float(i/8); l.type=0;
                        l.color[0]=l.color[1]=l.color[2]=4.f; l.range=8; l.radius=0.03f; l.id=700+uint32_t(i); many.push_back(l);
                    }
                    expect(SetLights(many.data(), uint32_t(many.size()))==0, "32 lamps over the pool");
                    scene(&pool, nullptr);
                    for(int rep=0;rep<3;++rep) {
                        for(uint32_t i=0;i<200;++i) run(TGPT_VIEW_NONE, wallCam, wallCam, i ? 7400+i : 0, TGPT_OPTION_NO_DENOISER);
                        const double c=stageMs("caustics "); manyCaustics=std::min(manyCaustics, c); manyOn=std::min(manyOn, c+stageMs("composite "));
                        for(uint32_t i=0;i<200;++i) run(TGPT_VIEW_NONE, wallCam, wallCam, i ? 7600+i : 0, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CAUSTICS);
                        manyOff=std::min(manyOff, stageMs("caustics ")+stageMs("composite "));
                    }
                    std::printf("      32 lamps over the pool: the caustics %.3f ms, with the composite %.3f ms; with the option off %.3f ms (%ux%u)\n", manyCaustics, manyOn, manyOff, unsigned(W), unsigned(H));
                }
                // Every lamp's reflected light counts: four spots over the pool, each pointing down as the one above (none lights the
                // wall itself), as strong as each other. Three of them together give the wall the sum of their three images alone,
                // exactly (a tile traces its two strongest, and the third is the rest's one sample, chosen every time); all four too,
                // on average (the two weakest a tile stand for each other in turn, each divided by its chance), and nothing like the
                // two strongest.
                {
                    const V3 spotsAt[4]={{-0.3f, 1.0f, -0.6f}, {0.3f, 0.9f, -0.2f}, {-0.2f, 0.95f, -1.0f}, {0.4f, 0.8f, 0.2f}};
                    auto spotsOn=[&](uint32_t mask, uint32_t frames) {
                        std::vector<TGPT_Light> ls;
                        for(int i=0;i<4;++i) if(mask&(1u<<i)) {
                            TGPT_Light l=spot; l.position[0]=spotsAt[i].x; l.position[1]=spotsAt[i].y; l.position[2]=spotsAt[i].z; l.id=640+uint32_t(i); ls.push_back(l);
                        }
                        expect(SetLights(ls.data(), uint32_t(ls.size()))==0, "spots over the pool");
                        scene(&pool, nullptr);
                        wallImage(0);
                        return wallImage(0, frames);
                    };
                    std::vector<double> single[4];
                    for(int i=0;i<4;++i) single[i]=spotsOn(1u<<i, 40);
                    const auto three=spotsOn(7u, 40), four=spotsOn(15u, 208);
                    double most=0; for(size_t j=0;j<three.size();++j) most=std::max(most, single[0][j]+single[1][j]+single[2][j]+single[3][j]);
                    int n3=0, on3=0, n4=0, on4=0; double sum3=0, all3=0, sum4=0, all4=0, top2=0;
                    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                        const size_t j=size_t(y)*W+x;
                        const float c[4]={(float(x)+0.5f)/float(W)*2.f-1.f, 1.f-(float(y)+0.5f)/float(H)*2.f, 1.f, 1.f};
                        float wv[4]{}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) wv[r]+=wallCam.invViewProj.m[r][k]*c[k];
                        const V3 dir=normalize(V3{wv[0]/wv[3], wv[1]/wv[3], wv[2]/wv[3]});
                        if(dir.x>-1e-3f) continue;
                        const V3 hit=wallCam.position+dir*((-1.6f-wallCam.position.x)/dir.x);
                        if(hit.y<0.5f || hit.y>2.1f || hit.z<-1.9f || hit.z>-0.1f) continue;
                        const double s3=single[0][j]+single[1][j]+single[2][j], s4=s3+single[3][j];
                        if(s3>0.1*most) { ++n3; sum3+=s3; all3+=three[j]; if(std::fabs(three[j]-s3)<0.03*s3) ++on3; }
                        if(s4>0.1*most) {
                            ++n4; sum4+=s4; all4+=four[j]; if(std::fabs(four[j]-s4)<0.05*s4) ++on4;
                            double v[4]={single[0][j], single[1][j], single[2][j], single[3][j]}; std::sort(v, v+4); top2+=v[2]+v[3];
                        }
                    }
                    std::printf("      lamps adding up: three spots %d pixels, together %.5f of the sum of each alone (%d within 3%%); four spots %d pixels, %.5f (%d within 5%%),"
                                " the two strongest alone would give %.3f\n", n3, sum3>0 ? all3/sum3 : 0.0, on3, n4, sum4>0 ? all4/sum4 : 0.0, on4, sum4>0 ? top2/sum4 : 0.0);
                    expect(n3>3000 && std::fabs(all3/sum3-1.0)<0.01 && on3>0.95*n3, "three lamps' light reflected off the water adds up to each one's alone");
                    expect(n4>3000 && std::fabs(all4/sum4-1.0)<0.02 && on4>0.9*n4 && top2/sum4<0.9, "four lamps' reflected light adds up on average, the two weakest sampled for each other");
                }
                water(false, false); objects.pop_back(); sunOn=true;
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                {
                    std::vector<float> img(size_t(W)*H*3);
                    for(size_t j=0;j<wet.size();++j) img[j*3]=img[j*3+1]=img[j*3+2]=float(std::max(0.0, rippled[j]-dry[j])*40.0);
                    save(img, "43b-water-reflected-lamp.png", true);
                }
                // The lamp's mirror image on the wall, in the direct view's units (HDRP's exposure, no traced gain), worked out
                // for a wall point p: the reflection point w on the water's level between p and the lamp's mirror image, the
                // light's window and I / d^2 over p to the mirror image, its cone along the lamp to w, the water's exact Fresnel
                // term at the lamp's angle on it, the wall's cosine towards w.
                auto expectedAt=[&](V3 p, bool& onPool, bool& offPool, double& cone) {
                    const double h=0.4, a=double(p.y)-h, b=double(lampAt.y)-h, f=a/(a+b);
                    const double wx=p.x+(double(lampAt.x)-p.x)*f, wz=p.z+(double(lampAt.z)-p.z)*f;
                    onPool=wx>=-1.45 && wx<=0.85 && wz>=-1.15 && wz<=0.35;
                    offPool=wx<-1.55 || wx>0.95 || wz<-1.25 || wz>0.45;
                    const double mx=wx-p.x, my=h-p.y, mz=wz-p.z, nm=mx/std::sqrt(mx*mx+my*my+mz*mz);
                    const double dx=double(lampAt.x)-p.x, dy=(h-b)-p.y, dz=double(lampAt.z)-p.z, d2=dx*dx+dy*dy+dz*dz;
                    const double q=d2/(double(lampRange)*lampRange), window=std::pow(std::clamp(1.0-q*q, 0.0, 1.0), 2.0);
                    const double lx=wx-lampAt.x, lz=wz-lampAt.z, down=b/std::sqrt(lx*lx+b*b+lz*lz); // the cosine of the lamp's light on the water
                    cone=std::clamp(down*angleScale+angleOffset, 0.0, 1.0);
                    return double(lampI)*window*cone*cone/std::max(d2, double(lampRadius)*lampRadius+1e-4)*fresnelExact(down, eta)*nm/3.14159265*double(exposure);
                };
                std::vector<float> check(size_t(W)*H*3, 0.f); // the wall against the worked-out light: grey on it, red under, blue over
                double coneBins[10]{}, coneExpected[10]{}, downBins[10]{}; // by the cone's value: the light and the worked-out light; the cosine on the water
                int lit=0, onValue=0, missed=0, bright=0, dark=0, nearBlocked=0, nearOpen=0, nearOpenOn=0, inner=0, nearEdge=0;
                double brightest=0, missedMost=0, offMost=0, nearMost=0, lampMost=0, behindMost=0, worstOff=0, edgeSum=0, edgeExpected=0;
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const size_t j=size_t(y)*W+x;
                    const float c[4]={(float(x)+0.5f)/float(W)*2.f-1.f, 1.f-(float(y)+0.5f)/float(H)*2.f, 1.f, 1.f};
                    float wv[4]{}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) wv[r]+=wallCam.invViewProj.m[r][k]*c[k];
                    const V3 dir=normalize(V3{wv[0]/wv[3], wv[1]/wv[3], wv[2]/wv[3]});
                    if(dir.x>-1e-3f) continue;
                    const V3 hit=wallCam.position+dir*((-1.6f-wallCam.position.x)/dir.x);
                    if(hit.y<0.5f || hit.y>2.1f || hit.z<-1.9f || hit.z>-0.1f) continue;
                    bool onPool=false, offPool=false; double cone=0;
                    const double expected=expectedAt(hit, onPool, offPool, cone);
                    offMost=std::max(offMost, std::fabs(wetOff[j]-dry[j]));
                    lampMost=std::max(lampMost, std::fabs(lampShaded[j]-dry[j]));
                    behindMost=std::max(behindMost, std::fabs(behind[j]-dry[j]));
                    const double added=wet[j]-dry[j];
                    if(onPool && cone>0.05) {
                        ++lit; brightest=std::max(brightest, expected);
                        { const double r=added/expected; const float g=float(std::clamp(cone, 0.0, 1.0))*0.5f+0.2f;
                          check[j*3]=std::fabs(r-1.0)<0.03 ? g : r<1.0 ? 1.f : 0.f; check[j*3+1]=std::fabs(r-1.0)<0.03 ? g : 0.f; check[j*3+2]=std::fabs(r-1.0)<0.03 ? g : r>1.0 ? 1.f : 0.f; }
                        // A cell whose corners' rays land across the wall's edge (some on the wall, some past it) is left out: within
                        // one landed cell of the edge (up to 8 cm here, the lamp's image magnifies the water's 2 cm cells up to 4
                        // times) the wall misses some light. Pointwise and in sum away from the edges.
                        const bool clear=hit.y>0.55f && hit.y<2.05f && hit.z>-1.85f && hit.z<-0.15f;
                        { const int cb=std::min(int(cone*10.0), 9); coneBins[cb]+=added; coneExpected[cb]+=expected; downBins[cb]=cone; }
                        if(!clear) ++nearEdge;
                        else if(cone<0.5) { edgeSum+=added; edgeExpected+=expected; }
                        else { ++inner; if(std::fabs(added-expected)<0.03*expected) ++onValue; else worstOff=std::max(worstOff, std::fabs(added/expected-1.0)); }
                        const double band=rippled[j]-dry[j];
                        if(band>1.5*expected) ++bright; else if(band<0.5*expected) ++dark;
                        // The card before the wall's half below z -1 is crossed by every path from z -1.35 down; none from -0.85 up.
                        if(hit.z<-1.35f) { ++nearBlocked; nearMost=std::max(nearMost, std::fabs(nearShaded[j]-dry[j])/expected); }
                        else if(hit.z>-0.85f && hit.z<-0.15f && hit.y>0.55f && hit.y<2.05f && cone>=0.5) { ++nearOpen; if(std::fabs(nearShaded[j]-dry[j]-expected)<0.03*expected) ++nearOpenOn; }
                    } else if(offPool && cone>0.05) { ++missed; missedMost=std::max(missedMost, std::fabs(added)); }
                }
                std::printf("      lamplight reflected off the water onto a wall: %d pixels see the lamp's image in the pool, %d within 15 cm of the wall's edges, %d inside the cone's edge, %d of them get the worked-out light within 3%%"
                            " (brightest %.5f, the worst other off by %.3f), the cone's edge in sum %.4f of its worked-out light; under the sine wave %d bright and %d dark; %d see no water, %.6f at most; with the option off %.6f;"
                            " a card before half the wall: %d shaded pixels at most %.4f of theirs, %d of %d open ones on value; a card under the lamp %.6f;"
                            " the pool behind the wall %.6f; caustics and composite %.3f ms with it, %.3f ms with the option off\n",
                            lit, nearEdge, inner, onValue, brightest, worstOff, edgeExpected>0 ? edgeSum/edgeExpected : 0.0, bright, dark, missed, missedMost, offMost, nearBlocked, nearMost, nearOpenOn, nearOpen, lampMost,
                            behindMost, msOn, msOff);
                save(check, "43c-water-reflected-lamp-check.png", false);
                std::printf("      the lamp's mirror light by its cone's value (0.1 bins):");
                for(int i=0;i<10;++i) std::printf(" %.3f", coneExpected[i]>0 ? coneBins[i]/coneExpected[i] : 0.0);
                std::printf("\n");
                expect(lit>3000 && inner>0.4*lit && onValue>0.95*inner && std::fabs(edgeSum/edgeExpected-1.0)<0.03,
                       "a wall facing still water takes the lamp's mirror image in it, by the water's Fresnel term and the whole path's falloff");
                expect(bright>0.1*lit && dark>0.1*lit, "the waves split the lamp's reflected light into lines and gaps");
                expect(missed>500 && missedMost<0.02*brightest, "no reflected lamplight where the mirrored path misses the water");
                expect(offMost<0.02*brightest, "no reflected lamplight with TGPT_OPTION_NO_CAUSTICS");
                expect(nearBlocked>500 && nearMost<0.02 && nearOpen>500 && nearOpenOn>0.9*nearOpen, "no reflected lamplight where something stands between the wall and the water");
                expect(lampMost<0.02*brightest, "no reflected lamplight where something stands between the water and the lamp");
                expect(behindMost<0.02*brightest, "no reflected lamplight from water behind the wall");
            }
            // Wet ground (PT-52, pt_wet.hlsl; docs/agents/water-edges.md): two red ramps run into the pool's water, one at 60 degrees
            // (rock: steeper than any sand stands) and one at 20 (sand). The water stands at 0.45 m until the game's clock reads
            // 9.99 s, then at 0.30 m from 10 s on: the ramps between the two levels were under water and are wet behind it. Wet ground is
            // darker by the water's internal reflection, per channel A' = (1 - r_e)(1 - r_i) A / (1 - r_i A) (r_e 0.0665, r_i 0.4749:
            // Lekner and Dorf), and dries as its slope and the time since say: rock's 0.05 mm at 0.2 mm an hour (15 min), sand's 0.4 mm
            // (2 h), after its film has run off (Jeffreys) or soaked in (5 s). Checked against the same frames with the ground kept dry
            // (TGPT_OPTION_DRY_GROUND): the camera's own rays' surfaces (the Albedo view) and other rays' (the Materials view), at
            // 30 s, 450 s and 1,000 s after the water left; what lies above the highest water and under the water now unchanged
            // (the ground under the water now from a camera under it: from above the camera's ray meets the water first).
            {
                const float rockA=-60.f*pi/180.f, sandA=-20.f*pi/180.f;
                auto ramp=[&](V3 at, float angle, float width, float length) { return mul(translate_scale(at, {1.f, 1.f, 1.f}), mul(rot_x(angle), translate_scale({0.f, 0.f, 0.f}, {width, 1.f, length}))); };
                const M4 rock=ramp({-1.0f, 0.40f, -0.3f}, rockA, 0.8f, 0.35f), sand=ramp({0.0f, 0.40f, -0.3f}, sandA, 0.8f, 0.8f);
                const M4 rockInv=inverse(rock), sandInv=inverse(sand);
                const Camera wetCam=make_camera({-0.35f, 1.25f, -1.9f}, {-0.35f, 0.38f, -0.3f}, 60.f, float(W)/float(H));
                const auto keepObjects=objects;
                objects.push_back({0, rock, {1}}); objects.push_back({0, sand, {1}}); // HDRP's GBuffer: the ramps, red
                float level=0.45f; bool waves=false; float shift=0.f;
                // The rock's ramp as the tracer holds it: the red material, or (73) the same as a material light walks inside.
                uint32_t rockMaterial=1u;
                {
                    TGPT_Material inside=materials[1]; inside.flags|=TGPT_MATERIAL_SUBSURFACE; inside.subsurfaceMask=1.f;
                    for(int c=0;c<3;++c) inside.subsurface[c]=100.f;
                    expect(SetMaterials(73, &inside, 1)==0, "the rock's material as one light walks inside");
                }
                auto wetScene=[&]() {
                    auto list=instances; auto listIndices=indices;
                    auto add=[&](const M4& m, uint32_t flags, uint32_t material) {
                        TGPT_Instance w{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) w.transform[r*4+c]=m.m[r][c];
                        w.mesh=quadHandle; w.materialBase=uint32_t(listIndices.size()); w.mask=0xFF; w.flags=flags; listIndices.push_back(material); list.push_back(w); };
                    add(rock, 0u, rockMaterial); add(sand, 0u, 1u);
                    add(translate_scale({-0.3f, level, -0.4f}, {2.4f, 1.f, 1.6f}), TGPT_INSTANCE_WATER, 0u);
                    expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the ramps and the pool's water");
                    TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI; c.worldToWater[0]=c.worldToWater[5]=c.worldToWater[10]=1.f;
                    if(waves) {
                        c.displacement=uint64_t(uintptr_t(waveDisplacement.Get())); c.normals=uint64_t(uintptr_t(waveGradient.Get()));
                        c.bands[0][0]=float(1.0/period); c.bands[0][1]=shift; c.bands[0][3]=1.f; c.fades[0][0]=0.f; c.fades[0][1]=1.f; c.bandCount=1;
                        c.maxDisplacement=float(amplitude);
                    }
                    c.top=c.bottom=level; c.bounds[0]=-1.5f; c.bounds[1]=-1.2f; c.bounds[2]=0.9f; c.bounds[3]=0.4f;
                    expect(SetWaterCaustics(&c, 1u)==0, "the pool's water slot");
                };
                uint32_t wetFrame=9100;
                auto at=[&](float clock, uint32_t view, uint32_t options, uint32_t frames=2, const Camera* from=nullptr) {
                    std::vector<float> px;
                    const Camera& c=from ? *from : wetCam;
                    for(uint32_t i=0;i<frames;++i) { SetClothWind(clock, nullptr, 0); run(view, c, c, wetFrame++, TGPT_OPTION_NO_DENOISER|options); px=readback(); }
                    return px;
                };
                // The ground under the water now is seen from under it: a camera in the pool, 8 cm under the 0.30 m the water
                // falls to, looking up the sand's ramp at its foot (the ramp leans towards it). From above, the camera's own ray
                // meets the water's surface first there (the water is a surface of the traced scene, PT-71 item 5) and the
                // Albedo view shows the water's own. Each of its pixels' point on the sand's ramp along its straight ray, where
                // that point lies under 0.30 m less the margin (so the ray never left the water) and off the ramp's very edge.
                const Camera underCam=make_camera({0.f, 0.22f, -1.05f}, {0.f, 0.278f, -0.635f}, 60.f, float(W)/float(H));
                std::vector<uint8_t> underAt(size_t(W)*H, 0);
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const float cl[4]={(float(x)+0.5f)/float(W)*2.f-1.f, 1.f-(float(y)+0.5f)/float(H)*2.f, 1.f, 1.f};
                    float w4[4]{}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) w4[r]+=underCam.invViewProj.m[r][k]*cl[k];
                    const V3 dir=normalize(V3{w4[0]/w4[3], w4[1]/w4[3], w4[2]/w4[3]});
                    auto tp=[&](V3 p, float wv) { V3 o{0, 0, 0}; o.x=sandInv.m[0][0]*p.x+sandInv.m[0][1]*p.y+sandInv.m[0][2]*p.z+sandInv.m[0][3]*wv;
                        o.y=sandInv.m[1][0]*p.x+sandInv.m[1][1]*p.y+sandInv.m[1][2]*p.z+sandInv.m[1][3]*wv; o.z=sandInv.m[2][0]*p.x+sandInv.m[2][1]*p.y+sandInv.m[2][2]*p.z+sandInv.m[2][3]*wv; return o; };
                    const V3 o=tp(underCam.position, 1.f), d=tp(dir, 0.f);
                    if(std::fabs(d.y)<1e-6f) continue;
                    const float s=-o.y/d.y; if(!(s>0.f)) continue;
                    const V3 q=o+d*s;
                    if(std::fabs(q.x)>0.42f || std::fabs(q.z)>0.48f) continue;
                    underAt[size_t(y)*W+x]=(underCam.position+dir*s).y<0.30f-0.012f ? 1 : 0;
                }
                // Each pixel's point on a ramp (0 rock, 1 sand, -1 neither), its height.
                std::vector<int> which(size_t(W)*H, -1); std::vector<float> heightAt(size_t(W)*H, 0.f), xAt(size_t(W)*H, 0.f);
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const float cl[4]={(float(x)+0.5f)/float(W)*2.f-1.f, 1.f-(float(y)+0.5f)/float(H)*2.f, 1.f, 1.f};
                    float w4[4]{}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) w4[r]+=wetCam.invViewProj.m[r][k]*cl[k];
                    const V3 dir=normalize(V3{w4[0]/w4[3], w4[1]/w4[3], w4[2]/w4[3]});
                    float best=1e9f;
                    for(int r=0;r<2;++r) {
                        const M4& inv=r ? sandInv : rockInv;
                        auto tp=[&](V3 p, float wv) { V3 o{0, 0, 0}; o.x=inv.m[0][0]*p.x+inv.m[0][1]*p.y+inv.m[0][2]*p.z+inv.m[0][3]*wv;
                            o.y=inv.m[1][0]*p.x+inv.m[1][1]*p.y+inv.m[1][2]*p.z+inv.m[1][3]*wv; o.z=inv.m[2][0]*p.x+inv.m[2][1]*p.y+inv.m[2][2]*p.z+inv.m[2][3]*wv; return o; };
                        const V3 o=tp(wetCam.position, 1.f), d=tp(dir, 0.f);
                        if(std::fabs(d.y)<1e-6f) continue;
                        const float s=-o.y/d.y; if(!(s>0.f) || s>=best) continue;
                        const V3 q=o+d*s;
                        if(std::fabs(q.x)>0.42f || std::fabs(q.z)>0.42f) continue;
                        best=s; which[size_t(y)*W+x]=r; heightAt[size_t(y)*W+x]=(wetCam.position+dir*s).y; xAt[size_t(y)*W+x]=(wetCam.position+dir*s).x;
                    }
                }
                // The drying as the tracer is told it (pt_common.hlsli wet_dry): the share still moist after `age` seconds of a point `above`
                // metres under where the water stood, on ground whose normal is `up` from the vertical.
                auto moistAfter=[](double age, double above, double up) {
                    const double sin2=std::max(1.0-up*up, 1e-6), sandW=std::clamp((up-0.7071)/(0.8290-0.7071), 0.0, 1.0);
                    const double drain=1.0e-6*above/(9.81*sin2*1e-8), film=drain*(1.0-sandW)+std::min(drain, 5.0)*sandW;
                    const double holds=0.5e-4*(1.0-sandW)+0.4e-3*sandW;
                    return age<=film ? 1.0 : std::clamp(1.0-(age-film)*(0.2e-3/3600.0)/holds, 0.0, 1.0);
                };
                const double mat[3]={0.75, 0.08, 0.06}, rE=0.0665, rI=0.4749;
                double wetRatio[3]; for(int c=0;c<3;++c) wetRatio[c]=(1.0-rE)*(1.0-rI)/(1.0-rI*mat[c]);
                const double ups[2]={std::cos(60.0*3.14159265358979/180.0), std::cos(20.0*3.14159265358979/180.0)};
                // Compares a run with the dry one, pixel by pixel: the band between the levels against the drying's darkening (red within 1.5 %,
                // green within 4 %: 8-bit sRGB in the GBuffer), above the highest water and under the water unchanged (within 1 %).
                struct Band { int n=0, on=0, above=0, aboveOn=0, under=0, underOn=0; double mean=0; };
                auto compare=[&](const std::vector<float>& wet, const std::vector<float>& dry, int r, double age, double high, double low) {
                    Band b;
                    for(size_t j=0;j<which.size();++j) {
                        if(which[j]!=r || !(dry[j*3]>1e-4f)) continue;
                        const double y=heightAt[j], red=wet[j*3]/dry[j*3], green=dry[j*3+1]>1e-5f ? wet[j*3+1]/dry[j*3+1] : 1.0;
                        if(y>high+0.012) { ++b.above; if(std::fabs(red-1.0)<0.01) ++b.aboveOn; continue; }
                        if(y<low-0.012) { ++b.under; if(std::fabs(red-1.0)<0.01) ++b.underOn; continue; }
                        if(y>high-0.012 || y<low+0.012) continue;
                        const double m=age<0 ? 1.0 : moistAfter(age, high-y, ups[r]);
                        const double wantR=1.0-m*(1.0-wetRatio[0]), wantG=1.0-m*(1.0-wetRatio[1]);
                        ++b.n; b.mean+=red/wantR;
                        if(std::fabs(red/wantR-1.0)<0.015 && std::fabs(green/wantG-1.0)<0.04) ++b.on;
                    }
                    if(b.n) b.mean/=b.n;
                    return b;
                };
                auto report=[&](const char* what, const Band& b) {
                    std::printf("      wet ground, %s: %d of %d band pixels on the drying's darkening (mean ratio %.4f); above the water %d of %d unchanged, under it %d of %d\n",
                                what, b.on, b.n, b.mean, b.aboveOn, b.above, b.underOn, b.under);
                };
                expect(SetLights(nullptr, 0)==0, "the sun alone over the ramps");
                // Each pixel's surface is the tracer's own camera ray's hit (PT-59): made wet in hit_material, where every other ray's
                // hit is. (HDRP's G-buffer is no longer made wet in pt_prepare: PT-58.)
                ownRays=true;
                level=0.45f; wetScene();
                at(0.f, TGPT_VIEW_NONE, 0, 4);  // the ramps and the water built; the store remembers 0.45 m
                at(9.99f, TGPT_VIEW_NONE, 0, 1);
                level=0.30f; wetScene();
                at(10.f, TGPT_VIEW_NONE, 0, 2);
                const double ages[3]={30.0, 450.0, 1000.0};
                Band rockB[3], sandB[3], rayB[3];
                int notGround=0, notGroundDry=0;
                for(int a=0;a<3;++a) {
                    const float clock=10.f+float(ages[a]);
                    const auto wet=at(clock, TGPT_VIEW_ALBEDO, 0), dry=at(clock, TGPT_VIEW_ALBEDO, TGPT_OPTION_DRY_GROUND);
                    const auto wetSeen=at(clock, TGPT_VIEW_MATERIALS, 0), drySeen=at(clock, TGPT_VIEW_MATERIALS, TGPT_OPTION_DRY_GROUND);
                    rockB[a]=compare(wet, dry, 0, ages[a], 0.45, 0.30); sandB[a]=compare(wet, dry, 1, ages[a], 0.45, 0.30);
                    rayB[a]=compare(wetSeen, drySeen, 0, ages[a], 0.45, 0.30);
                    // Under the water now: the sand's foot from the camera in the pool (the count above, from over the water, is
                    // of the water's own pixels there: none of the ground).
                    {
                        const auto wetUnder=at(clock, TGPT_VIEW_ALBEDO, 0, 2, &underCam), dryUnder=at(clock, TGPT_VIEW_ALBEDO, TGPT_OPTION_DRY_GROUND, 2, &underCam);
                        sandB[a].under=sandB[a].underOn=0;
                        for(size_t j=0;j<underAt.size();++j) {
                            // Only where the camera's ray did meet the ramp: its own red (0.75, 0.08, 0.06), which wet would
                            // darken by a quarter. "Unchanged" on the grey floor or on the water's black would say nothing.
                            if(!underAt[j] || !(dryUnder[j*3]>0.3f) || !(dryUnder[j*3+1]<0.5f*dryUnder[j*3])) continue;
                            ++sandB[a].under; if(std::fabs(double(wetUnder[j*3])/double(dryUnder[j*3])-1.0)<0.01) ++sandB[a].underOn;
                        }
                        if(a==0) save(wetUnder, "44c-wet-ground-from-under-water.png", false);
                    }
                    char what[96];
                    std::snprintf(what, sizeof what, "rock at %.0f s", ages[a]); report(what, rockB[a]);
                    std::snprintf(what, sizeof what, "sand at %.0f s", ages[a]); report(what, sandB[a]);
                    std::snprintf(what, sizeof what, "rock seen by rays at %.0f s", ages[a]); report(what, rayB[a]);
                    if(a==0) { save(wet, "44a-wet-ground-30s.png", false); save(dry, "44b-dry-ground.png", false); }
                    // What is not ground stays dry where the camera's own ray meets it (hit_material: a material light walks inside,
                    // skin or wax, whose color is the walk's medium): the rock's ramp traced as such a material, 30 s after the
                    // water left it.
                    if(a==0) {
                        rockMaterial=73u; wetScene();
                        const auto skin=at(clock, TGPT_VIEW_ALBEDO, 0, 3), skinDry=at(clock, TGPT_VIEW_ALBEDO, TGPT_OPTION_DRY_GROUND, 3);
                        rockMaterial=1u; wetScene(); at(clock, TGPT_VIEW_NONE, 0, 2);
                        for(size_t j=0;j<which.size();++j) {
                            if(which[j]!=0 || !(skinDry[j*3]>1e-4f) || heightAt[j]>0.45-0.012 || heightAt[j]<0.30+0.012) continue;
                            ++notGround; if(std::fabs(skin[j*3]/skinDry[j*3]-1.0)<0.01) ++notGroundDry;
                        }
                        std::printf("      wet ground, the rock's ramp traced as a material light walks inside, at 30 s: %d of %d band pixels as dry\n", notGroundDry, notGround);
                        expect(notGround>300 && notGroundDry==notGround, "a material light walks inside (skin, wax) is not made wet where the camera's own ray meets it");
                    }
                }
                for(int a=0;a<3;++a) {
                    expect(rockB[a].n>300 && rockB[a].on>0.95*rockB[a].n, "wet rock is darker by a water film's internal reflection and dries in its time");
                    expect(sandB[a].n>300 && sandB[a].on>0.95*sandB[a].n, "wet sand is darker by a water film's internal reflection and dries in its time");
                    expect(rayB[a].n>300 && rayB[a].on>0.95*rayB[a].n, "rays meet the same wet ground the camera sees");
                    expect(rockB[a].above>200 && rockB[a].aboveOn==rockB[a].above && sandB[a].under>200 && sandB[a].underOn==sandB[a].under,
                           "ground above the highest water and under the water now is not made wet");
                }
                // The film: right after the water left, the rock's film (t = nu x / (g sin^2 sigma^2), a few seconds) is one smooth mirror
                // (water's 0.02 and its roughness) where the ground's own finish was rough: the specular light there changes; gone once drained.
                {
                    level=0.45f; wetScene(); at(1500.f, TGPT_VIEW_NONE, 0, 2); at(1509.99f, TGPT_VIEW_NONE, 0, 1);
                    level=0.30f; wetScene(); at(1510.f, TGPT_VIEW_NONE, 0, 1);
                    auto bandMean=[&](const std::vector<float>& px) { double s=0; int n=0;
                        for(size_t j=0;j<which.size();++j) if(which[j]==0 && heightAt[j]<0.40 && heightAt[j]>0.32) { s+=0.2126*px[j*3]+0.7152*px[j*3+1]+0.0722*px[j*3+2]; ++n; }
                        return n ? s/n : 0.0; };
                    const double filmOn=bandMean(at(1510.2f, TGPT_VIEW_SPECULAR, 0, 6)), filmDry=bandMean(at(1510.2f, TGPT_VIEW_SPECULAR, TGPT_OPTION_DRY_GROUND, 6));
                    const double drained=bandMean(at(4510.f, TGPT_VIEW_SPECULAR, 0, 6)), drainedDry=bandMean(at(4510.f, TGPT_VIEW_SPECULAR, TGPT_OPTION_DRY_GROUND, 6));
                    std::printf("      wet ground, the rock's film: the band's reflected light %.5f with it, %.5f dry; 3,000 s on %.5f, %.5f dry\n", filmOn, filmDry, drained, drainedDry);
                    // (By 6 %: the dry rock is its traced material's, 0.7 rough. With the film never chosen it is the dry rock's, within
                    // 0.3 %: the line lies between. It was 5 % while the dry rock was HDRP's G-buffer's, 0.6 rough: 18 % there.)
                    expect(filmDry>0 && std::fabs(filmOn/filmDry-1.0)>0.03 && std::fabs(drained/drainedDry-1.0)<0.03,
                           "a film of water just left on the rock is a mirror of its own, gone once it has run off");
                }
                // HDRP's waves (the caustics' sine wave, 3 cm high): the water at 0.40 m, its crests passing every point of the rock as the
                // wave moves (eight phases): the rock is wet as high as the crests reached, 0.43 m, and dry above. Where the wave stands over
                // the rock in the frame looked at (0.40 + 0.03 sin(2 pi x / 0.4) m), it is under the water now and left as it is.
                {
                    waves=true; level=0.40f;
                    for(int k=0;k<8;++k) { shift=float(k)/8.f; wetScene(); at(5000.f+0.1f*float(k), TGPT_VIEW_NONE, 0, 1); }
                    shift=0.f; wetScene();
                    const auto wet=at(5001.f, TGPT_VIEW_ALBEDO, 0), dry=at(5001.f, TGPT_VIEW_ALBEDO, TGPT_OPTION_DRY_GROUND);
                    int reached=0, reachedWet=0, over=0, overDry=0;
                    for(size_t j=0;j<which.size();++j) {
                        if(which[j]!=0 || !(dry[j*3]>1e-4f)) continue;
                        const double y=heightAt[j], red=wet[j*3]/dry[j*3];
                        const double now=0.40+amplitude*std::sin(2.0*3.14159265358979*double(xAt[j])/period);
                        if(y>0.405 && y<0.42 && y>now+0.004) { ++reached; if(red<1.0-0.8*(1.0-wetRatio[0])) ++reachedWet; }
                        if(y>0.445) { ++over; if(std::fabs(red-1.0)<0.01) ++overDry; }
                    }
                    std::printf("      wet ground under HDRP's waves: %d of %d rock pixels between 0.405 and 0.42 m out of the water now wet, %d of %d over 0.445 m dry\n", reachedWet, reached, overDry, over);
                    expect(reached>200 && reachedWet>0.95*reached && over>200 && overDry==over, "the rock is wet as high as HDRP's waves reached, and dry above");
                    waves=false;
                }
                // A full record keeps the highest the water reached: still water stepping down a centimetre at a time with pauses (no
                // two steps on one line) fills a cell's six points; the rock just under the first level, where the water stood 90 s
                // before, is still wet (a rock dries in about 15 minutes) when the record has dropped one of its inner points.
                {
                    waves=false;
                    const float steps[9]={0.45f, 0.44f, 0.43f, 0.42f, 0.41f, 0.40f, 0.39f, 0.38f, 0.37f};
                    const float times[9]={7000.f, 7010.f, 7011.f, 7030.f, 7031.f, 7060.f, 7061.f, 7080.f, 7081.f};
                    for(int k=0;k<9;++k) { level=steps[k]; wetScene(); at(times[k], TGPT_VIEW_NONE, 0, 1); }
                    const auto wet=at(7090.f, TGPT_VIEW_ALBEDO, 0), dry=at(7090.f, TGPT_VIEW_ALBEDO, TGPT_OPTION_DRY_GROUND);
                    int top=0, topWet=0;
                    for(size_t j=0;j<which.size();++j) {
                        if(which[j]!=0 || !(dry[j*3]>1e-4f)) continue;
                        const double y=heightAt[j], red=wet[j*3]/dry[j*3];
                        if(y>0.4415 && y<0.4485) { ++top; if(red<1.0-0.8*(1.0-wetRatio[0])) ++topWet; }
                    }
                    std::printf("      wet ground, a full record: %d of %d rock pixels just under the first level (0.4415 to 0.4485 m) still wet 90 s on\n", topWet, top);
                    expect(top>100 && topWet>0.95*top, "a cell's full record keeps the highest the water reached");
                }
                // What the store costs: the prepare stage (the store's pass runs in it) with the ground wet and kept dry.
                {
                    level=0.30f; wetScene();
                    double withIt=1e9, without=1e9;
                    for(int rep=0;rep<3;++rep) {
                        for(uint32_t i=0;i<60;++i) { SetClothWind(6000.f+float(rep*120+i)*0.016f, nullptr, 0); run(TGPT_VIEW_NONE, wetCam, wetCam, wetFrame++, TGPT_OPTION_NO_DENOISER); }
                        withIt=std::min(withIt, stageMs("prepare "));
                        for(uint32_t i=0;i<60;++i) { SetClothWind(6000.f+float(rep*120+60+i)*0.016f, nullptr, 0); run(TGPT_VIEW_NONE, wetCam, wetCam, wetFrame++, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_DRY_GROUND); }
                        without=std::min(without, stageMs("prepare "));
                    }
                    std::printf("      wet ground: the prepare stage %.3f ms with the store (its levels round a %ux%u view of the pool), %.3f ms without\n", withIt, unsigned(W), unsigned(H), without);
                }
                ownRays=false;
                expect(SetMaterials(73, &materials[1], 1)==0, "the rock's second material plain again");
                objects=keepObjects;
                SetClothWind(0.f, nullptr, 0);
                expect(SetWaterCaustics(nullptr, 0u)==0, "no water slot");
                water_scene(false);
            }
            water(false, false); water_scene(false);
            expect(SetLights(&light, 1)==0, "one light again");
        }
        if(GetEnvironmentVariableA("TGPT_TEST_STOP_AFTER_CAUSTICS", nullptr, 0)>0) { std::printf(failures ? "FAILED %d checks\n" : "checks up to the caustics passed\n", failures); return failures ? 1 : 0; }
        // The eye's color as the game's eye shader computes it (TGPT_RegisterEyeColor), baked into a detail tile: a white sclera,
        // and within the iris map's alpha disc its green iris (doubled by the shader's overlay), darkening toward its edge in
        // the limbal ring.
        {
            auto map=[&](auto texel) {
                const int size=128; std::vector<uint32_t> px(size*size);
                for(int y=0;y<size;++y) for(int x=0;x<size;++x) px[y*size+x]=texel((x+0.5f)/size-0.5f, (y+0.5f)/size-0.5f);
                D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{px.data(), UINT(size*4), 0};
                ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
            };
            auto sclera=map([](float, float) { return 0xFFFFFFFFu; });
            auto iris=map([](float x, float y) { return std::sqrt(x*x+y*y)<0.45f ? 0xFF00B400u : 0x00000000u; }); // green (sRGB 180 is ~0.46 linear), alpha disc
            TGPT_EyeColor eyeColor{}; eyeColor.size=sizeof(TGPT_EyeColor); eyeColor.abi=TGPT_ABI;
            eyeColor.scleraMap=uint64_t(uintptr_t(sclera.Get())); eyeColor.irisMap=uint64_t(uintptr_t(iris.Get()));
            for(int i=0;i<4;++i) { eyeColor.scleraTint[i]=0.9f; eyeColor.irisTint[i]=1.f; }
            eyeColor.scleraTiling=1.f; eyeColor.irisTiling=2.f; eyeColor.pupilSize=0.75f; // no pupil magnification at this tiling
            eyeColor.ringSize=0.3f; eyeColor.ringIntensity=2.f; eyeColor.emissionIntensity=1.f;
            const int eyeTile=RegisterEyeColor(49, &eyeColor);
            expect(eyeTile>=4096, "an eye's color baked into a detail tile");
            expect(RegisterEyeColor(49, &eyeColor)==eyeTile, "the same eye keeps its tile");
            auto em=materials[0]; em.flags=0; em.baseColor[0]=em.baseColor[1]=em.baseColor[2]=em.baseColor[3]=1.f; em.albedoTile=eyeTile;
            em.uvScaleOffset[0]=em.uvScaleOffset[1]=0.25f; em.uvScaleOffset[2]=em.uvScaleOffset[3]=0.f;
            const uint32_t eyeIndex=uint32_t(materials.size())+2;
            expect(SetMaterials(eyeIndex, &em, 1)==0, "the baked eye material");
            const M4 ew=mul(translate_scale({0.f, 3.f, 3.f}, {2.f, 1, 2.f}), rot_x(-pi/2));
            auto shot=[&](bool withQuad) {
                auto list=instances; auto listIndices=indices;
                if(withQuad) {
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=ew.m[r][c];
                    q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                    listIndices.push_back(eyeIndex); list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with the eye quad");
                std::vector<float> last;
                for(uint32_t i=0;i<4;++i) { run(TGPT_VIEW_MATERIALS, cam, cam, i ? 6300+i : 0, TGPT_OPTION_NO_DENOISER); last=readback(); }
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return last;
            };
            const auto without=shot(false), with=shot(true);
            int n=0, white=0, bright=0, rim=0;
            float peak=0.f;
            for(size_t j=0;j+2<with.size();j+=3) if(std::fabs(with[j]-without[j])+std::fabs(with[j+1]-without[j+1])+std::fabs(with[j+2]-without[j+2])>=0.05) peak=std::max(peak, with[j+1]);
            for(size_t j=0;j+2<with.size();j+=3) {
                if(std::fabs(with[j]-without[j])+std::fabs(with[j+1]-without[j+1])+std::fabs(with[j+2]-without[j+2])<0.05) continue;
                ++n; const float r=with[j], g=with[j+1], b=with[j+2];
                if(r>0.4f*peak && b>0.4f*peak && std::fabs(r-g)<0.1f*peak) ++white;
                else if(g>2.5f*std::max(r, b)) { if(g>0.6f*peak) ++bright; else if(g<0.4f*peak) ++rim; }
            }
            std::printf("      a baked eye quad (%d pixels): %d white sclera, %d bright green iris, %d darker green in its limbal ring\n", n, white, bright, rim);
            // One iris: repeated past the iris map's edge (a wrapping sampler), dark copies of it covered half the sclera.
            expect(n>3000 && white>n*2/3 && bright>n/50 && rim>n/200 && rim<n/10, "an eye takes its shader's sclera, iris and limbal ring");
        }
        // PT-45 phase 3: Unity's mip streaming makes a texture anew (a new D3D11 object) when it loads its top mips; the plugin
        // registers the new one and calls TGPT_SwapSharedTexture. An eye's sclera map swapped so (dark grey for white) and the old
        // one then freed by the game: the eye is read from the new map, its map record following it. Without that, the old
        // number dies with the old map and the eye falls back to its tile, baked dark from the old map.
        if(SwapSharedTexture && RegisterSharedTexture) {
            auto map=[&](uint32_t texel) {
                const int size=512; std::vector<uint32_t> px(size*size, texel);
                D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{px.data(), UINT(size*4), 0};
                ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
            };
            auto shareOf=[&](ID3D11Texture2D* t, TGPT_SharedTexture* i) { *i=TGPT_SharedTexture{}; i->size=sizeof(*i); i->abi=TGPT_ABI; return t ? RegisterSharedTexture(uint64_t(uintptr_t(t)), i) : -100; };
            auto oldSclera=map(0xFF303030u), noIris=map(0x00000000u);
            TGPT_SharedTexture si{}, ii{};
            const int oldNumber=shareOf(oldSclera.Get(), &si), irisNumber=shareOf(noIris.Get(), &ii);
            TGPT_EyeColor eye{}; eye.size=sizeof(eye); eye.abi=TGPT_ABI;
            eye.scleraMap=uint64_t(uintptr_t(oldSclera.Get())); eye.irisMap=uint64_t(uintptr_t(noIris.Get()));
            for(int i=0;i<4;++i) { eye.scleraTint[i]=1.f; eye.irisTint[i]=1.f; }
            eye.scleraTiling=1.f; eye.irisTiling=2.f; eye.pupilSize=0.75f; eye.ringSize=0.f; eye.ringIntensity=1.f; eye.emissionIntensity=1.f;
            const int tile=RegisterEyeColor(7001, &eye);
            auto em=materials[0]; em.flags=0; em.baseColor[0]=em.baseColor[1]=em.baseColor[2]=em.baseColor[3]=1.f; em.albedoTile=tile;
            em.uvScaleOffset[0]=em.uvScaleOffset[1]=0.25f; em.uvScaleOffset[2]=em.uvScaleOffset[3]=0.f;
            const uint32_t eyeIndex=uint32_t(materials.size())+3;
            expect(SetMaterials(eyeIndex, &em, 1)==0, "the eye material whose sclera map mip streaming makes anew");
            const M4 ew=mul(translate_scale({0.f, 3.f, 3.f}, {2.f, 1, 2.f}), rot_x(-pi/2));
            auto shot=[&](bool withQuad, uint32_t seed) {
                auto list=instances; auto listIndices=indices;
                if(withQuad) {
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=ew.m[r][c];
                    q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                    listIndices.push_back(eyeIndex); list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with the eye quad");
                std::vector<float> last;
                for(uint32_t i=0;i<4;++i) { run(TGPT_VIEW_MATERIALS, cam, cam, i ? seed+i : 0, TGPT_OPTION_NO_DENOISER); last=readback(); }
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return last;
            };
            const auto without=shot(false, 6400), dark=shot(true, 6410);
            // Mip streaming's new texture: registered, swapped in, and the old one freed by the game (its death notice taken in).
            auto newSclera=map(0xFFFFFFFFu);
            TGPT_SharedTexture ni{};
            const int newNumber=shareOf(newSclera.Get(), &ni);
            const int swapped=SwapSharedTexture(uint64_t(uintptr_t(oldSclera.Get())), uint64_t(uintptr_t(newSclera.Get())));
            TGPT_SharedTexture before{}, after{}; shareOf(nullptr, &before); RegisterSharedTexture(0, &before);
            oldSclera.Reset(); context->Flush();
            for(int k=0;k<40;++k) { TGPT_Status s{sizeof(TGPT_Status), TGPT_ABI}; ReadStatus(&s); after=TGPT_SharedTexture{}; after.size=sizeof(after); after.abi=TGPT_ABI;
                                    RegisterSharedTexture(0, &after); if(after.freed>before.freed) break; Sleep(50); }
            const auto bright=shot(true, 6420);
            double darkSum=0, brightSum=0; int n=0;
            for(size_t j=0;j+2<without.size();j+=3) {
                const double dd=std::fabs(dark[j]-without[j])+std::fabs(dark[j+1]-without[j+1])+std::fabs(dark[j+2]-without[j+2]);
                const double db=std::fabs(bright[j]-without[j])+std::fabs(bright[j+1]-without[j+1])+std::fabs(bright[j+2]-without[j+2]);
                if(dd<0.05 && db<0.05) continue;
                ++n; darkSum+=dark[j]+dark[j+1]+dark[j+2]; brightSum+=bright[j]+bright[j+1]+bright[j+2];
            }
            const double darkMean=n ? darkSum/(3.0*n) : 0.0, brightMean=n ? brightSum/(3.0*n) : 0.0;
            std::printf("      an eye's sclera map made anew by mip streaming (numbers: old %d, iris %d, new %d; swap %d; the old one freed: %u -> %u): %d pixels, "
                        "%.3f with the old map, %.3f after the swap\n", oldNumber, irisNumber, newNumber, swapped, before.freed, after.freed, n, darkMean, brightMean);
            expect(oldNumber>=0 && irisNumber>=0 && newNumber>=0 && swapped==0 && after.freed>before.freed, "the eye's maps are shared, and the old sclera map is swapped and freed");
            expect(n>3000 && brightMean>4.0*darkMean && brightMean>0.3, "an eye whose map mip streaming made anew reads the new texture, not its tile baked from the old one");
        }
        // RealHair's color as the game's shader computes it (TGPT_RegisterHairColor), baked into a tile: its root map's gradient
        // from a red root to a blue end, and where its ID map lies in highlight A's band, highlight A's green. Registered again
        // with a green root and end (the game colored the hair since), the same tile is baked again, all green. A 512x512 detail
        // tile while there are any (the camera sees hair's color from its tile: 64x64 blurred the strands into flat clumps), a
        // 64x64 one after.
        {
            auto map=[&](DXGI_FORMAT format, auto texel) {
                const int size=64; std::vector<uint32_t> px(size*size);
                for(int y=0;y<size;++y) for(int x=0;x<size;++x) px[y*size+x]=texel(x, y);
                D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=1; d.Format=format; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{px.data(), UINT(size*4), 0};
                ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
            };
            auto white=map(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, [](int, int) { return 0xFFFFFFFFu; });
            auto root=map(DXGI_FORMAT_R8G8B8A8_UNORM, [](int x, int) { return 0xFF000000u | (uint32_t(x*255/63)<<8); });            // green: 0 at the root to 1 at the end
            auto id=map(DXGI_FORMAT_R8G8B8A8_UNORM, [](int, int y) { return 0xFF000000u | (uint32_t(y<32 ? 0 : 179)<<8); });       // 0.7 in half of it
            TGPT_HairColor hair{}; hair.size=sizeof(TGPT_HairColor); hair.abi=TGPT_ABI;
            hair.diffuseMap=uint64_t(uintptr_t(white.Get())); hair.rootMap=uint64_t(uintptr_t(root.Get())); hair.idMap=uint64_t(uintptr_t(id.Get()));
            hair.diffuseTilingOffset[0]=hair.diffuseTilingOffset[1]=1.f;
            for(int i=0;i<4;++i) hair.diffuseColor[i]=1.f;
            hair.rootColor[0]=1.f; hair.rootColor[3]=1.f; hair.endColor[2]=1.f; hair.endColor[3]=1.f; hair.highlightAColor[1]=1.f; hair.highlightAColor[3]=1.f;
            hair.highlightA[0]=0.5f; hair.highlightA[1]=0.7f; hair.highlightA[2]=0.9f;
            hair.baseColorStrength=hair.globalStrength=hair.rootColorStrength=hair.endColorStrength=1.f;
            hair.highlightBlend=hair.highlightAStrength=1.f; hair.diffuseStrength=1.f;
            const int hairTile=RegisterHairColor(48, &hair);
            expect(hairTile>=4096, "a hair material's color baked into a detail tile while there are any");
            auto hm=materials[0]; hm.flags=0; hm.baseColor[0]=hm.baseColor[1]=hm.baseColor[2]=hm.baseColor[3]=1.f; hm.albedoTile=hairTile;
            hm.uvScaleOffset[0]=hm.uvScaleOffset[1]=0.25f; hm.uvScaleOffset[2]=hm.uvScaleOffset[3]=0.f; // the quad's 0-4 once across
            const uint32_t hairIndex=uint32_t(materials.size())+1;
            expect(SetMaterials(hairIndex, &hm, 1)==0, "the baked hair material");
            const M4 hw=mul(translate_scale({0.f, 3.f, 3.f}, {2.f, 1, 1.5f}), rot_x(-pi/2));
            auto shot=[&](bool withQuad) {
                auto list=instances; auto listIndices=indices;
                if(withQuad) {
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=hw.m[r][c];
                    q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                    listIndices.push_back(hairIndex); list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with the hair quad");
                std::vector<float> last;
                for(uint32_t i=0;i<4;++i) { run(TGPT_VIEW_MATERIALS, cam, cam, i ? 6100+i : 0, TGPT_OPTION_NO_DENOISER); last=readback(); }
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return last;
            };
            auto count=[&](const std::vector<float>& with, const std::vector<float>& without, int& n, int& red, int& blue, int& green) {
                n=red=blue=green=0;
                for(size_t j=0;j+2<with.size();j+=3) {
                    if(std::fabs(with[j]-without[j])+std::fabs(with[j+1]-without[j+1])+std::fabs(with[j+2]-without[j+2])<0.05) continue;
                    ++n; const float r=with[j], g=with[j+1], b=with[j+2];
                    if(r>2.5f*std::max(g, b)) ++red; else if(b>2.5f*std::max(r, g)) ++blue; else if(g>2.5f*std::max(r, b)) ++green;
                }
            };
            const auto without=shot(false), with=shot(true);
            int n, red, blue, green; count(with, without, n, red, blue, green);
            std::printf("      a baked hair quad (%d pixels): %d red at its root, %d blue at its end, %d green in its highlight\n", n, red, blue, green);
            expect(n>3000 && red>n/12 && blue>n/12 && green>n/4, "hair takes its shader's root-to-end gradient and highlight band");
            hair.rootColor[0]=0.f; hair.rootColor[1]=1.f; hair.endColor[2]=0.f; hair.endColor[1]=1.f;
            expect(RegisterHairColor(48, &hair)==hairTile, "hair colored again keeps its tile");
            const auto again=shot(true);
            int n2, red2, blue2, green2; count(again, without, n2, red2, blue2, green2);
            std::printf("      colored green since: %d red, %d blue, %d green\n", red2, blue2, green2);
            expect(red2<n2/50 && blue2<n2/50 && green2>n2*3/4, "hair colored again is baked again");
            // The detail tiles are a pool (PT-35, PT-13: the 32 there were went to a dozen characters' hair, a tile for each
            // clone of a hair material, and jars' and eyes' textures found none). What follows needs no picture until the
            // colors are looked at: the tiles' numbers say who reads what.
            // Hair of the same maps and values shares one tile: a clone of this hair reads its tile. Colored anew (red), the
            // clone gets a tile of its own and the tile it shared stays as it was for the other.
            auto colored=[&](float r, float g, float b) {
                auto h=hair; h.rootColor[0]=h.endColor[0]=h.highlightAColor[0]=r; h.rootColor[1]=h.endColor[1]=h.highlightAColor[1]=g;
                h.rootColor[2]=h.endColor[2]=h.highlightAColor[2]=b; return h;
            };
            expect(RegisterHairColor(5001, &hair)==hairTile, "hair materials of the same maps and values share one detail tile");
            const auto redHair=colored(1.f, 0.f, 0.f), blueHair=colored(0.f, 0.f, 1.f);
            const int redTile=RegisterHairColor(5001, &redHair);
            expect(redTile>=4096 && redTile!=hairTile, "hair colored anew that shared its tile gets a tile of its own");
            const auto kept=shot(true);
            int n3, red3, blue3, green3; count(kept, without, n3, red3, blue3, green3);
            auto other=hm; other.albedoTile=redTile;
            expect(SetMaterials(hairIndex, &other, 1)==0, "the quad reads the clone's tile");
            const auto moved=shot(true);
            int n4, red4, blue4, green4; count(moved, without, n4, red4, blue4, green4);
            std::printf("      a clone colored red: the tile it shared %d green, %d red; its own tile %d red, %d green\n", green3, red3, red4, green4);
            expect(green3>n3*3/4 && red3<n3/50, "the tile a hair material shared keeps its colors when that material is colored anew");
            expect(red4>n4*3/4 && green4<n4/50, "hair colored anew reads a tile baked with its new colors");
            // A hair material that is gone gives its tile back, and the next hair that takes it is baked into it.
            expect(ReleaseHairColor(5001)==1, "a hair material that is gone gives its detail tile back");
            expect(RegisterHairColor(5002, &blueHair)==redTile, "a tile given back is the next one handed out");
            const auto reused=shot(true);
            int n5, red5, blue5, green5; count(reused, without, n5, red5, blue5, green5);
            std::printf("      that tile given back and taken by blue hair: %d blue, %d red\n", blue5, red5);
            expect(blue5>n5*3/4 && red5<n5/50, "a tile given back is baked with the next hair's colors");
            expect(ReleaseHairColor(5002)==1 && SetMaterials(hairIndex, &hm, 1)==0, "the quad reads the hair's tile again");
            // 200 clones (the game clones a hair material for each character) take no tile at all.
            bool shared=true;
            for(uint64_t key=1000;key<1200;++key) shared=shared && RegisterHairColor(key, &hair)==hairTile;
            expect(shared, "200 clones of a hair material read its one tile");
            // Hair of its own colors takes a tile each: well over the 32 there were, until hair has left the pool only the tiles
            // it leaves to see-through glass and the eyes; from there on a 64x64 tile, as before.
            auto own=hair;
            std::vector<int> ownTiles; uint64_t fallbackKey=0; int fallback=-1;
            for(uint64_t key=2000;key<2400 && fallback<0;++key) {
                own.diffuseStrength=1.f+float(key-1999)/1024.f;
                const int t=RegisterHairColor(key, &own);
                if(t>=4096) ownTiles.push_back(t); else if(t>=0) { fallback=t; fallbackKey=key; }
            }
            int highest=0; for(int t:ownTiles) highest=std::max(highest, t);
            std::printf("      detail tiles: %zu hair colors of their own took one each (the highest number %d past the 64x64 tiles), the next a 64x64 tile\n",
                        ownTiles.size(), highest-4096);
            expect(ownTiles.size()>=96 && highest-4096>=96, "the pool holds well over 32 hair colors (128 detail tiles)");
            expect(fallback>=0, "hair past the last detail tile takes a 64x64 color tile");
            // Hair stops 16 tiles short of the pool's end (runtime.hpp kDetailReserve): its highest is tile 111 of 0 to 127.
            expect(highest-4096==127-16, "hair leaves exactly the last 16 detail tiles alone");
            // What hair leaves: see-through glass and an eye registered after all that hair still get detail tiles, and a clone
            // needs none.
            TGPT_EyeColor lateEye{}; lateEye.size=sizeof(TGPT_EyeColor); lateEye.abi=TGPT_ABI;
            for(int i=0;i<4;++i) { lateEye.scleraTint[i]=0.5f; lateEye.irisTint[i]=1.f; }
            lateEye.scleraTiling=lateEye.irisTiling=lateEye.pupilSize=lateEye.emissionIntensity=1.f;
            const int lateGlass=RegisterDetailTexture(5100, uint64_t(uintptr_t(root.Get()))), lateEyeTile=RegisterEyeColor(5101, &lateEye);
            expect(lateGlass>=4096 && lateEyeTile>=4096, "see-through glass and an eye registered after the hair still get detail tiles");
            expect(RegisterHairColor(1200, &hair)==hairTile, "a clone needs no free tile");
            // Given back, the tiles are handed out again: to half as many new hair colors as were let go of (the glass and the
            // eye above hold two that hair could take before).
            int freed=0;
            for(uint64_t key=2000;key<=fallbackKey;++key) freed+=ReleaseHairColor(key);
            const size_t wanted=ownTiles.size()/2;
            std::vector<int> againTiles;
            for(uint64_t key=3000;key<3000+wanted;++key) {
                own.diffuseStrength=2.f+float(key-2999)/1024.f;
                againTiles.push_back(RegisterHairColor(key, &own));
            }
            const size_t tookAgain=size_t(std::count_if(againTiles.begin(), againTiles.end(), [](int t) { return t>=4096; }));
            std::printf("      %d tiles given back by %zu hair materials (one of them on a 64x64 tile); %zu of %zu new hair colors took one again\n",
                        freed, ownTiles.size()+1, tookAgain, wanted);
            expect(freed==int(ownTiles.size()), "every hair material that is gone gives its detail tile back (a 64x64 one gives none)");
            expect(wanted>=40 && tookAgain==wanted, "tiles given back are handed out again");
            // The clones let go: none of them frees the tile while this hair's own material reads it.
            int cloneFreed=0;
            for(uint64_t key=1000;key<=1200;++key) cloneFreed+=ReleaseHairColor(key);
            expect(cloneFreed==0 && RegisterHairColor(48, &hair)==hairTile, "a shared tile stays while one hair material reads it");
            // And this hair on to a tile past the first 32, for the hair layer's check further down (its strands' row in the
            // frame constants is read by the tile's number: there were 64 rows, for 32 tiles): it lets go of its tile, another
            // hair takes that one, and it takes the highest-numbered tile, given back for it.
            const size_t top=size_t(std::max_element(againTiles.begin(), againTiles.end())-againTiles.begin());
            own.diffuseStrength=3.f;
            expect(ReleaseHairColor(48)==1 && RegisterHairColor(5200, &own)==hairTile && ReleaseHairColor(3000+top)==1, "the hair's tile and the highest one change hands");
            const int highTile=RegisterHairColor(48, &hair);
            expect(highTile==againTiles[top] && highTile-4096>=32, "this hair reads a tile past the first 32 now");
            // The pool as the next checks expect it: this hair's tile, the glass's and the eyes'.
            for(uint64_t key=3000;key<3000+wanted;++key) ReleaseHairColor(key);
            ReleaseHairColor(5200);
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
        // image with it stays much closer to the room without it than with the same cube opaque. Its shadow tinted along
        // straight lines (TGPT_OPTION_NO_CAUSTICS): a cube's hard edges bend most of the sun's light away from its shadow.
        {
            const bool previousOwn=ownRays; ownRays=true;
            auto optical=materials[10]; optical.smoothness=1.f; optical.ior=1.5f; SetMaterials(10,&optical,1);
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
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_CAUSTICS);
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
                    SetMaterials(10,&materials[10],1);
            ownRays=previousOwn;
        }
#include "pathtracing_glass_tint_checks.inl"
        // See-through glass reflects the lamps around it (pt_specular reflected_lamps): the see-through test's cube of clear
        // glass, and a lamp of 25 cm radius 2 m along the mirror direction from the cube's face towards the camera, the sun and
        // the sky off. Where a face pixel's mirror ray meets the lamp's sphere, the glass shows the lamp's radiance by the
        // Fresnel term (I / (pi r^2) for intensity I and radius r), worked out here for the pixels whose mirror ray passes
        // well inside the sphere, above the pixels whose ray passes well outside it (the Specular view, undenoised, with the
        // light the glass lets through much the same in both). Sampled from the lamp's disc, 2 % of it arrived.
        {
            const bool previousOwn=ownRays; ownRays=true;
            auto optical=materials[10]; optical.smoothness=1.f; optical.ior=1.5f; SetMaterials(10,&optical,1);
            auto dielectricF=[](float cosine) {
                const float ct=std::sqrt(1.f-(1.f-cosine*cosine)/2.25f);
                const float rs=(cosine-1.5f*ct)/(cosine+1.5f*ct), rp=(1.5f*cosine-ct)/(1.5f*cosine+ct);
                return .5f*(rs*rs+rp*rp);
            };
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
                const float fresnel=dielectricF(nv);
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
                    SetMaterials(10,&materials[10],1);
            ownRays=previousOwn;
        }
        // See-through glass reflects the sun (pt_specular): a pane of clear glass, drawn smooth and traced, turned so that the
        // camera sees the sun's mirror image at its centre, the lamp and the sky off. Over the pane's pixels whose mirror ray
        // lies within 2 degrees of the sun, the reflected light times each pixel's solid angle adds up to the sun's illuminance
        // by the Fresnel term, as a mirror's image of the sun does (a flat mirror maps the camera's rays onto as many
        // directions as they span), undenoised and unclamped. From the direct light's sample of the sun, 2.5 % of it arrived.
        {
            const bool previousOwn=ownRays; ownRays=true;
            auto optical=materials[10]; optical.smoothness=1.f; optical.ior=1.5f; SetMaterials(10,&optical,1);
            auto dielectricF=[](float cosine) {
                const float ct=std::sqrt(1.f-(1.f-cosine*cosine)/2.25f);
                const float rs=(cosine-1.5f*ct)/(cosine+1.5f*ct), rp=(1.5f*cosine-ct)/(1.5f*cosine+ct);
                return .5f*(rs*rs+rp*rp);
            };
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
            // The pane is one quad: an open sheet, which has no inside (pt_common glass_solid) and reflects as a pane's two faces
            // do with the light going to and fro between them, 2F / (1 + F) of the sun (one face's F until 2026-10-08, when
            // such a sheet was the face of a solid the ray entered and never left).
            const float face=dielectricF(dot(n, toCamera)), fresnel=2.f*face/(1.f+face);
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
                    SetMaterials(10,&materials[10],1);
            ownRays=previousOwn;
        }
        // Hair's highlights (TGPT_MATERIAL_HAIR_LOBES, pt_common hair_specular: HDRP's Kajiya-Kay hair, as the game's RealHair
        // is): a quad of hair drawn forward in the sun, turned 25 degrees off the sun's mirror angle about one of its axes, the
        // lamps and the sky off. With its strands along that axis (its flow map across the texture's u) the half vector is
        // square to them and its highlight is the lobe's peak, and pixel by pixel what HDRP's formula gives (unshifted lobes,
        // smoothness 0.5: exponent 30); with them along the other axis (the flow along v) the half vector leans 25 degrees
        // into them, and the highlight is some 20 times fainter. A plain, round specular lobe would light both alike.
        {
            const uint32_t first=index;
            const V3 hairAt{3.f, 2.6f, 3.f}; const float half=0.5f, tilt=25.f*3.14159265f/180.f;
            const V3 toCamera=normalize(cam.position-hairAt), h=normalize(sunDir+toCamera);
            const V3 t2=normalize(cross(h, V3{0, 1, 0})), t1=cross(t2, h);
            const V3 n=normalize(h*std::cos(tilt)-t1*std::sin(tilt)), b=cross(t2, n);
            M4 turn=identity(); // the quad's x (u) onto t2, its +y onto n, its z (v) onto b
            const V3 axes[3]={t2, n, b};
            for(int c=0;c<3;++c) { turn.m[0][c]=axes[c].x; turn.m[1][c]=axes[c].y; turn.m[2][c]=axes[c].z; }
            const M4 w=mul(translate_scale(hairAt, {1, 1, 1}), mul(turn, translate_scale({0, 0, 0}, {2*half, 1, 2*half})));
            auto flowMap=[&](uint32_t texel) {
                std::vector<uint32_t> px(64*64, texel);
                D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=64; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{px.data(), 64*4, 0};
                ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
            };
            auto acrossFlow=flowMap(0xFF0080FFu), alongFlow=flowMap(0xFF00FF80u); // red, green (1, 0.5): the flow along u; (0.5, 1): along v
            TGPT_HairLobes lobes{}; lobes.size=sizeof(TGPT_HairLobes); lobes.abi=TGPT_ABI; lobes.smoothnessPower=1.f;
            lobes.flowMap=uint64_t(uintptr_t(acrossFlow.Get())); const int acrossTile=RegisterHairLobes(60, &lobes);
            lobes.flowMap=uint64_t(uintptr_t(alongFlow.Get())); const int alongTile=RegisterHairLobes(61, &lobes);
            expect(acrossTile>=0 && alongTile>=0 && acrossTile!=alongTile, "hair's strands baked into lobe tiles");
            auto hair=materials[0];
            hair.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LOBES; hair.alphaCutoff=0.f; hair.albedoTile=-1;
            hair.baseColor[0]=hair.baseColor[1]=hair.baseColor[2]=0.3f; hair.baseColor[3]=1.f;
            hair.hairShift=hair.hairSecondaryShift=0.f; hair.hairSmoothnessMin=hair.hairSmoothnessMax=0.5f; hair.hairSecondarySmoothness=0.5f;
            hair.hairStrength=1.f; hair.hairSecondaryStrength=0.f; hair.hairSpecularTint[0]=hair.hairSpecularTint[1]=hair.hairSpecularTint[2]=1.f;
            const uint32_t hairIndex=uint32_t(materials.size())+6;
            objects.push_back(Object{0, w, {0}, false, false, 0, true, -1, 0.5f});
            auto list=instances; auto listIndices=indices;
            TGPT_Instance quad{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) quad.transform[r*4+c]=w.m[r][c];
            quad.mesh=quadHandle; quad.materialBase=uint32_t(listIndices.size()); quad.mask=0xFF; quad.flags=TGPT_INSTANCE_ALPHA_TEST;
            listIndices.push_back(hairIndex); list.push_back(quad);
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with a quad of hair");
            expect(SetLights(nullptr, 0)==0, "the sun alone");
            skyIntensity=1e-6f;
            auto highlight=[&](int tile) {
                hair.hairLobeTile=tile;
                expect(SetMaterials(hairIndex, &hair, 1)==0, "the hair material");
                std::vector<double> sum(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_SPECULAR, cam, cam, i ? index++ : 0, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<sum.size();++j) sum[j]+=r[j]/32.0; }
                }
                return sum;
            };
            index=first;
            const auto across=highlight(acrossTile), along=highlight(alongTile);
            // HDRP's Kajiya-Kay at each pixel of the quad's middle: 0.25 x F(L.H) x D(t2, H) x N.L, D = (n + 2) / 2pi x
            // (1 - (t.H)^2)^(n/2) with n = 30; the sun's illuminance and the exposure make it the specular view's.
            double measured=0, expected=0, faint=0; int count=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                const float dn=dot(d, n);
                if(dn>=0.f) continue;
                const V3 at=cam.position+d*(dot(hairAt-cam.position, n)/dn), q=at-hairAt;
                if(std::fabs(dot(q, t2))>half*0.8f || std::fabs(dot(q, b))>half*0.8f) continue;
                const V3 v=normalize(cam.position-at), hv=normalize(v+sunDir);
                const float th=dot(t2, hv), nl=dot(n, sunDir), lh=dot(sunDir, hv);
                const double kk=std::min(th+1.f, 1.f)*32.0/(2.0*3.14159265)*std::pow(std::max(1.0-double(th)*th, 0.0), 15.0);
                const double fresnel=0.0465+(1.0-0.0465)*std::pow(1.0-std::max(lh, 0.f), 5.0);
                const size_t j=(size_t(y)*W+x)*3;
                measured+=across[j+1]; faint+=along[j+1];
                expected+=0.25*fresnel*kk*std::max(nl, 0.f)*double(sunLux)*double(exposure); ++count;
            }
            { std::vector<float> f(across.size()); for(size_t j=0;j<f.size();++j) f[j]=float(across[j]); save(f, "44-hair-highlight.png", true); }
            std::printf("      hair's highlight in the sun (%d pixels): strands square to the half vector %.5f (%.5f from HDRP's formula), along it %.5f (%.1fx fainter)\n",
                count, measured/std::max(count, 1), expected/std::max(count, 1), faint/std::max(count, 1), measured/std::max(faint, 1e-12));
            expect(count>100, "the quad of hair is in view");
            expect(measured>expected*0.8 && measured<expected*1.2, "hair's highlight is HDRP's Kajiya-Kay lobe at its peak");
            expect(measured>faint*8.0, "hair's highlight lies along its strands (the half vector leaning into them dims it)");
            objects.pop_back();
            skyIntensity=1;
            expect(SetLights(&light, 1)==0, "one light again");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the hair");
        }
        // Hair HDRP drew as a whole card (held opaque, the game has no build of its shader that cuts it then): a black card of
        // strands with a checkered alpha, drawn forward whole, 1 cm in front of a white surface only the traced scene has. The
        // camera ray cuts the card by its own texel's alpha (STRANDS_CAMERA_CUT) and finds the white surface through its
        // transparent squares: half the card's pixels are white. Stopping at the card wherever it met it, all were black.
        {
            const V3 cardAt{-1.2f, 2.4f, 3.f}; const float half=0.5f;
            const V3 n=normalize(cam.position-cardAt), t=normalize(cross(n, V3{1, 0, 0})), b=cross(t, n);
            M4 turn=identity();
            const V3 axes[3]={t, n, b};
            for(int c=0;c<3;++c) { turn.m[0][c]=axes[c].x; turn.m[1][c]=axes[c].y; turn.m[2][c]=axes[c].z; }
            auto place=[&](const V3& at) { return mul(translate_scale(at, {1, 1, 1}), mul(turn, translate_scale({0, 0, 0}, {2*half, 1, 2*half}))); };
            const M4 cardW=place(cardAt), behindW=place(cardAt-n*0.01f);
            auto card=materials[6]; card.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_STRANDS; card.alphaTile=-1; card.alphaCutoff=0.f;
            card.baseColor[0]=card.baseColor[1]=card.baseColor[2]=0.f; card.baseColor[3]=1.f;
            const uint32_t cardIndex=uint32_t(materials.size())+7;
            expect(SetMaterials(cardIndex, &card, 1)==0, "a card of strands with a checkered alpha");
            objects.push_back(Object{0, cardW, {0}, false, false, 0, true, -1, 0.5f});
            auto list=instances; auto listIndices=indices;
            for(int k=0;k<2;++k) {
                TGPT_Instance q{}; const M4& w=k ? behindW : cardW;
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=k ? 0u : TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(k ? 0u : cardIndex); list.push_back(q);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the card before a white surface");
            std::vector<float> px;
            for(uint32_t i=0;i<4;++i) { run(TGPT_VIEW_ALBEDO, cam, cam, i ? 7200+i : 0, TGPT_OPTION_NO_DENOISER); px=readback(); }
            int count=0, white=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                const float dn=dot(d, n);
                if(dn>=0.f) continue;
                const V3 q=cam.position+d*(dot(cardAt-cam.position, n)/dn)-cardAt;
                if(std::fabs(dot(q, t))>half*0.8f || std::fabs(dot(q, b))>half*0.8f) continue;
                const size_t j=(size_t(y)*W+x)*3; ++count;
                if(0.2126f*px[j]+0.7152f*px[j+1]+0.0722f*px[j+2]>0.3f) ++white;
            }
            save(px, "45-hair-card-cut.png", true);
            std::printf("      a whole card of strands drawn before a white surface (%d pixels): %d white through its transparent squares (%.0f %%)\n",
                count, white, 100.0*white/std::max(count, 1));
            expect(count>500, "the card is in view");
            expect(white>count*35/100 && white<count*65/100, "hair HDRP drew as a whole card is cut by its own alpha, the surface behind shows through");
            objects.pop_back();
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the card");
        }
        // Hair drawn as a layer over HDRP's surface (TGPT_MATERIAL_HAIR_LAYER, pt_hair): a red card HDRP doesn't draw (the plugin
        // hides it from HDRP), its alpha checkered and kept at its texture's own size (the strand atlas), 2 cm in front of a grey
        // wall HDRP draws. Its full squares are 0.3 in the texture with RealHair's _AlphaPower 0.5: 0.55 as the game takes it, at
        // least its depth prepass (0.5), so the game draws them whole. The albedo view: over the card, the card's own red where
        // its squares are full (none of the wall mixed in) and the wall's grey where they are empty, about half each. Lit (no
        // denoiser), the red pixels are neither black nor blown out. Until 2026-10-06 the card was the unit quad, whose texture
        // runs 0 to 4: 32 squares across 52 pixels, finer than a pixel's footprint, which the layer rightly averaged to pink;
        // and the layer covered a texel by its texture's alpha alone (0.3 of the pixel here, the wall showing through the rest).
        {
            const V3 wallAt{-1.2f, 2.4f, 3.f}; const float half=0.5f;
            const V3 n=normalize(cam.position-wallAt), t=normalize(cross(n, V3{1, 0, 0})), b=cross(t, n);
            M4 turn=identity();
            const V3 axes[3]={t, n, b};
            for(int c=0;c<3;++c) { turn.m[0][c]=axes[c].x; turn.m[1][c]=axes[c].y; turn.m[2][c]=axes[c].z; }
            auto place=[&](const V3& at, float s) { return mul(translate_scale(at, {1, 1, 1}), mul(turn, translate_scale({0, 0, 0}, {2*s, 1, 2*s}))); };
            const V3 cardAt=wallAt+n*0.02f;
            const M4 cardW=place(cardAt, half), wallW=place(wallAt, half*1.6f);
            std::vector<uint32_t> texels(64*64);
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) texels[size_t(y)*64+x]=((x/8+y/8)%2==0 ? 77u<<24 : 0u)|0x00FFFFFFu; // 0.3 and 0
            D3D11_TEXTURE2D_DESC td{}; td.Width=td.Height=64; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            td.SampleDesc.Count=1; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init{texels.data(), 64*4, 0};
            ComPtr<ID3D11Texture2D> checkered; device->CreateTexture2D(&td, &init, &checkered);
            TGPT_HairColor hair{}; hair.size=sizeof(TGPT_HairColor); hair.abi=TGPT_ABI;
            hair.diffuseMap=uint64_t(uintptr_t(checkered.Get()));
            hair.diffuseTilingOffset[0]=hair.diffuseTilingOffset[1]=1.f;
            for(int i=0;i<4;++i) hair.diffuseColor[i]=1.f;
            hair.rootColor[0]=hair.endColor[0]=0.8f; hair.rootColor[1]=hair.endColor[1]=0.05f; hair.rootColor[2]=hair.endColor[2]=0.04f;
            hair.rootColor[3]=hair.endColor[3]=1.f;
            hair.baseColorStrength=hair.globalStrength=hair.rootColorStrength=hair.endColorStrength=1.f; hair.diffuseStrength=1.f;
            hair.alphaClip=0.1f; hair.alphaPrepass=0.5f;
            const int layerTile=RegisterHairColor(48, &hair);
            expect(layerTile>=4096, "layer hair's color in its detail tile");
            expect(layerTile-4096>=32, "the layer card's tile lies past the first 32 detail tiles, and its strands' row with it");
            auto card=materials[0]; card.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LAYER;
            card.albedoTile=layerTile; card.alphaTile=-1; card.alphaCutoff=0.f;
            card.baseColor[0]=card.baseColor[1]=card.baseColor[2]=card.baseColor[3]=1.f;
            card.uvScaleOffset[0]=card.uvScaleOffset[1]=1.f; card.uvScaleOffset[2]=card.uvScaleOffset[3]=0.f;
            card.hairAlpha=0x3800u|(0x3C00u<<16); // RealHair's _AlphaPower 0.5 and _AlphaRemap 1, as halves
            // Glossier than the wall (roughness 0.1 against the 0.6 HDRP's normal buffer gives it), which only Ray Reconstruction's roughness guide shows: hair
            // without highlights is lit by Lambert alone (pt_hair hair_bsdf).
            card.smoothness=0.9f;
            const uint32_t cardIndex=uint32_t(materials.size())+8;
            expect(SetMaterials(cardIndex, &card, 1)==0, "a card of layer hair with a checkered alpha");
            objects.push_back(Object{0, wallW, {0}});
            auto list=instances; auto listIndices=indices;
            for(int k=0;k<2;++k) {
                TGPT_Instance q{}; const M4& w=k ? wallW : cardW;
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                // The card's texture once across it (8 squares of about 6 pixels each); the wall the unit quad.
                q.mesh=k ? quadHandle : cardHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=k ? 0u : TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(k ? 0u : cardIndex); list.push_back(q);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "layer hair before a wall HDRP draws");
            std::vector<float> albedo, lit;
            // The albedo view's mean over 16 frames (after two for the view to arrive), what the picture converges to: a pixel a
            // strand covers in part is the hair in some frames and the wall in the others (the pixel's ray stops at the card as
            // often as it covers), so one frame says nothing of how much of it the hair covers.
            {
                std::vector<double> sum(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<18;++i) {
                    run(TGPT_VIEW_ALBEDO, cam, cam, i ? 7300+i : 0, TGPT_OPTION_NO_DENOISER);
                    if(i>=2) { const auto px=readback(); for(size_t j=0;j<sum.size();++j) sum[j]+=px[j]/16.0; }
                }
                albedo.assign(sum.begin(), sum.end());
            }
            for(uint32_t i=0;i<24;++i) { run(TGPT_VIEW_NONE, cam, cam, i ? 7400+i : 0, TGPT_OPTION_NO_DENOISER); lit=readback(); }
            int count=0, red=0, grey=0, redShown=0; double redLit=0, greyLit=0; bool finite=true;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                const float dn=dot(d, n);
                if(dn>=0.f) continue;
                const V3 q=cam.position+d*(dot(cardAt-cam.position, n)/dn)-cardAt;
                if(std::fabs(dot(q, t))>half*0.8f || std::fabs(dot(q, b))>half*0.8f) continue;
                const size_t j=(size_t(y)*W+x)*3; ++count;
                const float ar=albedo[j], ag=albedo[j+1], ab=albedo[j+2];
                const double l=0.2126*lit[j]+0.7152*lit[j+1]+0.0722*lit[j+2];
                if(!std::isfinite(l)) finite=false;
                // Red: the card's own color (0.8, 0.05, 0.04), none of the wall's grey (0.73) through it: a full square covered
                // 0.55 of the pixel would be 0.36 in green, 0.3 of it 0.53. Grey: the wall at least a third of the pixel
                // (green and blue above 0.3). The squares' edges, filtered at the pixel's footprint and remapped by the power
                // 0.5, leave faint strands a pixel or two into the empty squares, as the game's shader does.
                if(ar>0.3f && ag<0.1f && ab<0.1f) { ++red; redLit+=l; if(lit[j]>1.8f*lit[j+1] && lit[j]>1.8f*lit[j+2]) ++redShown; }
                else if(ag>0.3f && ab>0.3f) { ++grey; greyLit+=l; }
            }
            save(albedo, "46-hair-layer-albedo.png", true); save(lit, "47-hair-layer.png", true);
            redLit/=std::max(red, 1); greyLit/=std::max(grey, 1);
            std::printf("      layer hair over a wall HDRP draws (%d pixels): %d red strands, %d grey wall between them; lit %.4f on the strands, %.4f on the wall\n",
                count, red, grey, redLit, greyLit);
            expect(count>500, "the layer card is in view");
            expect(red>count*30/100 && red<count*70/100 && grey>count*30/100, "layer hair covers the wall HDRP drew where its alpha is, the wall shows between its strands");
            expect(finite && redLit>0.03*greyLit && redLit<2.0*greyLit, "layer hair is lit, neither black nor blown out");
            // The picture itself: the hair's own light where the pixel's ray stopped at it, not the wall's under the G-buffer's
            // colour (the composite takes the texel the tracer's own ray made on a pixel of the G-buffer: a brow over skin).
            std::printf("      of its %d red pixels, %d are red in the lit picture too\n", red, redShown);
            expect(redShown>red*85/100, "hair before a surface of the G-buffer shows as hair in the picture (the pixel takes its own ray's light)");
            // Ray Reconstruction's guides over the layer (pt_hair layer_guides, d3d11.hlsl traced_guides): the hair's own color,
            // by its cover, over the wall's that HDRP's GBuffer holds there (the albedo view composes the same two), the card's
            // red where its squares are full and the wall's grey between them; the card's roughness (0.1) where it covers, the
            // wall's (0.6) between. Until 2026-10-06 (PT-01) every pixel of the card had the wall's guides, and Ray
            // Reconstruction took the strands' light for noise on the wall: on Ron's crown it kept 0.36 of the fine detail the
            // undenoised picture has against the game's, 0.63.
            {
                auto getGuidesLayer=reinterpret_cast<decltype(&TGPT_GetGuides)>(GetProcAddress(pt, "TGPT_GetGuides"));
                rayReconstruction=true;
                for(uint32_t i=0;i<6;++i) run(TGPT_VIEW_NONE, cam, cam, 7500+i);
                rayReconstruction=false;
                TGPT_Guides gd{sizeof(TGPT_Guides), TGPT_ABI};
                const bool have=getGuidesLayer && getGuidesLayer(&gd)==0 && gd.width==W && gd.height==H && gd.extrasFrameIndex==gd.frameIndex &&
                                gd.diffuseAlbedo && gd.specularAlbedo && gd.normalRoughness;
                expect(have, "Ray Reconstruction's guides and the composite's extras over layer hair");
                if(have) {
                    const auto gDiffuse=readGuide(gd.diffuseAlbedo), gSpecular=readGuide(gd.specularAlbedo), gNormal=readGuide(gd.normalRoughness);
                    int area=0, redN=0, greyN=0, bareN=0; double err=0, redGuide[3]{}, greyGuide[3]{}, redRough=0, bareRough=0; bool sane=true;
                    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                        const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                        float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                        const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                        const float dn=dot(d, n);
                        if(dn>=0.f) continue;
                        const V3 q=cam.position+d*(dot(cardAt-cam.position, n)/dn)-cardAt;
                        if(std::fabs(dot(q, t))>half*0.8f || std::fabs(dot(q, b))>half*0.8f) continue;
                        const size_t j=(size_t(y)*W+x)*3, k=(size_t(y)*W+x)*4; ++area;
                        for(int c=0;c<3;++c) err+=std::fabs(double(gDiffuse[k+c])-albedo[j+c])/3.0;
                        const double len=std::sqrt(double(gNormal[k])*gNormal[k]+double(gNormal[k+1])*gNormal[k+1]+double(gNormal[k+2])*gNormal[k+2]);
                        if(!(std::fabs(len-1.0)<0.05) || !(gSpecular[k+1]>0.005f && gSpecular[k+1]<0.2f)) sane=false;
                        const float ar=albedo[j], ag=albedo[j+1], ab=albedo[j+2];
                        if(ar>0.3f && ag<0.1f && ab<0.1f) { ++redN; for(int c=0;c<3;++c) redGuide[c]+=gDiffuse[k+c]; redRough+=gNormal[k+3]; }
                        else if(ag>0.3f && ab>0.3f) { ++greyN; for(int c=0;c<3;++c) greyGuide[c]+=gDiffuse[k+c]; }
                        // The wall bare (no strand reaching into the pixel: the wall's grey of 0.73 whole), whose roughness (0.6,
                        // HDRP's normal buffer) the guide keeps; partly covered pixels blend it with the hair's.
                        if(ag>0.65f && ab>0.65f) { ++bareN; bareRough+=gNormal[k+3]; }
                    }
                    err/=std::max(area, 1);
                    for(int c=0;c<3;++c) { redGuide[c]/=std::max(redN, 1); greyGuide[c]/=std::max(greyN, 1); }
                    redRough/=std::max(redN, 1); bareRough/=std::max(bareN, 1);
                    std::printf("      Ray Reconstruction's guides over layer hair (%d pixels): diffuse %.3f from the albedo view's on average; on the strands %.3f %.3f %.3f, "
                                "roughness %.2f; on the wall between %.3f %.3f %.3f; roughness where it is bare (%d pixels) %.2f\n",
                        area, err, redGuide[0], redGuide[1], redGuide[2], redRough, greyGuide[0], greyGuide[1], greyGuide[2], bareN, bareRough);
                    expect(area>500 && redN>area*30/100 && greyN>area*30/100, "the layer card is in view, strands and wall");
                    expect(redGuide[0]>0.3 && redGuide[1]<0.1 && redGuide[2]<0.1 && greyGuide[1]>0.5 && greyGuide[2]>0.5,
                           "the diffuse albedo guide is the hair's own color where it covers the pixel, the wall's between its strands");
                    expect(err<0.04, "the diffuse albedo guide blends the hair's color over the wall's by the hair's cover, as the albedo view");
                    expect(bareN>area*10/100 && redRough<0.2 && bareRough>0.55, "the roughness guide is the hair's where it covers the pixel, the wall's between its strands");
                    expect(sane, "the normal guide over layer hair is a unit normal, the specular albedo guide a dielectric's");
                }
            }
            // HDRP's GBuffer3 of the surface behind the hair (its emission and what HDRP bakes into it): the composite adds it
            // to a deferred surface's traced light, and hair in front covers as much of it as of the surface's light. The wall
            // drawn again with light in its GBuffer3 (the traced wall as before): the wall between the strands gains it, the
            // full squares (hair over the whole pixel) next to none. Added whole until 2026-10-06 (round 4): a crown its hair
            // covered showed the GBuffer3 of the skin under it at full strength, a pale veil sharp along the skull's outline.
            {
                objects.back().raster=7;
                std::vector<float> glow;
                for(uint32_t i=0;i<24;++i) { run(TGPT_VIEW_NONE, cam, cam, i ? 7600+i : 0, TGPT_OPTION_NO_DENOISER); glow=readback(); }
                objects.back().raster=-1;
                int glowRed=0, glowGrey=0; double redGlow=0, greyGlow=0;
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                    float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                    const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                    const float dn=dot(d, n);
                    if(dn>=0.f) continue;
                    const V3 q=cam.position+d*(dot(cardAt-cam.position, n)/dn)-cardAt;
                    if(std::fabs(dot(q, t))>half*0.8f || std::fabs(dot(q, b))>half*0.8f) continue;
                    const size_t j=(size_t(y)*W+x)*3;
                    const double l=0.2126*glow[j]+0.7152*glow[j+1]+0.0722*glow[j+2];
                    if(albedo[j]>0.3f && albedo[j+1]<0.1f && albedo[j+2]<0.1f) { ++glowRed; redGlow+=l; }
                    else if(albedo[j+1]>0.3f && albedo[j+2]>0.3f) { ++glowGrey; greyGlow+=l; }
                }
                save(glow, "47b-hair-layer-over-glow.png", true);
                redGlow/=std::max(glowRed, 1); greyGlow/=std::max(glowGrey, 1);
                const double wallGain=greyGlow-greyLit, hairGain=redGlow-redLit;
                std::printf("      the wall behind with light in HDRP's GBuffer3: the wall between the strands %.4f -> %.4f (+%.4f), the strands over it %.4f -> %.4f (+%.4f, %.0f %% of the wall's gain)\n",
                    greyLit, greyGlow, wallGain, redLit, redGlow, hairGain, 100.0*hairGain/std::max(wallGain, 1e-9));
                expect(wallGain>0.1*greyLit, "the GBuffer3 of a surface shows where hair leaves it bare");
                expect(hairGain<0.15*wallGain, "hair covering the pixel hides the GBuffer3 of the surface behind it (no pale veil over a crown)");
            }
            // PT-53: hair stays where it is while the camera moves. The card of layer hair a quarter of the way from the camera
            // to the wall HDRP draws (3 m and 12 m), the camera moved 10 cm sideways from one frame to the next (until
            // 2026-10-08: between the traced frame and the one it was shown in; a frame's light is shown in that frame now,
            // PT-84). HDRP has no hair (the plugin hides it), so its motion vectors there are the wall's: laid into the
            // wall's texels (until 2026-10-07), the card moved with the wall, about 15 pixels off where it is (the owner saw the
            // hair slide on the heads as the player moved sideways, without any denoiser). Each pixel's ray in the frame shown
            // meets the card's plane at a full or an empty square of its checker: the red the composite shows must agree with
            // it as well as with a still camera.
            {
                const V3 camTarget{0, 2.2f, 2};
                const Camera still=make_camera(cam.position, camTarget, 60.f, float(W)/float(H));
                const float step=0.1f;
                const Camera moved=make_camera(cam.position+b*step, camTarget+b*step, 60.f, float(W)/float(H)); // b: the card's horizontal axis
                const V3 nearAt=cam.position+(cardAt-cam.position)*0.25f;
                const M4 nearW=place(nearAt, half);
                auto scene=instances; auto sceneIndices=indices;
                for(int k=0;k<2;++k) {
                    TGPT_Instance q{}; const M4& w=k ? wallW : nearW;
                    for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                    q.mesh=k ? quadHandle : cardHandle; q.materialBase=uint32_t(sceneIndices.size()); q.mask=0xFF; q.flags=k ? 0u : TGPT_INSTANCE_ALPHA_TEST;
                    sceneIndices.push_back(k ? 0u : cardIndex); scene.push_back(q);
                }
                expect(SetScene(scene.data(), uint32_t(scene.size()), sceneIndices.data(), uint32_t(sceneIndices.size()))==0, "layer hair 3 m from the camera, the wall 12 m");
                // The card drawn into the depth and the motion vectors, whole, and not its colour: as the plugin has HDRP draw hair
                // while tracing (CharacterCapture.HoldLayerDepth). The pixel's own ray decides what it shows: the card where its
                // texel covers, the wall 9 m behind it through its empty squares.
                objects.push_back(Object{0, nearW, {0}, false, false, 0, true, -1, 0.5f});
                // Per pixel of a camera's view over the card's middle (away from its squares' edges): 1 a full square, 0 an empty
                // one, -1 elsewhere.
                auto expected=[&](const Camera& c, UINT x, UINT y) {
                    const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                    float p[4]; for(int r=0;r<4;++r) p[r]=c.invViewProj.m[r][0]*cx+c.invViewProj.m[r][1]*cy+c.invViewProj.m[r][2]*0.5f+c.invViewProj.m[r][3];
                    const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                    const float dn=dot(d, n);
                    if(dn>=0.f) return -1;
                    const V3 q=c.position+d*(dot(nearAt-c.position, n)/dn)-nearAt;
                    const float u=dot(q, t)/(2.f*half)+0.5f, v=dot(q, b)/(2.f*half)+0.5f;
                    if(u<0.1f || u>0.9f || v<0.1f || v>0.9f) return -1;
                    const float su=u*8.f, sv=v*8.f;
                    if(std::fabs(su-std::round(su))<0.2f || std::fabs(sv-std::round(sv))<0.2f) return -1;
                    return (int(std::floor(su))+int(std::floor(sv)))%2==0 ? 1 : 0;
                };
                // How many of those pixels show what the card has there (red where a square is full, none where it is empty), for
                // either way round of the checker; the larger.
                auto agreement=[&](const std::vector<float>& px, const Camera& c, int& area) {
                    int agree[2]={0, 0}; area=0;
                    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                        const int e=expected(c, x, y);
                        if(e<0) continue;
                        const size_t j=(size_t(y)*W+x)*3; ++area;
                        const bool red=px[j]>1.8f*px[j+1] && px[j]>1.8f*px[j+2] && px[j]>1e-4f;
                        if(red==(e==1)) ++agree[0];
                        if(red==(e==0)) ++agree[1];
                    }
                    return double(std::max(agree[0], agree[1]))/std::max(area, 1);
                };
                std::vector<float> stillPx, movedPx, hiddenPx;
                for(uint32_t i=0;i<10;++i) { run(TGPT_VIEW_NONE, still, still, i ? 7800+i : 0, TGPT_OPTION_NO_DENOISER); stillPx=readback(); }
                run(TGPT_VIEW_NONE, moved, still, 7810, TGPT_OPTION_NO_DENOISER); movedPx=readback();
                // And with the tracer's own camera rays for every pixel (PT-59): the same depth and motion place its light.
                std::vector<float> ownPx;
                ownRays=true;
                for(uint32_t i=0;i<10;++i) run(TGPT_VIEW_NONE, still, still, i ? 7840+i : 0, TGPT_OPTION_NO_DENOISER);
                run(TGPT_VIEW_NONE, moved, still, 7850, TGPT_OPTION_NO_DENOISER); ownPx=readback();
                ownRays=false;
                // And the same card hidden from the drawn depth (as the plugin hid HDRP's hair until 2026-10-07): HDRP's depth and
                // motion vectors there are the wall's. Until 2026-10-08 that made the hair slide with the wall (the composite showed
                // the frame before's light, moved by HDRP's motion vectors; this was the check's control, "the check sees a slide").
                // Since PT-84 the composite shows the frame's own light, each pixel its own texel, and HDRP's depth and motion
                // vectors place nothing of it: the hair lies where it is, drawn into the depth or not.
                objects.pop_back();
                for(uint32_t i=0;i<10;++i) run(TGPT_VIEW_NONE, still, still, i ? 7820+i : 0, TGPT_OPTION_NO_DENOISER);
                run(TGPT_VIEW_NONE, moved, still, 7830, TGPT_OPTION_NO_DENOISER); hiddenPx=readback();
                save(stillPx, "47c-hair-layer-camera-still.png", true); save(movedPx, "47d-hair-layer-camera-moved.png", true);
                int stillArea=0, movedArea=0, hiddenArea=0;
                const double stillAgree=agreement(stillPx, still, stillArea), movedAgree=agreement(movedPx, moved, movedArea);
                const double hiddenAgree=agreement(hiddenPx, moved, hiddenArea);
                std::printf("      the same card hidden from the drawn depth: %.3f of %d pixels after the move\n", hiddenAgree, hiddenArea);
                std::printf("      layer hair 3 m away before a wall 12 m away (PT-53): its squares where they are, %.3f of %d pixels with the camera still, "
                            "%.3f of %d after it moved 10 cm sideways (the wall's motion would put the card about 15 pixels off)\n",
                    stillAgree, stillArea, movedAgree, movedArea);
                expect(stillArea>2000 && movedArea>2000, "the near card is in view, still and moved");
                expect(stillAgree>0.85, "layer hair lies where it is with the camera still");
                expect(movedAgree>0.85 && movedAgree>stillAgree-0.06, "hair in the drawn depth stays where it is while the camera moves (PT-53: its own depth and motion, not the wall's behind it)");
                expect(hiddenAgree>0.85 && hiddenAgree>stillAgree-0.06, "the same hair hidden from the drawn depth stays where it is too (nothing places the traced light by HDRP's motion vectors)");
                int ownArea=0;
                const double ownAgree=agreement(ownPx, moved, ownArea);
                std::printf("      with the tracer's own camera rays for every pixel: %.3f of %d pixels after the move\n", ownAgree, ownArea);
                expect(ownAgree>0.85 && ownAgree>stillAgree-0.06, "hair stays where it is while the camera moves, with the tracer's own camera rays for every pixel");
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "layer hair before a wall HDRP draws, again");
            }
            // Cards that cover part of a pixel, as a path tracer takes them: five thin red cards (the checker at 0.3 with no remap:
            // 0.3 of the pixel each, none opaque) 2 to 6 cm before the wall the G-buffer holds. Each stops the pixel's ray with
            // the chance it covers, a new draw every frame: over the full squares a frame shows the hair in 1 - 0.7^5 = 0.83 of
            // the pixels and the wall in the rest, and from frame to frame each pixel is now one, now the other. The mean of
            // the frames is the game's blend of all five. (Until 2026-10-07 a layer blended them in one pixel; a fixed cut at
            // half would show none of them, a card that always stops the ray all of them.)
            {
                std::vector<uint32_t> white(64*64, 0xFFFFFFFFu);
                D3D11_SUBRESOURCE_DATA whiteInit{white.data(), 64*4, 0};
                ComPtr<ID3D11Texture2D> opaque; device->CreateTexture2D(&td, &whiteInit, &opaque);
                TGPT_HairColor mask{}; mask.size=sizeof(TGPT_HairColor); mask.abi=TGPT_ABI;
                mask.diffuseMap=uint64_t(uintptr_t(opaque.Get()));
                mask.diffuseTilingOffset[0]=mask.diffuseTilingOffset[1]=1.f;
                for(int i=0;i<4;++i) mask.diffuseColor[i]=1.f;
                mask.baseColorStrength=1.f; mask.diffuseStrength=1.f; mask.alphaClip=0.05f; mask.alphaPrepass=0.f;
                // A whole white card's texture, for the normal map's check below. A key that holds a detail tile already (the hair
                // color test took them all): baked anew with this texture.
                const int shellTile=RegisterHairColor(1000, &mask);
                expect(shellTile>=4096, "a whole card's texture in a detail tile");
                auto thin=card; thin.hairAlpha=0u;
                expect(SetMaterials(cardIndex+1, &thin, 1)==0, "thin cards");
                auto stack=instances; auto stackIndices=indices;
                for(int k=0;k<6;++k) {
                    TGPT_Instance q{}; const M4 w=k<5 ? place(wallAt+n*(0.02f+0.01f*float(k)), half) : wallW;
                    for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                    q.mesh=k<5 ? cardHandle : quadHandle; q.materialBase=uint32_t(stackIndices.size()); q.mask=0xFF; q.flags=k<5 ? TGPT_INSTANCE_ALPHA_TEST : 0u;
                    stackIndices.push_back(k<5 ? cardIndex+1 : 0u); stack.push_back(q);
                }
                expect(SetScene(stack.data(), uint32_t(stack.size()), stackIndices.data(), uint32_t(stackIndices.size()))==0, "five thin cards of hair before the wall");
                // Sixteen frames of the albedo view (after two for the view to arrive): per pixel, in how many it was the hair. By
                // the drawn G-buffer's route (hair alone by its own rays), then with the tracer's own camera rays for every pixel.
                for(int route=0;route<2;++route) {
                ownRays=route==1;
                const int frames=16;
                std::vector<uint8_t> hairIn(size_t(W)*H, 0);
                std::vector<double> mean(size_t(W)*H*3, 0.0);
                for(int i=0;i<frames+2;++i) {
                    run(TGPT_VIEW_ALBEDO, cam, cam, i ? 7450+100*route+i : 0, TGPT_OPTION_NO_DENOISER);
                    if(i<2) continue;
                    albedo=readback();
                    for(size_t q=0;q<size_t(W)*H;++q) {
                        if(albedo[q*3]>0.55f && albedo[q*3+1]<0.3f && albedo[q*3+2]<0.3f) ++hairIn[q];
                        for(int c=0;c<3;++c) mean[q*3+c]+=albedo[q*3+c]/double(frames);
                    }
                }
                // The full squares' pixels, away from their edges (the card's texture runs once across it, 8 squares).
                int area=0, both=0; double share=0, lastShare=0, wallMean=0;
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                    float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                    const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                    const float dn=dot(d, n);
                    if(dn>=0.f) continue;
                    const V3 q=cam.position+d*(dot(cardAt-cam.position, n)/dn)-cardAt;
                    const float u=dot(q, t)/(2.f*half)+0.5f, v=dot(q, b)/(2.f*half)+0.5f;
                    if(u<0.1f || u>0.9f || v<0.1f || v>0.9f) continue;
                    const float su=u*8.f, sv=v*8.f;
                    if(std::fabs(su-std::round(su))<0.25f || std::fabs(sv-std::round(sv))<0.25f) continue;
                    const size_t j=size_t(y)*W+x;
                    // A full square by the mean: mostly red (an empty one is the wall's grey in every frame).
                    if(!(mean[j*3]>0.5 && mean[j*3+1]<0.5)) continue;
                    ++area; share+=hairIn[j]/double(frames);
                    if(albedo[j*3]>0.55f && albedo[j*3+1]<0.3f && albedo[j*3+2]<0.3f) lastShare+=1.0;
                    if(hairIn[j]>0 && hairIn[j]<frames) ++both;
                    wallMean+=std::clamp((mean[j*3+1]-0.05)/(0.73-0.05), 0.0, 1.0);
                }
                share/=std::max(area, 1); lastShare/=std::max(area, 1); wallMean/=std::max(area, 1);
                save(albedo, route ? "46c-hair-five-thin-cards-own-rays.png" : "46b-hair-five-thin-cards.png", true);
                std::printf(route ? "      the same with the tracer's own camera rays for every pixel:\n" : "");
                std::printf("      five thin cards of 0.3 before the wall (%d pixels of their full squares): the hair in %.3f of them in one frame, %.3f over %d frames "
                            "(the game's blend 0.832); %.3f of the pixels were now hair, now wall; the wall's share of the mean %.3f (0.168)\n",
                    area, lastShare, share, frames, double(both)/std::max(area, 1), wallMean);
                expect(area>120, "the thin cards' full squares are in view");
                expect(share>0.78 && share<0.88 && lastShare>0.74 && lastShare<0.92, route ? "cards that cover part of a pixel stop its ray as often as they cover it, with the tracer's own camera rays for every pixel"
                    : "cards that cover part of a pixel stop its ray as often as they cover it (the game's blend on average, in every frame)");
                expect(both>area*85/100, route ? "a new draw every frame, with the tracer's own camera rays for every pixel" : "a new draw every frame: a partly covered pixel is now the hair, now what lies behind it");
                expect(wallMean>0.12 && wallMean<0.22, route ? "the mean of the frames is the game's blend, with the tracer's own camera rays for every pixel" : "the mean of the frames is the game's blend of the five cards over the wall");
                }
                ownRays=false;
                // PT-01, 2026-10-07 (docs/agents/strands.md). Each check below: the albedo or lit view over a card, read back pixel by
                // pixel through the card's plane (its texture coordinates u along t, v along the card's own b).
                auto cardTexture=[&](UINT size, auto texel, DXGI_FORMAT format) {
                    std::vector<uint32_t> px(size_t(size)*size);
                    for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) px[size_t(y)*size+x]=texel(x, y);
                    D3D11_TEXTURE2D_DESC d{size, size, 1, 1, format, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
                    const D3D11_SUBRESOURCE_DATA init{px.data(), size*4, 0};
                    ComPtr<ID3D11Texture2D> tex; device->CreateTexture2D(&d, &init, &tex); return tex;
                };
                auto hairOf=[&](const ComPtr<ID3D11Texture2D>& map, float prepass) {
                    TGPT_HairColor c=hair; c.diffuseMap=uint64_t(uintptr_t(map.Get())); c.alphaPrepass=prepass; return c;
                };
                // Where a pixel's camera ray meets a card's plane (at, its normal nn, its axes tu and tv, size metres across): its u, v.
                auto cardUv=[&](UINT x, UINT y, const V3& at, const V3& nn, const V3& tu, const V3& tv, float& u, float& v, float size=1.f) {
                    const float cx=(float(x)+0.5f)/float(W)*2.f-1.f, cy=1.f-(float(y)+0.5f)/float(H)*2.f;
                    float p[4]; for(int r=0;r<4;++r) p[r]=cam.invViewProj.m[r][0]*cx+cam.invViewProj.m[r][1]*cy+cam.invViewProj.m[r][2]*0.5f+cam.invViewProj.m[r][3];
                    const V3 d=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
                    const float dn=dot(d, nn);
                    if(std::fabs(dn)<1e-4f) return false;
                    const V3 q=cam.position+d*(dot(at-cam.position, nn)/dn)-at;
                    u=dot(q, tu)/(2.f*half*size)+0.5f; v=dot(q, tv)/(2.f*half*size)+0.5f;
                    return true;
                };
                auto instanceAt=[&](const M4& w, int mesh, uint32_t material, std::vector<TGPT_Instance>& list, std::vector<uint32_t>& ids) {
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                    q.mesh=mesh; q.materialBase=uint32_t(ids.size()); q.mask=0xFF; q.flags=mesh==cardHandle ? TGPT_INSTANCE_ALPHA_TEST : 0u;
                    ids.push_back(material); list.push_back(q);
                };
                // A thick head of hair: sixteen red cards a centimetre apart before the wall HDRP draws, each texel 0.2 (below the
                // depth prepass of 0.5: blended, never whole). The game blends all sixteen: 0.8^16, 0.03 of the wall shows. With a
                // fixed eight cards and nothing after (until 2026-10-07), 0.8^8 = 0.17 of it showed through: the background seen
                // through the top of a head of hair, as a veil (the owner's npc-day run 20261007-081218). The cards past the eighth
                // stop the ray by chance (pt_hair layer_tail): over the stack's pixels, the wall's share averages to the game's.
                {
                    const auto thinMap=cardTexture(64, [](UINT, UINT) { return (51u<<24)|0x00FFFFFFu; }, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
                    TGPT_HairColor thinHair=hairOf(thinMap, 0.5f);
                    const int thinTile=RegisterHairColor(48, &thinHair);
                    auto thick=card; thick.albedoTile=thinTile; thick.hairAlpha=0u;
                    expect(thinTile>=4096 && SetMaterials(cardIndex+3, &thick, 1)==0, "cards of hair whose texels cover 0.2 each");
                    auto list=instances; auto ids=indices;
                    for(int k=0;k<16;++k) instanceAt(place(wallAt+n*(0.02f+0.01f*float(k)), half), cardHandle, cardIndex+3, list, ids);
                    instanceAt(wallW, quadHandle, 0u, list, ids);
                    expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "sixteen thin cards before the wall");
                    std::vector<float> thickAlbedo, thickLit;
                    for(uint32_t i=0;i<6;++i) { run(TGPT_VIEW_ALBEDO, cam, cam, i ? 7600+i : 0, TGPT_OPTION_NO_DENOISER); thickAlbedo=readback(); }
                    for(uint32_t i=0;i<12;++i) { run(TGPT_VIEW_NONE, cam, cam, i ? 7620+i : 0, TGPT_OPTION_NO_DENOISER); thickLit=readback(); }
                    double share=0, lit=0; int pixels=0; bool finite=true;
                    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                        float u, v;
                        if(!cardUv(x, y, wallAt+n*0.17f, n, t, b, u, v) || u<0.1f || u>0.9f || v<0.1f || v>0.9f) continue;
                        const size_t j=(size_t(y)*W+x)*3;
                        // The wall's share of the pixel: its green (0.73) against the cards' (0.05).
                        share+=std::clamp((double(thickAlbedo[j+1])-0.05)/(0.73-0.05), -0.5, 1.5); ++pixels;
                        const double l=0.2126*thickLit[j]+0.7152*thickLit[j+1]+0.0722*thickLit[j+2];
                        if(!std::isfinite(l)) finite=false; else lit+=l;
                    }
                    share/=std::max(pixels, 1); lit/=std::max(pixels, 1);
                    save(thickAlbedo, "47c-hair-layer-thick-albedo.png", true); save(thickLit, "47d-hair-layer-thick.png", true);
                    std::printf("      sixteen cards of 0.2 before the wall (%d pixels): the wall shows through %.3f of the pixel (the game's blend 0.028, eight cards and no more 0.168); lit %.4f\n",
                        pixels, share, lit);
                    expect(pixels>300, "the card stack is in view");
                    expect(share<0.07 && share>-0.02, "a thick stack of hair cards over a bright background stays as opaque as the game's blend (no veil past the eighth card)");
                    expect(finite && lit>0.0, "the thick stack is lit");
                }
                // Strands at a slant, as the top of a head shows them: a card of 32 strands across (stripes 4 texels of 0.6 and 4 of
                // nothing in a 256 map, 2 m across: 3 pixels a pair here) turned 80 degrees from the camera about its u axis, so a pixel's
                // footprint is six times longer along the strands than across. The game samples hair anisotropically (its quality
                // setting forces it): across the strands at the short side's level, the strands stay apart, each texel of 0.6
                // whole (at least the prepass 0.5) and the wall between them. Read at the geometric mean of the two sides (until
                // 2026-10-07) the strands blurred into one thin even veil (0.3 of every pixel).
                {
                    const auto stripes=cardTexture(256, [](UINT x, UINT) { return ((x/4)%2==0 ? 153u<<24 : 0u)|0x00FFFFFFu; }, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
                    TGPT_HairColor striped=hairOf(stripes, 0.5f);
                    const int stripeTile=RegisterHairColor(48, &striped);
                    auto slanted=card; slanted.albedoTile=stripeTile; slanted.hairAlpha=0u;
                    expect(stripeTile>=4096 && SetMaterials(cardIndex+4, &slanted, 1)==0, "a card of fine strands");
                    const float tilt=80.f*3.14159265f/180.f;
                    const V3 n2=n*std::cos(tilt)+b*std::sin(tilt), b2=b*std::cos(tilt)-n*std::sin(tilt);
                    M4 turn2=identity(); const V3 axes2[3]={t, n2, b2};
                    for(int c=0;c<3;++c) { turn2.m[0][c]=axes2[c].x; turn2.m[1][c]=axes2[c].y; turn2.m[2][c]=axes2[c].z; }
                    const V3 slantAt=wallAt+n*0.6f;
                    const M4 slantW=mul(translate_scale(slantAt, {1, 1, 1}), mul(turn2, translate_scale({0, 0, 0}, {4*half, 1, 4*half})));
                    auto list=instances; auto ids=indices;
                    instanceAt(slantW, cardHandle, cardIndex+4, list, ids); instanceAt(wallW, quadHandle, 0u, list, ids);
                    expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "a card of strands at a slant before the wall");
                    std::vector<float> slantAlbedo;
                    for(uint32_t i=0;i<6;++i) { run(TGPT_VIEW_ALBEDO, cam, cam, i ? 7640+i : 0, TGPT_OPTION_NO_DENOISER); slantAlbedo=readback(); }
                    double s=0, ss=0; int pixels=0;
                    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                        float u, v;
                        if(!cardUv(x, y, slantAt, n2, t, b2, u, v, 2.f) || u<0.1f || u>0.9f || v<0.15f || v>0.85f) continue;
                        const size_t j=(size_t(y)*W+x)*3;
                        const double hairShare=std::clamp((0.73-double(slantAlbedo[j+1]))/(0.73-0.05), -0.5, 1.5);
                        s+=hairShare; ss+=hairShare*hairShare; ++pixels;
                    }
                    const double mean=s/std::max(pixels, 1), spread=std::sqrt(std::max(ss/std::max(pixels, 1)-mean*mean, 0.0));
                    save(slantAlbedo, "47e-hair-layer-strands-at-a-slant.png", true);
                    std::printf("      strands on a card turned 80 degrees from the camera (%d pixels): the hair's share of a pixel %.3f on average, spread %.3f (strands apart: about 0.5; blurred into a veil: under 0.2)\n",
                        pixels, mean, spread);
                    expect(pixels>300, "the slanted card is in view");
                    expect(spread>0.3, "strands at a slant stay apart, read anisotropically as the game reads them (no even veil)");
                }
                // The game's normal map on hair: a white card facing the camera, its normal map in stripes 32 texels wide (6 pixels)
                // tilted 0.6 toward +u and -u in turn (a 256 map, shared as the game's own are), lit by a lamp off its +u side. RealHair
                // lights each texel by its normal-mapped normal, so the stripes tilted toward the lamp are lighter: the fine light and
                // dark lines along the game's hair. The tracer lit every strand of a card alike (2026-10-07: no normal map).
                if(RegisterSharedTexture) {
                    const auto bumps=cardTexture(256, [](UINT x, UINT) { return (x/32)%2==0 ? 0xFFFF80CCu : 0xFFFF8033u; }, DXGI_FORMAT_R8G8B8A8_UNORM); // red 204 (+0.6) or 51 (-0.6), green 128
                    TGPT_SharedTexture info{}; info.size=sizeof(info); info.abi=TGPT_ABI;
                    const int32_t shared=RegisterSharedTexture(uint64_t(uintptr_t(bumps.Get())), &info);
                    const int32_t bumpHandle=shared>=0 ? info.handle : -1;
                    auto bumpy=card; bumpy.albedoTile=shellTile; bumpy.hairAlpha=0u; bumpy.normalTexture=bumpHandle; bumpy.normalScale=1.f;
                    bumpy.baseColor[0]=bumpy.baseColor[1]=bumpy.baseColor[2]=bumpy.baseColor[3]=1.f;
                    expect(bumpHandle>=0 && SetMaterials(cardIndex+5, &bumpy, 1)==0, "a card of hair with the game's normal map, shared");
                    const V3 bumpAt=wallAt+n*0.3f;
                    auto list=instances; auto ids=indices;
                    instanceAt(place(bumpAt, half), cardHandle, cardIndex+5, list, ids); instanceAt(wallW, quadHandle, 0u, list, ids);
                    expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "a card of hair with a normal map");
                    std::vector<double> sum(size_t(W)*H*3, 0.0);
                    sunOn=false; // the lamp alone: the sun, from behind the wall, lights part of the card from the other side
                    TGPT_Light lamp=light; const V3 lampAt=bumpAt+t*1.2f+n*0.5f;
                    lamp.position[0]=lampAt.x; lamp.position[1]=lampAt.y; lamp.position[2]=lampAt.z;
                    expect(SetLights(&lamp, 1)==0, "a lamp off the card's +u side");
                    for(uint32_t i=0;i<24;++i) { run(TGPT_VIEW_NONE, cam, cam, i ? 7660+i : 0, TGPT_OPTION_NO_DENOISER); if(i>=8) { const auto px=readback(); for(size_t j=0;j<sum.size();++j) sum[j]+=px[j]/16.0; } }
                    double lum[2]={0, 0}; int count[2]={0, 0};
                    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                        float u, v;
                        if(!cardUv(x, y, bumpAt, n, t, b, u, v) || u<0.1f || u>0.9f || v<0.1f || v>0.9f) continue;
                        // Pixels well inside a stripe (a pixel is about 5 texels here): its texel column at least 8 from an edge.
                        const float tx=u*256.f; const int col=int(tx)%32;
                        if(col<8 || col>23) continue;
                        const int k=(int(tx)/32)%2;
                        const size_t j=(size_t(y)*W+x)*3;
                        lum[k]+=0.2126*sum[j]+0.7152*sum[j+1]+0.0722*sum[j+2]; ++count[k];
                    }
                    sunOn=true; expect(SetLights(&light, 1)==0, "one light again");
                    for(int k=0;k<2;++k) lum[k]/=std::max(count[k], 1);
                    const V3 lampDir=normalize(lampAt-bumpAt);
                    const bool plusFacesLamp=dot(lampDir, t)>0.f;
                    const double toward=plusFacesLamp ? lum[0] : lum[1], away=plusFacesLamp ? lum[1] : lum[0];
                    std::vector<float> img(sum.begin(), sum.end()); save(img, "47f-hair-layer-normal-map.png", true);
                    std::printf("      a card of hair with the game's normal map in stripes (%d and %d pixels): tilted toward the lamp %.4f, away %.4f (x%.2f)\n",
                        count[0], count[1], toward, away, toward/std::max(away, 1e-9));
                    expect(count[0]>100 && count[1]>100, "the normal-mapped card is in view");
                    expect(toward>1.5*away, "hair's strands take the light by the game's normal map: those tilted toward a lamp lighter");
                }
            }
            objects.pop_back();
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the layer card");
        }
        // The tracer's own camera rays for every pixel (PT-59; the owner, 2026-10-07: "make sure WE trace the world, not the
        // game"): each pixel's surface is what its own ray finds in the traced scene, and the drawn G-buffer is only what that
        // hit is compared with: the yardstick (TGPT_VIEW_YARDSTICK, TGPT_GetYardstick).
        {
            const uint32_t raw=TGPT_OPTION_NO_DENOISER;
            ownRays=true;
            // The view's colours counted: 0 black (sky for both), 1 dim green (the same surface, read the same), 2 yellow,
            // 3 blue, 4 cyan (another colour, normal, roughness), 5 grey (drawn forward), 6 orange (nearer), 7 red (farther),
            // 8 magenta (nothing traced), 9 purple (traced over sky), 10 anything else (a pixel whose motion differs is whitened).
            auto colours=[&](const std::vector<float>& px, int counts[11]) {
                for(int i=0;i<11;++i) counts[i]=0;
                for(size_t q=0;q<size_t(W)*H;++q) {
                    const float r=px[q*3], gr=px[q*3+1], b=px[q*3+2];
                    auto is=[&](float cr, float cg, float cb) { return std::fabs(r-cr)<0.04f && std::fabs(gr-cg)<0.04f && std::fabs(b-cb)<0.04f; };
                    ++counts[is(0, 0, 0) ? 0 : is(0.03f, 0.2f, 0.03f) ? 1 : is(1, 0.9f, 0) ? 2 : is(0.1f, 0.3f, 1) ? 3 : is(0, 0.9f, 0.9f) ? 4 : is(0.25f, 0.25f, 0.25f) ? 5 :
                             is(1, 0.45f, 0) ? 6 : is(1, 0, 0) ? 7 : is(1, 0, 1) ? 8 : is(0.45f, 0, 0.9f) ? 9 : 10];
                }
            };
            auto report=[&](const char* what, const TGPT_Yardstick& y) {
                const double all=std::max(y.pixels, 1u);
                std::printf("      the yardstick, %s: the same surface %.2f %% (another colour %u, normal %u, roughness %u, drawn forward %u pixels), nearer %.2f %%, farther %.2f %%, "
                            "nothing traced %.2f %%, traced over sky %.2f %%, sky %.2f %%; motion differs %.2f %%; materials:", what, 100.0*y.where[1]/all, y.colour, y.normal, y.roughness, y.forward,
                            100.0*y.where[2]/all, 100.0*y.where[3]/all, 100.0*y.where[4]/all, 100.0*y.where[5]/all, 100.0*y.where[0]/all, 100.0*y.motion/all);
                for(uint32_t i=0;i<y.materialCount && i<6;++i) std::printf(" %u (%u of %u)", y.materials[i].material, y.materials[i].differing, y.materials[i].pixels);
                std::printf("\n");
            };
            auto yardstick=[&](uint32_t base, const Camera& now, const Camera& before, TGPT_Yardstick& y, std::vector<float>* picture) {
                for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_YARDSTICK, cam, cam, i ? base+i : 0, raw);
                run(TGPT_VIEW_YARDSTICK, now, before, base+3, raw);
                const bool have=yardstickOf(base+3, now, raw, y, nullptr);
                if(picture) *picture=readback();
                return have;
            };
            // The room as it stands.
            TGPT_Yardstick room{sizeof(TGPT_Yardstick), TGPT_ABI};
            std::vector<float> view;
            const bool haveRoom=yardstick(9000, cam, cam, room, &view);
            save(view, "70-yardstick.png", false);
            report("the room", room);
            int seen[11]; colours(view, seen);
            uint32_t sum=0; for(int i=0;i<6;++i) sum+=room.where[i];
            expect(haveRoom && room.pixels==W*H && sum==room.pixels, "the yardstick counts every pixel of a frame");
            std::printf("      its view: %d the same and read the same, %d another colour, %d normal, %d roughness, %d forward, %d nearer, %d farther, %d nothing traced, %d over sky, %d sky, %d other\n",
                seen[1], seen[2], seen[3], seen[4], seen[5], seen[6], seen[7], seen[8], seen[9], seen[0], seen[10]);
            const int sameSeen=seen[1]+seen[2]+seen[3]+seen[4]+seen[5];
            expect(std::abs(sameSeen-int(room.where[1]))<int(W*H/100) && std::abs(seen[6]-int(room.where[2]))<int(W*H/100) && std::abs(seen[7]-int(room.where[3]))<int(W*H/100),
                   "the yardstick's view shows what its numbers count");
            // What moves with the camera alone: a hit's own motion is the drawn motion vector's (the same rule, HDRP's).
            const Camera aside=make_camera(cam.position+V3{0.1f, 0, 0}, V3{0.1f, 2.2f, 2}, 60.f, float(W)/float(H));
            TGPT_Yardstick walked{sizeof(TGPT_Yardstick), TGPT_ABI};
            const bool haveWalked=yardstick(9020, aside, cam, walked, nullptr);
            report("the camera moved 10 cm sideways", walked);
            expect(haveWalked && walked.motion<walked.pixels/200, "a still world's own motion under a moving camera is the drawn motion vectors'");
            // Something only the traced scene has (a cube HDRP does not draw), and something only drawn (a slab the traced scene
            // lacks): the yardstick finds both, the first by its material.
            {
                auto extra=materials[1];
                const uint32_t extraIndex=uint32_t(materials.size())+20;
                expect(SetMaterials(extraIndex, &extra, 1)==0, "a material for what only the traced scene has");
                auto list=instances; auto ids=indices;
                TGPT_Instance q{}; const M4 w=translate_scale({2.5f, 3.6f, 0.0f}, {1.2f, 1.2f, 1.2f}); // in the air, right of the view's middle
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
                q.mesh=cubeHandle; q.materialBase=uint32_t(ids.size()); q.mask=0xFF; q.flags=0u;
                ids.push_back(extraIndex); ids.push_back(extraIndex); list.push_back(q);
                expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "a cube only the traced scene has");
                objects.push_back(Object{0, translate_scale({-2.5f, 1.5f, 1.0f}, {2.0f, 1.0f, 2.0f}), {0}});
                TGPT_Yardstick gaps{sizeof(TGPT_Yardstick), TGPT_ABI};
                std::vector<float> gapView;
                const bool haveGaps=yardstick(9040, cam, cam, gaps, &gapView);
                save(gapView, "71-yardstick-gaps.png", false);
                report("a cube only traced and a slab only drawn", gaps);
                objects.pop_back();
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                expect(haveGaps && gaps.where[2]>room.where[2]+2000, "the yardstick finds what only the traced scene has (the tracer's hit nearer)");
                expect(gaps.where[3]>room.where[3]+2000, "the yardstick finds what the traced scene lacks (the tracer's hit farther)");
                bool named=false; for(uint32_t i=0;i<gaps.materialCount && i<3;++i) named=named || gaps.materials[i].material==extraIndex;
                expect(named, "the yardstick names the material of what differs");
                // PT-64: and how it differs. The cube is in another place than what HDRP drew there (the geometry's part, not the
                // material's): its differing pixels count as "place", none as colour, normal or roughness.
                for(uint32_t i=0;i<gaps.materialCount;++i) if(gaps.materials[i].material==extraIndex) {
                    const auto& m=gaps.materials[i];
                    std::printf("      the cube only traced, by how it differs: %u pixels, %u differing: place %u, colour %u, normal %u, roughness %u\n", m.pixels, m.differing, m.place, m.colour, m.normal, m.roughness);
                    expect(m.place>m.differing*9/10 && m.colour==0 && m.normal==0 && m.roughness==0, "the yardstick tells a material's pixels in another place from those read otherwise");
                }
            }
            // A material read otherwise than HDRP drew it: the floor's traced material turned dark (the drawn floor stays white)
            // and smooth. Its pixels are on the same surface, of another colour and another roughness; none in another place.
            {
                auto changed=materials;
                changed[0].baseColor[0]=changed[0].baseColor[1]=changed[0].baseColor[2]=0.2f; changed[0].smoothness=1.f;
                expect(SetMaterials(0, changed.data(), uint32_t(changed.size()))==0, "the floor's traced material read otherwise");
                TGPT_Yardstick read{sizeof(TGPT_Yardstick), TGPT_ABI};
                const bool haveRead=yardstick(9060, cam, cam, read, nullptr);
                expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "the harness's materials again");
                bool found=false;
                for(uint32_t i=0;i<read.materialCount;++i) if(read.materials[i].material==0u) {
                    const auto& m=read.materials[i]; found=true;
                    std::printf("      the room's white material read dark and smooth: %u pixels, %u differing: place %u, colour %u, normal %u, roughness %u\n", m.pixels, m.differing, m.place, m.colour, m.normal, m.roughness);
                    expect(m.differing>m.pixels/2 && m.colour>m.differing*9/10 && m.roughness>m.differing/2 && m.place<m.differing/10,
                           "the yardstick says how a material differs: another colour and roughness on the same surface, not another place");
                }
                expect(haveRead && found, "the yardstick lists the material that is read otherwise");
            }
            // The picture: the mean of undenoised frames by the tracer's own camera rays against the mean by the drawn G-buffer's
            // route, over the pixels the yardstick calls the same surface read the same (dim green in its view).
            auto mean=[&](uint32_t base, std::vector<double>& sum) {
                sum.assign(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<32;++i) { run(TGPT_VIEW_NONE, cam, cam, i ? base+i : 0, raw); if(i>=16) { const auto px=readback(); for(size_t j=0;j<sum.size();++j) sum[j]+=px[j]/16.0; } }
            };
            std::vector<double> own, drawn;
            mean(9100, own);
            ownRays=false;
            mean(9200, drawn);
            double ownSum=0, drawnSum=0, apart=0; int pixels=0;
            for(size_t q=0;q<size_t(W)*H;++q) {
                if(!(std::fabs(view[q*3]-0.03f)<0.04f && std::fabs(view[q*3+1]-0.2f)<0.04f && std::fabs(view[q*3+2]-0.03f)<0.04f)) continue;
                const double a=0.2126*own[q*3]+0.7152*own[q*3+1]+0.0722*own[q*3+2], d=0.2126*drawn[q*3]+0.7152*drawn[q*3+1]+0.0722*drawn[q*3+2];
                ownSum+=a; drawnSum+=d; apart+=std::fabs(a-d); ++pixels;
            }
            { std::vector<float> img(own.begin(), own.end()); save(img, "72-own-camera-rays.png", true); }
            { std::vector<float> img(drawn.begin(), drawn.end()); save(img, "73-drawn-gbuffer-route.png", true); }
            std::printf("      the picture over the %d pixels the yardstick calls the same: the tracer's own camera rays %.5f, the drawn G-buffer's route %.5f (x%.3f), apart %.5f a pixel (%.1f %%)\n",
                pixels, ownSum/std::max(pixels, 1), drawnSum/std::max(pixels, 1), ownSum/std::max(drawnSum, 1e-12), apart/std::max(pixels, 1), 100.0*apart/std::max(drawnSum, 1e-12));
            expect(pixels>int(W*H/4), "most of the room is the same surface for the tracer's own ray and the drawn G-buffer");
            expect(ownSum>0.9*drawnSum && ownSum<1.1*drawnSum, "the picture from the tracer's own camera rays is as bright as the one from the drawn G-buffer where the two agree");
            // And denoised: the tracer's built-in filters, then NRD, follow each hit's own motion (pt_common motion_at) with the
            // tracer's own camera rays. Judged without a denoiser above; here only that they run on such surfaces and keep the
            // light, with the camera still and in the frame after it stepped 10 cm aside.
            auto denoised=[&](uint32_t base, uint32_t options, const char* what) {
                ownRays=true;
                std::vector<float> px;
                for(uint32_t i=0;i<24;++i) { run(TGPT_VIEW_NONE, cam, cam, i ? base+i : 0, options); px=readback(); }
                run(TGPT_VIEW_NONE, aside, cam, base+24, options); run(TGPT_VIEW_NONE, aside, aside, base+25, options);
                const auto after=readback();
                ownRays=false;
                double sum=0, sumAfter=0; bool finite=true;
                for(size_t q=0;q<size_t(W)*H;++q) {
                    const double l=0.2126*px[q*3]+0.7152*px[q*3+1]+0.0722*px[q*3+2], m=0.2126*after[q*3]+0.7152*after[q*3+1]+0.0722*after[q*3+2];
                    if(!std::isfinite(l) || !std::isfinite(m)) { finite=false; continue; }
                    sumAfter+=m;
                    if(std::fabs(view[q*3]-0.03f)<0.04f && std::fabs(view[q*3+1]-0.2f)<0.04f && std::fabs(view[q*3+2]-0.03f)<0.04f) sum+=l;
                }
                double whole=0; for(size_t q=0;q<size_t(W)*H;++q) whole+=0.2126*px[q*3]+0.7152*px[q*3+1]+0.0722*px[q*3+2];
                std::printf("      %s: %.5f a pixel against %.5f undenoised (x%.3f); the whole picture after the camera's step x%.3f of before\n",
                    what, sum/std::max(pixels, 1), ownSum/std::max(pixels, 1), sum/std::max(ownSum, 1e-12), sumAfter/std::max(whole, 1e-12));
                expect(finite && sum>0.9*ownSum && sum<1.1*ownSum && sumAfter>0.85*whole && sumAfter<1.15*whole, what);
            };
            denoised(9300, 0, "the tracer's own camera rays with its built-in filters keep the light, still and after a step of the camera");
            denoised(9400, TGPT_OPTION_NRD, "the tracer's own camera rays with NRD keep the light, still and after a step of the camera");
        }
        // Light scattered under skin (pt_walk.hlsl): the floor a material light walks inside, its profile scattering 3 cm, met by
        // the tracer's own camera rays (PT-59; until PT-58 the floor was drawn as skin in HDRP's G-buffer, with a list of
        // profiles). Its light is the walk's (a random walk under the traced floor, lit where it comes back out): the sun's
        // shadows on it (the box's, the leaf's checkers) come out softer, and it keeps the light it had. The whole picture,
        // denoised.
        {
            const uint32_t first=index;
            const float scatter=0.03f;
            TGPT_Material skinFloor=materials[0]; skinFloor.flags|=TGPT_MATERIAL_SUBSURFACE; skinFloor.subsurfaceMask=1.f;
            for(int c=0;c<3;++c) skinFloor.subsurface[c]=1.f/scatter;
            expect(SetMaterials(73, &skinFloor, 1)==0, "the floor's material as one light walks inside");
            auto floorAs=[&](bool scatters) {
                auto li=indices; if(scatters) li[0]=73u;
                expect(SetScene(instances.data(), uint32_t(instances.size()), li.data(), uint32_t(li.size()))==0, scatters ? "the floor a material that scatters" : "the floor a plain surface");
                ownRays=true;
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                index=first;
                for(uint32_t i=0;i<40;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? index++ : 0);
                    if(i>=16) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]; }
                }
                ownRays=false;
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
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
            std::printf("      skin scattering 3 cm: the shadow edges on the floor %.2f of the sharp ones, its light %.3f of the sharp floor's\n", edges, total);
            expect(SetMaterials(73, &materials[1], 1)==0, "the floor's second material plain again");
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
            // For the record (PT-50, a limit that is left): a glow far brighter than what the brightest lamp lights. A lamp of
            // 0.00026 cd in the furnace makes the reference radiance the tracer stores its light against so small that the
            // walls' glow is 6,000 times it, three times what a cell of the radiance cache can hold (CACHE_SAMPLE_MOST), so
            // the paths that end on the cache lose the rest: 1 is right. A number, not a check, until the cache holds it.
            {
                TGPT_Light dim{}; dim.position[1]=3; dim.type=0; dim.color[0]=dim.color[1]=dim.color[2]=2.6e-4f; dim.range=20; dim.radius=0.05f; dim.id=77;
                expect(SetLights(&dim, 1)==0, "a dim lamp in the furnace");
                const double bright=glow(1, honest, 400, 300);
                std::printf("      furnace with a lamp so dim that its walls glow 6,000 times the reference radiance: ending on the radiance cache %.3f "
                            "(1 is right: the cache holds a cell's light to 2,000 times the reference)\n", bright);
                expect(SetLights(nullptr, 0)==0, "no lights for the furnace again");
            }
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
        // Materials 11 black, 12 glowing (grey, 200 nits white, single-sided), 13 glowing and black (it reflects nothing), 14 as
        // 13 and marked as a kept light's own surface (TGPT_MATERIAL_OWN_SURFACE).
        auto glowing=materials; glowing.resize(15);
        glowing[11]=materials[0]; for(int c=0;c<3;++c) glowing[11].baseColor[c]=0.f;
        glowing[12]=materials[0]; for(int c=0;c<3;++c) { glowing[12].baseColor[c]=0.2f; glowing[12].emissive[c]=200.f; }
        glowing[13]=glowing[12]; for(int c=0;c<3;++c) glowing[13].baseColor[c]=0.f;
        glowing[14]=glowing[13]; glowing[14].flags|=TGPT_MATERIAL_OWN_SURFACE;
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
        // 6b. A glowing surface the plugin marks as a kept light's own (TGPT_MATERIAL_OWN_SURFACE: a crystal whose light the game
        // placed beside it, farther than the light's radius and 25 cm) lights nothing either, row on or off; the light 1.5 m off
        // lights the room alone. Unmarked, the same quad adds its own light.
        {
            TGPT_Light crystal{}; crystal.position[0]=quadAt.x+1.5f; crystal.position[1]=quadAt.y; crystal.position[2]=quadAt.z; crystal.type=0;
            crystal.color[0]=4; crystal.color[1]=6; crystal.color[2]=8; crystal.range=12; crystal.radius=0.3f; crystal.id=402;
            expect(SetLights(&crystal, 1)==0, "a light 1.5 m beside the quad set");
            double seAlone=0, seOn=0, seOff=0, seGlow=0;
            const uint32_t diffuse=plain|TGPT_OPTION_NO_SPECULAR;
            setRoom({{down, 11u, 0u, 0}});
            const double alone=energy(diffuse|on, seAlone);
            setRoom({{down, 14u, 0u, 0}});
            const double ownOn=energy(diffuse|on, seOn), ownOff=energy(diffuse, seOff);
            setRoom({{down, 13u, 0u, 0}});
            const double glowOn=energy(diffuse|on, seGlow);
            setRoom({{down, 12u, 0u, 0}});
            std::printf("      a light 1.5 m beside a glowing quad: the room's light %.5f with the quad dark (+-%.5f), marked as its own surface %.5f row on "
                "(+-%.5f), %.5f off (+-%.5f), unmarked %.5f (+-%.5f)\n", alone, seAlone, ownOn, seOn, ownOff, seOff, glowOn, seGlow);
            expect(alone>0 && std::fabs(ownOn-alone)<3.0*std::sqrt(seAlone*seAlone+seOn*seOn)+0.005*alone &&
                   std::fabs(ownOff-alone)<3.0*std::sqrt(seAlone*seAlone+seOff*seOff)+0.005*alone,
                   "a glowing surface marked as a kept light's own adds no light of its own, however far the light sits from it");
            expect(glowOn>alone*1.1+3.0*std::sqrt(seAlone*seAlone+seGlow*seGlow), "unmarked, the same glowing surface lights the room");
            expect(SetLights(nullptr, 0)==0, "no lamps");
        }
        // 7. What the Emissive debug view makes of glowing quads facing the camera: green sampled as a light, blue a kept light's own
        // surface (one 5 cm in front of it), red one the game moves (TGPT_SetMoving: placed every frame, met by paths, not
        // sampled: the emitters are built for the scene's list), and red a glowing mesh of more triangles than the budget
        // (a 363 x 363 grid, 263,538: PT-50 raised the budget from 16,384 to 262,144, and a 130 x 130 grid of 33,800 is now
        // sampled) while a small quad beside it is sampled; with the row off, only the light's own surface stays blue.
        {
            std::vector<float> gp; std::vector<uint32_t> gi;
            const int cells=363;
            for(int y=0;y<=cells;++y) for(int x=0;x<=cells;++x) gp.insert(gp.end(), {float(x)/cells*3.f, float(y)/cells*2.f, 0.f});
            for(int y=0;y<cells;++y) for(int x=0;x<cells;++x) {
                const uint32_t a=uint32_t(y*(cells+1)+x), b=a+1, c=a+uint32_t(cells+1), d=c+1;
                gi.insert(gi.end(), {a, c, b, b, c, d}); // wound to face -z, towards the camera
            }
            TGPT_CpuMeshDesc big{sizeof(TGPT_CpuMeshDesc), TGPT_ABI}; big.key=90; big.positions=gp.data(); big.indices=gi.data();
            big.vertexCount=uint32_t(gp.size()/3); big.indexCount=uint32_t(gi.size());
            const int bigHandle=RegisterCpuMesh(&big);
            expect(bigHandle>0, "a glowing mesh of 263,538 triangles registered");
            auto facingCamera=[&](float x, float y) { return mul(translate_scale({x, y, 4.f}, {0.8f, 0.8f, 1.f}), rot_x(-pi/2)); };
            const M4 sampledQuad=facingCamera(-3.f, 3.5f), ownQuad=facingCamera(-1.f, 3.5f), movingQuad=facingCamera(1.f, 3.5f), besideBig=facingCamera(-3.f, 2.2f);
            setRoom({{sampledQuad, 12u, 0u, 0}, {ownQuad, 12u, 0u, 0}, {besideBig, 12u, 0u, 0},
                     {translate_scale({2.f, 2.5f, 5.8f}, {1.f, 1.f, 1.f}), 12u, 0u, bigHandle}});
            TGPT_Moving carried{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) carried.transform[r*4+c]=carried.previous[r*4+c]=movingQuad.m[r][c];
            carried.mesh=quadHandle; carried.materialBase=0; carried.mask=0xFF;
            const uint32_t carriedMaterial=12u;
            expect(SetMoving && SetMoving(&carried, 1, &carriedMaterial, 1, nullptr, 0)==0, "a glowing quad the game moves, placed for the frames");
            TGPT_Light lamp{}; lamp.position[0]=-1.f; lamp.position[1]=3.5f; lamp.position[2]=3.95f; lamp.type=0;
            lamp.color[0]=5; lamp.color[1]=4; lamp.color[2]=3; lamp.range=6; lamp.radius=0.03f; lamp.id=401;
            expect(SetLights(&lamp, 1)==0, "a light in front of a glowing quad set");
            const V3 points[5]={{-3.f, 3.5f, 4.f}, {-1.f, 3.5f, 4.f}, {1.f, 3.5f, 4.f}, {-3.f, 2.2f, 4.f}, {3.5f, 3.5f, 5.8f}};
            auto classify=[&](uint32_t options) {
                for(uint32_t i=0;i<6;++i) run(TGPT_VIEW_EMISSIVE, cam, cam, i ? next++ : 0, options); // the big mesh converts meanwhile
                // ... and is built when its turn comes: six frames were enough until bottom-level builds went side by side
                // with a frame's own scratch (2026-10-08: the first row read the big mesh's place before it was there, "gbrg?").
                for(uint32_t i=0;i<240;++i) { ReadStatus(&status); if(!status.meshesQueued) break; run(TGPT_VIEW_EMISSIVE, cam, cam, next++, options); }
                for(uint32_t i=0;i<2;++i) run(TGPT_VIEW_EMISSIVE, cam, cam, next++, options);
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
            expect(SetLights(nullptr, 0)==0 && SetMoving(nullptr, 0, nullptr, 0, nullptr, 0)==0, "no lamps, nothing moving");
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
        }
    }
    // Opacity micromaps (DirectX Raytracing 1.2) settle ahead of the alpha test only what it would decide, so
    // the same frames with and without them must come out the same. The quad mesh's micromaps were made for
    // the leaf's material: the floor, walls and the invisible quad share its BLAS with micromaps disabled.
    // (The checks before this one gave the card and the quad other alpha-tested materials: a mesh's micromaps are made anew
    // for the materials in use once no instance has traced with them for 8 frames (scene.hpp kOmmUnusedFrames), so the room
    // is traced some frames first. Without that the outcome hung on which check had the card last.)
    {
        for(uint32_t i=0;i<24;++i) { run(TGPT_VIEW_NONE, cam, cam, i); context->Flush(); Sleep(10); }
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
        // The quad and the card mesh, the cube since the slab test gave it an alpha-tested material, and the cylinder since its
        // wax look did. (The cylinder's first alpha-tested look is glass, which can have no micromaps: until 2026-10-08 that
        // left the mesh without them for good, and three were counted here.)
        if(dxr12) expect(status.micromapMeshes==4 && status.micromapsWaiting==0, "every mesh with alpha-tested instances has opacity micromaps");
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
    // Reflection Boost: the floor as a grey metal (smoothness 0.6) becomes nearly a mirror, reflecting the room's colored walls
    // (a non-metal only gets glossier: its mirror share stays, the owner's ruling of 2026-10-06).
    {
        const auto keepFloorFinish=objects[0]; objects[0].f0=0.5f;
        auto floorColor=[&]() {
            for(uint32_t i=0;i<32;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 4000+uint32_t(reflectionBoost*100)+i : 0);
            const auto px=readback(); double r=0, g=0; int n=0;
            for(UINT y=20;y<140;++y) for(UINT x=200;x<1080;++x) { const size_t i=(size_t(y)*W+x)*3; r+=px[i]; g+=px[i+1]; ++n; }
            return std::array<double, 2>{r/n, g/n};
        };
        reflectionBoost=0; const auto plain=floorColor();
        reflectionBoost=1; const auto boosted=floorColor();
        reflectionBoost=0; objects[0]=keepFloorFinish;
        std::printf("      Reflection Boost on a metal floor: red %.4f green %.4f as made, %.4f %.4f boosted\n", plain[0], plain[1], boosted[0], boosted[1]);
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
    // PT-46: a mirror shows what it reflects as the camera sees it there (the owner: "we need our tracer to be able to perfectly
    // render the world in a mirror"). The floor a perfect mirror (roughness 0, a metal of reflectance 1, no diffuse color), the
    // camera low over it, looking down towards the red wall: the floor shows the wall. The reference: the wall seen directly
    // from the camera's mirror image under the floor (the floor left out of the raster there; the traced scene is the same),
    // whose picture is the mirror's upside down, pixel on pixel (a camera keeps its up, so the reflected view's is flipped).
    // The room's warm lamp lights the wall, the sun and the sky too; Indirect Color 3 and Bounces 1, as the owner plays; none of
    // the estimator's own changes (reuse, clamps, boiling filters, denoiser); 48 frames, the first 16 left out while the
    // radiance cache fills. The wall's finish drawn as the tracer's material has it (roughness 0.7).
    {
        const auto keepFloor=objects[0], keepWall=objects[2];
        const float keepIndirect=indirectColor; const uint32_t keepBounces=bounces;
        objects[0].roughness=0.f; objects[0].f0=1.f; objects[0].raster=8;
        objects[2].roughness=1.f-materials[1].smoothness;
        indirectColor=3.f; bounces=1;
        const Camera over=make_camera({-3.f, 1.f, -0.5f}, {-6.f, -0.6f, -0.5f}, 60.f, float(W)/float(H));
        const Camera under=make_camera({-3.f, -1.f, -0.5f}, {-6.f, 0.6f, -0.5f}, 60.f, float(W)/float(H));
        const uint32_t honest=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS;
        auto average=[&](const Camera& c, bool floorDrawn) {
            const auto keep=objects;
            if(!floorDrawn) objects.erase(objects.begin());
            std::vector<double> acc(size_t(W)*H*3, 0.0);
            for(uint32_t i=0;i<48;++i) {
                run(TGPT_VIEW_NONE, c, c, i ? 9100+i : 0, honest);
                if(i>=16) { const auto px=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=px[j]/32.0; }
            }
            objects=keep;
            return acc;
        };
        // Where a pixel's ray (reflected by the floor where it meets it, for the camera over it) meets the wall's plane (x -6):
        // the wall from 0.3 to 1.7 m high and z -2.5 to 1.5 (clear of the emissive panel, the traced-only leaf card above and
        // the room's corners), else none.
        auto wallPoint=[&](const Camera& c, UINT x, UINT y, bool reflected, V3& q, float& segment) {
            const float nx=(x+0.5f)/W*2-1, ny=1-(y+0.5f)/H*2, in[4]={nx, ny, 0.5f, 1.f};
            float o[4]={0, 0, 0, 0}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) o[r]+=c.invViewProj.m[r][k]*in[k];
            V3 d=normalize({o[0]/o[3], o[1]/o[3], o[2]/o[3]});
            V3 from=c.position;
            if(reflected) {
                if(d.y>=-1e-4f) return false;
                from=c.position+d*(-c.position.y/d.y);
                if(from.x<-5.9f || from.x>-3.f || std::fabs(from.z)>5.9f) return false;
                d.y=-d.y;
            }
            if(d.x>=-1e-4f) return false;
            const float t=(-6.f-from.x)/d.x;
            q=from+d*t; segment=t;
            return q.y>0.3f && q.y<1.7f && q.z>-2.5f && q.z<1.5f;
        };
        // At Indirect Color 1 (the bounced light as traced) and at the owner's 3. At 3 the camera's own pixel takes the
        // color scale per noisy sample of its bounced light (pt_gi_spatial: a strongly colored sample is held back so that no
        // channel goes negative, and with one ray a pixel and no reuse here every sample is one ray's), where the mirror's
        // takes it on the radiance cache's mean: the mirror's color may come out a little stronger there, never weaker.
        auto toFloat=[](const std::vector<double>& v) { return std::vector<float>(v.begin(), v.end()); };
        for(const float scale:{1.f, 3.f}) {
            indirectColor=scale;
            const auto mirror=average(over, true), direct=average(under, false);
            if(scale>1.f) { save(toFloat(mirror), "63-mirror-red-wall.png", true); save(toFloat(direct), "64-red-wall-from-the-mirror-image.png", true); }
            double m[3]={0, 0, 0}, d[3]={0, 0, 0}, apart=0; size_t n=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                V3 q, q2; float seg, seg2;
                if(!wallPoint(over, x, y, true, q, seg) || !wallPoint(under, x, H-1-y, false, q2, seg2)) continue;
                apart=std::max(apart, double(std::sqrt(dot(q-q2, q-q2))));
                const size_t i=(size_t(y)*W+x)*3, j=(size_t(H-1-y)*W+x)*3;
                for(int k=0;k<3;++k) { m[k]+=mirror[i+k]; d[k]+=direct[j+k]; }
                ++n;
            }
            const double lm=0.2126*m[0]+0.7152*m[1]+0.0722*m[2], ld=0.2126*d[0]+0.7152*d[1]+0.0722*d[2];
            const double green=m[1]/std::max(m[0], 1e-12)/(d[1]/std::max(d[0], 1e-12)), blue=m[2]/std::max(m[0], 1e-12)/(d[2]/std::max(d[0], 1e-12));
            std::printf("      a perfect mirror showing the red wall at Indirect Color %.0f, %zu pixels (the same wall points as seen from the mirror image, %.1f mm apart "
                        "at most): light %.5f %.5f %.5f against the wall's own %.5f %.5f %.5f (brightness %.3f; green/red %.3f, blue/red %.3f of the wall's)\n",
                        scale, n, apart*1000.0, m[0]/std::max<size_t>(n, 1), m[1]/std::max<size_t>(n, 1), m[2]/std::max<size_t>(n, 1),
                        d[0]/std::max<size_t>(n, 1), d[1]/std::max<size_t>(n, 1), d[2]/std::max<size_t>(n, 1), lm/std::max(ld, 1e-12), green, blue);
            expect(n>20000 && apart<0.01, "the mirror shows the wall points the camera's mirror image sees, pixel for pixel");
            expect(std::fabs(lm/std::max(ld, 1e-12)-1.0)<0.05, "a mirror shows the wall as bright as the camera sees it directly");
            if(scale==1.f) expect(std::fabs(green-1.0)<0.05 && std::fabs(blue-1.0)<0.05, "a mirror shows the wall's colors as the camera sees them directly");
            else expect(green>0.8 && green<1.03 && blue>0.8 && blue<1.03, "a mirror shows the wall's colors with the camera's Indirect Color (a little stronger at most)");
        }
        // (The mirror in HDRP's height fog, HDRP's formula over the reflected way, went on 2026-10-11 with that formula: a
        // mirror crosses the traced medium now, pt_medium.hlsli traced_fog_on_way. Its own check here is owed, once the traced
        // fog looks right in the game.)
        // What it costs (rough: other programs may share the GPU): the stages over the last second of 90 whole frames.
        if(GetStageTimes) {
            for(uint32_t i=0;i<90;++i) run(TGPT_VIEW_NONE, over, over, i ? 9300+i : 0, 0);
            char stages[1024]{}; GetStageTimes(stages, sizeof(stages));
            std::printf("      the stages with the mirror filling most of the view: %s\n", stages);
        }
        // Ray Reconstruction's guides at the mirror's pixels: a mirror's (specular albedo near 1, roughness near 0, facing up, no
        // diffuse color), and the reflections' hit distance the way from the floor to the wall, so that it moves the reflected
        // image as the wall's mirror image moves.
        auto getGuidesMirror=reinterpret_cast<decltype(&TGPT_GetGuides)>(GetProcAddress(pt, "TGPT_GetGuides"));
        if(getGuidesMirror) {
            rayReconstruction=true;
            for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, over, over, 9200+i);
            rayReconstruction=false;
            TGPT_Guides gd{sizeof(TGPT_Guides), TGPT_ABI};
            const bool have=getGuidesMirror(&gd)==0 && gd.diffuseAlbedo && gd.specularAlbedo && gd.normalRoughness && gd.specularHitDistance;
            expect(have, "guides for the mirror");
            if(have) {
                const auto gDiffuse=readGuide(gd.diffuseAlbedo), gSpecular=readGuide(gd.specularAlbedo), gNormal=readGuide(gd.normalRoughness);
                std::vector<float> hits(size_t(W)*H, 0.f);
                {
                    auto* t=reinterpret_cast<ID3D11Texture2D*>(uintptr_t(gd.specularHitDistance));
                    D3D11_TEXTURE2D_DESC dd{}; t->GetDesc(&dd); dd.BindFlags=0; dd.Usage=D3D11_USAGE_STAGING; dd.CPUAccessFlags=D3D11_CPU_ACCESS_READ; dd.MiscFlags=0;
                    ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&dd, nullptr, &staging);
                    context->CopyResource(staging.Get(), t);
                    D3D11_MAPPED_SUBRESOURCE mm{};
                    if(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mm))) {
                        for(UINT y=0;y<H && y<dd.Height;++y) for(UINT x=0;x<W && x<dd.Width;++x)
                            hits[size_t(y)*W+x]=half_to_float(reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mm.pData)+size_t(y)*mm.RowPitch)[x]);
                        context->Unmap(staging.Get(), 0);
                    }
                }
                double specular=0, diffuse=0, roughness=0, up=0; size_t g=0, hitOk=0, hitExact=0;
                std::vector<double> ratios;
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    V3 q; float seg;
                    if(!wallPoint(over, x, y, true, q, seg)) continue;
                    const size_t i=(size_t(y)*W+x)*4;
                    specular+=gSpecular[i+1]; diffuse+=std::max({gDiffuse[i], gDiffuse[i+1], gDiffuse[i+2]}); roughness+=gNormal[i+3]; up+=gNormal[i+1];
                    const double ratio=hits[size_t(y)*W+x]/std::max(seg, 1e-3f);
                    ratios.push_back(ratio); if(std::fabs(ratio-1.0)<0.05) ++hitOk; if(std::fabs(ratio-1.0)<0.01) ++hitExact;
                    ++g;
                }
                if(g) { specular/=g; diffuse/=g; roughness/=g; up/=g; }
                std::sort(ratios.begin(), ratios.end());
                std::printf("      Ray Reconstruction's guides on the mirror (%zu pixels): specular albedo %.3f, diffuse %.3f, roughness %.3f, normal's up %.3f; "
                            "hit distance over the way from the floor to the wall %.4f (median), within 5%% at %.1f%% of them, within 1%% at %.1f%%\n",
                            g, specular, diffuse, roughness, up, ratios.empty() ? 0.0 : ratios[ratios.size()/2], 100.0*double(hitOk)/std::max<size_t>(g, 1),
                            100.0*double(hitExact)/std::max<size_t>(g, 1));
                expect(g>20000 && specular>0.9 && diffuse<0.06 && roughness<0.05 && up>0.98, "Ray Reconstruction's guides on the mirror are a mirror's");
                expect(double(hitOk)>0.95*double(g), "the mirror's hit distance is the way from the mirror to what it shows");
                // NVIDIA's DLSS-RR guide (3.4.9): the distance from the specular ray's origin "on the Primary Surface" to its hit, not
                // from where the ray starts (a little off the surface, so it misses the surface itself). The wall stands across the
                // floor, where the two agree; the ceiling lies parallel to it, where the ray's start, 2 cm up, made it 1.6 cm near
                // (5.984 m): looking straight down into the mirror, the ceiling 6 m over the floor.
                expect(double(hitExact)>0.95*double(g), "the mirror's hit distance to a wall across it is the way from the mirror's surface");
                const Camera down=make_camera({-3.f, 1.f, -0.5f}, {-3.f, 0.f, -0.499f}, 60.f, float(W)/float(H));
                rayReconstruction=true;
                for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, down, down, 9250+i);
                rayReconstruction=false;
                TGPT_Guides gc{sizeof(TGPT_Guides), TGPT_ABI};
                std::vector<double> up6;
                if(getGuidesMirror(&gc)==0 && gc.specularHitDistance) {
                    auto* t=reinterpret_cast<ID3D11Texture2D*>(uintptr_t(gc.specularHitDistance));
                    D3D11_TEXTURE2D_DESC dd{}; t->GetDesc(&dd); dd.BindFlags=0; dd.Usage=D3D11_USAGE_STAGING; dd.CPUAccessFlags=D3D11_CPU_ACCESS_READ; dd.MiscFlags=0;
                    ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&dd, nullptr, &staging);
                    context->CopyResource(staging.Get(), t);
                    D3D11_MAPPED_SUBRESOURCE mm{};
                    if(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mm))) {
                        for(UINT y=H/2-40;y<H/2+40 && y<dd.Height;++y) for(UINT x=W/2-40;x<W/2+40 && x<dd.Width;++x)
                            up6.push_back(half_to_float(reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mm.pData)+size_t(y)*mm.RowPitch)[x]));
                        context->Unmap(staging.Get(), 0);
                    }
                }
                std::sort(up6.begin(), up6.end());
                const double ceiling=up6.empty() ? 0.0 : up6[up6.size()/2];
                std::printf("      looking straight down into the mirror: its hit distance to the ceiling %.4f m (6 m over the floor; %zu pixels)\n", ceiling, up6.size());
                expect(std::fabs(ceiling/6.0-1.0)<0.002, "the mirror's hit distance to a ceiling over it is the way from the mirror's surface, as Ray Reconstruction's guide asks");
            }
        }
        // The hero's first-person body (TGPT_MATERIAL_FIRST_PERSON): the game loads no head in first person. That body's mesh
        // keeps the back of a head, an open shell facing outwards with the face cut out for the camera, which HDRP never draws
        // from in front (its skin is one-sided): the game's view from a mirror's side shows nothing above the collar. The
        // tracer's rays pass the shell's inside, at any distance (first_person_inside; a rule by the distance from the camera
        // left most of it in the mirror, a bare egg: docs/agents/mirror-2.md). Here the shell is a traced-only sheet 30 cm
        // across, 35 cm behind a camera that looks straight down into the mirror from 0.5, 1 and 3 m, its front away from the
        // mirror, as the back of the head's is. Its mirror image, in the middle of the picture, against the picture with no
        // sheet (the ceiling over the camera): with the first-person material the same, at each distance; with another
        // material the sheet (a ray meets a back face as a surface); and the first-person material turned to face the mirror,
        // or drawn two-sided, shows too: the rest of that body (a hand, the back of the head seen from behind) is seen.
        {
            std::vector<TGPT_Material> list=materials;
            const int plainSheet=int(list.size());
            list.push_back(materials[2]);                                                   // green
            list.push_back(materials[2]); list.back().flags|=TGPT_MATERIAL_FIRST_PERSON;    // green, the hero's first-person body
            list.push_back(materials[2]); list.back().flags|=TGPT_MATERIAL_FIRST_PERSON|TGPT_MATERIAL_DOUBLE_SIDED;
            // And the traced floor a mirror, as the drawn one is: the pixel's surface is the tracer's own camera ray's hit.
            const int mirrorFloor=int(list.size());
            list.push_back(materials[0]); list.back().baseColor[0]=list.back().baseColor[1]=list.back().baseColor[2]=1.f; list.back().metallic=1.f; list.back().smoothness=1.f;
            expect(SetMaterials(0, list.data(), uint32_t(list.size()))==0, "materials with the first-person body's");
            auto apart=[](const std::array<double, 3>& a, const std::array<double, 3>& b) { return (std::fabs(a[0]-b[0])+std::fabs(a[1]-b[1])+std::fabs(a[2]-b[2]))/std::max(a[0]+a[1]+a[2], 1e-12); };
            // The middle of the picture (2 x reach pixels square, 8 frames' mean) with a sheet 30 cm across of that material `above`
            // the eye, its front up (turned: down), or with none (sheet < 0).
            auto centre=[&](const Camera& cam, V3 eye, float above, int reach, int sheet, bool turned) {
                auto l=instances; auto li=indices;
                li[0]=uint32_t(mirrorFloor);
                if(sheet>=0) {
                    M4 w=translate_scale(eye+V3{0, above, 0}, {0.3f, 1.f, 0.3f});
                    if(turned) w=mul(w, rot_x(pi));
                    TGPT_Instance b{}; for(int row=0;row<3;++row) for(int col=0;col<4;++col) b.transform[row*4+col]=w.m[row][col];
                    b.mesh=quadHandle; b.materialBase=uint32_t(li.size()); b.mask=0xFF; li.push_back(uint32_t(sheet)); l.push_back(b);
                }
                expect(SetScene(l.data(), uint32_t(l.size()), li.data(), uint32_t(li.size()))==0, "scene with the sheet at the camera");
                std::array<double, 3> c{0, 0, 0};
                const double n=8.0*double(2*reach)*double(2*reach);
                for(uint32_t i=0;i<16;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, i ? 9400+i : 0, honest);
                    if(i<8) continue;
                    const auto px=readback();
                    for(int y=int(H)/2-reach;y<int(H)/2+reach;++y) for(int x=int(W)/2-reach;x<int(W)/2+reach;++x)
                        for(int k=0;k<3;++k) c[size_t(k)]+=px[(size_t(y)*W+size_t(x))*3+size_t(k)]/n;
                }
                return c;
            };
            // In a mirror, whose own pixels are the tracer's own camera ray's hits (PT-59).
            ownRays=true;
            bool hidden=true, others=true, front=true, twoSided=true;
            for(const float height:{0.5f, 1.f, 3.f}) {
                const V3 eye{-3.f, height, -0.5f};
                const Camera down=make_camera(eye, {-3.f, 0.f, -0.499f}, 60.f, float(W)/float(H));
                // The sheet's mirror image stands 2 x height + 0.35 m from the camera: the middle 60 % of its width in pixels.
                const int reach=std::max(3, int(0.6f*0.15f/(2.f*height+0.35f)*(float(H)/2.f)/std::tan(pi/6.f)));
                const auto none=centre(down, eye, 0.35f, reach, -1, false), passed=centre(down, eye, 0.35f, reach, plainSheet+1, false);
                const auto seen=centre(down, eye, 0.35f, reach, plainSheet, false);
                const auto facing=centre(down, eye, 0.35f, reach, plainSheet+1, true), both=centre(down, eye, 0.35f, reach, plainSheet+2, false);
                std::printf("      a mirror %.1f m under the camera, the middle %d pixels: with no sheet behind the camera %.5f %.5f %.5f; "
                            "the first-person body's from behind %.5f %.5f %.5f (off by %.3f); another material's %.5f %.5f %.5f (off by %.3f); the first-person "
                            "body's from in front off by %.3f, drawn two-sided off by %.3f\n", height,
                            2*reach, none[0], none[1], none[2], passed[0], passed[1], passed[2], apart(none, passed),
                            seen[0], seen[1], seen[2], apart(none, seen), apart(none, facing), apart(none, both));
                hidden=hidden && apart(none, passed)<0.03; others=others && apart(none, seen)>0.2;
                front=front && apart(none, facing)>0.2; twoSided=twoSided && apart(none, both)>0.2;
            }
            expect(others, "a sheet behind the camera shows in the mirror in front of it, met from behind");
            expect(hidden, "the inside of the hero's first-person body does not show in a mirror 0.5, 1 or 3 m ahead (the back of its head, never drawn from in front)");
            expect(front, "the hero's first-person body met from in front shows in a mirror (the rest of that body is seen)");
            expect(twoSided, "a first-person body drawn two-sided shows from behind too");
            // And the camera's own ray (PT-59), which is in that head: looking straight up at the sheet 20 cm over the eye (HDRP's
            // G-buffer holds no sheet: the ceiling). From behind (the shell's inside, as the camera in the head sees it) it shows
            // nothing, as HDRP never draws the back of a one-sided surface; turned to face the camera (a hand, the collar: within
            // the 30 cm a first rule passed everything of that body in) it shows; and drawn two-sided it shows from behind too.
            {
                const V3 eye{-3.f, 1.f, -0.5f};
                const Camera up=make_camera(eye, {-3.f, 2.f, -0.499f}, 60.f, float(W)/float(H));
                const auto none=centre(up, eye, 0.2f, 40, -1, false), inside=centre(up, eye, 0.2f, 40, plainSheet+1, false);
                const auto facing=centre(up, eye, 0.2f, 40, plainSheet+1, true), both=centre(up, eye, 0.2f, 40, plainSheet+2, false);
                std::printf("      the camera's own ray, a sheet 20 cm over the eye: nothing there %.5f %.5f %.5f; the first-person body's from behind %.5f %.5f %.5f "
                            "(off by %.3f); from in front %.5f %.5f %.5f (off by %.3f); drawn two-sided, from behind, off by %.3f\n", none[0], none[1], none[2],
                            inside[0], inside[1], inside[2], apart(none, inside), facing[0], facing[1], facing[2], apart(none, facing), apart(none, both));
                expect(apart(none, inside)<0.03, "the camera's own ray passes the inside of the hero's first-person body (the head it sits in)");
                expect(apart(none, facing)>0.2, "the camera's own ray meets the hero's first-person body from in front, 20 cm away (a hand, the collar)");
                expect(apart(none, both)>0.2, "the camera's own ray meets a first-person body drawn two-sided from behind too");
                ownRays=false;
            }
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the sheet");
            expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "materials as before");
        }
        objects[0]=keepFloor; objects[2]=keepWall; indirectColor=keepIndirect; bounces=keepBounces;
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
                    if(i>=2) { const auto px=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=px[j]/8.0; } // (from the third frame on, as when the composite showed the frame before's light)
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
                // In a reflection (PT-46) the metallic half is lit as the camera's own pixel of it is, a metal's way: its highlights and
                // its own reflection of the room, no diffuse light. Not black, and not the plain panel's diffuse light either.
                if(part.view==TGPT_VIEW_INDIRECT) expect(left>0.002*total && std::fabs(leftOff-left)<0.02*left, "the metallic half of a mask map bounces its specular color's light");
                else expect(left>0.002*total && leftOff>0.2*left && std::fabs(leftOff-left)>0.1*left, "the metallic half of a mask map is reflected lit as a metal, not black");
                expect(right>0.002*total && rightOff<0.02*right, part.view==TGPT_VIEW_INDIRECT ? "the dielectric half of a mask map bounces its light as before"
                                                                                            : "the dielectric half of a mask map is reflected as before");
            }
            reflectionBoost=0; sunOn=true; rays=keepRays;
            expect(SetLights(&light, 1)==0, "one light again");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the panel");
        }
        // PT-46: rays read the game's own textures (shared by the hook, TGPT_Material.colorTexture, normalTexture), as the game's
        // sampler reads them, over the ray cone's footprint. A traced-only panel stands at the floor's far side, the floor a
        // perfect mirror (roughness 0, a metal of reflectance 1), the camera low, looking down into the floor at the panel's
        // reflection. Its texture: 1024x1024 BC7 sRGB with every mip, cells 8 texels wide and 16 tall (128 by 64 of them), light
        // and dark: each cell about 3 pixels wide in the mirror. The 64x64 atlas tile holds two cells a texel: grey. The
        // agreement of the mirror's pixels with the texture as a pinhole sees it (each pixel's reflected ray to the panel, its
        // texel looked up: the panel seen directly, as sharp as can be) is their correlation, 1 for a perfect copy.
        if(RegisterSharedTexture) {
            const float lightLinear=0.8f, darkLinear=0.05f;
            auto srgbOdd=[](float l) { const float s=l<=0.0031308f ? 12.92f*l : 1.055f*std::pow(l, 1.f/2.4f)-0.055f; return uint32_t(std::lround(std::clamp(s, 0.f, 1.f)*255.f))|1u; };
            // A BC7 block of at most two greys (mode 6: endpoint 0 the first texel's grey, endpoint 1 the other; each texel's index
            // 0 or 15, which decode to the endpoints exactly, as the bytes are odd and alpha is 255).
            auto bc7TwoGreys=[](const uint32_t texels[16], uint8_t* out) {
                const uint32_t a=texels[0]; uint32_t b=a; for(int i=0;i<16;++i) if(texels[i]!=a) { b=texels[i]; break; }
                uint64_t lo=0, hi=0; uint32_t bit=0;
                auto put=[&](uint64_t v, uint32_t n) { for(uint32_t i=0;i<n;++i,++bit) if((v>>i)&1) { if(bit<64) lo|=1ull<<bit; else hi|=1ull<<(bit-64); } };
                put(1u<<6, 7); for(int c=0;c<3;++c) { put(a>>1, 7); put(b>>1, 7); } put(127, 7); put(127, 7); put(1, 1); put(1, 1);
                for(int i=0;i<16;++i) put(texels[i]==a ? 0u : 15u, i==0 ? 3u : 4u);
                std::memcpy(out, &lo, 8); std::memcpy(out+8, &hi, 8);
            };
            auto lightCell=[](uint32_t x, uint32_t y) { return ((x/8+y/16)&1u)!=0; };
            ComPtr<ID3D11Texture2D> cells, dead, bumpPlus, bumpMinus;
            {
                // Every mip a box filter of mip 0 (each texel the share of light texels under it): two greys a block at most.
                const UINT size=1024, mips=11;
                std::vector<std::vector<uint8_t>> data(mips); std::vector<D3D11_SUBRESOURCE_DATA> init(mips);
                for(UINT m=0;m<mips;++m) {
                    const UINT w=std::max(1u, size>>m), blocks=(w+3)/4, span=1u<<m; // a texel's side in mip 0's texels
                    std::vector<uint32_t> grey(size_t(w)*w);
                    for(UINT y=0;y<w;++y) for(UINT x=0;x<w;++x) {
                        uint32_t lit=0;
                        for(UINT j=0;j<span;++j) for(UINT i=0;i<span;++i) lit+=lightCell(x*span+i, y*span+j);
                        grey[size_t(y)*w+x]=srgbOdd(darkLinear+(lightLinear-darkLinear)*float(lit)/float(span*span));
                    }
                    data[m].resize(size_t(blocks)*blocks*16);
                    for(UINT by=0;by<blocks;++by) for(UINT bx=0;bx<blocks;++bx) {
                        uint32_t texels[16];
                        for(UINT j=0;j<4;++j) for(UINT i=0;i<4;++i) texels[j*4+i]=grey[size_t(std::min(w-1, by*4+j))*w+std::min(w-1, bx*4+i)];
                        bc7TwoGreys(texels, &data[m][(size_t(by)*blocks+bx)*16]);
                    }
                    init[m]={data[m].data(), blocks*16, 0};
                }
                D3D11_TEXTURE2D_DESC d{size, size, mips, 1, DXGI_FORMAT_BC7_UNORM_SRGB, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
                device->CreateTexture2D(&d, init.data(), &cells);
                d.Usage=D3D11_USAGE_DEFAULT; device->CreateTexture2D(&d, init.data(), &dead);
                // Normal maps (RGBA8, linear), one tangent-space normal over all: tilted half way toward +u, and toward -u.
                std::vector<uint32_t> px(256*256);
                D3D11_TEXTURE2D_DESC n{256, 256, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
                const D3D11_SUBRESOURCE_DATA ni{px.data(), 256*4, 0};
                for(auto& p:px) p=0xFFFF80BFu; device->CreateTexture2D(&n, &ni, &bumpPlus);  // r 191 (x 0.5), g 128 (y 0)
                for(auto& p:px) p=0xFFFF8040u; device->CreateTexture2D(&n, &ni, &bumpMinus); // r 64 (x -0.5)
            }
            auto shared=[&](const ComPtr<ID3D11Texture2D>& t) { TGPT_SharedTexture i{}; i.size=sizeof(i); i.abi=TGPT_ABI; const int32_t n=t ? RegisterSharedTexture(pointer(t), &i) : -100; return n>=0 ? i.handle : -1; };
            const int32_t cellsHandle=shared(cells), deadHandle=shared(dead), plusHandle=shared(bumpPlus), minusHandle=shared(bumpMinus);
            const int cellsTile=cells ? RegisterTexture(70, pointer(cells)) : -1;
            expect(cellsHandle>=0 && deadHandle>=0 && plusHandle>=0 && minusHandle>=0 && cellsTile>=0, "the BC7 texture and the normal maps shared, the texture an atlas tile too");
            // The dead one: the game frees it (its death notice comes; ReadStatus collects it).
            dead.Reset(); context->Flush();
            { TGPT_Status s{sizeof(TGPT_Status), TGPT_ABI}; for(int k=0;k<4;++k) { ReadStatus(&s); Sleep(30); } }
            std::vector<TGPT_Material> list=materials;
            TGPT_Material white=materials[0]; white.baseColor[0]=white.baseColor[1]=white.baseColor[2]=1.f; white.smoothness=0.f;
            white.colorTexture=white.normalTexture=white.maskTexture=-1;
            const uint32_t first=uint32_t(list.size());
            TGPT_Material c=white; c.albedoTile=cellsTile; c.colorTexture=cellsHandle; list.push_back(c);   // +0: shared, its tile behind
            TGPT_Material none=c; none.colorTexture=-1; list.push_back(none);                                  // +1: no number: the tile
            TGPT_Material gone=c; gone.colorTexture=deadHandle; list.push_back(gone);                          // +2: its texture died: the tile
            TGPT_Material tiled=c; tiled.uvScaleOffset[0]=tiled.uvScaleOffset[1]=16.f; list.push_back(tiled);  // +3: tiled 16 times: cells far below a pixel
            TGPT_Material flat=white; list.push_back(flat);                                                    // +4: no normal map
            TGPT_Material plus=white; plus.normalTexture=plusHandle; plus.normalScale=1.f; list.push_back(plus);    // +5: tilted toward +u (+x)
            TGPT_Material minus=white; minus.normalTexture=minusHandle; minus.normalScale=1.f; list.push_back(minus); // +6: toward -u
            expect(SetMaterials(0, list.data(), uint32_t(list.size()))==0, "materials reading the game's own textures set");
            const auto keepFloor=objects[0]; objects[0].roughness=0.f; objects[0].f0=1.f;
            const Camera mcam=make_camera({0, 0.8f, 0.2f}, {0, -1.f, 2.f}, 60.f, float(W)/float(H));
            // x -1 to 1, y 0.02 to 2.02 at z 2, facing -z: u along +x, v along +y.
            const M4 panelWorld=mul(translate_scale({0.f, 1.02f, 2.f}, {2.f, 2.f, 1.f}), rot_x(-pi/2));
            sunOn=false; rays=1;
            const uint32_t honest=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CLAMPS;
            auto mirrorImage=[&](uint32_t material, const TGPT_Light& lamp, uint32_t options) {
                expect(SetLights(&lamp, 1)==0, "a lamp for the panel");
                auto l=instances; auto li=indices;
                TGPT_Instance p{}; for(int row=0;row<3;++row) for(int col=0;col<4;++col) p.transform[row*4+col]=panelWorld.m[row][col];
                p.mesh=cardHandle; p.materialBase=uint32_t(li.size()); p.mask=0xFF; li.push_back(material); l.push_back(p);
                expect(SetScene(l.data(), uint32_t(l.size()), li.data(), uint32_t(li.size()))==0, "scene with the textured panel");
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                for(uint32_t i=0;i<10;++i) { run(TGPT_VIEW_SPECULAR, mcam, mcam, i ? 7000+i : 0, honest|options); if(i>=2) { const auto px=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=px[j]/8.0; } }
                return acc;
            };
            // Each pixel's reflected ray to the panel: its texture coordinates, or none.
            const M4 inv=mcam.invViewProj;
            std::vector<float> pu(size_t(W)*H, -1.f), pv(size_t(W)*H, -1.f);
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const float nx=(x+0.5f)/W*2-1, ny=1-(y+0.5f)/H*2, in[4]={nx, ny, 0.5f, 1.f};
                float o[4]={0, 0, 0, 0}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) o[r]+=inv.m[r][k]*in[k];
                const V3 d=normalize({o[0]/o[3], o[1]/o[3], o[2]/o[3]});
                if(d.y>=-1e-4f) continue;
                V3 f=mcam.position+d*(-mcam.position.y/d.y); // on the floor
                const V3 r{d.x, -d.y, d.z};
                if(r.z<=1e-4f || std::fabs(f.x)>6 || f.z>6) continue;
                // The tracer's reflection ray starts ray_offset above the floor (0.02 + 0.002 per metre from the camera: 2 to
                // 3 cm here), which moves what the mirror shows up by as much: about three quarters of a cell's height.
                f.y+=0.02f+0.002f*std::sqrt(dot(f-mcam.position, f-mcam.position));
                const V3 q=f+r*((2.f-f.z)/r.z);
                const float u=(q.x+1.f)/2.f, v=(q.y-0.02f)/2.f;
                if(u>0.06f && u<0.94f && v>0.06f && v<0.94f) { pu[size_t(y)*W+x]=u; pv[size_t(y)*W+x]=v; }
            }
            auto luminanceAt=[&](const std::vector<double>& img, size_t i) { return 0.2126*img[i*3]+0.7152*img[i*3+1]+0.0722*img[i*3+2]; };
            // The correlation of the mirror's pixels with the cells a pinhole sees there, and the panel's mean and relative spread.
            struct Stats { double agreement, mean, spread; size_t pixels; };
            auto stats=[&](const std::vector<double>& img, float tiling) {
                double sl=0, si=0, sll=0, sii=0, sli=0; size_t n=0;
                for(size_t i=0;i<pu.size();++i) {
                    if(pu[i]<0) continue;
                    const double L=luminanceAt(img, i);
                    const double I=lightCell(uint32_t(std::fmod(pu[i]*tiling, 1.f)*1024.f), uint32_t(std::fmod(pv[i]*tiling, 1.f)*1024.f)) ? 1.0 : 0.0;
                    sl+=L; si+=I; sll+=L*L; sii+=I*I; sli+=L*I; ++n;
                }
                if(!n) return Stats{0, 0, 0, 0};
                const double ml=sl/n, mi=si/n, vl=sll/n-ml*ml, vi=sii/n-mi*mi, cv=sli/n-ml*mi;
                return Stats{vl>0 && vi>0 ? cv/std::sqrt(vl*vi) : 0.0, ml, ml>0 ? std::sqrt(std::max(vl, 0.0))/ml : 0.0, n};
            };
            TGPT_Light front{}; front.position[0]=0; front.position[1]=1.6f; front.position[2]=0.2f; front.type=0;
            front.color[0]=front.color[1]=front.color[2]=30.f; front.range=12; front.radius=0.05f; front.id=700;
            const auto sharp=mirrorImage(first, front, 0), atlas=mirrorImage(first, front, TGPT_OPTION_ATLAS_TEXTURES);
            const auto noNumber=mirrorImage(first+1, front, 0), died=mirrorImage(first+2, front, 0), fine=mirrorImage(first+3, front, 0);
            auto toFloat=[](const std::vector<double>& v) { return std::vector<float>(v.begin(), v.end()); };
            save(toFloat(sharp), "60-mirror-shared-texture.png", true); save(toFloat(atlas), "61-mirror-atlas-tile.png", true);
            save(toFloat(fine), "62-mirror-shared-tiled-16x.png", true);
            const Stats s=stats(sharp, 1.f), a=stats(atlas, 1.f), f=stats(fine, 16.f);
            std::printf("      a mirror showing a panel of 128 by 64 cells (BC7, shared), %zu pixels: agreement with the cells as a pinhole sees them %.3f "
                        "(the 64x64 atlas tile: %.3f); its light %.4f against the tile's %.4f\n", s.pixels, s.agreement, a.agreement, s.mean, a.mean);
            expect(s.pixels>20000, "the panel fills a good part of the mirror");
            expect(s.agreement>0.5 && a.agreement<0.2, "a mirror shows the game's own texture sharp, cell for cell, where the 64x64 tile was grey");
            expect(std::fabs(s.mean-a.mean)<0.1*a.mean, "the shared texture's light is the tile's on average (same colors, same light)");
            double differNone=0, differDied=0, total=0;
            for(size_t i=0;i<atlas.size();++i) { differNone+=std::fabs(noNumber[i]-atlas[i]); differDied+=std::fabs(died[i]-atlas[i]); total+=atlas[i]; }
            std::printf("      without a number, and with the number of a texture the game freed: off the atlas image by %.2e and %.2e of its light\n",
                        differNone/std::max(total, 1e-12), differDied/std::max(total, 1e-12));
            expect(differNone<1e-3*total && differDied<1e-3*total, "a material without a number, or whose texture died, reads its atlas tile as before");
            // Tiled 16 times the cells are a sixth of a pixel: read at the ray's footprint they average to the cells' mean, even
            // grey; read at the top mip (what a ray without a cone reads) they alias into noise.
            std::printf("      tiled 16 times (cells a sixth of a pixel): the panel's relative spread %.3f (the grey tile's, the lamp's falloff alone: %.3f), its mean %.4f against the untiled's %.4f\n",
                        f.spread, a.spread, f.mean, s.mean);
            expect(f.spread<a.spread+0.06 && std::fabs(f.mean-s.mean)<0.15*s.mean, "a texture far finer than a pixel is read at its small mips: the cells' mean, no aliasing");
            // What it costs (rough: other programs may share the GPU): the stages over the last second of 90 whole frames each way.
            if(GetStageTimes) {
                auto stagesOf=[&](uint32_t options) {
                    for(uint32_t i=0;i<90;++i) run(TGPT_VIEW_NONE, mcam, mcam, i ? 7100+i : 0, options);
                    char stages[1024]{}; GetStageTimes(stages, sizeof(stages)); return std::string(stages);
                };
                mirrorImage(first, front, 0); // the panel in the scene
                const std::string withShared=stagesOf(0), withAtlas=stagesOf(TGPT_OPTION_ATLAS_TEXTURES);
                std::printf("      the stages reading the game's textures: %s\n      reading the atlas tiles: %s\n", withShared.c_str(), withAtlas.c_str());
            }
            // The normal map: the lamp off to the panel's +x side. Tilted toward it, the panel is brighter than flat; tilted away,
            // darker (the tangent frame's u is the world's +x on this panel).
            TGPT_Light side=front; side.position[0]=3.f; side.position[1]=1.f; side.position[2]=1.f;
            const Stats nf=stats(mirrorImage(first+4, side, 0), 1.f), np=stats(mirrorImage(first+5, side, 0), 1.f), nm=stats(mirrorImage(first+6, side, 0), 1.f);
            const Stats na=stats(mirrorImage(first+5, side, TGPT_OPTION_ATLAS_TEXTURES), 1.f);
            std::printf("      a normal map tilted toward a lamp at the side: the panel's light %.4f, flat %.4f, tilted away %.4f; with the atlas (no normal maps) %.4f\n",
                        np.mean, nf.mean, nm.mean, na.mean);
            expect(np.mean>1.5*nf.mean && nm.mean<0.5*nf.mean && std::fabs(na.mean-nf.mean)<0.02*nf.mean, "the game's normal map tilts the shading at a hit, in the mesh's tangent frame");
            // PT-46, mirrors not sharp (the owner: "these mirrors still doesn't look like full resolution, are they?"): a perfect
            // mirror shows a texture as sharp as the camera sees it directly from the mirror's place. The same panel, floor and
            // camera; the reference is the panel seen directly from the camera's mirror image under the floor (the floor left out
            // of the traced scene there) along camera rays (the Materials view: what a camera ray reads of a material, over one
            // pixel's footprint), whose picture is the mirror's upside down. Two textures (RGBA8, linear, every mip): a checker of
            // 8-texel squares, each mip its box filter; and one whose every mip is a flat grey of its own ((k + 0.5) / 12 for mip
            // k), which tells the mip a ray read, its fraction too (trilinear filtering blends the two nearest). What a mirror's
            // pixel shows of a texture: its light with the texture less its light with a black panel, over its light with a white
            // one less the black (the same frame indices: the same rays, the same lamp), so the lamp's falloff and the panel's own
            // gloss drop out; seen directly, the Materials view's color with the texture over the white panel's. Fine detail: the
            // spread of each pixel's value less the mean of the 5x5 pixels around it, over the panel's middle.
            {
                const UINT size=1024, mips=11;
                ComPtr<ID3D11Texture2D> fineChecker, mipCoded;
                {
                    std::vector<std::vector<uint32_t>> fine(mips), coded(mips); std::vector<D3D11_SUBRESOURCE_DATA> fi(mips), ci(mips);
                    auto grey=[](float l) { const uint32_t b=uint32_t(std::lround(std::clamp(l, 0.f, 1.f)*255.f)); return b|(b<<8)|(b<<16)|0xFF000000u; };
                    for(UINT m=0;m<mips;++m) {
                        const UINT w=std::max(1u, size>>m), span=1u<<m; // a texel's side in mip 0's texels
                        fine[m].resize(size_t(w)*w); coded[m].assign(size_t(w)*w, grey((float(m)+0.5f)/12.f));
                        for(UINT y=0;y<w;++y) for(UINT x=0;x<w;++x) {
                            uint32_t lit=0;
                            for(UINT j=0;j<span;++j) for(UINT i=0;i<span;++i) lit+=(((x*span+i)/8u+(y*span+j)/8u)&1u);
                            fine[m][size_t(y)*w+x]=grey(darkLinear+(lightLinear-darkLinear)*float(lit)/float(span*span));
                        }
                        fi[m]={fine[m].data(), w*4, 0}; ci[m]={coded[m].data(), w*4, 0};
                    }
                    D3D11_TEXTURE2D_DESC d{size, size, mips, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
                    device->CreateTexture2D(&d, fi.data(), &fineChecker); device->CreateTexture2D(&d, ci.data(), &mipCoded);
                }
                const int32_t fineHandle=shared(fineChecker), codedHandle=shared(mipCoded);
                expect(fineHandle>=0 && codedHandle>=0, "the fine checker and the texture with a grey for each mip shared");
                const uint32_t sharpFirst=uint32_t(list.size());
                TGPT_Material t=white; t.colorTexture=fineHandle; list.push_back(t);                            // +0: the fine checker
                t.colorTexture=codedHandle; list.push_back(t);                                                  // +1: a grey for each mip
                list.push_back(white);                                                                          // +2: white
                TGPT_Material dark=white; for(int k=0;k<3;++k) dark.baseColor[k]=0.f; list.push_back(dark);     // +3: black
                expect(SetMaterials(0, list.data(), uint32_t(list.size()))==0, "materials for the sharp mirror check");
                auto withPanel=[&](uint32_t material, bool floor) {
                    std::vector<TGPT_Instance> l(instances.begin()+(floor ? 0 : 1), instances.end()); auto li=indices;
                    TGPT_Instance p{}; for(int row=0;row<3;++row) for(int col=0;col<4;++col) p.transform[row*4+col]=panelWorld.m[row][col];
                    p.mesh=cardHandle; p.materialBase=uint32_t(li.size()); p.mask=0xFF; li.push_back(material); l.push_back(p);
                    expect(SetScene(l.data(), uint32_t(l.size()), li.data(), uint32_t(li.size()))==0, "scene with the panel");
                };
                // The mirror's reflections alone (32 frames after 2), and the direct view (deterministic: its last frame).
                auto mirrorSeen=[&](uint32_t material, uint32_t options) {
                    expect(SetLights(&front, 1)==0, "the lamp for the panel");
                    withPanel(material, true);
                    std::vector<double> acc(size_t(W)*H*3, 0.0);
                    for(uint32_t i=0;i<34;++i) { run(TGPT_VIEW_SPECULAR, mcam, mcam, i ? 7600+i : 0, honest|options); if(i>=2) { const auto px=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=px[j]/32.0; } }
                    return acc;
                };
                const Camera under=make_camera({0, -0.8f, 0.2f}, {0, 1.f, 2.f}, 60.f, float(W)/float(H));
                auto directSeen=[&](uint32_t material) {
                    withPanel(material, false); // no floor: the camera looks up through its place
                    for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_MATERIALS, under, under, i ? 7700+i : 0, honest);
                    const auto px=readback(); return std::vector<double>(px.begin(), px.end());
                };
                // Each pixel's direct ray to the panel: its texture coordinates, or none.
                std::vector<float> du(size_t(W)*H, -1.f), dv(size_t(W)*H, -1.f);
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const float nx=(x+0.5f)/W*2-1, ny=1-(y+0.5f)/H*2, in[4]={nx, ny, 0.5f, 1.f};
                    float o[4]={0, 0, 0, 0}; for(int r=0;r<4;++r) for(int k=0;k<4;++k) o[r]+=under.invViewProj.m[r][k]*in[k];
                    const V3 d=normalize({o[0]/o[3], o[1]/o[3], o[2]/o[3]});
                    if(d.z<=1e-4f) continue;
                    const V3 q=under.position+d*((2.f-under.position.z)/d.z);
                    const float u=(q.x+1.f)/2.f, v=(q.y-0.02f)/2.f;
                    if(u>0.06f && u<0.94f && v>0.06f && v<0.94f) { du[size_t(y)*W+x]=u; dv[size_t(y)*W+x]=v; }
                }
                struct Sharp { double mip, detail; size_t pixels; };
                auto measure=[&](const std::vector<double>& coded, const std::vector<double>& checkerImg, const std::vector<double>& whiteImg,
                                 const std::vector<double>& blackImg, const std::vector<float>& us, const std::vector<float>& vs) {
                    auto seen=[&](const std::vector<double>& img, size_t i) {
                        const double den=luminanceAt(whiteImg, i)-luminanceAt(blackImg, i);
                        return den>1e-9 ? (luminanceAt(img, i)-luminanceAt(blackImg, i))/den : 0.0;
                    };
                    std::vector<double> sv(size_t(W)*H, 0.0);
                    for(size_t i=0;i<sv.size();++i) sv[i]=seen(checkerImg, i);
                    double mip=0, hp2=0; size_t n=0;
                    for(UINT y=2;y+2<H;++y) for(UINT x=2;x+2<W;++x) {
                        const size_t i=size_t(y)*W+x;
                        if(us[i]<0.2f || us[i]>0.8f || vs[i]<0.2f || vs[i]>0.8f) continue;
                        double box=0; for(int j=-2;j<=2;++j) for(int q=-2;q<=2;++q) box+=sv[size_t(int(y)+j)*W+size_t(int(x)+q)];
                        const double h=sv[i]-box/25.0;
                        hp2+=h*h; mip+=seen(coded, i)*12.0-0.5; ++n;
                    }
                    return Sharp{n ? mip/double(n) : 0.0, n ? std::sqrt(hp2/double(n)) : 0.0, n};
                };
                const auto mWhite=mirrorSeen(sharpFirst+2, 0), mBlack=mirrorSeen(sharpFirst+3, 0);
                const auto mChecker=mirrorSeen(sharpFirst, 0), mCoded=mirrorSeen(sharpFirst+1, 0);
                const auto tChecker=mirrorSeen(sharpFirst, TGPT_OPTION_REFLECTION_TOP_MIP), tCoded=mirrorSeen(sharpFirst+1, TGPT_OPTION_REFLECTION_TOP_MIP);
                const auto dWhite=directSeen(sharpFirst+2), dBlack=directSeen(sharpFirst+3), dChecker=directSeen(sharpFirst), dCoded=directSeen(sharpFirst+1);
                reflectionBoost=1.f;
                const auto bWhite=mirrorSeen(sharpFirst+2, 0), bBlack=mirrorSeen(sharpFirst+3, 0), bChecker=mirrorSeen(sharpFirst, 0), bCoded=mirrorSeen(sharpFirst+1, 0);
                reflectionBoost=0.f;
                save(toFloat(mChecker), "67-mirror-fine-checker.png", true); save(toFloat(dChecker), "68-fine-checker-from-the-mirror-image.png", true);
                const Sharp sm=measure(mCoded, mChecker, mWhite, mBlack, pu, pv), sd=measure(dCoded, dChecker, dWhite, dBlack, du, dv);
                const Sharp st=measure(tCoded, tChecker, mWhite, mBlack, pu, pv), sb=measure(bCoded, bChecker, bWhite, bBlack, pu, pv);
                const double ref=std::max(sd.detail, 1e-12);
                std::printf("      a perfect mirror showing a checker of 8-texel squares, against the camera's view of it from the mirror's place (%zu and %zu pixels): "
                            "mip %.2f read in the mirror, %.2f seen directly; fine detail %.3f of the direct view's (%.4f against %.4f); reading the top mip: mip %.2f, "
                            "detail %.3f; at Reflection Boost 1: mip %.2f, detail %.3f\n", sm.pixels, sd.pixels, sm.mip, sd.mip, sm.detail/ref, sm.detail, sd.detail,
                            st.mip, st.detail/ref, sb.mip, sb.detail/ref);
                expect(sm.pixels>20000 && sd.pixels>20000, "the panel fills a good part of the mirror and of the direct view");
                expect(std::fabs(sm.mip-sd.mip)<0.25, "a mirror's ray reads the mip the camera reads seeing the same thing from the mirror's place");
                expect(sm.detail>0.9*sd.detail, "a perfect mirror shows a fine texture as sharp as the camera sees it directly");
                expect(st.mip<0.25, "for testing, reflections read the top mip");
                expect(std::fabs(sb.mip-sd.mip)<0.25 && sb.detail>0.9*sd.detail, "Reflection Boost leaves a perfect mirror a mirror");
            }
            objects[0]=keepFloor; sunOn=true; rays=keepRays;
            expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "the harness's materials again");
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
        // PT-64: the game's own shaders at a hit (TGPT_Material.program, TGPT_SetMaterialPrograms; pt_materials.hlsli). Since the
        // tracer's own camera rays make every pixel's surface, what a hit reads of a material is what the player sees of it. A
        // traced-only panel (and a cube) stand alone in the traced scene, seen by the tracer's own camera rays; what the tracer
        // read of each material is its albedo and normal views and the roughness under the screen's centre, against what the
        // game's shader computes there, worked out here from its compiled code's arithmetic (not from the tracer's shader).
        // Every map is 64 or 128 texels wide with one mip, 16 to 64 KB: read as the game's own texture, not as an atlas tile.
        if(RegisterSharedTexture && SetMaterialPrograms) {
            auto flat=[&](uint32_t rgba, bool srgb, UINT size=64) {
                std::vector<uint32_t> px(size_t(size)*size, rgba);
                D3D11_TEXTURE2D_DESC d{size, size, 1, 1, srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
                const D3D11_SUBRESOURCE_DATA init{px.data(), size*4, 0};
                ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
            };
            auto halves=[&](uint32_t left, uint32_t right, bool srgb) {
                std::vector<uint32_t> px(128*128);
                for(int y=0;y<128;++y) for(int x=0;x<128;++x) px[size_t(y)*128+x]=x<64 ? left : right;
                D3D11_TEXTURE2D_DESC d{128, 128, 1, 1, srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
                const D3D11_SUBRESOURCE_DATA init{px.data(), 128*4, 0};
                ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
            };
            std::vector<ComPtr<ID3D11Texture2D>> keep;
            uint32_t sharedCount=0, wanted=0;
            auto handleOf=[&](const ComPtr<ID3D11Texture2D>& t) {
                ++wanted; keep.push_back(t);
                TGPT_SharedTexture i{}; i.size=sizeof(i); i.abi=TGPT_ABI;
                const int32_t n=t ? RegisterSharedTexture(pointer(t), &i) : -100;
                if(n>=0) ++sharedCount;
                return n>=0 ? i.handle : -1;
            };
            auto rgba=[](float r, float g, float b, float a) { auto u=[](float v) { return uint32_t(std::lround(std::clamp(v, 0.f, 1.f)*255.f)); }; return u(r)|(u(g)<<8)|(u(b)<<16)|(u(a)<<24); };
            auto srgbToLinear=[](float s) { return s<=0.04045f ? s/12.92f : std::pow((s+0.055f)/1.055f, 2.4f); };
            auto bits=[](float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; };
            // The records, as the plugin builds them: a first row, the textures' handles four to a row, the values.
            std::vector<uint32_t> rows;
            auto record=[&](uint32_t program, const std::vector<int32_t>& textures, const std::vector<std::array<float, 4>>& values) {
                const uint32_t row=uint32_t(rows.size()/4);
                uint32_t present=0; for(size_t i=0;i<textures.size();++i) if(textures[i]!=-1) present|=1u<<i;
                rows.insert(rows.end(), {program, uint32_t(textures.size()), uint32_t(values.size()), present});
                for(size_t i=0;i<((textures.size()+3)/4)*4;++i) rows.push_back(i<textures.size() ? uint32_t(textures[i]) : 0xFFFFFFFFu);
                for(const auto& v:values) for(int k=0;k<4;++k) rows.push_back(bits(v[size_t(k)]));
                return row;
            };
            // The game's value noise and hue turn, from its compiled shaders' arithmetic.
            auto hashOf=[](float cx, float cy) { uint32_t x=uint32_t(int32_t(std::lround(cx))), y=uint32_t(int32_t(std::lround(cy))); y^=1103515245u; x+=y; x*=y; x^=x>>5; x*=0x27d4eb2du; return float(x>>8)/16777215.f; };
            auto valueNoise=[&](float u, float v) {
                const float iu=std::floor(u), iv=std::floor(v); float fu=u-iu, fv=v-iv; fu=fu*fu*(3-2*fu); fv=fv*fv*(3-2*fv);
                const float a=hashOf(iu, iv), b=hashOf(iu+1, iv), c=hashOf(iu, iv+1), d=hashOf(iu+1, iv+1);
                return (a+(b-a)*fu)+((c+(d-c)*fu)-(a+(b-a)*fu))*fv;
            };
            auto simpleNoise=[&](float u, float v, float scale) { return valueNoise(u*scale, v*scale)*0.125f+valueNoise(u*scale*0.5f, v*scale*0.5f)*0.25f+valueNoise(u*scale*0.25f, v*scale*0.25f)*0.5f; };
            auto hueTurn=[](const float in[3], float offset, float out[3]) {
                const float r=in[0], g=in[1], b=in[2];
                float p[4]; if(g>=b) { p[0]=g; p[1]=b; p[2]=0; p[3]=-1.f/3; } else { p[0]=b; p[1]=g; p[2]=-1; p[3]=2.f/3; }
                float q[4]; if(r>=p[0]) { q[0]=r; q[1]=p[1]; q[2]=p[2]; q[3]=p[0]; } else { q[0]=p[0]; q[1]=p[1]; q[2]=p[3]; q[3]=r; }
                const float d=q[0]-std::min(q[3], q[1]), e=1e-10f;
                float h=std::fabs(q[2]+(q[3]-q[1])/(6*d+e))+offset; h=h<0 ? h+1 : h>1 ? h-1 : h;
                const float s=d/(q[0]+e), v=q[0];
                const float k[3]={1.f, 2.f/3, 1.f/3};
                for(int c=0;c<3;++c) { float f=h+k[c]; f-=std::floor(f); const float t=std::clamp(std::fabs(f*6-3)-1, 0.f, 1.f); out[c]=v*(1+(t-1)*s); }
            };
            // The panel: x -1 to 1, y 0.02 to 2.02 at z 2, facing -z (u along +x, v along +y); the camera 3 m in front of its middle.
            const M4 panelWorld=mul(translate_scale({0.f, 1.02f, 2.f}, {2.f, 2.f, 1.f}), rot_x(-pi/2));
            const Camera pcam=make_camera({0, 1.02f, -1.f}, {0, 1.02f, 2.f}, 60.f, float(W)/float(H));
            auto pixelOf=[&](const Camera& c, V3 world, int& x, int& y) {
                const V3 r=world-c.position; const float in[4]={r.x, r.y, r.z, 1.f}; float o[4]={0, 0, 0, 0};
                for(int i=0;i<4;++i) for(int k=0;k<4;++k) o[i]+=c.viewProj.m[i][k]*in[k];
                x=int((o[0]/o[3]*0.5f+0.5f)*W); y=int((0.5f-o[1]/o[3]*0.5f)*H);
            };
            struct Rgb { double r, g, b; };
            auto boxAt=[&](const std::vector<float>& px, const Camera& c, V3 world, int half=8) {
                int cx, cy; pixelOf(c, world, cx, cy);
                Rgb s{0, 0, 0}; int n=0;
                for(int y=cy-half;y<=cy+half;++y) for(int x=cx-half;x<=cx+half;++x) {
                    if(x<0 || y<0 || x>=int(W) || y>=int(H)) continue;
                    const size_t i=(size_t(y)*W+x)*3; s.r+=px[i]; s.g+=px[i+1]; s.b+=px[i+2]; ++n;
                }
                if(n) { s.r/=n; s.g/=n; s.b/=n; }
                return s;
            };
            auto near3=[](Rgb a, double r, double g, double b, double tolerance) { return std::fabs(a.r-r)<tolerance && std::fabs(a.g-g)<tolerance && std::fabs(a.b-b)<tolerance; };
            const auto keepObjectsOwn=ownRays; const bool keepSun=sunOn; const float keepBoost=reflectionBoost;
            ownRays=true; reflectionBoost=0;
            const uint32_t plain=TGPT_OPTION_NO_DENOISER;
            // A scene of one instance of a mesh with one material, seen by pcam: a view's picture (the fourth frame's).
            auto see=[&](int mesh, const M4& world, uint32_t material, uint32_t view, const Camera& c, uint32_t options=0) {
                TGPT_Instance p{}; for(int row=0;row<3;++row) for(int col=0;col<4;++col) p.transform[row*4+col]=world.m[row][col];
                p.mesh=mesh; p.materialBase=0; p.mask=0xFF;
                const uint32_t index[2]={material, material}; // a cube has two submeshes
                expect(SetScene(&p, 1, index, 2)==0, "a scene of one panel");
                for(uint32_t i=0;i<4;++i) run(view, c, c, i ? 8600+i : 0, plain|options);
                return readback();
            };
            auto roughnessAt=[&](int mesh, const M4& world, uint32_t material, const Camera& c) {
                see(mesh, world, material, TGPT_VIEW_NONE, c);
                for(int k=0;k<3;++k) { run(TGPT_VIEW_NONE, c, c, 8610+uint32_t(k), plain); ReadStatus(&status); }
                return int((status.pickSurface>>16)&0xFFu);
            };
            // Maps: greys and colors (sRGB), masks, normals and noise (linear).
            const float baseGrey=srgbToLinear(128/255.f), lightGrey=srgbToLinear(230/255.f);
            const int32_t greyMap=handleOf(flat(rgba(128/255.f, 128/255.f, 128/255.f, 1), true));         // 0.2158 linear
            const int32_t lightMap=handleOf(flat(rgba(230/255.f, 230/255.f, 230/255.f, 1), true));        // 0.7913 linear
            const int32_t orangeMap=handleOf(flat(rgba(230/255.f, 128/255.f, 51/255.f, 1), true));
            const int32_t tiltU=handleOf(flat(rgba(0.75f, 0.5f, 1, 1), false));                             // a normal map: x 0.5 (toward +u)
            const int32_t tiltV=handleOf(flat(rgba(0.5f, 0.75f, 1, 1), false));                             // y 0.5 (toward +v)
            const int32_t maskHalf=handleOf(flat(rgba(0, 0.25f, 0, 0.5f), false));                          // metal 0, green 0.25, alpha 0.5
            const int32_t noiseHalf=handleOf(flat(rgba(0.25f, 0.25f, 0.5f, 1), false));                     // red 0.25, blue 0.5
            const int32_t stripes=handleOf(halves(rgba(128/255.f, 128/255.f, 128/255.f, 1), rgba(230/255.f, 230/255.f, 230/255.f, 1), true)); // left 0.2158, right 0.7913
            const int32_t splatHalves=handleOf(halves(rgba(1, 0, 0, 0), rgba(0, 1, 0, 0), false));          // left: layer 0, right: layer 1
            const int32_t maskLeftRight=handleOf(halves(rgba(0, 0, 0, 0), rgba(1, 1, 1, 1), false));        // left 0, right 1
            const int32_t detailMap=handleOf(flat(rgba(0.75f, 0.5f, 0.75f, 0.5f), false));                  // albedo +0.5, smoothness +0.5, no bump
            const int32_t redQuarter=handleOf(flat(rgba(0.25f, 1, 1, 1), false));                           // an emissive map: red 0.25
            std::printf("      the maps of the material programs' checks: %u of %u (64x64 and 128x128, one mip: 16 and 64 KB) shared with the tracer\n", sharedCount, wanted);
            expect(sharedCount==wanted && greyMap>=0, "small textures are shared as any other (none of these maps is read from an atlas tile)");
            TGPT_Material white=materials[0];
            white.baseColor[0]=white.baseColor[1]=white.baseColor[2]=1.f; white.smoothness=0.f; white.albedoTile=-1;
            white.colorTexture=white.normalTexture=white.maskTexture=white.emissiveTexture=-1;
            std::vector<TGPT_Material> list=materials;
            auto add=[&](const TGPT_Material& m) { list.push_back(m); return uint32_t(list.size()-1); };
            auto commit=[&] {
                expect(SetMaterialPrograms(0, rows.data(), uint32_t(rows.size()/4))==0, "the material programs' records set");
                expect(SetMaterials(0, list.data(), uint32_t(list.size()))==0, "the materials with programs set");
            };
            const V3 middle{0, 1.02f, 2.f}, leftSide{-0.5f, 1.02f, 2.f}, rightSide{0.5f, 1.02f, 2.f};

            // ---- TG/Architecture/KF_Architecture
            // Its record's values: [0] overlay tint, overlay smoothness; [1] base and overlay normal strength, overlay tiling, micro
            // tiling; [2] slope ends, bottom height, micro strength; [3] mask tiling, mask offset, base smoothness; [4] overlay on,
            // mask texture on, add occlusion mask, distance smoothness; [5] occlusion multiply.
            // (overlayNormal: -1 the material has none; a handle no texture has: it has one the tracer has not open.)
            auto architecture=[&](float overlayOn, float maskTexture, float addOcclusion, float bottomHeight, float microStrength, float distanceSmooth, int32_t base=-2, int32_t overlayNormal=-1) {
                TGPT_Material m=white; m.colorTexture=base==-2 ? greyMap : base; m.normalTexture=tiltU; m.maskTexture=maskHalf; m.normalScale=1.f;
                m.baseColor[0]=m.baseColor[1]=m.baseColor[2]=0.6f; // its tint
                m.program=TGPT_PROGRAM_ARCHITECTURE;
                m.programRow=record(TGPT_PROGRAM_ARCHITECTURE, {lightMap, overlayNormal, maskHalf, tiltV, noiseHalf, m.colorTexture, tiltU, maskHalf},
                    {{0.5f, 0.5f, 0.5f, 1.f}, {1.f, 1.f, 1.f, 1.f}, {0.f, 1.f, bottomHeight, microStrength}, {1.f, 0.f, 0.f, 0.8f}, {overlayOn, maskTexture, addOcclusion, distanceSmooth}, {0.f, 0.f, 0.f, 0.f}});
                return add(m);
            };
            const uint32_t archPlain=architecture(0, 0, 0, -100, 0, 0), archMicro=architecture(0, 0, 0, -100, 1, 0), archOverlay=architecture(1, 0, 0, -100, 0, 0);
            const uint32_t archOcclusion=architecture(1, 0, 1, -100, 0, 0), archNoise=architecture(1, 1, 0, -100, 0, 0), archFoot=architecture(1, 0, 0, 100, 0, 0);
            const uint32_t archFar=architecture(0, 0, 0, -100, 0, 1), archOrange=architecture(0, 0, 0, -100, 0, 0, orangeMap);
            // As archMicro, but its record names an overlay normal map no open texture has (number 9000 of the registry, serial 1).
            const uint32_t archWaiting=architecture(0, 0, 0, -100, 1, 0, -2, int32_t(9000u|(1u<<14)));
            // ---- TG/Environment/Cliffs/Cliff
            // [0] layer 0 normal strength, curvature, occlusion to base color, occlusion-based smoothness; [1] layer 1 tint and normal
            // strength; [2] layer 2 tint and normal strength; [3] layer 1 and 2 tiling, layer 2 coverage, its occlusion-based mask;
            // [4] smoothness ends of layers 0 and 1; [5] of layer 2, the world mask's tiling and influence; [6] fuzz vector and
            // fuzziness; [7] lower layers' normal share under moss, formation mask, layer 2 on.
            auto cliff=[&](float curvature, float occlusionToBase, float coverage, float layer2, int32_t layer1Map, float fuzziness, int32_t mask) {
                TGPT_Material m=white; m.colorTexture=greyMap; m.normalTexture=-1; m.maskTexture=mask; m.normalScale=1.f;
                m.baseColor[0]=m.baseColor[1]=m.baseColor[2]=0.5f; // layer 0's tint
                m.program=TGPT_PROGRAM_CLIFF;
                m.programRow=record(TGPT_PROGRAM_CLIFF, {layer1Map, -1, lightMap, -1, -1, greyMap, -1, mask},
                    {{1.f, curvature, occlusionToBase, 0.f}, {0.8f, 0.8f, 0.8f, 1.f}, {0.4f, 0.4f, 0.4f, 1.f}, {1.f, 1.f, coverage, 0.f}, {0.2f, 0.6f, 0.f, 1.f}, {0.f, 0.5f, 0.02f, 1.f},
                     {1.f, -2.f, 0.5f, fuzziness}, {0.5f, 1.f, layer2, 0.f}});
                return add(m);
            };
            const int32_t curvedMask=handleOf(flat(rgba(0, 0.9f, 0, 0.5f), false)); // green 0.9 (an edge), alpha 0.5 (a hollow)
            const uint32_t cliffBase=cliff(0, 0, 1, 0, lightMap, 0, -1), cliffCurved=cliff(0.5f, 1, 1, 0, lightMap, 0, curvedMask), cliffMoss=cliff(0, 0, 1, 1, lightMap, 0, -1);
            const uint32_t cliffStripes=cliff(0, 0, 1, 0, stripes, 0, -1), cliffFuzz=cliff(0, 0, 1, 1, lightMap, 1, -1), cliffBare=cliff(0, 0, 0.5f, 1, lightMap, 0, -1);
            // ---- TG/Vegetation/Vegetation_Grass and _Leaves
            // [0] healthy color, variation scale; [1] dry color; grass: [2] ground color, hardness; [3] ground on, fuzzy on.
            auto plant=[&](bool leaves, float ground, float fuzzy) {
                TGPT_Material m=white; m.colorTexture=lightMap; m.flags|=TGPT_MATERIAL_DOUBLE_SIDED;
                m.baseColor[0]=m.baseColor[1]=m.baseColor[2]=0.5f; // HDRP Lit's way: the mean of its two colors
                m.program=leaves ? TGPT_PROGRAM_LEAVES : TGPT_PROGRAM_GRASS;
                std::vector<std::array<float, 4>> values={{0.2f, 0.6f, 0.2f, 16.f}, {0.8f, 0.8f, 0.2f, 0.f}};
                if(!leaves) { values.push_back({0.3f, 0.1f, 0.1f, 4.f}); values.push_back({ground, fuzzy, 0.f, 0.f}); }
                m.programRow=record(m.program, {noiseHalf, lightMap, -1, -1}, values);
                return add(m);
            };
            const uint32_t grassPlain=plant(false, 0, 0), grassFuzzy=plant(false, 0, 1), grassGround=plant(false, 1, 0), leavesPlain=plant(true, 0, 0);
            // ---- TG/Vegetation/Vegetation_Trunk: [0] bark color and normal scale, [1] bark tiling and offset.
            TGPT_Material trunkM=white; trunkM.colorTexture=greyMap; trunkM.baseColor[0]=trunkM.baseColor[1]=trunkM.baseColor[2]=0.5f;
            trunkM.program=TGPT_PROGRAM_TRUNK;
            trunkM.programRow=record(TGPT_PROGRAM_TRUNK, {stripes, -1, -1, maskLeftRight, greyMap, -1, -1}, {{0.9f, 0.9f, 0.9f, 1.f}, {1.f, 1.f, 0.f, 0.f}});
            const uint32_t trunk=add(trunkM);
            // ---- TG/Environment/MeshTerrain: [0] offset x and z, splat size, height transition; per layer its tint and tiling, and
            // its normal strength, height and smoothness scales; [17] the splat maps clamp.
            TGPT_Material terrainM=white; terrainM.program=TGPT_PROGRAM_TERRAIN;
            {
                std::vector<int32_t> textures={splatHalves, -1};
                std::vector<std::array<float, 4>> values={{0.5f, 0.f, 1.f, 0.1f}};
                for(int i=0;i<8;++i) {
                    textures.push_back(i==0 ? stripes : i==1 ? lightMap : -1); textures.push_back(-1); textures.push_back(i==1 ? maskHalf : -1);
                    values.push_back({i==1 ? 0.5f : 1.f, i==1 ? 0.5f : 1.f, i==1 ? 0.5f : 1.f, i==0 ? 2.f : 1.f});
                    values.push_back({1.f, 1.f, i==1 ? 0.6f : 1.f, 0.f});
                }
                values.push_back({0.f, 0.f, 0.f, 0.f});
                terrainM.programRow=record(TGPT_PROGRAM_TERRAIN, textures, values);
            }
            const uint32_t terrain=add(terrainM);
            // ---- TG/Architecture/TriplanarArchitecture2: [0] layer A's tint and tiling, [1] B's; [2] the sharpness of A, B and the
            // blend map, its tiling; [3] A's and B's normal strength, the blend normal's strength, _Blend; [4] the blend map's
            // remap, the feather, the height-based blend; [5] the dirt tint and dirt A; [6] dirt B, C, D, the rotation.
            const int32_t maskB25=handleOf(flat(rgba(0, 0.6f, 0.25f, 0.4f), false));     // layer B: height 0.25, smoothness 0.4
            const int32_t blendMap=handleOf(flat(rgba(0.25f, 0.5f, 0, 0), false));      // red 0.25, green 0.5
            auto triplanar=[&](float blendAmount, float dirtB, int32_t normalA) {
                TGPT_Material m=white; m.program=TGPT_PROGRAM_TRIPLANAR;
                m.programRow=record(TGPT_PROGRAM_TRIPLANAR, {stripes, normalA, -1, lightMap, -1, maskB25, blendMap, -1},
                    {{0.5f, 0.5f, 0.5f, 1.f}, {0.8f, 0.8f, 0.8f, 1.f}, {0.5f, 0.5f, 0.5f, 0.5f}, {1.f, 1.f, 0.f, blendAmount}, {0.f, 1.f, 0.1f, 0.5f},
                     {0.5f, 0.5f, 0.5f, 0.f}, {dirtB, 0.f, 0.f, 0.f}});
                return add(m);
            };
            const uint32_t triA=triplanar(1.f, 0.f, -1), triB=triplanar(0.f, 0.f, -1), triDirt=triplanar(0.f, 1.f, -1), triBumped=triplanar(1.f, 0.f, tiltU);
            // ---- HDRP Lit with its detail map: [0] the detail map's tiling and offset, [1] its albedo, normal and smoothness scales.
            TGPT_Material detailM=white; detailM.colorTexture=greyMap; detailM.smoothness=0.2f; detailM.program=TGPT_PROGRAM_DETAIL;
            detailM.programRow=record(TGPT_PROGRAM_DETAIL, {detailMap, greyMap, -1, -1}, {{1.f, 1.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 0.f}});
            const uint32_t detail=add(detailM);
            TGPT_Material noDetail=detailM; noDetail.program=0; const uint32_t litPlain=add(noDetail);
            // ---- HDRP Lit's normal map by its scale, Shader Graph's Normal Strength, and HDRP's Flip mode on a back face.
            TGPT_Material bumped=white; bumped.normalTexture=tiltU; bumped.normalScale=2.f; const uint32_t litScaled=add(bumped);
            bumped.normalScale=0.5f; const uint32_t litHalf=add(bumped);
            bumped.textureFlags=TGPT_TEXTURE_GRAPH_NORMAL; const uint32_t graphHalf=add(bumped);
            bumped=white; bumped.normalTexture=tiltU; bumped.normalScale=1.f; bumped.flags|=TGPT_MATERIAL_DOUBLE_SIDED; const uint32_t mirrorBack=add(bumped);
            bumped.textureFlags=TGPT_TEXTURE_FLIP_BACK; const uint32_t flipBack=add(bumped);
            bumped=white; bumped.normalTexture=tiltU; bumped.normalScale=1.f; bumped.uvScaleOffset[0]=-1.f; const uint32_t mirroredTiling=add(bumped);
            // ---- the emissive map itself, and its red alone.
            TGPT_Material glow=white; glow.baseColor[0]=glow.baseColor[1]=glow.baseColor[2]=0.f; glow.emissive[0]=glow.emissive[1]=glow.emissive[2]=4.f;
            const uint32_t glowPlain=add(glow);
            glow.emissiveTexture=redQuarter; const uint32_t glowMapped=add(glow);
            glow.textureFlags=TGPT_TEXTURE_EMISSIVE_RED; const uint32_t glowRed=add(glow);
            commit();

            // KF_Architecture.
            {
                const Rgb base=boxAt(see(cardHandle, panelWorld, archPlain, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb off=boxAt(see(cardHandle, panelWorld, archPlain, TGPT_VIEW_ALBEDO, pcam, TGPT_OPTION_ATLAS_TEXTURES), pcam, middle);
                const Rgb overlay=boxAt(see(cardHandle, panelWorld, archOverlay, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb occluded=boxAt(see(cardHandle, panelWorld, archOcclusion, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb noisy=boxAt(see(cardHandle, panelWorld, archNoise, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb foot=boxAt(see(cardHandle, panelWorld, archFoot, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                // The base layer: its map times its tint. The overlay (its map 0.79 times its tint 0.5) over it: by the slope's share
                // (a wall: half way between its two ends, 0 and 1), by the darker of that and the mask map's green (0.25: 0.75),
                // times the mask noise's blue (0.5: 0.25), and whole over the object's foot (a bottom height no point is above).
                const double b=baseGrey*0.6, o=lightGrey*0.5;
                std::printf("      KF_Architecture: base %.4f (its map times its tint: %.4f; without its program %.4f); with its overlay by slope %.4f (%.4f), by the occlusion mask %.4f (%.4f), "
                            "by the mask noise %.4f (%.4f), over the object's foot %.4f (%.4f)\n", base.g, b, off.g, overlay.g, b+(o-b)*0.5, occluded.g, b+(o-b)*0.75, noisy.g, b+(o-b)*0.25, foot.g, o);
                expect(near3(base, b, b, b, 0.01), "KF_Architecture's base layer is its color map times its tint");
                expect(near3(overlay, b+(o-b)*0.5, b+(o-b)*0.5, b+(o-b)*0.5, 0.012), "KF_Architecture's overlay layer comes in by the slope between its two ends");
                expect(near3(occluded, b+(o-b)*0.75, b+(o-b)*0.75, b+(o-b)*0.75, 0.012), "KF_Architecture's overlay follows the mask map's occlusion where the material adds it");
                expect(near3(noisy, b+(o-b)*0.25, b+(o-b)*0.25, b+(o-b)*0.25, 0.012), "KF_Architecture's overlay is cut by its mask noise over the world");
                expect(near3(foot, o, o, o, 0.012), "KF_Architecture's overlay covers the object below its bottom height");
                // Its normal: the base normal map by its strength, the micro detail normal laid over it. The panel's tangent is +x,
                // its bitangent +y, its normal -z: a map tilted half way toward +u reads (0.5, 0, -0.866), with the micro map
                // (half way toward +v) (0.5, 0.5, -0.75) over its length.
                const Rgb n0=boxAt(see(cardHandle, panelWorld, archPlain, TGPT_VIEW_NORMALS, pcam), pcam, middle);
                const Rgb n1=boxAt(see(cardHandle, panelWorld, archMicro, TGPT_VIEW_NORMALS, pcam), pcam, middle);
                const double l=std::sqrt(0.5*0.5+0.5*0.5+0.75*0.75);
                std::printf("      its normal: (%.3f, %.3f, %.3f) from its base map (0.500, 0.000, -0.866); with the micro detail map (%.3f, %.3f, %.3f) (%.3f, %.3f, %.3f)\n",
                            n0.r*2-1, n0.g*2-1, n0.b*2-1, n1.r*2-1, n1.g*2-1, n1.b*2-1, 0.5/l, 0.5/l, -0.75/l);
                expect(near3(n0, 0.75, 0.5, 0.5-0.433, 0.01), "KF_Architecture's base normal map is read (its name is not HDRP Lit's)");
                expect(near3(n1, 0.5+0.25/l, 0.5+0.25/l, 0.5-0.375/l, 0.01), "KF_Architecture's micro detail normal is laid over its base normal");
                // A program waits for every texture its material has: with one not open (a streamed texture not there yet) the
                // material is read HDRP Lit's way from its own fields (its base normal map alone: no micro detail), never half.
                const Rgb n2=boxAt(see(cardHandle, panelWorld, archWaiting, TGPT_VIEW_NORMALS, pcam), pcam, middle);
                std::printf("      with a texture of its record not open: (%.3f, %.3f, %.3f) (HDRP Lit's way: 0.500, 0.000, -0.866)\n", n2.r*2-1, n2.g*2-1, n2.b*2-1);
                expect(near3(n2, 0.75, 0.5, 0.5-0.433, 0.01), "a program waits for every texture its material has (HDRP Lit's way from the material's own fields until then)");
                // Its smoothness: the mask map's alpha times the base smoothness (0.5 x 0.8: roughness 0.6), toward 0.1 with the
                // distance where the material says so (3 m: 15 % of the way).
                const int r0=roughnessAt(cardHandle, panelWorld, archPlain, pcam), r1=roughnessAt(cardHandle, panelWorld, archFar, pcam);
                std::printf("      its roughness under the screen's centre: %d of 255 (153), fading with distance %d (164)\n", r0, r1);
                expect(std::abs(r0-153)<=2 && std::abs(r1-164)<=2, "KF_Architecture's smoothness is its mask's alpha times its base smoothness, fading with distance where the material says so");
                // The slight turn of its hue from place to place: an orange map, pixel by pixel against the game's arithmetic.
                const auto hued=see(cardHandle, panelWorld, archOrange, TGPT_VIEW_ALBEDO, pcam);
                const float orange[3]={srgbToLinear(230/255.f)*0.6f, srgbToLinear(128/255.f)*0.6f, srgbToLinear(51/255.f)*0.6f};
                double off2=0, turned=0; int count=0;
                for(int k=-8;k<=8;++k) {
                    const V3 at{float(k)*0.1f, 1.02f, 2.f};
                    float want[3]; hueTurn(orange, simpleNoise(at.x, at.z, 1.f)*0.06f-0.03f, want);
                    const Rgb got=boxAt(hued, pcam, at, 2);
                    off2+=std::fabs(got.r-want[0])+std::fabs(got.g-want[1])+std::fabs(got.b-want[2]);
                    turned+=std::fabs(want[0]-orange[0])+std::fabs(want[1]-orange[1])+std::fabs(want[2]-orange[2]); ++count;
                }
                std::printf("      its hue from place to place, 17 points across the panel: off the game's arithmetic by %.4f a channel on average; that arithmetic moves the color %.4f a channel\n",
                            off2/(3*count), turned/(3*count));
                expect(turned/(3*count)>0.004 && off2/(3*count)<0.4*turned/(3*count), "KF_Architecture's hue turns from place to place as the game's noise over the world says");
            }
            // Cliff.
            {
                const float closeBy=1.f-3.f*0.005f; // the panel's middle is 3 m from the camera
                const double l0=baseGrey*0.5, l1=lightGrey*0.8, base=l0+(l1-l0)*closeBy;
                const Rgb plainC=boxAt(see(cardHandle, panelWorld, cliffBase, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb curved=boxAt(see(cardHandle, panelWorld, cliffCurved, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb moss=boxAt(see(cardHandle, panelWorld, cliffMoss, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb bare=boxAt(see(cardHandle, panelWorld, cliffBare, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                // Edges brighten it (the mask's green 0.9: fully an edge; curvature 0.5, less half of it up close), hollows darken
                // it (the mask's alpha 0.5: times 0.5, as far as the material's occlusion to base color, 1).
                const double edge=1-(1-1*0.5*(1-0.5*closeBy))*(1-base), curvedWant=edge*0.5;
                const double mossWant=0.4*lightGrey;
                std::printf("      Cliff: up close %.4f (layer 1 over the formation's own color: %.4f); with curvature and occlusion %.4f (%.4f); moss over it %.4f (%.4f); "
                            "at a coverage that leaves a wall bare %.4f\n", plainC.g, base, curved.g, curvedWant, moss.g, mossWant, bare.g);
                expect(near3(plainC, base, base, base, 0.012), "the Cliff's layer 1 (the rock up close) takes over from the formation's own color toward the camera");
                expect(near3(curved, curvedWant, curvedWant, curvedWant, 0.012), "the Cliff's mask brightens its edges and darkens its hollows");
                expect(near3(moss, mossWant, mossWant, mossWant, 0.012) && near3(bare, base, base, base, 0.012), "the Cliff's moss covers what its coverage, slope and masks let it");
                // Layer 1 is tiled by the object's size: the panel is 2 m wide, so a map of two halves shows four stripes across it.
                const auto striped=see(cardHandle, panelWorld, cliffStripes, TGPT_VIEW_ALBEDO, pcam);
                const Rgb s0=boxAt(striped, pcam, {-0.75f, 1.02f, 2.f}), s1=boxAt(striped, pcam, {-0.25f, 1.02f, 2.f}), s2=boxAt(striped, pcam, {0.25f, 1.02f, 2.f}), s3=boxAt(striped, pcam, {0.75f, 1.02f, 2.f});
                std::printf("      its layer 1 across the 2 m panel (a map of a dark and a light half): %.3f %.3f %.3f %.3f\n", s0.g, s1.g, s2.g, s3.g);
                expect(s1.g>s0.g+0.2 && s3.g>s2.g+0.2 && std::fabs(s0.g-s2.g)<0.03, "the Cliff's layer 1 is tiled by the object's size (its map twice across a 2 m panel)");
                // The moss's fuzz: by the sun's direction and the camera's axis (the panel's bumps flat: the sun's forward read in
                // the panel's frame, tangent +x, bitangent +y, normal -z).
                const Rgb fuzzy=boxAt(see(cardHandle, panelWorld, cliffFuzz, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const V3 forward=sunDir*-1.f; const V3 leaned=normalize(V3{forward.x, forward.y, -forward.z});
                const double along=std::clamp(double(leaned.z), 0.0, 1.0), fuzz=std::pow(1-along, 1.0)*0.5+along*2+1;
                std::printf("      its fuzz: %.4f against %.4f (the moss times %.3f)\n", fuzzy.g, std::min(mossWant*fuzz, 1.0), fuzz);
                expect(std::fabs(fuzzy.g-std::min(mossWant*fuzz, 1.0))<0.02 && fuzz>1.1, "the Cliff's moss is brightened by its fuzz, by the sun's direction and the camera's axis");
                const int rough=roughnessAt(cardHandle, panelWorld, cliffBase, pcam);
                // Smoothness: layer 0's from its color map's red between its ends (0.2 to 0.6 by 0.2158: 0.286), layer 1's (0 to 1 by
                // 0.79) screened over it up close, and layer 2's (0 to 0.5 by 0.79) over that as far as the moss would cover (a
                // material without the layer keeps that part: the shader's variant without it computes it so).
                const double s0s=0.2+0.4*baseGrey, s01=s0s+closeBy*(1-s0s)*lightGrey, smooth=s01+(1-s01)*0.5*lightGrey;
                std::printf("      its roughness: %d of 255 (%.0f)\n", rough, (1-smooth)*255);
                expect(std::abs(rough-int(std::lround((1-smooth)*255)))<=3, "the Cliff's smoothness comes from its layers' color maps' red between their ends");
            }
            // Grass and leaves.
            {
                const double tex=lightGrey;
                const double want[3]={tex*(0.2+(0.8-0.2)*0.25), tex*(0.6+(0.8-0.6)*0.25), tex*0.2};
                const Rgb grass=boxAt(see(cardHandle, panelWorld, grassPlain, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb without=boxAt(see(cardHandle, panelWorld, grassPlain, TGPT_VIEW_ALBEDO, pcam, TGPT_OPTION_ATLAS_TEXTURES), pcam, middle);
                const Rgb fuzzy=boxAt(see(cardHandle, panelWorld, grassFuzzy, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb leaf=boxAt(see(cardHandle, panelWorld, leavesPlain, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                std::printf("      grass: (%.3f, %.3f, %.3f) against its healthy and dry colors by the noise (%.3f, %.3f, %.3f); without its program (%.3f, %.3f, %.3f); "
                            "with fuzzy shading (%.3f, %.3f, %.3f); leaves (%.3f, %.3f, %.3f)\n", grass.r, grass.g, grass.b, want[0], want[1], want[2], without.r, without.g, without.b,
                            fuzzy.r, fuzzy.g, fuzzy.b, leaf.r, leaf.g, leaf.b);
                expect(near3(grass, want[0], want[1], want[2], 0.012), "grass is its map times its healthy color, toward its dry color by the game's noise over the world");
                expect(std::fabs(without.g-without.r)<0.02, "(without its program, grass is its map times one grey: the reading before)");
                expect(near3(fuzzy, std::min(want[0]*1.5, 1.0), std::min(want[1]*1.5, 1.0), want[2]*1.5, 0.015), "grass with fuzzy shading is half again as bright, as the game's shader makes it");
                expect(near3(leaf, want[0], want[1], want[2], 0.012), "leaves take their healthy and dry colors by the game's noise from three sides of the world");
                // The ground's color at a blade's foot: a cube from y 0 to 2 (its own space -0.5 to 0.5), the ground's color below its
                // middle, the grass's from a quarter of its height above it (hardness 4).
                const M4 cubeWorld=translate_scale({0.f, 1.02f, 3.f}, {2.f, 2.f, 2.f});
                const auto footed=see(cubeHandle, cubeWorld, grassGround, TGPT_VIEW_ALBEDO, pcam);
                const Rgb top=boxAt(footed, pcam, {0, 1.82f, 2.f}), low=boxAt(footed, pcam, {0, 0.4f, 2.f});
                std::printf("      a 2 m cube of grass with the ground's color at its foot: (%.3f, %.3f, %.3f) near its top, (%.3f, %.3f, %.3f) below its middle (the ground's 0.3, 0.1, 0.1)\n",
                            top.r, top.g, top.b, low.r, low.g, low.b);
                expect(near3(top, want[0], want[1], want[2], 0.012) && near3(low, 0.3, 0.1, 0.1, 0.012), "grass takes the ground's color at its foot, by its height in the object");
            }
            // Trunk: bark over the trunk where its mask says (the panel's right half), the bark's map read by the mesh's fourth set
            // of texture coordinates (0.25 across everywhere: the map's dark half, where the first coordinates there read its
            // light half).
            {
                std::vector<float> uv2(8); for(int k=0;k<4;++k) { uv2[size_t(k)*2]=0.25f; uv2[size_t(k)*2+1]=0.5f; }
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth=UINT(uv2.size()*4); bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
                const D3D11_SUBRESOURCE_DATA init{uv2.data()}; ComPtr<ID3D11Buffer> second; device->CreateBuffer(&bd, &init, &second);
                TGPT_MeshDesc d{sizeof(TGPT_MeshDesc), TGPT_ABI};
                d.key=901; d.vertexBuffers[0]=uint64_t(uintptr_t(cardMesh.vb.Get())); d.strides[0]=sizeof(Vertex);
                d.vertexBuffers[1]=uint64_t(uintptr_t(second.Get())); d.strides[1]=8;
                d.positionStream=0; d.positionOffset=0; d.positionFormat=TGPT_VF_FLOAT32; d.positionDimension=3;
                d.uvStream=0; d.uvOffset=24; d.uvFormat=TGPT_VF_FLOAT32; d.uvDimension=2;
                d.colorStream=0xFFFFFFFFu;
                d.normalStream=0; d.normalOffset=12; d.normalFormat=TGPT_VF_FLOAT32; d.normalDimension=3;
                d.uv2Stream=1; d.uv2Offset=0; d.uv2Format=TGPT_VF_FLOAT32; d.uv2Dimension=2;
                d.indexBuffer=uint64_t(uintptr_t(cardMesh.ib.Get())); d.indexFormat=16; d.vertexCount=cardMesh.vertexCount;
                d.submeshCount=uint32_t(cardMesh.submeshes.size()); d.submeshes=cardMesh.submeshes.data();
                const int twoSets=RegisterMesh(&d);
                expect(twoSets>0, "a mesh with a fourth set of texture coordinates registered");
                std::vector<float> px;
                for(int attempt=0;attempt<6;++attempt) { px=see(twoSets, panelWorld, trunk, TGPT_VIEW_ALBEDO, pcam); if(boxAt(px, pcam, middle).g>0.01) break; }
                const Rgb left=boxAt(px, pcam, leftSide), right=boxAt(px, pcam, rightSide);
                std::printf("      a trunk: %.4f where its mask leaves the trunk (its map times its color: %.4f), %.4f where it lays bark (the bark's map by the fourth coordinates times its color: %.4f)\n",
                            left.g, baseGrey*0.5, right.g, baseGrey*0.9);
                expect(near3(left, baseGrey*0.5, baseGrey*0.5, baseGrey*0.5, 0.012), "a trunk's own layer is its color map times its color");
                expect(near3(right, baseGrey*0.9, baseGrey*0.9, baseGrey*0.9, 0.012), "a trunk's bark is laid by its mask and read by the mesh's fourth set of texture coordinates");
            }
            // MeshTerrain: a 4 m square lying at y 0.05, seen from above. Layer 0 (a map of a dark and a light half, 2 m a repeat,
            // moved 0.5 m) where the splat map's red is, layer 1 (a light map times 0.5) where its green is: the splat map over the
            // mesh's own extent (its halves), the layers over the world.
            {
                const M4 ground=translate_scale({0.f, 0.05f, 2.f}, {4.f, 1.f, 4.f});
                const Camera above=make_camera({0, 5.f, 1.99f}, {0, 0.f, 2.f}, 60.f, float(W)/float(H));
                const auto px=see(cardHandle, ground, terrain, TGPT_VIEW_ALBEDO, above);
                // The mesh's own x from -0.5 to 0.5 over the splat map: below 0 its right half (layer 1), above its left (layer 0).
                const Rgb l1=boxAt(px, above, {-1.f, 0.05f, 2.f}), a=boxAt(px, above, {0.25f, 0.05f, 2.f}), b=boxAt(px, above, {1.f, 0.05f, 2.f}), c=boxAt(px, above, {1.75f, 0.05f, 2.f});
                std::printf("      a terrain: %.4f where the splat map gives layer 1 (its map times its tint: %.4f); layer 0 over the world at x 0.25, 1 and 1.75 m: %.4f %.4f %.4f "
                            "(its dark half %.4f from -0.5 to 0.5 and 1.5 to 2.5, its light half %.4f between)\n", l1.g, lightGrey*0.5, a.g, b.g, c.g, baseGrey, lightGrey);
                expect(near3(l1, lightGrey*0.5, lightGrey*0.5, lightGrey*0.5, 0.012), "a terrain's splat map picks the layer, laid over the mesh's own extent");
                expect(near3(a, baseGrey, baseGrey, baseGrey, 0.012) && near3(b, lightGrey, lightGrey, lightGrey, 0.012) && near3(c, baseGrey, baseGrey, baseGrey, 0.012),
                       "a terrain's layers are laid flat over the world by their tiling and offset, not by the mesh's coordinates");
                const Camera overLayer1=make_camera({-1.f, 5.f, 1.99f}, {-1.f, 0.f, 2.f}, 60.f, float(W)/float(H));
                const int rough=roughnessAt(cardHandle, ground, terrain, overLayer1);
                std::printf("      its roughness where layer 1 is: %d of 255 (its mask's alpha 0.5 times 0.6: 179)\n", rough);
                expect(std::abs(rough-179)<=2, "a terrain's smoothness is its layer's mask alpha times the layer's scale");
            }
            // TriplanarArchitecture2. The panel faces -z: only the side seen along z counts, its map laid by the world's x and y.
            // _Blend 1 leaves layer A (its map of a dark and a light half, 1 m a repeat, times 0.5): by the world's x it is dark at
            // -0.75 and 0.25 m, light at -0.25 and 0.75 m (by the mesh's coordinates, two halves across the panel). _Blend 0, and
            // layer B's height 0.25 under the blend map's 0.25, lets layer B in whole (its map times 0.8); dirt B by the blend
            // map's green 0.5 takes it half way to the dirt tint 0.5. Its normal: A's map tilted half way toward +x, halved
            // where the surface faces away from the sun (this panel does): (0.259, 0, -0.966) in the world.
            {
                const auto across=see(cardHandle, panelWorld, triA, TGPT_VIEW_ALBEDO, pcam);
                const Rgb t0=boxAt(across, pcam, {-0.75f, 1.02f, 2.f}), t1=boxAt(across, pcam, {-0.25f, 1.02f, 2.f}), t2=boxAt(across, pcam, {0.25f, 1.02f, 2.f}), t3=boxAt(across, pcam, {0.75f, 1.02f, 2.f});
                const Rgb onlyB=boxAt(see(cardHandle, panelWorld, triB, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb dirty=boxAt(see(cardHandle, panelWorld, triDirt, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb bumped=boxAt(see(cardHandle, panelWorld, triBumped, TGPT_VIEW_NORMALS, pcam), pcam, {0.f, 0.8f, 2.f});
                const int rough=roughnessAt(cardHandle, panelWorld, triB, pcam);
                std::printf("      TriplanarArchitecture2: layer A across the panel %.3f %.3f %.3f %.3f (%.3f and %.3f by the world's x); layer B %.4f (%.4f), with its dirt %.4f (%.4f), "
                            "roughness %d of 255 (153); its normal (%.3f, %.3f, %.3f) (0.259, 0, -0.966)\n", t0.g, t1.g, t2.g, t3.g, baseGrey*0.5, lightGrey*0.5, onlyB.g, lightGrey*0.8,
                            dirty.g, lightGrey*0.6, rough, bumped.r*2-1, bumped.g*2-1, bumped.b*2-1);
                expect(near3(t0, baseGrey*0.5, baseGrey*0.5, baseGrey*0.5, 0.012) && near3(t1, lightGrey*0.5, lightGrey*0.5, lightGrey*0.5, 0.012) &&
                       near3(t2, baseGrey*0.5, baseGrey*0.5, baseGrey*0.5, 0.012) && near3(t3, lightGrey*0.5, lightGrey*0.5, lightGrey*0.5, 0.012),
                       "TriplanarArchitecture2's layers are laid over the world, not by the mesh's coordinates");
                expect(near3(onlyB, lightGrey*0.8, lightGrey*0.8, lightGrey*0.8, 0.012) && std::abs(rough-153)<=2, "TriplanarArchitecture2's layer B comes in by its blend map and its height");
                expect(near3(dirty, lightGrey*0.6, lightGrey*0.6, lightGrey*0.6, 0.012), "TriplanarArchitecture2's dirt takes it toward its dirt tint by the blend map's channels");
                expect(near3(bumped, 0.5+0.5*0.2588, 0.5, 0.5-0.5*0.9659, 0.012), "TriplanarArchitecture2's bumps are laid from three sides and halved where the surface faces away from the sun");
            }
            // HDRP Lit's detail map: its red 0.75 (half way to white) over a base of 0.2158: sqrt(0.2158) = 0.4645, a quarter of the
            // way to 1, squared: 0.358; its blue 0.75 raises a smoothness of 0.2 half way to 1 (roughness 0.4).
            {
                const Rgb with=boxAt(see(cardHandle, panelWorld, detail, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const Rgb without=boxAt(see(cardHandle, panelWorld, litPlain, TGPT_VIEW_ALBEDO, pcam), pcam, middle);
                const double root=std::sqrt(double(baseGrey)), want=std::pow(root+(1-root)*0.25, 2.0);
                const int rough=roughnessAt(cardHandle, panelWorld, detail, pcam), roughWithout=roughnessAt(cardHandle, panelWorld, litPlain, pcam);
                std::printf("      HDRP Lit's detail map: albedo %.4f (%.4f; without it %.4f), roughness %d of 255 (102; without it %d)\n", with.g, want, without.g, rough, roughWithout);
                expect(near3(with, want, want, want, 0.012) && near3(without, baseGrey, baseGrey, baseGrey, 0.01), "HDRP Lit's detail map lightens the base color as HDRP's Lit does");
                expect(std::abs(rough-102)<=2 && std::abs(roughWithout-204)<=2, "HDRP Lit's detail map moves the smoothness as HDRP's Lit does");
            }
            // The normal map by its scale. A map tilted half way toward +u, at a scale of 2: Unity takes z from the map (0.866) and
            // then doubles x: (1, 0, 0.866), 49.1 degrees off the surface's normal (scaled first, z was 0: 90 degrees). At a scale of
            // a half: HDRP Lit's (0.25, 0, 0.866), 16.1 degrees; Shader Graph's Normal Strength moves z half way to 1: 15.0 degrees.
            {
                auto tilt=[&](uint32_t material, const Camera& c, double* x=nullptr) {
                    const Rgb n=boxAt(see(cardHandle, panelWorld, material, TGPT_VIEW_NORMALS, c), c, middle);
                    const double nx=n.r*2-1, nz=n.b*2-1; if(x) *x=nx;
                    return std::atan2(std::fabs(nx), std::fabs(nz))*180.0/3.14159265358979;
                };
                const double two=tilt(litScaled, pcam), half=tilt(litHalf, pcam), graph=tilt(graphHalf, pcam);
                std::printf("      a normal map tilted 30 degrees, by its scale: %.1f degrees at 2 (49.1), %.1f at a half HDRP Lit's way (16.1), %.1f by Shader Graph's Normal Strength (15.0)\n", two, half, graph);
                expect(std::fabs(two-49.1)<1.0, "a normal map's scale is applied as Unity applies it (z from the map, then x and y scaled)");
                expect(std::fabs(half-16.1)<0.5 && std::fabs(graph-15.0)<0.5, "Shader Graph's Normal Strength is told from HDRP Lit's scale");
                // A two-sided panel seen from behind: its bumps keep their side (HDRP's Mirror mode), or turn with the surface (Flip).
                const Camera behind=make_camera({0, 1.02f, 5.f}, {0, 1.02f, 2.f}, 60.f, float(W)/float(H));
                double mirrorX=0, flipX=0, tiledX=0;
                tilt(mirrorBack, behind, &mirrorX); tilt(flipBack, behind, &flipX); tilt(mirroredTiling, pcam, &tiledX);
                std::printf("      seen from behind: its normal's x %.3f in HDRP's Mirror mode (0.500), %.3f in its Flip mode (-0.500); with a tiling of -1 across, from the front %.3f (0.500: the mesh's tangent stays)\n",
                            mirrorX, flipX, tiledX);
                expect(std::fabs(mirrorX-0.5)<0.02 && std::fabs(flipX+0.5)<0.02, "a back face keeps its bumps' side in HDRP's Mirror mode and turns them in its Flip mode");
                expect(std::fabs(tiledX-0.5)<0.02, "a negative tiling mirrors the map, not the tangent frame (the game's shaders take the mesh's tangent)");
            }
            // The emissive map itself (no tile of it exists): a glow of 4 times its red 0.25, and with the red-only flag the same
            // in every channel (the game's grass and leaves), where the map's own green and blue are 1.
            {
                const bool keepSunNow=sunOn; sunOn=false; const float keepSky=skyIntensity; skyIntensity=1e-6f;
                expect(SetLights(nullptr, 0)==0, "no lights for the glowing panel");
                const Rgb plainG=boxAt(see(cardHandle, panelWorld, glowPlain, TGPT_VIEW_NONE, pcam), pcam, middle);
                const Rgb mapped=boxAt(see(cardHandle, panelWorld, glowMapped, TGPT_VIEW_NONE, pcam), pcam, middle);
                const Rgb red=boxAt(see(cardHandle, panelWorld, glowRed, TGPT_VIEW_NONE, pcam), pcam, middle);
                std::printf("      a glowing panel as the camera sees it: (%.3f, %.3f, %.3f) without a map; with its emissive map (red 0.25, green and blue 1) (%.3f, %.3f, %.3f); by its red alone (%.3f, %.3f, %.3f)\n",
                            plainG.r, plainG.g, plainG.b, mapped.r, mapped.g, mapped.b, red.r, red.g, red.b);
                expect(plainG.g>0.01 && std::fabs(mapped.r-0.25*plainG.r)<0.03*plainG.r && std::fabs(mapped.g-plainG.g)<0.05*plainG.g, "the emissive map itself is read at a hit (no atlas tile of it)");
                expect(std::fabs(red.g-0.25*plainG.g)<0.03*plainG.g && std::fabs(red.b-0.25*plainG.b)<0.03*plainG.b, "the game's plants' glow is scaled by their emissive map's red alone");
                sunOn=keepSunNow; skyIntensity=keepSky;
            }
            ownRays=keepObjectsOwn; sunOn=keepSun; reflectionBoost=keepBoost;
            expect(SetMaterialPrograms(0, nullptr, 0)==0, "the material programs' records forgotten");
            expect(SetLights(&light, 1)==0, "one light again");
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
        // Its own motion (PT-59, pt_primary hit_motion): the camera's own ray meets the character, and its motion to the frame
        // before is where the same point of the same triangle was then, by the positions the skinning wrote a frame ago. The
        // drawn motion vectors know nothing of it (the box is only traced), so the yardstick counts its pixels as moving
        // otherwise than HDRP's: next to none while it stands, most of them in the frame it moved 10 cm in.
        {
            ownRays=true;
            const auto each=[&] { SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1); };
            pose(at);
            for(uint32_t i=0;i<4;++i) { each(); run(TGPT_VIEW_YARDSTICK, cam, cam, i ? 3400+i : 0, TGPT_OPTION_NO_DENOISER); }
            TGPT_Yardstick stood{sizeof(TGPT_Yardstick), TGPT_ABI}, went{sizeof(TGPT_Yardstick), TGPT_ABI};
            const bool haveStood=yardstickOf(3403, cam, TGPT_OPTION_NO_DENOISER, stood, each);
            pose(V3{at.x+0.1f, at.y, at.z});
            each(); run(TGPT_VIEW_YARDSTICK, cam, cam, 3420, TGPT_OPTION_NO_DENOISER);
            const bool haveWent=yardstickOf(3420, cam, TGPT_OPTION_NO_DENOISER, went, each);
            uint32_t body=0; for(int i=1;i<6;++i) body+=went.kinds[1][i];
            std::printf("      a character's own motion: %u of its %u pixels move otherwise than the drawn motion vectors in the frame it moved 10 cm in, %u of the screen's while it stood\n",
                went.motion, body, stood.motion);
            expect(haveStood && haveWent && body>1000, "the yardstick counts a character's pixels");
            expect(stood.motion<body/50, "a character standing still has no motion of its own");
            expect(went.motion>body/2 && went.motion<body*3/2, "a character's pixels move by its own previous positions (the skinning's of the frame before)");
            ownRays=false;
            pose(at);
        }
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
        // A character's eye, which HDRP draws forward with its eye shader, is found along the camera ray and shaded by the
        // tracer (TGPT_MATERIAL_EYE): the character's cube on the box HDRP draws forward, in place of the box in the traced
        // scene. With a plain material the same pixel is a forward surface the traced scene lacks (the head under far hair):
        // HDRP's shading stays (here the sun alone, which misses the face).
        {
            auto hidden=instances; hidden[5].flags|=TGPT_INSTANCE_HIDDEN;
            expect(SetScene(hidden.data(), uint32_t(hidden.size()), indices.data(), uint32_t(indices.size()))==0, "the box left out of the traced scene");
            for(uint32_t i=0;i<count;++i) verts[first+i]=SkinVertex{1.5f+cube[i].px*2.f, 1.f+cube[i].py*2.f, cube[i].pz*2.f, {0, 0}};
            context->UpdateSubresource(skinning.Get(), 0, nullptr, verts.data(), 0, 0);
            auto eyeMaterial=materials[0]; eyeMaterial.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_EYE; eyeMaterial.alphaCutoff=0.f;
            const uint32_t eyeMaterialIndex=uint32_t(materials.size())+3;
            expect(SetMaterials(eyeMaterialIndex, &eyeMaterial, 1)==0, "an eye material");
            auto face=[&](uint32_t material, uint32_t seed) {
                si.materials[0]=si.materials[1]=material;
                objects[5].forward=true;
                frames(true, seed); const auto px=frames(true, seed+100);
                objects[5].forward=false;
                return region(px, 700, 190, 810, 300);
            };
            const double plain=face(0, 3800), eyeFace=face(eyeMaterialIndex, 4000);
            // Ray Reconstruction's guides on the eye (pt_compose traced_guides, d3d11.hlsl traced_guides): a blue, wet eye
            // (smoothness 0.9) has its own color and roughness there, where HDRP's GBuffer holds nothing (it draws the eye forward)
            // and the guides pass gave it a grey albedo of 0.5 and the box's roughness of 0.6. Until 2026-10-06 (PT-01) Ray
            // Reconstruction took the iris for noise on a grey surface.
            {
                auto blue=eyeMaterial; blue.baseColor[0]=0.1f; blue.baseColor[1]=0.3f; blue.baseColor[2]=0.7f; blue.smoothness=0.9f;
                const uint32_t blueIndex=uint32_t(materials.size())+5;
                expect(SetMaterials(blueIndex, &blue, 1)==0, "a blue eye material");
                si.materials[0]=si.materials[1]=blueIndex;
                objects[5].forward=true; rayReconstruction=true;
                for(uint32_t i=0;i<8;++i) {
                    SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
                    run(TGPT_VIEW_NONE, cam, cam, i ? 4300+i : 0, TGPT_OPTION_NO_RADIANCE_CACHE);
                }
                objects[5].forward=false; rayReconstruction=false;
                auto getGuidesEye=reinterpret_cast<decltype(&TGPT_GetGuides)>(GetProcAddress(pt, "TGPT_GetGuides"));
                TGPT_Guides gd{sizeof(TGPT_Guides), TGPT_ABI};
                const bool have=getGuidesEye && getGuidesEye(&gd)==0 && gd.width==W && gd.height==H && gd.extrasFrameIndex==gd.frameIndex &&
                                gd.diffuseAlbedo && gd.specularAlbedo && gd.normalRoughness;
                expect(have, "Ray Reconstruction's guides and the composite's extras on the eye");
                if(have) {
                    const auto gDiffuse=readGuide(gd.diffuseAlbedo), gSpecular=readGuide(gd.specularAlbedo), gNormal=readGuide(gd.normalRoughness);
                    double diffuse[3]{}, specular=0, roughness=0; int count=0; bool unit=true;
                    for(int y=210;y<280;++y) for(int x=720;x<790;++x) {
                        const size_t k=(size_t(y)*W+x)*4; ++count;
                        for(int c=0;c<3;++c) diffuse[c]+=gDiffuse[k+c];
                        specular+=gSpecular[k+1]; roughness+=gNormal[k+3];
                        const double len=std::sqrt(double(gNormal[k])*gNormal[k]+double(gNormal[k+1])*gNormal[k+1]+double(gNormal[k+2])*gNormal[k+2]);
                        if(!(std::fabs(len-1.0)<0.05)) unit=false;
                    }
                    for(double& c:diffuse) c/=count;
                    specular/=count; roughness/=count;
                    std::printf("      Ray Reconstruction's guides on a blue wet eye (0.10 0.30 0.70, roughness 0.10): diffuse %.3f %.3f %.3f, specular %.3f, roughness %.3f\n",
                        diffuse[0], diffuse[1], diffuse[2], specular, roughness);
                    expect(std::fabs(diffuse[0]-0.1)<0.03 && std::fabs(diffuse[1]-0.3)<0.03 && std::fabs(diffuse[2]-0.7)<0.03,
                           "the diffuse albedo guide on an eye is its own color, not a grey");
                    expect(std::fabs(roughness-0.1)<0.03, "the roughness guide on an eye is its own wet finish");
                    expect(specular>0.01 && specular<0.2 && unit, "the specular albedo guide on an eye is a dielectric's, its normal guide a unit normal");
                }
                si.materials[0]=si.materials[1]=0;
            }
            // Hair HDRP cut away at a pixel it drew forward: a black lash card 4 cm in front of the eye (seen by camera rays
            // only here, so it shades nothing). The camera ray passes it (hair nearer than HDRP's depth is a card HDRP's alpha
            // test left out there) and finds the eye behind; stopping at it, the eye's pixels took the lash's black (R4).
            auto lash=materials[0]; lash.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_STRANDS; lash.alphaCutoff=0.f;
            lash.baseColor[0]=lash.baseColor[1]=lash.baseColor[2]=0.f; lash.baseColor[3]=1.f; lash.albedoTile=-1;
            const uint32_t lashIndex=uint32_t(materials.size())+4;
            expect(SetMaterials(lashIndex, &lash, 1)==0, "a lash material");
            double behindLash=0;
            {
                auto withLash=hidden; auto lashIndices=indices;
                const M4 lw=mul(translate_scale({1.5f, 1.f, -1.04f}, {2.f, 2.f, 1.f}), rot_x(-pi/2));
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=lw.m[r][c];
                q.mesh=quadHandle; q.materialBase=uint32_t(lashIndices.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
                lashIndices.push_back(lashIndex); withLash.push_back(q);
                expect(SetScene(withLash.data(), uint32_t(withLash.size()), lashIndices.data(), uint32_t(lashIndices.size()))==0, "a lash card in front of the eye");
                behindLash=face(eyeMaterialIndex, 4200);
            }
            // A face's own small shadows (PT-43, PT-44): a lid 1.5 cm tall standing across the eye (the cube's top face, the sun
            // up and behind the lid). From ray_offset's 2 cm out the eye's rays began above the lid, and the game's eye, dark under
            // its lid, was lit edge to edge traced. From just above the eye's own triangle (onMesh) the lid shadows a band about
            // 0.8 cm deep in front of it; turned away from the eye (its back face to the eye's rays, as a lid's inside is) it does
            // too. So does a character's skin HDRP draws into its GBuffer (a solid profile), on the triangle the camera ray finds.
            {
                const Camera close=make_camera({1.5f, 2.26f, -0.26f}, {1.5f, 2.f, -0.02f}, 60.f, float(W)/float(H));
                auto box=[&](const std::vector<float>& px, V3 a, V3 b) {
                    int xs[2], ys[2]; const V3 corner[2]={a, b};
                    for(int k=0;k<2;++k) {
                        const V3 r=corner[k]-close.position; const auto& m=close.viewProj.m;
                        const float cx=m[0][0]*r.x+m[0][1]*r.y+m[0][2]*r.z+m[0][3], cy=m[1][0]*r.x+m[1][1]*r.y+m[1][2]*r.z+m[1][3];
                        const float cw=m[3][0]*r.x+m[3][1]*r.y+m[3][2]*r.z+m[3][3];
                        xs[k]=int((cx/cw*0.5f+0.5f)*float(W)); ys[k]=int((0.5f-0.5f*cy/cw)*float(H));
                    }
                    return region(px, std::max(std::min(xs[0], xs[1]), 0), std::max(std::min(ys[0], ys[1]), 0),
                                  std::min(std::max(xs[0], xs[1])+1, int(W)), std::min(std::max(ys[0], ys[1])+1, int(H)));
                };
                // lid: 0 none, 1 facing the eye, 2 facing away from it. Direct light, averaged over 16 frames.
                auto underLid=[&](uint32_t material, bool skinDrawn, int lid, uint32_t seed, double& nearLid, double& open) {
                    auto scene=hidden; auto sceneIndices=indices;
                    if(lid) {
                        const M4 lw=mul(translate_scale({1.5f, 2.0075f, 0.f}, {1.8f, 0.015f, 1.f}), rot_x(lid==1 ? -pi/2 : pi/2));
                        TGPT_Instance q{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=lw.m[r][c];
                        q.mesh=quadHandle; q.materialBase=uint32_t(sceneIndices.size()); q.mask=0xFF; q.flags=0;
                        sceneIndices.push_back(0); scene.push_back(q);
                    }
                    expect(SetScene(scene.data(), uint32_t(scene.size()), sceneIndices.data(), uint32_t(sceneIndices.size()))==0, "a lid across the eye");
                    si.materials[0]=si.materials[1]=material;
                    const auto keep=objects[5];
                    if(skinDrawn) { objects[5].feature=3; objects[5].gb2g=51; objects[5].gb2b=127; thickProfiles=1u<<7; }
                    else objects[5].forward=true;
                    std::vector<float> mean(size_t(W)*H*3, 0.f);
                    for(uint32_t i=0;i<24;++i) {
                        SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
                        run(TGPT_VIEW_DIRECT, close, close, i ? seed+i : 0, TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE);
                        if(i>=8) { const auto r=readback(); for(size_t j=0;j<mean.size();++j) mean[j]+=r[j]/16.f; }
                    }
                    objects[5]=keep; thickProfiles=0;
                    if(lid==1 && !skinDrawn) save(mean, "33-eye-under-lid-direct.png", true);
                    nearLid=box(mean, {1.f, 2.f, -0.006f}, {2.f, 2.f, -0.002f});
                    open=box(mean, {1.f, 2.f, -0.08f}, {2.f, 2.f, -0.04f});
                };
                double eyeBare=0, eyeBareOpen=0, eyeLid=0, eyeLidOpen=0, eyeInside=0, eyeInsideOpen=0, skinLid=0, skinLidOpen=0;
                underLid(eyeMaterialIndex, false, 0, 6000, eyeBare, eyeBareOpen);
                underLid(eyeMaterialIndex, false, 1, 6100, eyeLid, eyeLidOpen);
                underLid(eyeMaterialIndex, false, 2, 6200, eyeInside, eyeInsideOpen);
                underLid(0, true, 1, 6300, skinLid, skinLidOpen);
                std::printf("      direct light 2 to 6 mm in front of a lid 1.5 cm tall, over that 4 to 8 cm away: eye without a lid %.4f / %.4f, "
                            "under the lid %.4f / %.4f, under its back face %.4f / %.4f; skin under the lid %.4f / %.4f\n",
                            eyeBare, eyeBareOpen, eyeLid, eyeLidOpen, eyeInside, eyeInsideOpen, skinLid, skinLidOpen);
                expect(eyeBareOpen>0.01 && eyeBare>0.8*eyeBareOpen, "the eye is sunlit up to the lid's place without a lid");
                expect(eyeLidOpen>0.01 && eyeLid<0.35*eyeLidOpen, "a lid 1.5 cm tall shadows the eye in front of it (its rays leave from just above the eye)");
                expect(eyeInsideOpen>0.01 && eyeInside<0.35*eyeInsideOpen, "a lid's back face shadows the eye too (no back face passed from the traced mesh)");
                expect(skinLidOpen>0.01 && skinLid<0.35*skinLidOpen, "a character's skin drawn into the GBuffer is shadowed by a lid 1.5 cm tall (its rays leave from its traced triangle)");
                // Skin's own ambient occlusion (PT-43): HDRP writes the material's into GBuffer3 behind its tag (the game's skin
                // shader darkens its mouth, nostrils and lips so), and it is taken off the skin's bounced and sky light alone, as
                // for a leaf. On the cube's front face, out of the sun: the light at occlusion 0 is what stays (its direct light),
                // and at 0.25 a quarter of the rest is added to it.
                auto skinFront=[&](float ao, uint32_t seed) {
                    expect(SetScene(hidden.data(), uint32_t(hidden.size()), indices.data(), uint32_t(indices.size()))==0, "the box left out of the traced scene");
                    si.materials[0]=si.materials[1]=0;
                    const auto keep=objects[5];
                    objects[5].feature=3; objects[5].gb2g=51; objects[5].gb2b=127; objects[5].ao=ao; thickProfiles=1u<<7;
                    std::vector<float> mean(size_t(W)*H*3, 0.f);
                    for(uint32_t i=0;i<32;++i) {
                        SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
                        run(TGPT_VIEW_NONE, cam, cam, i ? seed+i : 0, TGPT_OPTION_NO_DENOISER);
                        if(i>=16) { const auto r=readback(); for(size_t j=0;j<mean.size();++j) mean[j]+=r[j]/16.f; }
                    }
                    objects[5]=keep; thickProfiles=0;
                    return region(mean, 700, 190, 810, 300);
                };
                const double aoOne=skinFront(1.f, 6400), aoQuarter=skinFront(0.25f, 6500), aoNone=skinFront(0.f, 6600);
                const double share=(aoQuarter-aoNone)/std::max(aoOne-aoNone, 1e-9);
                std::printf("      skin out of the sun at its material's occlusion 1: %.4f, 0.25: %.4f, 0: %.4f (the bounced and sky light kept: %.2f of it, 0.25 expected)\n",
                            aoOne, aoQuarter, aoNone, share);
                expect(aoOne>aoNone*1.5 && share>0.12 && share<0.4, "skin's own ambient occlusion darkens its bounced and sky light");
            }
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
            si.materials[0]=si.materials[1]=0;
            // Since the glass and eyes migration (2026-10-07) nothing HDRP draws forward keeps HDRP's shading while tracing:
            // what the camera's ray finds there is lit by the tracer, a plain material as an eye is (the owner's ruling "not
            // to keep shading of anything in HDRP when PT is on"). Until then the plain face kept HDRP's (dark here: the sun
            // misses it), which this check still asked for (plain below 0.005) and so failed on the merged build (0.0806).
            std::printf("      a character's forward face: a plain material %.4f (traced), an eye %.4f (traced), behind a lash card 4 cm in front %.4f\n", plain, eyeFace, behindLash);
            expect(plain>0.02 && eyeFace>0.02, "a character's face drawn forward is shaded by the tracer, not left to HDRP: an eye and a plain material alike");
            expect(behindLash>0.8*eyeFace, "hair well in front of what HDRP drew forward is passed: the eye behind a lash card is found");
        }
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
        hair[12].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_ALPHA_TILE|TGPT_MATERIAL_STRANDS;
        hair[13]=hair[12]; hair[13].flags&=~(TGPT_MATERIAL_STRANDS); hair[13].alphaCutoff=0.5f;
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
            // Twice, as when the composite showed a frame's traced output with the next frame (until 2026-10-08, PT-84).
            run(TGPT_VIEW_SURFACES, cam, cam, 0); run(TGPT_VIEW_SURFACES, cam, cam, 1);
            const auto v=readback();
            objects[0]=keepFloor; thickProfiles=0;
            for(size_t j=0;j+2<v.size();j+=3) if(v[j]>1.5f*v[j+1] && v[j+1]>2.f*v[j+2] && v[j]>0.02f) skinPixels.push_back(j);
        }
        auto floorSpecular=[&](bool skin, float boost, float metalF0=-1.f) {
            const auto keepFloor=objects[0];
            if(skin) { objects[0].feature=3; objects[0].gb2g=51; objects[0].gb2b=127; thickProfiles=1u<<7; }
            objects[0].f0=metalF0;
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
        // (What the floor reflects takes the boost itself, as the camera sees the walls with it, PT-46: a few percent.)
        expect(skin0>0 && std::fabs(skin1/skin0-1.0)<0.04, "Reflection Boost leaves skin's reflections as the game made them");
        std::printf("      reflected light summed over the floor, Reflection Boost at 1 against 0: as it is x%.2f\n", plain0>0 ? plain1/plain0 : 0.0);
        // The boost's mirror share goes to metal (the owner: "we want metal to reflect more than stone and brick"), read where
        // the rule is applied: the specular albedo Ray Reconstruction is handed at a floor pixel (d3d11.hlsl's guides, the
        // same rule as pt_prepare's). The floor as a grey metal (a specular color of 0.5) and as a non-metal, boost 0 and 1.
        // (Summing the reflected light over the floor measured the sun's highlight, which a smoother floor concentrates.)
        auto getGuidesForBoost=reinterpret_cast<decltype(&TGPT_GetGuides)>(GetProcAddress(pt, "TGPT_GetGuides"));
        const size_t at=skinPixels.empty() ? 0 : skinPixels[skinPixels.size()/2]/3;
        const UINT gx=UINT(at%W), gy=UINT(at/W);
        auto guideSpecular=[&](float boost, float metalF0) {
            const auto keepFloor=objects[0];
            objects[0].f0=metalF0; reflectionBoost=boost; rayReconstruction=true;
            for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 5500+i : 0, TGPT_OPTION_NO_RADIANCE_CACHE);
            rayReconstruction=false; reflectionBoost=0; objects[0]=keepFloor;
            TGPT_Guides gd{sizeof(TGPT_Guides), TGPT_ABI};
            if(!getGuidesForBoost || getGuidesForBoost(&gd)!=0 || !gd.specularAlbedo) return -1.0;
            auto* t=reinterpret_cast<ID3D11Texture2D*>(uintptr_t(gd.specularAlbedo));
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
            ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
            context->CopyResource(staging.Get(), t);
            D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
            const uint8_t* row=static_cast<const uint8_t*>(m.pData)+size_t(gy)*m.RowPitch;
            const double v=(row[gx*4]+row[gx*4+1]+row[gx*4+2])/(3.0*255.0);
            context->Unmap(staging.Get(), 0);
            return v;
        };
        const double stone0=guideSpecular(0.f, -1.f), stone1=guideSpecular(1.f, -1.f), metal0=guideSpecular(0.f, 0.5f), metal1=guideSpecular(1.f, 0.5f);
        std::printf("      specular albedo at a floor pixel (%u, %u), Reflection Boost 0 and 1: a grey metal %.3f -> %.3f, the floor as a non-metal %.3f -> %.3f\n",
            gx, gy, metal0, metal1, stone0, stone1);
        expect(stone0>0 && metal0>3.0*stone0, "a metal floor reflects far more than a non-metal one to begin with");
        expect(metal1>metal0+0.15 && (metal1-metal0)>4.0*(stone1-stone0) && stone1<stone0+0.06,
               "Reflection Boost makes a metal mirror more, and more than stone");    }
    // What characters' acceleration structures cost the GPU, for the record: 128 renderers (twice the 64 the tracer took
    // until PT-50, and 540,800 vertices, past the old 524,288 a frame; a tavern's 55 had some 600k triangles), 8192
    // triangles each, posed anew every frame and rebuilt from their vertices, against none. They stand under the floor, where
    // no ray goes, but for the last, 3 m in front of the camera: it must be traced (the cap would have left it out). What they
    // add is their builds and the top level.
    // The GPU is shared with other work, so each figure is the median of many frames, the two cases taken in turn.
    {
        struct SkinVertex { float x, y, z; uint32_t packed[2]; };
        const uint32_t rings=64, segments=64, verticesEach=(rings+1)*(segments+1), meshCount=128;
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
                if(m+1==meshCount) { v.x=wobble*std::sin(th)*std::cos(ph); v.y=2.93f+wobble*std::cos(th); v.z=-6.f+wobble*std::sin(th)*std::sin(ph); }
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
        expect(people.size()==meshCount, "128 characters registered");
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
        for(uint32_t i=0;i<4;++i) frameOf(true, frame++);
        ReadStatus(&status);
        const uint32_t sceneInstances=status.instances;
        const bool lastTraced=status.pickFound==1 && status.pickInstance>=sceneInstances && status.pickDistance>2.f && status.pickDistance<3.5f;
        std::printf("      the 128th character, 3 m in front of the camera: %s, instance %u (the scene's %u), %.2f m\n", status.pickFound ? "found" : "nothing",
                    status.pickInstance, sceneInstances, status.pickDistance);
        expect(lastTraced, "the 128th character is traced (PT-50: no cap of 64 renderers or 524,288 vertices)");
        SetSkinned(0, 0, nullptr, 0);
        ReadStatus(&status);
        // The frames least slowed by other work on the GPU tell most: the tenth quantile, and the median.
        auto quantile=[](std::vector<double> v, double q) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[size_t(double(v.size()-1)*q)]; };
        std::printf("      characters' acceleration structures: 128 of 8192 triangles (1M), posed every frame: GPU %.2f ms a frame with them, "
                    "%.2f ms without (tenth quantile of %zu frames each): %.2f ms for them (%.2f by the medians, %.2f by the fastest frames)\n",
                    quantile(with, 0.1), quantile(without, 0.1), with.size(), quantile(with, 0.1)-quantile(without, 0.1),
                    quantile(with, 0.5)-quantile(without, 0.5), quantile(with, 0.0)-quantile(without, 0.0));
        expect(status.framesFailed==0, "frames with 128 characters trace");
    }
    // Grass and leaves as HDRP shades them (PT-03, docs/PT-VEGETATION.md): HDRP Lit's transmission features through a thin
    // diffusion profile, drawn as quads, every variant traced with the same frame indices, undenoised, without the radiance
    // cache. Profile 3 is what a drawn transmission quad carries unless a test sets GBuffer2's blue byte (56: the profile 3
    // with a mask of 8/15).
    {
        sunOn=true; skyIntensity=1; reflectionBoost=0; rays=1; thickProfiles=0;
        expect(SetLights(&light, 1)==0, "the room's lamp");
        expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "the room's materials");
        expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room");
        const uint32_t leafOptions=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE;
        auto lum3=[](const std::vector<double>& v, size_t j) { return 0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2]; };
        auto accumulate=[&](uint32_t view, const Camera& c, uint32_t first, uint32_t frames, uint32_t more=0u) {
            std::vector<double> acc(size_t(W)*H*3, 0.0);
            for(uint32_t i=0;i<frames+8;++i) {
                run(view, c, c, i ? first+i : 0, leafOptions|more);
                if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]/double(frames); }
            }
            return acc;
        };
        // The pixels of the object last pushed onto the drawn scene, from the Surfaces view (transmission is green there).
        auto leafPixels=[&](const Camera& c) {
            run(TGPT_VIEW_SURFACES, c, c, 0); run(TGPT_VIEW_SURFACES, c, c, 1);
            const auto v=readback(); std::vector<size_t> px;
            for(size_t j=0;j+2<v.size();j+=3) if(v[j+1]>0.05f && v[j+1]>3.f*v[j] && v[j+1]>3.f*v[j+2]) px.push_back(j);
            return px;
        };
        auto mean=[&](const std::vector<double>& a, const std::vector<size_t>& px, int channel) {
            double s=0; for(size_t j:px) s+=channel<0 ? lum3(a, j) : a[j+size_t(channel)]; return px.empty() ? 0.0 : s/double(px.size());
        };
        const M4 facing=mul(translate_scale({-3.5f, 4.3f, 4.5f}, {1.5f, 1, 1.5f}), rot_x(-pi/2)); // faces the camera, the sun behind it
        // What a leaf of HDRP's foliage profile lets through from behind, as the game sets the profile (DiffusionProfile_Foliage: a
        // tint of 5.26, 5.99, 1.22, 0 to 24 mm, Burley's shape 1 / (scattering distance x multiplier) per mm) for a material 0.2
        // of the remap thick (4.8 mm) with a full mask: tint / 4 x (e^-St + 3 e^-St/3), the share the plugin works out for such
        // a material (TGPT_Material.subsurface, HdrpPasses.ReadSubsurface).
        const double tint[3]={5.2573, 5.9922, 1.2154}, shape[3]={1.0/(0.7115*10.6806), 1.0/(0.749*10.6806), 1.0/(0.5345*10.6806)};
        double foliage[3];
        for(int c=0;c<3;++c) { const double st=shape[c]*24.0*(51.0/255.0); foliage[c]=tint[c]/4.0*(std::exp(-st)+3.0*std::exp(-st/3.0)); }
        // A leaf as the tracer holds it, where its own camera ray meets it (PT-59; what a leaf lets through is its material's to
        // say, HDRP's G-buffer is not asked: PT-58): a quad of a two-sided material (slot 74) added to the room, opaque (kind 0),
        // a leaf whose material gives no share (1: the grey half) or a leaf with the foliage's share (2). The frames after it
        // are traced with the tracer's own camera rays, until noLeaf.
        auto tracedLeaf=[&](const M4& w, int kind, const Camera& c) {
            TGPT_Material m=materials[0]; m.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_DOUBLE_SIDED|(kind ? TGPT_MATERIAL_TRANSLUCENT : 0u); m.alphaCutoff=0.f;
            for(int k=0;k<3;++k) m.subsurface[k]=kind==2 ? float(foliage[k]) : 0.f;
            expect(SetMaterials(74, &m, 1)==0, "the leaf's material");
            auto l=instances; auto li=indices;
            TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
            q.mesh=quadHandle; q.materialBase=uint32_t(li.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST; li.push_back(74u); l.push_back(q);
            expect(SetScene(l.data(), uint32_t(l.size()), li.data(), uint32_t(li.size()))==0, "the room with the leaf");
            ownRays=true;
            for(uint32_t i=0;i<10;++i) { run(TGPT_VIEW_NONE, c, c, 8990+i, leafOptions); context->Flush(); Sleep(20); }
        };
        auto noLeaf=[&]() {
            ownRays=false;
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room without the leaf");
        };

        // 1. A blade's mirror reflectance is its diffusion profile's (TGPT_SetProfileReflectance), as HDRP's: the game's foliage
        // mirrors 0.0023 (an index of refraction of 1.1) where a dielectric's 0.04 made wet grass glint white. A wet blade
        // (roughness 0.15) facing the camera, its reflections (the room and its lamp in it) in the Specular view, with the
        // profile's reflectance and without; and the specular albedo Ray Reconstruction's guide takes there.
        {
            Object leaf{0, facing, {0}, false, false, 2}; leaf.roughness=0.15f;
            objects.push_back(leaf);
            const auto px=leafPixels(cam);
            float f0s[16]{}; f0s[3]=0.0023f;
            auto getGuidesLeaf=reinterpret_cast<decltype(&TGPT_GetGuides)>(GetProcAddress(pt, "TGPT_GetGuides"));
            auto guide=[&](size_t j) {
                rayReconstruction=true;
                for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 9050+i : 0, TGPT_OPTION_NO_RADIANCE_CACHE);
                rayReconstruction=false;
                TGPT_Guides gd{sizeof(TGPT_Guides), TGPT_ABI};
                if(!getGuidesLeaf || getGuidesLeaf(&gd)!=0 || !gd.specularAlbedo) return -1.0;
                auto* t=reinterpret_cast<ID3D11Texture2D*>(uintptr_t(gd.specularAlbedo));
                D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
                ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&d, nullptr, &staging);
                context->CopyResource(staging.Get(), t);
                D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
                const size_t at=j/3; const UINT gx=UINT(at%W), gy=UINT(at/W);
                const uint8_t* row=static_cast<const uint8_t*>(m.pData)+size_t(gy)*m.RowPitch;
                const double v=(row[gx*4]+row[gx*4+1]+row[gx*4+2])/(3.0*255.0);
                context->Unmap(staging.Get(), 0);
                return v;
            };
            const size_t middle=px.empty() ? 0 : px[px.size()/2];
            auto reflected=[&](bool profile, double& guided) {
                SetProfileReflectance(f0s, profile ? 16u : 0u);
                const auto a=accumulate(TGPT_VIEW_SPECULAR, cam, 9000, 24);
                guided=guide(middle);
                return mean(a, px, -1);
            };
            double guideDielectric=0, guideLeaf=0;
            const double dielectric=reflected(false, guideDielectric), leafF0=reflected(true, guideLeaf);
            SetProfileReflectance(nullptr, 0);
            objects.pop_back();
            std::printf("      a wet blade (%zu pixels): its reflections with its profile's reflectance 0.0023 %.2f of a dielectric's 0.04 (%.5f against %.5f); "
                        "Ray Reconstruction's specular albedo there %.3f against %.3f\n", px.size(), dielectric>0 ? leafF0/dielectric : 0.0, leafF0, dielectric,
                        guideLeaf, guideDielectric);
            expect(px.size()>2000 && dielectric>0 && leafF0>0 && leafF0<0.2*dielectric, "a blade of grass mirrors what its diffusion profile says (0.0023), not 0.04");
            expect(guideDielectric>0 && guideLeaf>=0 && guideLeaf<0.5*guideDielectric, "Ray Reconstruction's specular guide takes a blade's reflectance from its profile");
        }
        // 2. Light through a blade from behind, by its material's own share: its profile's transmittance at its thickness, tinted
        // (pt_common leaf_transmittance, thin_through), where the tracer lets a grey half through. The sun behind the quad: the
        // light it lets through (the Direct view, less the same quad opaque), with the foliage's share against without one.
        {
            auto direct=[&](int kind) {
                tracedLeaf(facing, kind, cam);
                const auto a=accumulate(TGPT_VIEW_DIRECT, cam, 9100, 24);
                noLeaf();
                return a;
            };
            Object probe{0, facing, {0}, false, false, 2}; objects.push_back(probe); const auto px=leafPixels(cam); objects.pop_back();
            const auto opaque=direct(0), grey=direct(1), tinted=direct(2);
            // With its own share, the cosine from behind is HDRP's wrapped one (leaf_cos); without, the plain one. The quad faces -z.
            const double wrapped=(0.258819+double(sunDir.z))/(1.258819*1.258819)/double(sunDir.z);
            double ratio[3]{};
            for(int c=0;c<3;++c) { const double g=mean(grey, px, c)-mean(opaque, px, c); ratio[c]=g>0 ? (mean(tinted, px, c)-mean(opaque, px, c))/g : 0.0; }
            std::printf("      a blade lit from behind by the sun (%zu pixels): the light through it with the foliage's share against the grey half "
                        "%.2f %.2f %.2f (%.2f %.2f %.2f expected)\n", px.size(), ratio[0], ratio[1], ratio[2], foliage[0]/0.5*wrapped, foliage[1]/0.5*wrapped,
                        foliage[2]/0.5*wrapped);
            bool tinted3=px.size()>2000;
            for(int c=0;c<3;++c) tinted3=tinted3 && std::fabs(ratio[c]/(foliage[c]/0.5*wrapped)-1.0)<0.12;
            expect(tinted3, "a blade lit from behind takes the light its material's own share lets through, tinted as HDRP's");
            // And HDRP's wrapped cosine (Lit's thin-object transmission: 15 degrees past the terminator): a blade nearly edge-on to
            // the sun, its front 0.1 towards it, takes that 0.1 and, through it, its transmittance times (w - 0.1) / (1 + w)^2,
            // where the plain cosine from behind gave it nothing. The sun alone (the lamp off), with the profile against without.
            {
                // The quad's plane turned about x until its own normal, (0, cos a, sin a), is 0.1 towards the sun; scaled alike in
                // every direction, so the drawn normal (the quad's +y by its matrix) is the traced plane's.
                const double sunPhi=std::atan2(double(sunDir.z), double(sunDir.y)), sunR=std::hypot(double(sunDir.y), double(sunDir.z));
                const M4 edgeOn=mul(translate_scale({-3.5f, 4.3f, 4.5f}, {1.5f, 1.5f, 1.5f}), rot_x(float(sunPhi-std::acos(0.1/sunR))));
                const V3 n=normalize(V3{edgeOn.m[0][1], edgeOn.m[1][1], edgeOn.m[2][1]}); // the drawn normal: the quad's +y by its matrix
                const double c0=double(n.x*sunDir.x+n.y*sunDir.y+n.z*sunDir.z);
                expect(SetLights(nullptr, 0)==0, "the sun alone");
                auto sunOn1=[&](bool share) {
                    tracedLeaf(edgeOn, share ? 2 : 1, cam);
                    const auto a=accumulate(TGPT_VIEW_DIRECT, cam, 9150, 24);
                    noLeaf();
                    return a;
                };
                Object edgeProbe{0, edgeOn, {0}, false, false, 2}; objects.push_back(edgeProbe); const auto edgePx=leafPixels(cam); objects.pop_back();
                const auto plain=sunOn1(false), hdrp=sunOn1(true);
                expect(SetLights(&light, 1)==0, "the room's lamp again");
                double gain[3]{}, expected[3]{};
                bool wrapOk=edgePx.size()>1000 && c0>0.05 && c0<0.2;
                for(int c=0;c<3;++c) {
                    gain[c]=mean(plain, edgePx, c)>0 ? mean(hdrp, edgePx, c)/mean(plain, edgePx, c) : 0.0;
                    expected[c]=1.0+foliage[c]*(0.258819-c0)/(1.258819*1.258819)/c0;
                    wrapOk=wrapOk && std::fabs(gain[c]/expected[c]-1.0)<0.15;
                }
                std::printf("      a blade edge-on to the sun (%zu pixels, its front %.3f towards it): its light with HDRP's wrapped transmission "
                            "%.2f %.2f %.2f of the front's alone (%.2f %.2f %.2f expected)\n", edgePx.size(), c0, gain[0], gain[1], gain[2],
                            expected[0], expected[1], expected[2]);
                expect(wrapOk, "a leaf's light from behind is wrapped past the terminator as HDRP wraps it");
            }
        }
        // 2b. A candle's light at its flame (lights round 2, TGPT_LIGHT_AT_EMITTER). The tavern's candles have HDRP's light in the
        // wax 10 cm under the flame; traced there at the flame's 1.2 cm, it lit the whole wax from inside (the wax is drawn with
        // transmission through a thin profile, lit from behind), its rays passing the wax's own far side as a surface's rays pass
        // back faces within 15 cm (near_back) and nothing within 5 cm of a light: white-hot. A wax column 5 x 12 x 5 cm drawn with
        // transmission, the sun and the room's lamp off, the Direct view (E / pi on a white surface, times the exposure),
        // undenoised, without reuse or clamps (with them the direct light is held to 48 times the reference: printed).
        //   The top: a 5 cm square of wax 3 cm under the flame takes what physics gives it, I times the solid angle it fills over
        //   its area (a rectangle under a point: 4 asin(a^2 / (a^2 + 4 h^2))), no more.
        //   The sides: lit from the flame 3 cm over the top only through the top, which blocks the flame's rays once the light
        //   stands at its flame (TGPT_LIGHT_AT_EMITTER: tested to 1 cm past its size) and a surface lit from behind passes
        //   back faces within 1 cm only (near_back_towards): below the top 2 cm a sixth of the light without the flag (measured
        //   0.155; each rule undone, 1). Without the flag (5 cm round the light) they glow as a thin
        //   leaf would, 0.5 x cos / d^2 (summed over the face by hand); with the light in the wax, as in the game, far more.
        {
            sunOn=false;
            const uint32_t still=TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_CLAMPS;
            const V3 centre{-2.f, 1.5f, -2.f};
            const float side=0.05f, tall=0.12f, above=0.03f, intensity=2.f, flameRadius=0.008f;
            auto candleAt=[&](V3 p, float radius, uint32_t flags) {
                TGPT_Light c{}; c.position[0]=p.x; c.position[1]=p.y; c.position[2]=p.z; c.type=0;
                c.color[0]=c.color[1]=c.color[2]=intensity; c.range=2.f; c.radius=radius; c.id=410; c.flags=flags;
                expect(SetLights(&c, 1)==0, "a candle's light set");
            };
            const double toView=double(exposure)/3.14159265358979;
            // The top: a quad 5 cm square facing up, the light 3 cm over its centre, seen from 40 cm above.
            double topMean=0, topExpected=0, topClamped=0; size_t topPixels=0;
            {
                Object cap{0, translate_scale(centre, {side, 1, side}), {0}, false, false, 2};
                objects.push_back(cap);
                const Camera above40=make_camera({centre.x, centre.y+0.4f, centre.z-0.03f}, centre, 60.f, float(W)/float(H));
                const auto px=leafPixels(above40);
                candleAt({centre.x, centre.y+above, centre.z}, flameRadius, TGPT_LIGHT_AT_EMITTER);
                const auto a=accumulate(TGPT_VIEW_DIRECT, above40, 9700, 24, still);
                topClamped=mean(accumulate(TGPT_VIEW_DIRECT, above40, 9700, 24, still&~TGPT_OPTION_NO_CLAMPS), px, -1);
                objects.pop_back();
                const double aa=double(side)*side, hh=double(above)*above, omega=4.0*std::asin(aa/(aa+4.0*hh));
                topMean=mean(a, px, -1); topExpected=toView*intensity*omega/aa; topPixels=px.size();
            }
            // The sides: the column, its -z face seen straight on from 35 cm, below its top.
            const float topY=centre.y+0.5f*tall;
            Object column{1, translate_scale(centre, {side, tall, side}), {0, 0}, false, false, 2};
            objects.push_back(column);
            const Camera front=make_camera({centre.x, centre.y, centre.z-0.35f}, centre, 60.f, float(W)/float(H));
            // Drawn into the GBuffer and in the traced scene too (a closed box, its material opaque and not alpha-tested, as the
            // game's MI_Candle_01).
            {
                auto list=instances; auto listIndices=indices;
                const M4 w=translate_scale(centre, {side, tall, side});
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
                q.mesh=cubeHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=0;
                listIndices.push_back(0); listIndices.push_back(0); list.push_back(q);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a wax column");
                for(uint32_t i=0;i<10;++i) { run(TGPT_VIEW_NONE, front, front, 9740+i, leafOptions); context->Flush(); Sleep(20); }
            }
            const auto px=leafPixels(front);
            // Its pixels below the top 2 cm of the face (its rows, straight on; the top is the end the flame lights brighter without
            // the flag): there a ray to the flame crosses more than a thin thing's centimetre of wax. In the top band it crosses
            // less (near_back_towards), as the top of a real candle glows.
            const V3 flame{centre.x, topY+above, centre.z};
            candleAt(flame, flameRadius, 0u);
            const auto passedImage=accumulate(TGPT_VIEW_DIRECT, front, 9800, 24, still);
            std::vector<size_t> lower;
            {
                size_t r0=SIZE_MAX, r1=0;
                for(size_t j:px) { const size_t r=(j/3)/W; r0=std::min(r0, r); r1=std::max(r1, r); }
                const size_t band=(r1-r0+1)*2/12;
                std::vector<size_t> first, last;
                for(size_t j:px) { const size_t r=(j/3)/W; if(r<r0+band) first.push_back(j); if(r>r1-band) last.push_back(j); }
                const bool topFirst=mean(passedImage, first, -1)>mean(passedImage, last, -1);
                for(size_t j:px) { const size_t r=(j/3)/W; if(topFirst ? r>=r0+band : r<=r1-band) lower.push_back(j); }
            }
            auto sideOf=[&](V3 p, float radius, uint32_t flags, uint32_t first) {
                candleAt(p, radius, flags);
                const auto a=accumulate(TGPT_VIEW_DIRECT, front, first, 24, still);
                return std::make_pair(mean(a, px, -1), mean(a, lower, -1));
            };
            const auto atFlameBoth=sideOf(flame, flameRadius, TGPT_LIGHT_AT_EMITTER, 9750);
            const double atFlame=atFlameBoth.first, atFlameLower=atFlameBoth.second, passedThrough=mean(passedImage, px, -1), passedLower=mean(passedImage, lower, -1),
                inWax=sideOf(centre, flameRadius, 0u, 9850).first, inWaxHdrpRadius=sideOf(centre, 0.3f, 0u, 9900).first;
            objects.pop_back();
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
            // The face lit through from behind as a thin leaf is, the light within the 5 cm pass-through: 0.5 x (z / d) / d^2.
            double leak=0; const int n=200;
            for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
                const double x=-0.5*side+(i+0.5)*side/n, y=-0.5*tall+(j+0.5)*tall/n, dy=0.5*tall+above-y, z=0.5*side, d2=x*x+dy*dy+z*z;
                leak+=0.5*(z/std::sqrt(d2))/d2/(double(n)*n);
            }
            leak*=toView*intensity;
            sunOn=true;
            expect(SetLights(&light, 1)==0, "the room's lamp again");
            std::printf("      a candle's light 3 cm over wax: its top (%zu pixels) %.4f against %.4f by hand (%.3f), %.4f with the clamps; its side (%zu pixels) "
                        "%.5f at the flame (%.5f below its top 2 cm, %zu pixels), %.4f without the flag (%.4f by hand; %.4f below the top 2 cm), %.4f from inside the "
                        "wax, %.4f from inside at HDRP's 0.3 m\n", topPixels, topMean, topExpected, topExpected>0 ? topMean/topExpected : 0.0, topClamped, px.size(),
                        atFlame, atFlameLower, lower.size(), passedThrough, leak, passedLower, inWax, inWaxHdrpRadius);
            expect(topPixels>2000 && std::fabs(topMean/topExpected-1.0)<0.05, "the wax 3 cm under a candle's flame takes the light physics gives it, no more");
            expect(px.size()>2000 && passedThrough>0 && std::fabs(passedThrough/leak-1.0)<0.1, "without the flag, 5 cm round a light let its flame light a wax column's side as a leaf");
            expect(lower.size()>15000 && atFlameLower<0.3*passedLower && atFlame<0.6*passedThrough && atFlame<0.1*inWax,
                   "a candle's light at its flame no longer lights its own wax from inside: the wax's top shadows its sides");
        }
        // 2c. Light inside subsurface materials (PT-40, PT-43; pt_walk.hlsl, pt_common subsurface_walk), where the tracer's own
        // camera rays meet them (PT-59). Wax-like things (drawn too, with HDRP's subsurface scattering and transmission through a
        // thin profile as the game's candles, red in the Surfaces view: that only tells which pixels to measure), grey as the
        // room's white, their material's profile scattering 10, 7 and 5 mm (red farthest, as wax and skin), lit by one
        // lamp, the sun and the sky off. The Direct view (the walk's light where it leaves the thing, over the surface's color,
        // over pi, times the exposure), undenoised, without reuse or clamps, against a walk on the CPU through the same thing
        // (walk_view: planes, double precision, written from the method).
        //   A. A cylinder (16 sides, 4 cm across, 6 cm tall) with the lamp behind it: its far side, which no light reaches
        //      from outside, takes what the reference gives, in each channel; and a second piece of the same wax inside it
        //      changes nothing (the walk counts the pieces it is in).
        //   B. Thin slabs with the lamp behind (3 mm, an ear; 10 mm): each passes what the reference says for its thickness.
        //   C. A thick flat piece under even light sends back its color, as a painted surface does (HDRP's conversion from the
        //      color to what a scattering keeps is made for that), and takes what the reference gives.
        //   D. A lump with a deep fold (a base and two walls grown into it), the lamp low at its side: the fold's floor, in the
        //      walls' shadow, is lit by what went into the wall.
        //   E. Where a path meets wax (TGPT_MATERIAL_SUBSURFACE): the light a slab between a lamp and a floor passes to that
        //      floor is the walk's, colored as the reference's share that crosses the slab.
        //   F. Where a path meets a leaf (TGPT_MATERIAL_TRANSLUCENT): it passes its own share of the light from behind per
        //      channel (TGPT_Material.subsurface), tinted; one whose material says nothing, the grey half.
        {
            sunOn=false; const float keepSky=skyIntensity; skyIntensity=1e-6f; const uint32_t keepBounces=bounces;
            const uint32_t still=TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_CLAMPS;
            const V3 centre{-2.f, 1.5f, -2.f};
            const float aspect=float(W)/float(H), fov=60.f;
            const double toView=double(exposure)/3.14159265358979;
            // The surface's color as the tracer reads it from the GBuffer's 8 bits.
            auto toSrgb=[](double c) { return c<=0.0031308 ? c*12.92 : 1.055*std::pow(c, 1.0/2.4)-0.055; };
            auto toLinear=[](double c) { return c<=0.04045 ? c/12.92 : std::pow((c+0.055)/1.055, 2.4); };
            const double grey=toLinear(std::round(toSrgb(double(mats[0].r))*255.0)/255.0);
            const double albedo[3]={grey, grey, grey};
            const double shape[3]={1.0/0.010, 1.0/0.007, 1.0/0.005};
            // 70: the wax as the plugin sends it; 71: a leaf; 72: the plain white, for a floor.
            TGPT_Material wax=materials[0]; wax.flags=TGPT_MATERIAL_SUBSURFACE; wax.subsurfaceMask=1.f;
            for(int c=0;c<3;++c) wax.subsurface[c]=float(shape[c]);
            TGPT_Material leafMaterial=materials[0]; leafMaterial.flags=TGPT_MATERIAL_TRANSLUCENT|TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_DOUBLE_SIDED; leafMaterial.alphaCutoff=0.f;
            expect(SetMaterials(70, &wax, 1)==0 && SetMaterials(71, &leafMaterial, 1)==0 && SetMaterials(72, &materials[0], 1)==0, "a wax, a leaf and a floor material");
            // The cylinder: flat faces with their own normals, wound as the cube.
            const int sides=16; const float prismRadius=0.02f, prismHeight=0.06f;
            std::vector<Vertex> pv; std::vector<uint16_t> pi16;
            {
                const double step=2.0*3.14159265358979/sides, apothem=prismRadius*std::cos(0.5*step), halfWidth=prismRadius*std::sin(0.5*step);
                for(int i=0;i<sides;++i) {
                    const V3 n{float(std::cos(step*i)), 0, float(std::sin(step*i))}, t{0, 1, 0}, b=cross(n, t);
                    const uint16_t base=uint16_t(pv.size());
                    for(int k=0;k<4;++k) {
                        const float su=(k==1 || k==2) ? 0.5f*prismHeight : -0.5f*prismHeight, sv=(k>=2) ? float(halfWidth) : -float(halfWidth);
                        const V3 p=n*float(apothem)+t*su+b*sv;
                        pv.push_back({p.x, p.y, p.z, n.x, n.y, n.z, 0, 0});
                    }
                    pi16.insert(pi16.end(), {base, uint16_t(base+1), uint16_t(base+2), base, uint16_t(base+2), uint16_t(base+3)});
                }
                for(int s=-1;s<=1;s+=2) {
                    const V3 n{0, float(s), 0}, t{1, 0, 0}, b=cross(n, t);
                    const uint16_t base=uint16_t(pv.size());
                    pv.push_back({0, float(s)*0.5f*prismHeight, 0, n.x, n.y, n.z, 0, 0});
                    for(int k=0;k<sides;++k) {
                        const double a=step*(k+0.5);
                        const V3 p=n*(0.5f*prismHeight)+t*float(prismRadius*std::cos(a))+b*float(prismRadius*std::sin(a));
                        pv.push_back({p.x, p.y, p.z, n.x, n.y, n.z, 0, 0});
                    }
                    for(int k=0;k<sides;++k) pi16.insert(pi16.end(), {base, uint16_t(base+1+k), uint16_t(base+1+(k+1)%sides)});
                }
            }
            MeshBuffers prismMesh=make_mesh(pv, {pi16});
            const int prismHandle=register_mesh(prismMesh, 771);
            expect(prismHandle>0, "a 16-sided cylinder registered");
            struct Traced { int mesh; M4 world; uint32_t material; int submeshes; uint32_t flags; };
            // The drawn things (pushed onto the raster's list) and the traced ones (added to the room's), built over ten frames.
            auto place=[&](const std::vector<Object>& drawn, const std::vector<Traced>& traced, const Camera& c, uint32_t seed) {
                for(const auto& o:drawn) objects.push_back(o);
                auto list=instances; auto listIndices=indices;
                for(const auto& t:traced) {
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=t.world.m[r][k];
                    q.mesh=uint32_t(t.mesh); q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=t.flags;
                    for(int k=0;k<t.submeshes;++k) listIndices.push_back(t.material);
                    list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with the wax in it");
                for(uint32_t i=0;i<10;++i) { run(TGPT_VIEW_NONE, c, c, seed+i, leafOptions); context->Flush(); Sleep(20); }
            };
            auto lampAt=[&](V3 p, float intensity, float range) {
                TGPT_Light l{}; l.position[0]=p.x; l.position[1]=p.y; l.position[2]=p.z; l.type=0;
                l.color[0]=l.color[1]=l.color[2]=intensity; l.range=range; l.radius=0.01f; l.id=430;
                expect(SetLights(&l, 1)==0, "a lamp set");
            };
            // The pixels HDRP draws with subsurface scattering and transmission through a thin profile (red in the Surfaces view).
            auto waxPixels=[&](const Camera& c) {
                run(TGPT_VIEW_SURFACES, c, c, 0); run(TGPT_VIEW_SURFACES, c, c, 1);
                const auto v=readback(); std::vector<size_t> px;
                for(size_t j=0;j+2<v.size();j+=3) if(v[j]>0.05f && v[j]>3.f*v[j+1] && v[j]>3.f*v[j+2]) px.push_back(j);
                return px;
            };
            // The middle of a set of pixels (fx, fy of its extent each way), as pixels and as the screen's -1..1; whole: every
            // pixel of that rectangle is in the set.
            struct Region { std::vector<size_t> px; double x0=0, x1=0, y0=0, y1=0; bool whole=false; };
            auto central=[&](const std::vector<size_t>& px, double fx, double fy) {
                Region g; if(px.empty()) return g;
                size_t c0=W, c1=0, r0=H, r1=0;
                for(size_t j:px) { const size_t at=j/3, col=at%W, row=at/W; c0=std::min(c0, col); c1=std::max(c1, col); r0=std::min(r0, row); r1=std::max(r1, row); }
                const double cx=0.5*double(c0+c1+1), cy=0.5*double(r0+r1+1), hx=0.5*double(c1+1-c0)*fx, hy=0.5*double(r1+1-r0)*fy;
                const size_t ca=size_t(std::ceil(cx-hx)), cb=size_t(std::floor(cx+hx)), ra=size_t(std::ceil(cy-hy)), rb=size_t(std::floor(cy+hy));
                for(size_t j:px) { const size_t at=j/3, col=at%W, row=at/W; if(col>=ca && col<cb && row>=ra && row<rb) g.px.push_back(j); }
                g.x0=double(ca)/W*2.0-1.0; g.x1=double(cb)/W*2.0-1.0; g.y0=double(ra)/H*2.0-1.0; g.y1=double(rb)/H*2.0-1.0;
                g.whole=cb>ca && rb>ra && g.px.size()==(cb-ca)*(rb-ra);
                return g;
            };
            auto channels=[&](const std::vector<double>& a, const std::vector<size_t>& px, double out[3]) { for(int c=0;c<3;++c) out[c]=mean(a, px, c); };
            auto floats=[](const std::vector<double>& v) { std::vector<float> f(v.size()); for(size_t j=0;j<v.size();++j) f[j]=float(v[j]); return f; };
            auto within=[](const double got[3], const double want[3], double tolerance) {
                for(int c=0;c<3;++c) if(!(want[c]>0.0) || std::fabs(got[c]/want[c]-1.0)>tolerance) return false;
                return true;
            };
            double slabThrough[3]={0, 0, 0}; // the share of a walk's light that crosses the 3 mm slab (B, for E)
            double flightsMean=0;            // a walk's mean number of flights in the thick piece (C)

            // Each pixel's surface is the tracer's own camera ray's hit (PT-59): the walk starts at that hit and takes its medium
            // from the material met (TGPT_MATERIAL_SUBSURFACE and its shape: material 70). HDRP's G-buffer is not asked (the
            // drawn things only say which pixels are measured: the Surfaces view), and no list of profiles is sent.
            ownRays=true;
            // A. The cylinder, the lamp 25 cm behind it, seen from 16 cm.
            {
                const V3 lamp{centre.x, centre.y, centre.z+0.25f}, eye{centre.x, centre.y, centre.z-0.16f};
                const Camera c=make_camera(eye, centre, fov, aspect);
                Object drawn{0, translate_scale(centre, {1, 1, 1}), {0}, false, false, 3}; drawn.gb2b=95; drawn.drawn=&prismMesh;
                const Traced cylinder{prismHandle, translate_scale(centre, {1, 1, 1}), 70u, 1, 0u};
                place({drawn}, {cylinder}, c, 9950);
                lampAt(lamp, 2.f, 3.f);
                const auto all=waxPixels(c); const Region g=central(all, 0.5, 0.6);
                const auto image=accumulate(TGPT_VIEW_DIRECT, c, 9960, 48, still);
                const auto plain=accumulate(TGPT_VIEW_DIRECT, c, 9960, 8, still|TGPT_OPTION_NO_SUBSURFACE);
                // The same with a piece of the same wax inside it (a box 1.5 x 3 x 1.5 cm, traced only).
                objects.pop_back();
                place({drawn}, {cylinder, Traced{cubeHandle, translate_scale(centre, {0.015f, 0.03f, 0.015f}), 70u, 2, 0u}}, c, 9970);
                const auto nested=accumulate(TGPT_VIEW_DIRECT, c, 9980, 48, still);
                objects.pop_back();
                const WalkView ref=walk_view(walk_prism(centre, sides, prismRadius, prismHeight), albedo, shape, eye, centre, fov, aspect, g.x0, g.x1, g.y0, g.y1,
                                             lamp, 2.0, toView, 600000, 11);
                double got[3], inside[3]; channels(image, g.px, got); channels(nested, g.px, inside);
                save(floats(image), "92-wax-cylinder-far-side.png", true);
                std::printf("      a wax cylinder lit from behind, its far side (%zu pixels of %zu): %.5f %.5f %.5f against the reference walk's %.5f %.5f %.5f "
                            "(%.3f %.3f %.3f of it); with a piece of the same wax inside %.3f %.3f %.3f of it; lit as a plain surface %.6f\n",
                            g.px.size(), all.size(), got[0], got[1], got[2], ref.light[0], ref.light[1], ref.light[2],
                            got[0]/ref.light[0], got[1]/ref.light[1], got[2]/ref.light[2], inside[0]/ref.light[0], inside[1]/ref.light[1], inside[2]/ref.light[2],
                            mean(plain, g.px, -1));
                expect(g.px.size()>8000 && g.whole && ref.lost==0.0, "the cylinder's far side fills the measured part of the view");
                expect(within(got, ref.light, 0.04), "a wax cylinder lit from one side: its far side takes the light a reference random walk gives");
                expect(within(inside, ref.light, 0.04), "a piece of the same wax grown into it changes nothing: the walk counts the pieces it is in");
                expect(mean(plain, g.px, -1)<0.02*mean(image, g.px, -1), "lit as a plain surface, the far side is black");
            }
            // B. Slabs 6 cm square, the lamp 25 cm behind, seen from 12 cm.
            {
                const float thick[2]={0.003f, 0.010f};
                double share[2][3]{};
                bool ok=true;
                for(int k=0;k<2;++k) {
                    const V3 size{0.06f, 0.06f, thick[k]};
                    const V3 lamp{centre.x, centre.y, centre.z+0.25f}, eye{centre.x, centre.y, centre.z-0.12f};
                    const Camera c=make_camera(eye, centre, fov, aspect);
                    Object drawn{1, translate_scale(centre, size), {0, 0}, false, false, 3}; drawn.gb2b=95;
                    place({drawn}, {Traced{cubeHandle, translate_scale(centre, size), 70u, 2, 0u}}, c, 10000+uint32_t(k)*100);
                    lampAt(lamp, 2.f, 3.f);
                    const auto all=waxPixels(c); const Region g=central(all, 0.5, 0.5);
                    const auto image=accumulate(TGPT_VIEW_DIRECT, c, 10020+uint32_t(k)*100, 32, still);
                    objects.pop_back();
                    const WalkView ref=walk_view(walk_box(centre, size), albedo, shape, eye, centre, fov, aspect, g.x0, g.x1, g.y0, g.y1, lamp, 2.0, toView, 600000, 21+uint64_t(k));
                    double got[3]; channels(image, g.px, got);
                    for(int ch=0;ch<3;++ch) share[k][ch]=ref.through[ch];
                    if(k==0) for(int ch=0;ch<3;++ch) slabThrough[ch]=ref.through[ch];
                    // What HDRP's thin-object transmission gives a slab that thick, for comparison: the color x (e^-St + 3 e^-St/3) / 4
                    // of the light on its far side.
                    const double behind=2.0/((0.25-0.5*thick[k])*(0.25-0.5*thick[k]));
                    double hdrp[3]; for(int ch=0;ch<3;++ch) { const double st=shape[ch]*thick[k]; hdrp[ch]=toView*grey*behind*0.25*(std::exp(-st)+3.0*std::exp(-st/3.0))/grey; }
                    std::printf("      a wax slab %.0f mm thick lit from behind (%zu pixels): %.5f %.5f %.5f against the reference walk's %.5f %.5f %.5f (%.3f %.3f %.3f of it); "
                                "the share of a walk's light that crosses it %.3f %.3f %.3f; HDRP's thin-object formula would give %.5f %.5f %.5f\n",
                                thick[k]*1000.f, g.px.size(), got[0], got[1], got[2], ref.light[0], ref.light[1], ref.light[2],
                                got[0]/ref.light[0], got[1]/ref.light[1], got[2]/ref.light[2], ref.through[0], ref.through[1], ref.through[2], hdrp[0], hdrp[1], hdrp[2]);
                    ok=ok && g.px.size()>8000 && g.whole && ref.lost==0.0 && within(got, ref.light, 0.04);
                }
                expect(ok, "a thin slab of wax (an ear) passes the light a reference random walk gives for its thickness");
                expect(share[1][0]<share[0][0] && share[1][2]<share[0][2] && share[0][2]<share[0][0], "a thicker slab passes less, and less of the blue than of the red");
            }
            // C. A thick flat piece (30 x 12 x 30 cm), a lamp 1.2 m over it, its top's middle seen from 45 cm above.
            {
                const V3 size{0.30f, 0.12f, 0.30f};
                const V3 top{centre.x, centre.y+0.06f, centre.z}, lamp{centre.x, centre.y+1.26f, centre.z}, eye{centre.x, centre.y+0.51f, centre.z-0.04f};
                const Camera c=make_camera(eye, top, fov, aspect);
                Object drawn{1, translate_scale(centre, size), {0, 0}, false, false, 3}; drawn.gb2b=95;
                place({drawn}, {Traced{cubeHandle, translate_scale(centre, size), 70u, 2, 0u}}, c, 10200);
                lampAt(lamp, 20.f, 4.f);
                const auto all=waxPixels(c); const Region g=central(all, 0.3, 0.3);
                const auto image=accumulate(TGPT_VIEW_DIRECT, c, 10220, 32, still);
                const auto plain=accumulate(TGPT_VIEW_DIRECT, c, 10220, 32, still|TGPT_OPTION_NO_SUBSURFACE);
                // What the walk costs where wax fills the view (from 14 cm): the direct light's stage with it and without.
                double msWalk=-1, msPlain=-1;
                {
                    const Camera close=make_camera({centre.x, centre.y+0.20f, centre.z-0.02f}, top, fov, aspect);
                    auto stage=[&](uint32_t more) {
                        for(uint32_t i=0;i<70;++i) { run(TGPT_VIEW_NONE, close, close, i ? 10300+i : 0, more); context->Flush(); Sleep(15); }
                        char stages[1024]{};
                        if(!GetStageTimes || GetStageTimes(stages, sizeof(stages))<=0) return -1.0;
                        const char* at=std::strstr(stages, "direct light ");
                        return at ? std::atof(at+13) : -1.0;
                    };
                    msWalk=stage(0u); msPlain=stage(TGPT_OPTION_NO_SUBSURFACE);
                }
                objects.pop_back();
                // The eye sits 4 cm off the lamp's axis: the reference's rays are the view's own all the same (walk_view takes the
                // region about the view's axis, and the top is level, the lamp far: mirrored, the light differs by a thousandth).
                const WalkView ref=walk_view(walk_box(centre, size), albedo, shape, eye, top, fov, aspect, g.x0, g.x1, g.y0, g.y1, lamp, 20.0, toView, 300000, 31);
                flightsMean=ref.flightsMean;
                double got[3], flat[3]; channels(image, g.px, got); channels(plain, g.px, flat);
                std::printf("      a thick flat piece of wax under a lamp (%zu pixels): %.5f %.5f %.5f with the walk, %.5f %.5f %.5f as a painted surface (%.3f %.3f %.3f of it), "
                            "the reference walk's %.5f %.5f %.5f; a walk takes %.1f flights there on average; the direct light's stage with wax over the whole "
                            "1280x720 view %.2f ms against %.2f ms without the walk\n",
                            g.px.size(), got[0], got[1], got[2], flat[0], flat[1], flat[2], got[0]/flat[0], got[1]/flat[1], got[2]/flat[2],
                            ref.light[0], ref.light[1], ref.light[2], ref.flightsMean, msWalk, msPlain);
                expect(g.px.size()>8000 && g.whole && ref.lost==0.0 && within(got, ref.light, 0.03), "a thick flat piece of wax takes the light a reference random walk gives");
                expect(within(got, flat, 0.06), "a thick flat piece of wax under even light sends back its color, as a painted surface does");
            }
            // D. The fold: a base 12 x 3 x 8 cm and two walls 3.3 x 6 x 8.6 cm grown half a centimetre into it, 6 cm apart; the lamp
            // 40 cm to the side, a centimetre below the walls' tops; seen from 43 cm above. No straight line from the lamp reaches
            // the floor between the walls.
            {
                const V3 baseSize{0.12f, 0.03f, 0.08f}, wallSize{0.033f, 0.06f, 0.086f};
                const V3 wallA{centre.x+0.0465f, centre.y+0.04f, centre.z}, wallB{centre.x-0.0465f, centre.y+0.04f, centre.z};
                const V3 floorAt{centre.x, centre.y+0.015f, centre.z}, lamp{centre.x+0.40f, centre.y+0.06f, centre.z}, eye{centre.x, centre.y+0.45f, centre.z-0.03f};
                const Camera c=make_camera(eye, floorAt, fov, aspect);
                std::vector<Object> drawn; std::vector<Traced> traced;
                for(const auto& part:{std::make_pair(centre, baseSize), std::make_pair(wallA, wallSize), std::make_pair(wallB, wallSize)}) {
                    Object o{1, translate_scale(part.first, part.second), {0, 0}, false, false, 3}; o.gb2b=95; drawn.push_back(o);
                    traced.push_back(Traced{cubeHandle, translate_scale(part.first, part.second), 70u, 2, 0u});
                }
                place(drawn, traced, c, 10400);
                lampAt(lamp, 4.f, 3.f);
                // The floor's middle: half its 6 x 8 cm, about the view's centre.
                const double distance=0.436, halfX=0.5*0.03/(distance*std::tan(fov*3.14159265358979/360.0)*aspect), halfY=0.5*0.04/(distance*std::tan(fov*3.14159265358979/360.0));
                const auto all=waxPixels(c); std::vector<size_t> px;
                for(size_t j:all) {
                    const size_t at=j/3; const double x=(double(at%W)+0.5)/W*2.0-1.0, y=(double(at/W)+0.5)/H*2.0-1.0;
                    if(std::fabs(x)<halfX && std::fabs(y)<halfY) px.push_back(j);
                }
                const auto image=accumulate(TGPT_VIEW_DIRECT, c, 10420, 32, still);
                const auto plain=accumulate(TGPT_VIEW_DIRECT, c, 10420, 8, still|TGPT_OPTION_NO_SUBSURFACE);
                for(int k=0;k<3;++k) objects.pop_back();
                save(floats(image), "93-wax-fold.png", true);
                // The light on the wall's outer face, as the view shows a white surface under it.
                const double outer=toView*4.0/((0.40-0.063)*(0.40-0.063));
                double got[3]; channels(image, px, got);
                std::printf("      a deep fold in a wax lump, its floor in the walls' shadow (%zu pixels): %.5f %.5f %.5f with the walk (%.4f of the light on the lit wall's "
                            "face, %.4f), %.6f lit as a plain surface\n", px.size(), got[0], got[1], got[2], mean(image, px, -1)/outer, outer, mean(plain, px, -1));
                // (0.0117 of it as built; 0.0042 where the walk stops at the first of its own surfaces it meets, the wall's foot
                // inside the base: the walls are grown into it.)
                expect(px.size()>1500 && mean(image, px, -1)>0.008*outer && got[0]>got[2],
                       "a deep fold in a wax lump is lit by the light that went in elsewhere, redder than it went in");
                expect(mean(plain, px, -1)<0.05*mean(image, px, -1), "lit as a plain surface, the fold is black");
            }
            // E and F. Where paths meet such things: a floor 30 x 24 cm under the dark side of an upright sheet 20 cm square, the
            // lamp 25 cm behind the sheet, one bounce, no cache: the floor's bounced light (the Indirect view before denoising) in the
            // sheet's shadow is what its rays find on the sheet's dark side. With the walk off, or the sheet opaque: the rest.
            {
                bounces=1;
                const V3 lamp{centre.x, centre.y, centre.z+0.25f}, floorAt{centre.x, centre.y-0.11f, centre.z-0.14f}, eye{centre.x, centre.y+0.30f, centre.z-0.50f};
                const Camera c=make_camera(eye, floorAt, fov, aspect);
                const M4 floorWorld=translate_scale(floorAt, {0.30f, 1, 0.24f});
                Object floorDrawn{0, floorWorld, {0}};
                // The measured part of the floor, marked by a quad drawn over it for the Surfaces view alone (10 x 14 cm, in the
                // sheet's shadow).
                Object marker{0, translate_scale({floorAt.x, floorAt.y+0.0005f, floorAt.z}, {0.10f, 1, 0.14f}), {0}, false, false, 2};
                objects.push_back(floorDrawn); objects.push_back(marker);
                const auto px=leafPixels(c);
                objects.pop_back(); objects.pop_back();
                auto floorLight=[&](const Traced& sheet, uint32_t seed, uint32_t more, double out[3]) {
                    place({floorDrawn}, {Traced{quadHandle, floorWorld, 72u, 1, 0u}, sheet}, c, seed);
                    lampAt(lamp, 8.f, 1.f);
                    const auto a=accumulate(TGPT_VIEW_INDIRECT_RAW, c, seed+20, 48, still|more);
                    objects.pop_back();
                    channels(a, px, out);
                };
                // E. The sheet a slab of wax 3 mm thick.
                const Traced slab{cubeHandle, translate_scale(centre, {0.20f, 0.20f, 0.003f}), 70u, 2, 0u};
                double walked[3], blocked[3];
                floorLight(slab, 10500, 0u, walked); floorLight(slab, 10500, TGPT_OPTION_NO_SUBSURFACE, blocked);
                const double passed[3]={walked[0]-blocked[0], walked[1]-blocked[1], walked[2]-blocked[2]};
                std::printf("      a floor behind a wax slab (%zu pixels), its bounced light: %.6f %.6f %.6f with the walk at the path's vertices, %.6f %.6f %.6f without; "
                            "what the slab passes, green and blue against red: %.3f %.3f (the reference walk's shares that cross it: %.3f %.3f)\n",
                            px.size(), walked[0], walked[1], walked[2], blocked[0], blocked[1], blocked[2], passed[1]/passed[0], passed[2]/passed[0],
                            slabThrough[1]/slabThrough[0], slabThrough[2]/slabThrough[0]);
                expect(px.size()>3000 && passed[0]>5.0*std::max(blocked[0], 1e-9), "a path that meets wax takes the light that comes out of it: a slab passes a lamp's light to the floor behind it");
                expect(slabThrough[0]>0 && std::fabs(passed[1]/passed[0]/(slabThrough[1]/slabThrough[0])-1.0)<0.12 && std::fabs(passed[2]/passed[0]/(slabThrough[2]/slabThrough[0])-1.0)<0.12,
                       "the light a wax slab passes to a path is colored as the reference random walk's");
                // F. The sheet a leaf (a quad facing the floor's side), its material's own share of the light from behind: 0.3 grey;
                // 0.6, 0.3, 0.1; none given; and the same quad opaque.
                const Traced leafSheet{quadHandle, mul(translate_scale(centre, {0.20f, 1, 0.20f}), rot_x(-pi/2)), 71u, 1, TGPT_INSTANCE_ALPHA_TEST};
                auto leafLight=[&](const float share[3], bool translucent, uint32_t seed, double out[3]) {
                    TGPT_Material m=leafMaterial;
                    if(!translucent) m.flags&=~uint32_t(TGPT_MATERIAL_TRANSLUCENT);
                    for(int k=0;k<3;++k) m.subsurface[k]=share[k];
                    expect(SetMaterials(71, &m, 1)==0, "the leaf's material");
                    floorLight(leafSheet, seed, 0u, out);
                };
                const float greyShare[3]={0.3f, 0.3f, 0.3f}, tinted[3]={0.6f, 0.3f, 0.1f}, none[3]={0, 0, 0};
                double lightGrey[3], lightTinted[3], lightNone[3], lightOpaque[3];
                leafLight(greyShare, true, 10600, lightGrey); leafLight(tinted, true, 10600, lightTinted);
                leafLight(none, true, 10600, lightNone); leafLight(greyShare, false, 10600, lightOpaque);
                double tintOf[3], noneOf[3];
                for(int k=0;k<3;++k) {
                    const double base=lightGrey[k]-lightOpaque[k];
                    tintOf[k]=(lightTinted[k]-lightOpaque[k])/base; noneOf[k]=(lightNone[k]-lightOpaque[k])/base;
                }
                std::printf("      a floor behind a leaf, its bounced light through the leaf: with the material's own share 0.6 0.3 0.1, %.3f %.3f %.3f of that with 0.3 grey "
                            "(2, 1, 0.333); with none given %.3f %.3f %.3f (the grey half: 1.667); through the leaf at 0.3 grey %.6f, opaque %.6f\n",
                            tintOf[0], tintOf[1], tintOf[2], noneOf[0], noneOf[1], noneOf[2], lightGrey[1], lightOpaque[1]);
                const double wantTint[3]={2.0, 1.0, 1.0/3.0}, wantNone[3]={0.5/0.3, 0.5/0.3, 0.5/0.3};
                expect(lightGrey[1]>5.0*std::max(lightOpaque[1], 1e-9) && within(tintOf, wantTint, 0.08), "a leaf a path meets passes the light from behind by its material's own share, tinted");
                expect(within(noneOf, wantNone, 0.08), "a leaf whose material says nothing passes the grey half");
                // G. The leaf where the camera's own ray meets it (PT-59; HDRP's G-buffer holds no leaf here, and is not asked): seen
                // from its dark side, 30 cm away, the lamp 25 cm behind it. Its direct light (the Direct view: the light over the
                // surface's color) is the lamp's through the leaf by the material's own share: with 0.6, 0.3, 0.1 twice, once and a
                // third of what 0.3 grey lets through; and next to none of it where the same quad is opaque.
                {
                    const V3 eyeG{centre.x, centre.y, centre.z-0.30f};
                    const Camera cg=make_camera(eyeG, centre, fov, aspect);
                    std::vector<size_t> middle;
                    for(size_t y=H/2-60;y<H/2+60;++y) for(size_t x=W/2-60;x<W/2+60;++x) middle.push_back((y*W+x)*3);
                    auto seenLight=[&](const float share[3], bool translucent, uint32_t seed, double out[3]) {
                        TGPT_Material m=leafMaterial;
                        if(!translucent) m.flags&=~uint32_t(TGPT_MATERIAL_TRANSLUCENT);
                        for(int k=0;k<3;++k) m.subsurface[k]=share[k];
                        expect(SetMaterials(71, &m, 1)==0, "the leaf's material");
                        place({}, {leafSheet}, cg, seed);
                        lampAt(lamp, 8.f, 1.f);
                        const auto image=accumulate(TGPT_VIEW_DIRECT, cg, seed+20, 32, still);
                        channels(image, middle, out);
                    };
                    double seenGrey[3], seenTinted[3], seenOpaque[3], seenOf[3];
                    seenLight(greyShare, true, 10700, seenGrey); seenLight(tinted, true, 10700, seenTinted); seenLight(greyShare, false, 10700, seenOpaque);
                    for(int k=0;k<3;++k) seenOf[k]=seenTinted[k]/std::max(seenGrey[k], 1e-12);
                    std::printf("      a leaf the camera's own ray meets, lit from behind (%zu pixels): its direct light with the material's own share 0.6 0.3 0.1, "
                                "%.3f %.3f %.3f of that with 0.3 grey (2, 1, 0.333); at 0.3 grey %.6f, opaque %.6f\n",
                                middle.size(), seenOf[0], seenOf[1], seenOf[2], seenGrey[1], seenOpaque[1]);
                    expect(seenGrey[1]>5.0*std::max(seenOpaque[1], 1e-9) && within(seenOf, wantTint, 0.08),
                           "a leaf the camera's own ray meets passes the light from behind by its material's own share, tinted");
                    ownRays=false;
                }
                bounces=keepBounces;
            }
            (void)flightsMean;
            sunOn=true; skyIntensity=keepSky;
            expect(SetLights(&light, 1)==0, "the room's lamp again");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
        }
        // 3. The sky's and bounced light from behind a leaf (pt_gi, gathers_behind), as HDRP adds the far side's light times its
        // transmittance: a quad under the ceiling's opening facing down, its back to the open sky. As a leaf it gets its front's
        // light (the floor's) plus the grey half (and with the foliage's share in its material, that share) of what an opaque
        // quad facing up there gets from the sky; seen from a second camera under the ceiling, looking down at it.
        {
            const M4 down=mul(translate_scale({-1.5f, 5.f, 4.f}, {2.5f, 1, 2.5f}), rot_x(pi)), up=translate_scale({-1.5f, 5.f, 4.f}, {2.5f, 1, 2.5f});
            const Camera above=make_camera({-1.5f, 5.9f, 1.2f}, {-1.5f, 5.f, 4.f}, 60.f, float(W)/float(H));
            auto indirect=[&](const M4& w, int kind, const Camera& c, std::vector<size_t>& px) {
                if(px.empty()) { Object o{0, w, {0}, false, false, 2}; objects.push_back(o); px=leafPixels(c); objects.pop_back(); }
                tracedLeaf(w, kind, c);
                const auto a=accumulate(TGPT_VIEW_INDIRECT_RAW, c, 9200, 32);
                noLeaf();
                return a;
            };
            std::vector<size_t> below, fromAbove;
            // The sun off: a leaf lets half of the sun's shadow rays through (alpha_opaque), an opaque quad none, so the floor under
            // it, whose bounced light reaches the leaf's front, would differ between the two.
            sunOn=false;
            const auto opaqueDown=indirect(down, 0, cam, below), leafDown=indirect(down, 1, cam, below), tintedDown=indirect(down, 2, cam, below);
            const auto opaqueUp=indirect(up, 0, above, fromAbove);
            sunOn=true;
            double sky[3]{}, grey[3]{}, tinted[3]{};
            for(int c=0;c<3;++c) {
                sky[c]=mean(opaqueUp, fromAbove, c);
                grey[c]=sky[c]>0 ? (mean(leafDown, below, c)-mean(opaqueDown, below, c))/sky[c] : 0.0;
                tinted[c]=sky[c]>0 ? (mean(tintedDown, below, c)-mean(opaqueDown, below, c))/sky[c] : 0.0;
            }
            std::printf("      a leaf under the open sky, facing down (%zu pixels; the sky above it %zu pixels %.4f): the light from behind it %.2f %.2f %.2f "
                        "of the sky's (0.5 expected); with the foliage's share %.2f %.2f %.2f (%.2f %.2f %.2f expected)\n", below.size(), fromAbove.size(),
                        0.2126*sky[0]+0.7152*sky[1]+0.0722*sky[2], grey[0], grey[1], grey[2], tinted[0], tinted[1], tinted[2], foliage[0], foliage[1], foliage[2]);
            bool behind=below.size()>1000 && fromAbove.size()>1000 && sky[1]>0;
            for(int c=0;c<3;++c) behind=behind && std::fabs(grey[c]/0.5-1.0)<0.2 && std::fabs(tinted[c]/foliage[c]-1.0)<0.2;
            expect(behind, "a leaf gathers the sky's light from behind it, as much as it lets through");
        }
        // 4. A blade's own occlusion (pt_prepare bit 128): HDRP's ambient occlusion from its material (GBuffer3's green behind
        // HDRP's tag) takes its bounced and sky light down (pt_compose), its specular occlusion (GBuffer2's red) its reflections
        // (pt_specular); the sun and lamps stay. Each linear: with 0, a share and 1, the share's part of the 0-to-1 difference.
        {
            auto shaded=[&](uint32_t view, float ao, float so, float roughness) {
                Object leaf{0, facing, {0}, false, false, 2}; leaf.ao=ao; leaf.so=so; leaf.roughness=roughness;
                objects.push_back(leaf);
                const auto a=accumulate(view, cam, 9300, 16);
                objects.pop_back();
                return a;
            };
            Object probe{0, facing, {0}, false, false, 2}; objects.push_back(probe); const auto px=leafPixels(cam); objects.pop_back();
            const double ao0=mean(shaded(TGPT_VIEW_NONE, 0.f, 1.f, 0.6f), px, -1), ao3=mean(shaded(TGPT_VIEW_NONE, 0.3f, 1.f, 0.6f), px, -1),
                         ao1=mean(shaded(TGPT_VIEW_NONE, 1.f, 1.f, 0.6f), px, -1);
            const double so0=mean(shaded(TGPT_VIEW_SPECULAR, 1.f, 0.f, 0.3f), px, -1), so5=mean(shaded(TGPT_VIEW_SPECULAR, 1.f, 0.5f, 0.3f), px, -1),
                         so1=mean(shaded(TGPT_VIEW_SPECULAR, 1.f, 1.f, 0.3f), px, -1);
            const double aoShare=ao1>ao0 ? (ao3-ao0)/(ao1-ao0) : 0.0, soShare=so1>so0 ? (so5-so0)/(so1-so0) : 0.0;
            std::printf("      a blade's own occlusion: its picture %.4f with AO 0, %.4f with 0.3, %.4f with 1 (%.2f of the way: 0.30 expected); "
                        "its reflections %.5f with SO 0, %.5f with 0.5, %.5f with 1 (%.2f: 0.50 expected)\n", ao0, ao3, ao1, aoShare, so0, so5, so1, soShare);
            expect(px.size()>2000 && ao1>1.15*ao0 && std::fabs(aoShare-0.3)<0.05, "a blade's material occlusion takes its bounced and sky light down as HDRP's does");
            expect(so1>1.5*so0 && std::fabs(soShare-0.5)<0.06, "a blade's specular occlusion takes its reflections down");
        }
        // 5. A blade whose HDRP normal faces away from the camera (the game's back faces it leaves unflipped: 14 of its grass and
        // leaf materials): its rays leave from its own face (pt_prepare: HDRP's normal where the plane through its neighbours is
        // more than 60 degrees off). From the plane facing the camera, every ray it sent counted as below its surface: no light.
        {
            const M4 away=mul(translate_scale({-3.5f, 4.3f, 4.5f}, {1.5f, 1, 1.5f}), rot_x(pi/2));
            auto gathered=[&](const M4& w, std::vector<size_t>& px) {
                Object leaf{0, w, {0}, false, false, 2};
                objects.push_back(leaf);
                px=leafPixels(cam);
                const auto a=accumulate(TGPT_VIEW_INDIRECT_RAW, cam, 9400, 24);
                objects.pop_back();
                return a;
            };
            std::vector<size_t> pxFacing, pxAway;
            const auto towards=gathered(facing, pxFacing), from=gathered(away, pxAway);
            const double lightFacing=mean(towards, pxFacing, -1), lightAway=mean(from, pxAway, -1);
            std::printf("      a blade seen from its unflipped back (%zu pixels): bounced and sky light %.4f, %.2f of the same blade facing the camera\n",
                        pxAway.size(), lightAway, lightFacing>0 ? lightAway/lightFacing : 0.0);
            // (Broken, it still got 0.35: ReSTIR's spatial reuse lends it the back wall's samples beside it. Without the light
            // from behind a leaf, 0.60: the two sides see different parts of the room.)
            expect(pxAway.size()>2000 && lightFacing>0 && lightAway>0.5*lightFacing, "a blade whose normal faces away from the camera still gathers light");
        }
        // 6. Grass's shadows (the owner, 2026-10-06: "We want grass shadows, but not those horrible looking ones"): a leaf or a
        // blade (TGPT_MATERIAL_TRANSLUCENT) lets a light's shadow ray through as often as it is lit through (half), so its shadow
        // is half one. A traced-only card in the sun, alpha-tested with the checkered leaf's texture, on a mesh of its own whose
        // micromaps are baked for the translucent material first (they must leave its covered texels to the per-ray test): the
        // floor in its shadow (where the opaque card's takes most of the sun) gets half the sun the card takes from it.
        {
            const int grassHandle=register_mesh(cardMesh, 78);
            auto grass=materials[6]; grass.flags|=TGPT_MATERIAL_TRANSLUCENT;
            const uint32_t grassIndex=40;
            expect(grassHandle>0 && SetMaterials(grassIndex, &grass, 1)==0, "a translucent card of its own");
            auto withCard=[&](uint32_t material) {
                auto list=instances; auto listIndices=indices;
                const M4 w=translate_scale({-3.f, 4.f, 4.f}, {2, 1, 2});
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
                q.mesh=grassHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(material); list.push_back(q);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a card in the sun");
                for(uint32_t i=0;i<10;++i) { run(TGPT_VIEW_NONE, cam, cam, 9480+i, leafOptions); context->Flush(); Sleep(20); } // read back, built, its micromaps baked
                ReadStatus(&status);
                const uint32_t withMicromaps=status.micromapInstances;
                const auto a=accumulate(TGPT_VIEW_DIRECT, cam, 9500, 24);
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return std::make_pair(a, withMicromaps);
            };
            const auto translucent=withCard(grassIndex), opaque=withCard(6), none=withCard(7);
            double taken=0, through=0; size_t n=0;
            for(size_t j=0;j+2<none.first.size();j+=3) {
                const double l=lum3(none.first, j), o=lum3(opaque.first, j);
                if(!(l>0.02) || o>0.5*l) continue;
                taken+=l-o; through+=lum3(translucent.first, j)-o; ++n;
            }
            std::printf("      a translucent card's shadow (%zu pixels): %.2f of the sun the opaque card's takes comes through (0.5 expected); "
                        "instances with micromaps %u, %u, %u\n", n, taken>0 ? through/taken : 0.0, translucent.second, opaque.second, none.second);
            expect(n>500 && taken>0 && through/taken>0.38 && through/taken<0.62, "grass and leaves cast half shadows, as much as they let through");
        }
        // 7. Thin cloth's shadow (PT-39: a red canopy left a candle corner black): thin cloth lets a light's shadow ray through its
        // weave as often as it is lit through (CLOTH_TRANSMISSION, 0.3), tinted by its color at full brightness (glass_walk's
        // crossings: cloth_tint). A red card in the sun, alpha-tested with no cut-outs (as the plugin makes every thin cloth), on a
        // mesh of its own with micromaps baked for it: the floor in its shadow gets 0.3 of the sun it takes, in red, 0.107 of that
        // in green (the red material's 0.75, 0.08, 0.06).
        {
            const int clothHandle=register_mesh(cardMesh, 80);
            auto red=materials[1]; red.flags=TGPT_MATERIAL_ALPHA_TEST; red.alphaCutoff=0.f;
            auto canopy=red; canopy.flags|=TGPT_MATERIAL_THIN_CLOTH;
            expect(clothHandle>0 && SetMaterials(41, &canopy, 1)==0 && SetMaterials(42, &red, 1)==0, "a red cloth card of its own");
            auto withCloth=[&](uint32_t material) {
                auto list=instances; auto listIndices=indices;
                const M4 w=translate_scale({-3.f, 4.f, 4.f}, {2, 1, 2});
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
                q.mesh=clothHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(material); list.push_back(q);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a cloth card in the sun");
                for(uint32_t i=0;i<10;++i) { run(TGPT_VIEW_NONE, cam, cam, 9580+i, leafOptions); context->Flush(); Sleep(20); }
                ReadStatus(&status);
                const uint32_t withMicromaps=status.micromapInstances;
                const auto a=accumulate(TGPT_VIEW_DIRECT, cam, 9600, 24);
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return std::make_pair(a, withMicromaps);
            };
            const auto cloth=withCloth(41), solid=withCloth(42), none=withCloth(7);
            double taken[3]{}, through[3]{}; size_t n=0;
            for(size_t j=0;j+2<none.first.size();j+=3) {
                if(!(lum3(none.first, j)>0.02) || lum3(solid.first, j)>0.5*lum3(none.first, j)) continue;
                for(int c=0;c<3;++c) { taken[c]+=none.first[j+size_t(c)]-solid.first[j+size_t(c)]; through[c]+=cloth.first[j+size_t(c)]-solid.first[j+size_t(c)]; }
                ++n;
            }
            const double share=taken[0]>0 ? through[0]/taken[0] : 0.0, greenOfRed=through[0]>0 && taken[1]>0 ? (through[1]/taken[1])/share : 0.0;
            std::printf("      a red cloth card's shadow (%zu pixels): %.2f of the sun it takes comes through in red (0.30 expected), green %.3f of red's share "
                        "(0.107 expected); instances with micromaps %u, %u, %u\n", n, share, greenOfRed, cloth.second, solid.second, none.second);
            expect(n>500 && share>0.24 && share<0.36 && greenOfRed>0.06 && greenOfRed<0.16, "thin cloth lets a share of the light through, tinted by its color");
        }
        // 8. Cloth the wind moves is traced where it stands still, and HDRP draws it swayed away (PT-39: seen from under a canopy,
        // its pixels were not taken for cloth): a quad drawn facing the camera with the sun behind it, its traced cloth 25 cm
        // nearer the camera (beyond the camera ray's window there, 17 cm, within CLOTH_SWAY_REACH), a material the wind moves.
        // Its pixels are still lit through as thin cloth, as much as with the traced cloth in place.
        {
            auto swaying=materials[0]; swaying.flags|=TGPT_MATERIAL_THIN_CLOTH|TGPT_MATERIAL_SWAYS;
            expect(SetMaterials(43, &swaying, 1)==0, "a cloth material the wind moves");
            Object probe{0, facing, {0}}; objects.push_back(probe);
            auto lit=[&](float shift, bool traced) {
                auto list=instances; auto listIndices=indices;
                if(traced) {
                    const M4 w=mul(translate_scale({-3.5f, 4.3f, 4.5f-shift}, {1.5f, 1, 1.5f}), rot_x(-pi/2));
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
                    q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                    listIndices.push_back(43); list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a swaying cloth");
                const auto a=accumulate(TGPT_VIEW_DIRECT, cam, 9700, 24);
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return a;
            };
            objects.back().feature=2; const auto px=leafPixels(cam); objects.back().feature=0;
            const auto plain=lit(0.f, false), inPlace=lit(0.f, true), swayed=lit(0.25f, true);
            const double through=mean(inPlace, px, -1)-mean(plain, px, -1), throughSwayed=mean(swayed, px, -1)-mean(plain, px, -1);
            objects.pop_back();
            std::printf("      cloth drawn 25 cm from where it is traced (%zu pixels): lit through %.4f, %.2f of the cloth traced in place (%.4f)\n",
                        px.size(), throughSwayed, through>0 ? throughSwayed/through : 0.0, through);
            // (0.73: the cloth's still copy, 25 cm in front, now shades the lamp in front of it by its 0.7. Broken: below 0.)
            expect(px.size()>2000 && through>0 && throughSwayed>0.5*through, "cloth the wind sways away from where it is traced is still lit through");
        }
        // 9. Thin cloth under an open sky with no sun or lamp on it (PT-39: a canopy in a building's shade was black underneath):
        // its pixel gathers the sky's light from behind it as a leaf does (gathers_behind), by the share it lets through
        // (CLOTH_TRANSMISSION, 0.3; its own color is the composite's). A quad drawn facing down under the ceiling's opening and
        // traced as thin cloth, against the same quad traced as a solid one, over what an opaque quad facing up there gets from
        // the sky (a second camera under the ceiling). The sun and the lamp off: the sky's light alone.
        {
            const M4 down=mul(translate_scale({-1.5f, 5.f, 4.f}, {2.5f, 1, 2.5f}), rot_x(pi)), up=translate_scale({-1.5f, 5.f, 4.f}, {2.5f, 1, 2.5f});
            const Camera above=make_camera({-1.5f, 5.9f, 1.2f}, {-1.5f, 5.f, 4.f}, 60.f, float(W)/float(H));
            auto weave=materials[0]; weave.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_THIN_CLOTH; weave.alphaCutoff=0.f;
            auto sheet=materials[0]; sheet.flags=TGPT_MATERIAL_ALPHA_TEST; sheet.alphaCutoff=0.f;
            expect(SetMaterials(44, &weave, 1)==0 && SetMaterials(45, &sheet, 1)==0, "a cloth and a solid sheet");
            sunOn=false; expect(SetLights(nullptr, 0)==0, "the sky alone");
            auto under=[&](const M4& w, int material, const Camera& c, std::vector<size_t>& px) {
                auto list=instances; auto listIndices=indices;
                if(material>=0) {
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
                    q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
                    listIndices.push_back(uint32_t(material)); list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a sheet under the sky");
                Object o{0, w, {0}}; objects.push_back(o);
                if(px.empty()) { objects.back().feature=2; px=leafPixels(c); objects.back().feature=0; }
                const auto a=accumulate(TGPT_VIEW_INDIRECT_RAW, c, 9800, 32);
                objects.pop_back();
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return a;
            };
            std::vector<size_t> below, fromAbove;
            const auto clothDown=under(down, 44, cam, below), solidDown=under(down, 45, cam, below), skyUp=under(up, -1, above, fromAbove);
            sunOn=true; expect(SetLights(&light, 1)==0, "the sun and the lamp again");
            double share[3]{}; bool glows=below.size()>1000 && fromAbove.size()>1000;
            for(int c=0;c<3;++c) {
                const double sky=mean(skyUp, fromAbove, c);
                share[c]=sky>0 ? (mean(clothDown, below, c)-mean(solidDown, below, c))/sky : 0.0;
                glows=glows && std::fabs(share[c]/0.3-1.0)<0.25;
            }
            std::printf("      thin cloth under the open sky, seen from below (%zu pixels): the sky's light from behind it %.2f %.2f %.2f of the sky's "
                        "(0.30 expected); as a solid sheet %.4f, as cloth %.4f\n", below.size(), share[0], share[1], share[2], mean(solidDown, below, -1),
                        mean(clothDown, below, -1));
            expect(glows, "thin cloth is lit by the sky behind it, as much as it lets through");
        }
        // 10. And what lies under it: the rays that gather the sky's and bounced light for a surface pass thin cloth by the same
        // chance as a light's ray (CLOTH_TRANSMISSION), tinted by its color (g_bounceRay, cloth_walk); the sample keeps its light
        // over that chance (pt_gi's held, applied after the clamp), and the ray that checks it is seen (pt_gi_spatial) takes a
        // chance of its own at the cloth (g_raySalt) and its tint. A red cloth canopy 1.2 m over the floor, 4 by 6 m, traced
        // only, under the sky alone (no sun, no lamp, paths one vertex long: nothing but the sky seen from the floor): the floor
        // under it gets 0.3 of the sky's light the solid canopy takes from it, in red, and 0.107 of that in green (its color).
        // Without ReSTIR's reuse: its Basic bias correction darkens the floor near the solid canopy's edge (16 % in red, none 6 %),
        // which the cloth's light through hides, so with reuse cloth minus solid also holds that untinted loss (0.33 in red,
        // green 0.23 of red; 0.32 and 0.15 deep under it; with TGPT_OPTION_GI_VISIBILITY 0.34 and 0.11). Broken: a check ray
        // that decides as the ray that found the sample gave 0.365, the clamp after held 0.21, no tint green 1.
        {
            sunOn=false; expect(SetLights(nullptr, 0)==0, "the sky alone");
            const uint32_t keepBounces=bounces; bounces=1;
            auto canopied=[&](uint32_t material) {
                auto list=instances; auto listIndices=indices;
                const M4 w=translate_scale({-3.5f, 1.2f, 1.f}, {4, 1, 6});
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
                q.mesh=quadHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
                listIndices.push_back(material); list.push_back(q);
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a canopy over the floor");
                const auto a=accumulate(TGPT_VIEW_INDIRECT_RAW, cam, 9900, 32, TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL);
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return a;
            };
            const auto cloth=canopied(41), solid=canopied(42), none=canopied(7); // test 7's red cloth and red solid; the invisible material
            bounces=keepBounces; sunOn=true; expect(SetLights(&light, 1)==0, "the sun and the lamp again");
            double taken[3]{}, through[3]{}; size_t n=0;
            for(size_t j=0;j+2<none.size();j+=3) {
                if(!(lum3(none, j)>0.02) || lum3(solid, j)>0.5*lum3(none, j)) continue;
                for(int c=0;c<3;++c) { taken[c]+=none[j+size_t(c)]-solid[j+size_t(c)]; through[c]+=cloth[j+size_t(c)]-solid[j+size_t(c)]; }
                ++n;
            }
            const double share=taken[0]>0 ? through[0]/taken[0] : 0.0, greenOfRed=through[0]>0 && taken[1]>0 ? (through[1]/taken[1])/share : 0.0;
            std::printf("      the floor under a red cloth canopy, the sky alone (%zu pixels): %.2f of the sky's light the solid canopy takes comes through "
                        "in red (0.30 expected), green %.3f of red's share (0.107 expected)\n", n, share, greenOfRed);
            expect(n>2000 && share>0.24 && share<0.36 && greenOfRed>0.06 && greenOfRed<0.16, "the sky's light passes thin cloth to what lies under it, tinted by its color");
        }
        // 11-13. Cloth the wind sways (PT-49, the owner: "reflections and shadows of moving objects, including cloth, must be
        // accurately represented by our path tracer"). The game's Cloth_Wind moves its vertices in its vertex shader by the time
        // (cloth_wind_reference, from its compiled code); the tracer moves its copy of the still mesh the same way every frame
        // (TGPT_SetClothWind, pt_sway, its BLAS refitted). A sheet of 16 by 16 cells, its texture once across it, and a wind as
        // the game's materials set it (speed 0.18, wind 0.77: the shader's defaults).
        {
            const int cells=16;
            std::vector<Vertex> flat; std::vector<uint16_t> gridIndices;
            for(int z=0;z<=cells;++z) for(int x=0;x<=cells;++x) {
                const float u=float(x)/cells, v=float(z)/cells;
                flat.push_back({u-0.5f, 0, v-0.5f, 0, 1, 0, u, v});
            }
            for(int z=0;z<cells;++z) for(int x=0;x<cells;++x) {
                const uint16_t a=uint16_t(z*(cells+1)+x), b=uint16_t(a+1), c=uint16_t(a+cells+2), d=uint16_t(a+cells+1);
                gridIndices.insert(gridIndices.end(), {a, c, b, a, d, c}); // wound as the quad
            }
            // The same sheet hung 1 m under its object's origin (as a canopy under the top of its poles): the shader moves a
            // vertex by its object-space position, so this one sways up and down as well as along itself.
            std::vector<Vertex> hung=flat; for(auto& q:hung) q.py=-1.f;
            MeshBuffers sheetMesh=make_mesh(flat, {gridIndices}), hungMesh=make_mesh(hung, {gridIndices});
            const int sheetHandle=register_mesh(sheetMesh, 90), hungHandle=register_mesh(hungMesh, 92);
            expect(sheetHandle>0 && hungHandle>0 && SetClothWind!=nullptr, "a sheet of cloth and the cloth wind export");
            TGPT_ClothWind wind{}; wind.speed=0.18f; wind.wind=0.77f; wind.noiseScale=4.f; wind.direction[0]=0.8f; wind.direction[1]=0.6f;
            // The sheet as the game's shader poses it at a time, its normals from the moved grid.
            auto posed=[&](const std::vector<Vertex>& base, const M4& w, const TGPT_ClothWind& wd, float time) {
                const float a[9]={w.m[0][0], w.m[0][1], w.m[0][2], w.m[1][0], w.m[1][1], w.m[1][2], w.m[2][0], w.m[2][1], w.m[2][2]};
                const float det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
                const float inv[9]={(a[4]*a[8]-a[5]*a[7])/det, (a[2]*a[7]-a[1]*a[8])/det, (a[1]*a[5]-a[2]*a[4])/det,
                                    (a[5]*a[6]-a[3]*a[8])/det, (a[0]*a[8]-a[2]*a[6])/det, (a[2]*a[3]-a[0]*a[5])/det,
                                    (a[3]*a[7]-a[4]*a[6])/det, (a[1]*a[6]-a[0]*a[7])/det, (a[0]*a[4]-a[1]*a[3])/det};
                std::vector<Vertex> out=base;
                for(auto& q:out) { const V3 p=cloth_wind_reference({q.px, q.py, q.pz}, q.u, q.v, {1, 1, 1}, wd, time, inv); q.px=p.x; q.py=p.y; q.pz=p.z; }
                auto at=[&](int x, int z) { const auto& q=out[size_t(std::clamp(z, 0, cells)*(cells+1)+std::clamp(x, 0, cells))]; return V3{q.px, q.py, q.pz}; };
                std::vector<V3> normals;
                for(int z=0;z<=cells;++z) for(int x=0;x<=cells;++x) normals.push_back(normalize(cross(at(x, z+1)-at(x, z-1), at(x+1, z)-at(x-1, z))));
                for(size_t i=0;i<out.size();++i) { out[i].nx=normals[i].x; out[i].ny=normals[i].y; out[i].nz=normals[i].z; }
                return make_mesh(out, {gridIndices});
            };
            auto sheetInstance=[&](std::vector<TGPT_Instance>& list, std::vector<uint32_t>& listIndices, const M4& w, int handle, uint32_t material, bool alpha) {
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
                q.mesh=handle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=alpha ? TGPT_INSTANCE_ALPHA_TEST : 0u;
                listIndices.push_back(material); list.push_back(q);
            };
            auto weave=materials[0]; weave.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_THIN_CLOTH|TGPT_MATERIAL_SWAYS; weave.alphaCutoff=0.f;
            auto solidSways=materials[0]; solidSways.flags=TGPT_MATERIAL_SWAYS;
            auto redWeave=materials[1]; redWeave.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_THIN_CLOTH|TGPT_MATERIAL_SWAYS; redWeave.alphaCutoff=0.f;
            auto redSheet=materials[1]; redSheet.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_SWAYS; redSheet.alphaCutoff=0.f;
            expect(SetMaterials(46, &weave, 1)==0 && SetMaterials(47, &solidSways, 1)==0 && SetMaterials(48, &redWeave, 1)==0 && SetMaterials(49, &redSheet, 1)==0,
                   "swaying cloth materials");
            std::vector<TGPT_ClothWind> winds;
            for(uint32_t m=46;m<=49;++m) { auto w=wind; w.material=m; winds.push_back(w); }
            // 11. Its pixels: a 1.5 by 1 m sheet hangs facing a camera 3 m off (40 degrees: 3 mm a pixel there), drawn swayed as
            // the game's shader poses it, the wind's intensity 0.5 (its edges sway 10 to 35 cm). Where camera rays find the traced
            // sheet (the Coverage view, with the sheet traced against without) is where it is drawn (the Surfaces view, the drawn
            // sheet marked as transmission): the pixels that differ along its outline over the outline's length, in pixels, is how
            // far the traced edge lies from the drawn one. At two times, within a millimetre; traced still (no wind), far off.
            {
                const M4 hang=mul(translate_scale({-3.5f, 4.3f, 4.5f}, {1.5f, 1, 1.5f}), rot_x(-pi/2));
                const Camera sheetCam=make_camera({-3.5f, 4.3f, 1.5f}, {-3.5f, 4.3f, 4.5f}, 40.f, float(W)/float(H));
                const double mmPerPixel=2.0*3.0*std::tan(20.0*pi/180.0)/double(H)*1000.0;
                auto hanging=wind; hanging.intensity[0]=hanging.intensity[1]=hanging.intensity[2]=0.5f;
                std::vector<TGPT_ClothWind> hangingWinds; for(auto w:winds) { w.intensity[0]=w.intensity[1]=w.intensity[2]=0.5f; hangingWinds.push_back(w); }
                auto outline=[&](float time, bool windOn, std::vector<uint8_t>& drawnMask) {
                    MeshBuffers shape=posed(flat, hang, hanging, time);
                    Object o{0, hang, {0}}; o.drawn=&shape; o.feature=2; objects.push_back(o);
                    const auto px=leafPixels(sheetCam);
                    objects.back().feature=0;
                    drawnMask.assign(size_t(W)*H, 0); for(size_t j:px) drawnMask[j/3]=1;
                    auto coverage=[&](bool traced) {
                        auto list=instances; auto listIndices=indices;
                        if(traced) sheetInstance(list, listIndices, hang, sheetHandle, 46, true);
                        expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a hanging sheet");
                        expect(SetClothWind(time, hangingWinds.data(), windOn ? uint32_t(hangingWinds.size()) : 0u)==0, "the cloth's wind");
                        for(uint32_t i=0;i<6;++i) { run(TGPT_VIEW_COVERAGE, sheetCam, sheetCam, i); context->Flush(); Sleep(10); }
                        return readback();
                    };
                    const auto with=coverage(true), without=coverage(false);
                    objects.pop_back();
                    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                    size_t differ=0, edge=0, found=0;
                    for(UINT y=1;y+1<H;++y) for(UINT x=1;x+1<W;++x) {
                        const size_t i=size_t(y)*W+x;
                        const bool traced=std::fabs(with[i*3]-without[i*3])+std::fabs(with[i*3+1]-without[i*3+1])+std::fabs(with[i*3+2]-without[i*3+2])>1e-3f;
                        if(traced) ++found;
                        if(traced!=(drawnMask[i]!=0)) ++differ;
                        if(drawnMask[i] && (!drawnMask[i-1] || !drawnMask[i+1] || !drawnMask[i-W] || !drawnMask[i+W])) ++edge;
                    }
                    return std::make_tuple(edge ? double(differ)/double(edge) : 1e9, edge, found);
                };
                std::vector<uint8_t> drawnA, drawnB, drawnC;
                const auto [offsetA, edgeA, foundA]=outline(3.f, true, drawnA);
                const auto [offsetB, edgeB, foundB]=outline(7.5f, true, drawnB);
                const auto [offsetStill, edgeStill, foundStill]=outline(3.f, false, drawnC);
                size_t moved=0; for(size_t i=0;i<drawnA.size();++i) moved+=drawnA[i]!=drawnB[i];
                std::printf("      a swaying sheet's traced outline against its drawn one (%.1f mm a pixel): %.3f px (%.2f mm) at one time, %.3f px (%.2f mm) at "
                            "another, %.1f px (%.0f mm) traced still; its outline %zu and %zu pixels long, moved by %.1f px between the two times\n",
                            mmPerPixel, offsetA, offsetA*mmPerPixel, offsetB, offsetB*mmPerPixel, offsetStill, offsetStill*mmPerPixel, edgeA, edgeB,
                            edgeA ? double(moved)/double(edgeA) : 0.0);
                expect(edgeA>800 && edgeB>800 && foundA>20000 && foundB>20000, "the hanging sheet is drawn and traced");
                expect(offsetA*mmPerPixel<1.0 && offsetB*mmPerPixel<1.0, "a swaying cloth is traced in the pose HDRP draws it, at two times, within a millimetre");
                // (Measured: 8.2 px, 25 mm, between the two times; 0.001 and 0.002 px traced against drawn; 36.8 px traced still.)
                expect(edgeA && double(moved)/double(edgeA)>3.0, "the wind moved the sheet between the two times");
                expect(offsetStill*mmPerPixel>30.0, "traced still, the sheet lies centimetres from where it is drawn");
            }
            // 12. Its shadow: the hung sheet as a 2 by 2 m canopy 4 m up in the sun (where test 7's card stands), traced only,
            // solid, the wind's intensity 1 (it rises and falls 0.3 to 0.7 m). The floor's direct light with the canopy swayed by
            // the tracer at a time against the same canopy registered as a mesh already in that pose (posed on the CPU): the same
            // shadow, where the still canopy's and the one of another time fall elsewhere.
            {
                const M4 roof=translate_scale({-3.f, 5.f, 4.f}, {2, 1, 2});
                auto strong=wind; strong.intensity[0]=strong.intensity[1]=strong.intensity[2]=1.f;
                std::vector<TGPT_ClothWind> strongWinds; for(auto w:winds) { w.intensity[0]=w.intensity[1]=w.intensity[2]=1.f; strongWinds.push_back(w); }
                MeshBuffers frozen=posed(hung, roof, strong, 3.f);
                const int frozenHandle=register_mesh(frozen, 91);
                expect(frozenHandle>0, "the canopy posed on the CPU");
                auto floorLight=[&](int handle, float time, bool windOn) {
                    auto list=instances; auto listIndices=indices;
                    sheetInstance(list, listIndices, roof, handle, 47, false);
                    expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a canopy in the sun");
                    expect(SetClothWind(time, strongWinds.data(), windOn ? uint32_t(strongWinds.size()) : 0u)==0, "the canopy's wind");
                    for(uint32_t i=0;i<6;++i) { run(TGPT_VIEW_NONE, cam, cam, 9580+i, leafOptions); context->Flush(); Sleep(10); }
                    const auto a=accumulate(TGPT_VIEW_DIRECT, cam, 9600, 24);
                    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                    return a;
                };
                const auto swayed=floorLight(hungHandle, 3.f, true), reference=floorLight(frozenHandle, 3.f, false),
                           still=floorLight(hungHandle, 3.f, false), later=floorLight(hungHandle, 7.5f, true);
                double off=0, offStill=0, offLater=0; size_t n=0;
                for(size_t j=0;j+2<reference.size();j+=3) {
                    const double r=lum3(reference, j), s=lum3(still, j), l=lum3(later, j);
                    if(std::fabs(r-s)<0.01 && std::fabs(r-l)<0.01) continue; // where neither the still canopy's nor a later one's shadow differs
                    off+=std::fabs(lum3(swayed, j)-r); offStill+=std::fabs(s-r); offLater+=std::fabs(l-r); ++n;
                }
                std::printf("      a swaying canopy's shadow (%zu pixels where it moves): against the canopy posed on the CPU %.4f, the still canopy's %.4f, "
                            "a later pose's %.4f\n", n, n ? off/double(n) : 0.0, n ? offStill/double(n) : 0.0, n ? offLater/double(n) : 0.0);
                expect(n>300 && offStill>0 && off<0.1*offStill && off<0.1*offLater, "a swaying cloth's shadow falls where its pose casts it, and moves with it");
            }
            // 13. Seen from below under the open sky (PT-39, the owner: "when you looked up against the cloth I would have expected
            // to see some red hues and subsurface scattering as the light transmits through it"): the same canopy, red thin cloth,
            // drawn facing down in its swayed pose (it rises 0.3 to 0.7 m above where it hangs still), seen from 3 m below, the
            // sun and the lamp off. Its pixels gather the sky's light from behind it as cloth (against the same canopy as a solid
            // red sheet), about 0.3 of what a surface facing up there gets from the sky; traced still (the PT-39 case) the pixels
            // miss it. In the picture that light is red.
            {
                const M4 roofDown=mul(translate_scale({-3.f, 3.f, 4.f}, {2, 1, 2}), rot_x(pi)), roofUp=translate_scale({-3.f, 4.5f, 4.f}, {2, 1, 2});
                const Camera above=make_camera({-3.f, 5.9f, 1.2f}, {-3.f, 4.5f, 4.f}, 60.f, float(W)/float(H));
                const Camera low=make_camera({-3.f, 1.6f, 2.2f}, {-3.f, 4.5f, 4.2f}, 60.f, float(W)/float(H));
                auto strong=wind; strong.intensity[0]=strong.intensity[1]=strong.intensity[2]=1.f;
                std::vector<TGPT_ClothWind> strongWinds; for(auto w:winds) { w.intensity[0]=w.intensity[1]=w.intensity[2]=1.f; strongWinds.push_back(w); }
                MeshBuffers shape=posed(hung, roofDown, strong, 3.f);
                sunOn=false; expect(SetLights(nullptr, 0)==0, "the sky alone");
                std::vector<size_t> below;
                auto underside=[&](uint32_t material, bool windOn, uint32_t view) {
                    auto list=instances; auto listIndices=indices;
                    sheetInstance(list, listIndices, roofDown, hungHandle, material, true);
                    expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a red canopy under the sky");
                    expect(SetClothWind(3.f, strongWinds.data(), windOn ? uint32_t(strongWinds.size()) : 0u)==0, "the canopy's wind");
                    Object o{0, roofDown, {1}}; o.drawn=&shape; objects.push_back(o);
                    if(below.empty()) { objects.back().feature=2; below=leafPixels(low); objects.back().feature=0; }
                    const auto a=accumulate(view, low, 9800, 24);
                    objects.pop_back();
                    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                    return a;
                };
                const auto cloth=underside(48, true, TGPT_VIEW_INDIRECT_RAW), solid=underside(49, true, TGPT_VIEW_INDIRECT_RAW),
                           clothStill=underside(48, false, TGPT_VIEW_INDIRECT_RAW), solidStill=underside(49, false, TGPT_VIEW_INDIRECT_RAW),
                           clothSeen=underside(48, true, TGPT_VIEW_NONE), solidSeen=underside(49, true, TGPT_VIEW_NONE);
                std::vector<size_t> fromAbove;
                {
                    Object o{0, roofUp, {0}}; objects.push_back(o);
                    objects.back().feature=2; fromAbove=leafPixels(above); objects.back().feature=0;
                    const auto sky=accumulate(TGPT_VIEW_INDIRECT_RAW, above, 9800, 24);
                    objects.pop_back();
                    sunOn=true; expect(SetLights(&light, 1)==0, "the sun and the lamp again");
                    const double skyLight=mean(sky, fromAbove, -1);
                    const double behind=mean(cloth, below, -1)-mean(solid, below, -1), behindStill=mean(clothStill, below, -1)-mean(solidStill, below, -1);
                    const double red=mean(clothSeen, below, 0)-mean(solidSeen, below, 0), green=mean(clothSeen, below, 1)-mean(solidSeen, below, 1);
                    std::printf("      a red swaying canopy seen from below, the sky alone (%zu pixels): the light from behind it %.2f of the sky's above it "
                                "(0.30 expected), traced still %.2f; in the picture it adds %.4f red, %.4f green\n", below.size(),
                                skyLight>0 ? behind/skyLight : 0.0, skyLight>0 ? behindStill/skyLight : 0.0, red, green);
                    expect(below.size()>5000 && fromAbove.size()>1000 && skyLight>0, "the canopy seen from below and the sky's light above it");
                    expect(behind/skyLight>0.2 && behind/skyLight<0.42, "a swaying cloth's underside takes the sky's light through it, as thin cloth");
                    expect(behindStill<0.5*behind, "traced still, the swaying canopy's pixels missed it (PT-39's black underside)");
                    expect(red>0 && red>4.0*std::max(green, 0.0), "the light through a red cloth is red");
                }
            }
            expect(SetClothWind(0.f, nullptr, 0)==0, "no cloth wind");
        }
        // 14-15. Grass, leaves and trunks in the game's wind (PT-63, the owner: "of course our shadows and reflections should
        // move alongside the grass and leaves swaying in the wind. Otherwise what's the point of realtime ray tracing"). The
        // game's vegetation shaders move their vertices by the wind's speed and direction, the time and a noise map
        // (grass_wind_reference, leaves_wind_reference: from their compiled code); the tracer poses every such instance the
        // same way every frame, the instances of one 16 m cell of the world as one group with one BLAS (TGPT_SetPlantWind,
        // pt_sway steps 3 to 5). Five tufts of grass (three crossed cards a metre tall, turned and scaled each its own way,
        // one of them in the next cell: two groups) and a small tree (a trunk and three leaf cards, two materials) stand on
        // the floor in the sun, traced only.
        {
            expect(SetPlantWind!=nullptr && SetWindNoise!=nullptr, "the plant wind exports");
            auto rot_y=[&](float a) { M4 r=identity(); r.m[0][0]=std::cos(a); r.m[0][2]=std::sin(a); r.m[2][0]=-std::sin(a); r.m[2][2]=std::cos(a); return r; };
            // Cards standing up: width across, rows of two vertices from y0 to y1.
            auto cards=[&](std::vector<Vertex>& verts, std::vector<uint16_t>& list, int count, float turn, float width, float y0, float y1, int rows) {
                for(int c=0;c<count;++c) {
                    const float a=turn+float(c)*pi/float(count), dx=std::cos(a), dz=std::sin(a);
                    const uint16_t base=uint16_t(verts.size());
                    for(int r=0;r<=rows;++r) {
                        const float y=y0+(y1-y0)*float(r)/float(rows);
                        verts.push_back({-0.5f*width*dx, y, -0.5f*width*dz, -dz, 0, dx, 0, float(r)/float(rows)});
                        verts.push_back({0.5f*width*dx, y, 0.5f*width*dz, -dz, 0, dx, 1, float(r)/float(rows)});
                    }
                    for(int r=0;r<rows;++r) {
                        const uint16_t q=uint16_t(base+r*2);
                        list.insert(list.end(), {q, uint16_t(q+2), uint16_t(q+1), uint16_t(q+1), uint16_t(q+2), uint16_t(q+3)});
                    }
                }
            };
            std::vector<Vertex> tuft, tree; std::vector<uint16_t> tuftIndices, trunkIndices, leafIndices;
            cards(tuft, tuftIndices, 3, 0.2f, 0.3f, 0.f, 1.f, 4);
            cards(tree, trunkIndices, 2, 0.f, 0.14f, 0.f, 1.7f, 4);
            cards(tree, leafIndices, 3, 0.5f, 1.0f, 1.1f, 2.0f, 3);
            MeshBuffers tuftMesh=make_mesh(tuft, {tuftIndices}), treeMesh=make_mesh(tree, {trunkIndices, leafIndices});
            const int tuftHandle=register_mesh(tuftMesh, 100), treeHandle=register_mesh(treeMesh, 101);
            expect(tuftHandle>0 && treeHandle>0, "a tuft of grass and a small tree");
            // The noise maps: smooth, 64 by 64, between 0.2 and 1 (grass reads key 7, leaves and the trunk key 9).
            PlantNoise gustNoise, maskNoise;
            for(PlantNoise* n:{&gustNoise, &maskNoise}) {
                n->width=n->height=64; n->texels.resize(64*64);
                const float fx=n==&gustNoise ? 0.31f : 0.19f, fy=n==&gustNoise ? 0.23f : 0.41f;
                for(int y=0;y<64;++y) for(int x=0;x<64;++x)
                    n->texels[size_t(y)*64+size_t(x)]=uint8_t(std::lround(255.f*(0.6f+0.4f*std::sin(float(x)*fx+1.3f)*std::cos(float(y)*fy+0.7f))));
            }
            expect(SetWindNoise(7, 64, 64, 0, gustNoise.texels.data())==0 && SetWindNoise(9, 64, 64, 0, maskNoise.texels.data())==0, "the winds' noise maps");
            // Materials: the plants' plain, as the plugin sends them (posed, they are not flagged as things the wind moves);
            // a plain one and one flagged so (as cloth past its copies still is, and as every plant was) for the comparisons.
            auto sways=materials[0]; sways.flags=TGPT_MATERIAL_SWAYS;
            auto plain=materials[0];
            expect(SetMaterials(50, &plain, 1)==0 && SetMaterials(51, &plain, 1)==0 && SetMaterials(52, &plain, 1)==0 && SetMaterials(53, &plain, 1)==0 &&
                   SetMaterials(54, &sways, 1)==0, "plant materials");
            TGPT_PlantWind grassWind{}; grassWind.material=50; grassWind.kind=TGPT_PLANT_GRASS; grassWind.noise=7; grassWind.strength=80.f;
            TGPT_PlantWind leafWind{}; leafWind.material=51; leafWind.kind=TGPT_PLANT_LEAVES; leafWind.noise=9; leafWind.flags=TGPT_PLANT_BEND;
            // The bend fades from the camera to 25 m along its view (the game's default is 150 m): the tree stands 9 m off, so
            // a third of its bend is gone, and a pose made without the camera would cast another shadow.
            leafWind.strength=0.9f; leafWind.bendHeight=1.f; leafWind.pivotOffsetY=-1.f; leafWind.fade[0]=0.f; leafWind.fade[1]=25.f;
            TGPT_PlantWind trunkWind=leafWind; trunkWind.material=52; trunkWind.bendHeight=2.f; trunkWind.pivotOffsetY=0.f;
            const std::vector<TGPT_PlantWind> plantWinds={grassWind, leafWind, trunkWind};
            const float windSpeed=30.f, windDirection=40.f;
            const V3 camForward{-cam.view.m[2][0], -cam.view.m[2][1], -cam.view.m[2][2]};
            struct Plant { M4 world; bool tree; };
            const std::vector<Plant> plants={
                {mul(translate_scale({-2.9f, 0, -0.35f}, {1, 1, 1}), rot_y(0.f)), false},
                {mul(translate_scale({-2.4f, 0, -0.25f}, {1.2f, 1.2f, 1.2f}), rot_y(0.65f)), false},
                {mul(translate_scale({-1.9f, 0, -0.45f}, {1, 1, 1}), rot_y(pi/2)), false},
                {mul(translate_scale({-1.4f, 0, -0.3f}, {0.8f, 0.8f, 0.8f}), rot_y(3.5f)), false},
                {mul(translate_scale({-2.6f, 0, 0.12f}, {1, 1, 1}), rot_y(1.1f)), false},      // across the cell's edge at z = 0: a group of its own
                {mul(translate_scale({-0.6f, 0, -0.3f}, {0.6f, 0.6f, 0.6f}), rot_y(0.4f)), true},
            };
            // Each plant as the game's shaders pose it at a time, as a mesh of its own.
            std::vector<MeshBuffers> frozenMeshes; std::vector<int> frozenHandles;
            double tipMoved=0;
            for(size_t i=0;i<plants.size();++i) {
                const M4& w=plants[i].world;
                const float a[9]={w.m[0][0], w.m[0][1], w.m[0][2], w.m[1][0], w.m[1][1], w.m[1][2], w.m[2][0], w.m[2][1], w.m[2][2]};
                const float det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
                const float inv[9]={(a[4]*a[8]-a[5]*a[7])/det, (a[2]*a[7]-a[1]*a[8])/det, (a[1]*a[5]-a[2]*a[4])/det,
                                    (a[5]*a[6]-a[3]*a[8])/det, (a[0]*a[8]-a[2]*a[6])/det, (a[2]*a[3]-a[0]*a[5])/det,
                                    (a[3]*a[7]-a[4]*a[6])/det, (a[1]*a[6]-a[0]*a[7])/det, (a[0]*a[4]-a[1]*a[3])/det};
                std::vector<Vertex> out=plants[i].tree ? tree : tuft;
                const size_t trunkVertices=2*2*5; // the tree's first submesh: two cards of five rows
                for(size_t v=0;v<out.size();++v) {
                    auto& q=out[v];
                    const V3 p{q.px, q.py, q.pz};
                    const V3 moved=!plants[i].tree ? grass_wind_reference(p, w, inv, grassWind.strength, 3.f, windSpeed, windDirection, gustNoise)
                        : leaves_wind_reference(p, w, inv, v<trunkVertices ? trunkWind : leafWind, 3.f, windSpeed, windDirection, cam.position, camForward, maskNoise);
                    const V3 from=world_point(w, p), to=world_point(w, moved);
                    tipMoved=std::max(tipMoved, double(std::sqrt(dot(to-from, to-from))));
                    q.px=moved.x; q.py=moved.y; q.pz=moved.z;
                }
                frozenMeshes.push_back(plants[i].tree ? make_mesh(out, {trunkIndices, leafIndices}) : make_mesh(out, {tuftIndices}));
            }
            for(size_t i=0;i<frozenMeshes.size();++i) { frozenHandles.push_back(register_mesh(frozenMeshes[i], 102+i)); expect(frozenHandles.back()>0, "a plant posed on the CPU"); }
            // which: 0 every plant, 1 the grass alone, 2 the tree alone. posed: the meshes as posed on the CPU (else the still
            // ones). grass, trunk, leaf: the materials. windOn: the tracer's wind.
            auto floorLight=[&](int which, bool posed, uint32_t grass, uint32_t trunk, uint32_t leaf, float time, bool windOn) {
                auto list=instances; auto listIndices=indices;
                for(size_t i=0;i<plants.size();++i) {
                    if((which==1 && plants[i].tree) || (which==2 && !plants[i].tree)) continue;
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=plants[i].world.m[r][k];
                    q.mesh=posed ? frozenHandles[i] : (plants[i].tree ? treeHandle : tuftHandle); q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                    if(plants[i].tree) { listIndices.push_back(trunk); listIndices.push_back(leaf); } else listIndices.push_back(grass);
                    list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with plants in the sun");
                TGPT_PlantWindFrame frame{}; frame.time=time; frame.speed=windSpeed; frame.direction=windDirection;
                frame.forward[0]=camForward.x; frame.forward[1]=camForward.y; frame.forward[2]=camForward.z;
                expect(SetPlantWind(&frame, plantWinds.data(), windOn ? uint32_t(plantWinds.size()) : 0u)==0, "the plants' wind");
                for(uint32_t i=0;i<6;++i) { run(TGPT_VIEW_NONE, cam, cam, 9900+i, leafOptions); context->Flush(); Sleep(10); }
                const auto a=accumulate(TGPT_VIEW_DIRECT, cam, 9920, 24);
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
                return a;
            };
            // 14. Their shadows: swayed by the tracer at a time, against the same plants registered as meshes already in the
            // game's pose (plain materials, no wind): the same shadows, where the still plants' and a later time's fall
            // elsewhere. The grass and the tree each on their own, so neither shader's maths hides behind the other's.
            expect(tipMoved>0.1, "the game's wind moves the plants' tips by decimetres");
            bool shadowsFollow=true;
            for(int which=1;which<=2;++which) {
                const auto swayed=floorLight(which, false, 50, 52, 51, 3.f, true), reference=floorLight(which, true, 53, 53, 53, 3.f, false),
                           still=floorLight(which, false, 53, 53, 53, 3.f, false), later=floorLight(which, false, 50, 52, 51, 7.5f, true);
                double off=0, offStill=0, offLater=0; size_t n=0;
                for(size_t j=0;j+2<reference.size();j+=3) {
                    const double r=lum3(reference, j), st=lum3(still, j), l=lum3(later, j);
                    if(std::fabs(r-st)<0.01 && std::fabs(r-l)<0.01) continue; // where neither the still plants' nor a later pose's shadow differs
                    off+=std::fabs(lum3(swayed, j)-r); offStill+=std::fabs(st-r); offLater+=std::fabs(l-r); ++n;
                }
                std::printf("      %s in the game's wind (tips move up to %.0f cm; %zu pixels where the shadows move): against the plants posed on the CPU "
                            "%.4f, the still plants' %.4f, a later pose's %.4f\n", which==1 ? "five tufts of grass" : "a tree (a trunk and leaves)", tipMoved*100.0, n,
                            n ? off/double(n) : 0.0, n ? offStill/double(n) : 0.0, n ? offLater/double(n) : 0.0);
                shadowsFollow&=n>150 && offStill>0 && offLater>0 && off<0.1*offStill && off<0.1*offLater;
            }
            expect(shadowsFollow, "grass, leaves and a trunk in the wind cast their shadows where the game's pose puts them, and the shadows move with them");
            // 15. Trusted from the ground up: the same posed plants with a material the wind moves and no wind given (how
            // every plant was traced until 2026-10-07: standing still, and passed by rays within 20 cm of where they start)
            // let the sun through at their feet; posed by the tracer they shadow the floor as the plain ones do.
            {
                const auto swayed=floorLight(0, false, 50, 52, 51, 3.f, true), reference=floorLight(0, true, 53, 53, 53, 3.f, false);
                const auto distrusted=floorLight(0, true, 54, 54, 54, 3.f, false);
                double off=0, offDistrusted=0; size_t n=0;
                for(size_t j=0;j+2<reference.size();j+=3) {
                    const double r=lum3(reference, j), d=lum3(distrusted, j);
                    if(std::fabs(r-d)<0.01) continue;
                    off+=std::fabs(lum3(swayed, j)-r); offDistrusted+=std::fabs(d-r); ++n;
                }
                std::printf("      the floor at the plants' feet (%zu pixels the sun reached past plants rays did not trust): the posed plants %.4f from the "
                            "plain ones, the distrusted ones %.4f\n", n, n ? off/double(n) : 0.0, n ? offDistrusted/double(n) : 0.0);
                expect(n>60 && off<0.2*offDistrusted, "plants in the wind's pose are met by every ray from where it starts: no light passes them at their feet");
            }
            // What it costs (printed for docs/agents/world.md, judged only for being done: other programs may share the GPU).
            // A field of 19,881 tufts half a metre apart behind the room (70 by 70 m: 25 cells, 477,144 triangles), each
            // turned its own way: standing still (one BLAS for all, as before), and in the wind (25 groups posed and
            // refitted every frame, the time moving on).
            {
                auto list=instances; auto listIndices=indices;
                const int side=141;
                for(int z=0;z<side;++z) for(int x=0;x<side;++x) {
                    const M4 w=mul(translate_scale({-35.f+float(x)*0.5f, 0, 20.f+float(z)*0.5f}, {1, 1, 1}), rot_y(float((x*37+z*91)%360)*pi/180.f));
                    TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=w.m[r][k];
                    q.mesh=tuftHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                    listIndices.push_back(50); list.push_back(q);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a field of grass behind it");
                auto cost=[&](bool windOn, char* stages, size_t size, TGPT_Status& after) {
                    for(uint32_t i=0;i<130;++i) {
                        TGPT_PlantWindFrame frame{}; frame.time=3.f+float(i)/60.f; frame.speed=windSpeed; frame.direction=windDirection;
                        frame.forward[0]=camForward.x; frame.forward[1]=camForward.y; frame.forward[2]=camForward.z;
                        SetPlantWind(&frame, plantWinds.data(), windOn ? uint32_t(plantWinds.size()) : 0u);
                        run(TGPT_VIEW_NONE, cam, cam, 9960+i, leafOptions); context->Flush(); Sleep(10);
                    }
                    if(GetStageTimes) GetStageTimes(stages, uint32_t(size));
                    ReadStatus(&after);
                };
                char stillStages[1024]{}, windStages[1024]{};
                TGPT_Status stillStatus{sizeof(TGPT_Status), TGPT_ABI}, windStatus{sizeof(TGPT_Status), TGPT_ABI};
                cost(false, stillStages, sizeof(stillStages), stillStatus);
                cost(true, windStages, sizeof(windStages), windStatus);
                auto scene=[](const char* stages) { const char* at=std::strstr(stages, "scene "); return at ? std::atof(at+6) : -1.0; };
                std::printf("      a field of %d tufts of grass (%d triangles): standing still %u instances traced, the scene's stage %.2f ms of the GPU, "
                            "%.2f ms of the render thread recording; in the wind %u instances traced (a group a cell), the scene's stage %.2f ms, %.2f ms recording\n",
                            side*side, side*side*24, stillStatus.instancesTraced, scene(stillStages), double(stillStatus.recordMs),
                            windStatus.instancesTraced, scene(windStages), double(windStatus.recordMs));
                expect(stillStatus.instancesTraced>uint32_t(side*side) && windStatus.instancesTraced<1000u && windStatus.instancesTraced>20u,
                       "a field of grass in the wind is traced as a group a cell of the world, every tuft of it");
                expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
            }
            TGPT_PlantWindFrame none{};
            expect(SetPlantWind(&none, nullptr, 0)==0, "no plant wind");
        }
        expect(SetLights(&light, 1)==0 && SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room as it was");
        ReadStatus(&status);
    }
    // The things the game moves (defined after the warm-up), in the room as it was.
    movingChecks();
    ReadStatus(&status);
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
