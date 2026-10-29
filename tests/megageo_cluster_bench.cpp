// SPDX-License-Identifier: MIT
// PT-109 part 2, step (a) (docs/agents/megageo-2.md): what RTX Mega Geometry's clusters stamped from templates REALLY
// cost on this driver, for one real plant mesh of the game, against the plain refitted structures the plants' groups
// have today (scene.cpp pose_plants: one bottom level a group of plants, PREFER_FAST_TRACE | ALLOW_UPDATE |
// ALLOW_COMPACTION, compacted once, refitted to every pose).
//
// The program, on its own device (no game, no tracer):
//   - reads a plant mesh of the game (a PLNT file: positions, each submesh's indices in the mesh's order; written from
//     the game's asset bundles by a read-only export script, docs/agents/megageo-2.md);
//   - re-orders each submesh's triangles in an order of space (--spatial 1: middle splits, 2: least surface area
//     splits, 0: the mesh's own order) and cuts runs of that order into clusters of at most --maxtris triangles and
//     --maxverts vertices; each cluster lists its own vertices (the cluster layout), a vertex two clusters share twice;
//   - makes groups of posed copies (members) as the plants' groups are: each member turned, scaled and placed in a 16 m
//     cell (--spread m of it), its vertices in the group's space, bent by a fixed wind-like sway that differs by member
//     and by frame; the same poses in the mesh's layout (plain) and in the cluster layout;
//   - plain: one bottom level a group (a geometry a member's submesh), built, compacted, refitted every frame;
//   - clusters: one template a cluster of the mesh, built once; every frame every member's clusters stamped from the
//     templates with its posed vertices and each group's bottom level built from its clusters (two calls a frame);
//     each stamped cluster carries a geometry index of its own (member * clusters + cluster), as the tracer's submesh
//     records would;
//   - (--direct 1) the clusters built from triangles each frame (no templates), for the comparison;
//   - reads back the REAL sizes (what the driver wrote) and the reserved ones, GPU times a frame, and traces the same
//     rays through both top levels, from a compute pass with ray queries and (--rt 1) from a ray generation shader
//     with TraceRay: time, and whether every ray hits the same triangle at the same distance.
// Build (no CMake target; the VS 2022 x64 environment): dxc -T cs_6_5 -E main -Vn g_main -Fh megageo_cluster_bench_cs.h
// megageo_cluster_bench.hlsl; dxc -T lib_6_5 -Vn g_rt -Fh megageo_cluster_bench_rt.h megageo_cluster_bench_rt.hlsl;
// cl /std:c++17 /O2 /EHsc /MT /I<those headers> /I<NVAPI> megageo_cluster_bench.cpp d3d12.lib dxgi.lib nvapi64.lib.
// NVAPI (github.com/NVIDIA/nvapi, R615, MIT) is read for its declarations; no sample code is copied.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <nvapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "megageo_cluster_bench_cs.h" // g_main: the compiled tests/megageo_cluster_bench.hlsl
#include "megageo_cluster_bench_rt.h" // g_rt: the compiled tests/megageo_cluster_bench_rt.hlsl

using Microsoft::WRL::ComPtr;

namespace {
void check(HRESULT hr, const char* what) {
    if(FAILED(hr)) { char b[256]; std::snprintf(b, sizeof(b), "%s failed: 0x%08X", what, unsigned(hr)); throw std::runtime_error(b); }
}
void nv(NvAPI_Status s, const char* what) {
    if(s!=NVAPI_OK) {
        NvAPI_ShortString text{}; NvAPI_GetErrorMessage(s, text);
        char b[320]; std::snprintf(b, sizeof(b), "%s failed: %s (%d)", what, text, int(s)); throw std::runtime_error(b);
    }
}
double mb(uint64_t bytes) { return double(bytes)/1048576.0; }
uint64_t align(uint64_t v, uint64_t a) { return (v+a-1)/a*a; }

// ---- The device and its one queue ----
struct Gpu {
    ComPtr<ID3D12Device5> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList4> list;
    ComPtr<ID3D12Fence> fence; uint64_t fenceValue=0; HANDLE event=nullptr;
    ComPtr<ID3D12QueryHeap> queries; uint32_t queryCount=256;
    double ticksPerMs=0;
};
struct Buf {
    ComPtr<ID3D12Resource> r; uint64_t size=0; void* mapped=nullptr; D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
    D3D12_GPU_VIRTUAL_ADDRESS va() const { return r->GetGPUVirtualAddress(); }
};
Buf make(Gpu& g, uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    Buf b; b.size=std::max<uint64_t>(size, 256);
    D3D12_HEAP_PROPERTIES hp{}; hp.Type=heap;
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=b.size; d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1;
    d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=flags;
    if(heap==D3D12_HEAP_TYPE_UPLOAD) state=D3D12_RESOURCE_STATE_GENERIC_READ;
    if(heap==D3D12_HEAP_TYPE_READBACK) state=D3D12_RESOURCE_STATE_COPY_DEST;
    check(g.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&b.r)), "CreateCommittedResource");
    b.state=state;
    if(heap!=D3D12_HEAP_TYPE_DEFAULT) check(b.r->Map(0, nullptr, &b.mapped), "Map");
    return b;
}
void to(Gpu& g, Buf& b, D3D12_RESOURCE_STATES s) {
    if(b.state==s) return;
    D3D12_RESOURCE_BARRIER r{}; r.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; r.Transition.pResource=b.r.Get();
    r.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; r.Transition.StateBefore=b.state; r.Transition.StateAfter=s;
    g.list->ResourceBarrier(1, &r); b.state=s;
}
void uav(Gpu& g) { D3D12_RESOURCE_BARRIER r{}; r.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; g.list->ResourceBarrier(1, &r); }
void submit(Gpu& g) {
    check(g.list->Close(), "Close");
    ID3D12CommandList* lists[]={g.list.Get()};
    g.queue->ExecuteCommandLists(1, lists);
    check(g.queue->Signal(g.fence.Get(), ++g.fenceValue), "Signal");
    if(g.fence->GetCompletedValue()<g.fenceValue) { check(g.fence->SetEventOnCompletion(g.fenceValue, g.event), "SetEventOnCompletion"); WaitForSingleObject(g.event, INFINITE); }
    check(g.device->GetDeviceRemovedReason(), "the device (removed?)");
    check(g.allocator->Reset(), "allocator Reset"); check(g.list->Reset(g.allocator.Get(), nullptr), "list Reset");
}
// A default-heap buffer holding `bytes` of data, left in `state`.
Buf upload(Gpu& g, const void* data, uint64_t bytes, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
    Buf staging=make(g, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    std::memcpy(staging.mapped, data, bytes);
    Buf b=make(g, bytes, D3D12_HEAP_TYPE_DEFAULT, flags, D3D12_RESOURCE_STATE_COPY_DEST);
    g.list->CopyBufferRegion(b.r.Get(), 0, staging.r.Get(), 0, bytes);
    to(g, b, state);
    submit(g);
    return b;
}
template<class T> std::vector<T> read(Gpu& g, Buf& b, uint64_t bytes) {
    Buf rb=make(g, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto before=b.state;
    to(g, b, D3D12_RESOURCE_STATE_COPY_SOURCE);
    g.list->CopyBufferRegion(rb.r.Get(), 0, b.r.Get(), 0, bytes);
    to(g, b, before);
    submit(g);
    std::vector<T> out(bytes/sizeof(T)); std::memcpy(out.data(), rb.mapped, bytes);
    return out;
}

// ---- The mesh and its clusters ----
struct Mesh {
    uint32_t vertices=0, triangles=0;
    std::vector<float> positions;                 // xyz a vertex, the mesh's own space
    std::vector<uint32_t> indices;                // every submesh's, one after the other
    std::vector<uint32_t> subFirstTri, subTris;   // per submesh
    float lo[3]{1e30f, 1e30f, 1e30f}, hi[3]{-1e30f, -1e30f, -1e30f};
};
Mesh load(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if(!f) throw std::runtime_error(std::string("cannot open ")+path);
    char magic[4]; uint32_t v=0, s=0; f.read(magic, 4); f.read(reinterpret_cast<char*>(&v), 4); f.read(reinterpret_cast<char*>(&s), 4);
    if(std::memcmp(magic, "PLNT", 4)!=0) throw std::runtime_error("not a PLNT file");
    Mesh m; m.vertices=v;
    std::vector<uint32_t> counts(s); f.read(reinterpret_cast<char*>(counts.data()), 4*s);
    m.positions.resize(size_t(v)*3); f.read(reinterpret_cast<char*>(m.positions.data()), 12ull*v);
    uint32_t first=0;
    for(uint32_t c:counts) { m.subFirstTri.push_back(first); m.subTris.push_back(c/3); first+=c/3; }
    m.triangles=first; m.indices.resize(size_t(first)*3);
    f.read(reinterpret_cast<char*>(m.indices.data()), 12ull*first);
    if(!f) throw std::runtime_error("short PLNT file");
    for(uint32_t i=0;i<v;++i) for(int k=0;k<3;++k) { m.lo[k]=std::min(m.lo[k], m.positions[i*3+k]); m.hi[k]=std::max(m.hi[k], m.positions[i*3+k]); }
    return m;
}
// A cluster: a run of consecutive triangles of one submesh (the mesh's own triangle order is kept, so a triangle's number
// in its submesh is the cluster's first plus PrimitiveIndex()), at most maxTris of them and maxVerts distinct vertices.
// Its vertices are its own: the cluster layout lists each cluster's distinct vertices one after the other (base, verts),
// `source` names the mesh vertex of each, and `local` holds the cluster's 8-bit indices into its own list. A vertex two
// clusters share is listed twice.
// (The first cut tried, a window of 256 consecutive vertex numbers into the mesh's own vertex order with no copy, fails on
// the game's trees: in beech_tree_02_LOD0 27 of 7,603 triangles have corners more than 256 vertex numbers apart, and a
// window of 64 triangles spans 177 numbers of which 54 are used. docs/agents/megageo-2.md.)
struct Cluster { uint32_t sub, firstTri, tris, base, verts, localAt; };
std::vector<Cluster> cut(const Mesh& m, uint32_t maxTris, uint32_t maxVerts, std::vector<uint8_t>& local, std::vector<uint32_t>& source) {
    std::vector<Cluster> out;
    std::vector<int32_t> slot(m.vertices, -1);
    for(uint32_t s=0;s<m.subTris.size();++s) {
        uint32_t t=m.subFirstTri[s]; const uint32_t end=t+m.subTris[s];
        while(t<end) {
            Cluster c{s, t, 0, uint32_t(source.size()), 0, uint32_t(local.size())};
            std::vector<uint32_t> mine;
            while(t+c.tris<end && c.tris<maxTris) {
                const uint32_t* tri=&m.indices[size_t(t+c.tris)*3];
                uint32_t fresh=0;
                for(int k=0;k<3;++k) if(slot[tri[k]]<0 && (k==0 || (tri[k]!=tri[0] && (k<2 || tri[k]!=tri[1])))) ++fresh;
                if(mine.size()+fresh>maxVerts) break;
                for(int k=0;k<3;++k) {
                    if(slot[tri[k]]<0) { slot[tri[k]]=int32_t(mine.size()); mine.push_back(tri[k]); }
                    local.push_back(uint8_t(slot[tri[k]]));
                }
                ++c.tris;
            }
            for(uint32_t v:mine) slot[v]=-1;
            c.verts=uint32_t(mine.size());
            source.insert(source.end(), mine.begin(), mine.end());
            out.push_back(c); t+=c.tris;
        }
    }
    return out;
}
// The mesh with each submesh's triangles in an order of space: split in two along the longest side of their centres'
// box, again and again, until a part holds at most maxTris (the left part a whole number of clusters), the parts one
// after the other. A cut of this order into runs gives clusters that lie together. order[new triangle] = old triangle.
// sah: the split is the one of least surface area times triangles over every axis and every whole number of clusters
// (as a ray tracing clusterizer chooses), not the middle of the longest side.
Mesh spatial(const Mesh& m, uint32_t maxTris, std::vector<uint32_t>& order, bool sah) {
    Mesh out=m; order.resize(m.triangles);
    std::vector<float> centre(size_t(m.triangles)*3), box(size_t(m.triangles)*6);
    for(uint32_t t=0;t<m.triangles;++t) for(int a=0;a<3;++a) {
        float sum=0, lo=1e30f, hi=-1e30f;
        for(int k=0;k<3;++k) { const float v=m.positions[size_t(m.indices[size_t(t)*3+k])*3+a]; sum+=v; lo=std::min(lo, v); hi=std::max(hi, v); }
        centre[size_t(t)*3+a]=sum/3.f; box[size_t(t)*6+a]=lo; box[size_t(t)*6+3+a]=hi;
    }
    auto area=[](const float* b) { const float x=b[3]-b[0], y=b[4]-b[1], z=b[5]-b[2]; return x*y+y*z+z*x; };
    auto grow=[&](float* b, uint32_t t) { for(int a=0;a<3;++a) { b[a]=std::min(b[a], box[size_t(t)*6+a]); b[3+a]=std::max(b[3+a], box[size_t(t)*6+3+a]); } };
    if(sah) {
        for(uint32_t s=0;s<m.subTris.size();++s) {
            uint32_t* ids=&order[m.subFirstTri[s]];
            for(uint32_t i=0;i<m.subTris[s];++i) ids[i]=m.subFirstTri[s]+i;
            struct Part { uint32_t first, count; };
            std::vector<Part> stack{{0, m.subTris[s]}};
            std::vector<float> suffix;
            while(!stack.empty()) {
                const Part p=stack.back(); stack.pop_back();
                if(p.count<=maxTris) continue;
                double best=1e300; int bestAxis=0; uint32_t bestLeft=maxTris;
                for(int axis=0;axis<3;++axis) {
                    std::sort(ids+p.first, ids+p.first+p.count, [&](uint32_t x, uint32_t y) { return centre[size_t(x)*3+axis]<centre[size_t(y)*3+axis]; });
                    suffix.assign(size_t(p.count+1)*6, 0.f);
                    float b[6]{1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
                    for(uint32_t i=p.count;i-->0;) { grow(b, ids[p.first+i]); std::copy(b, b+6, &suffix[size_t(i)*6]); }
                    float l[6]{1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
                    for(uint32_t i=0;i+1<p.count;++i) {
                        grow(l, ids[p.first+i]);
                        const uint32_t left=i+1;
                        if(left%maxTris!=0) continue;
                        const double cost=double(area(l))*left+double(area(&suffix[size_t(left)*6]))*(p.count-left);
                        if(cost<best) { best=cost; bestAxis=axis; bestLeft=left; }
                    }
                }
                std::sort(ids+p.first, ids+p.first+p.count, [&](uint32_t x, uint32_t y) { return centre[size_t(x)*3+bestAxis]<centre[size_t(y)*3+bestAxis]; });
                stack.push_back({p.first+bestLeft, p.count-bestLeft}); stack.push_back({p.first, bestLeft});
            }
        }
        for(uint32_t t=0;t<m.triangles;++t) for(int k=0;k<3;++k) out.indices[size_t(t)*3+k]=m.indices[size_t(order[t])*3+k];
        return out;
    }
    for(uint32_t s=0;s<m.subTris.size();++s) {
        uint32_t* ids=&order[m.subFirstTri[s]];
        for(uint32_t i=0;i<m.subTris[s];++i) ids[i]=m.subFirstTri[s]+i;
        struct Part { uint32_t first, count; };
        std::vector<Part> stack{{0, m.subTris[s]}};
        while(!stack.empty()) {
            const Part p=stack.back(); stack.pop_back();
            if(p.count<=maxTris) continue;
            float lo[3]{1e30f, 1e30f, 1e30f}, hi[3]{-1e30f, -1e30f, -1e30f};
            for(uint32_t i=p.first;i<p.first+p.count;++i) for(int a=0;a<3;++a) { lo[a]=std::min(lo[a], centre[size_t(ids[i])*3+a]); hi[a]=std::max(hi[a], centre[size_t(ids[i])*3+a]); }
            int axis=0; for(int a=1;a<3;++a) if(hi[a]-lo[a]>hi[axis]-lo[axis]) axis=a;
            uint32_t left=(p.count/2+maxTris/2)/maxTris*maxTris; left=std::clamp<uint32_t>(left, maxTris, p.count-1);
            std::nth_element(ids+p.first, ids+p.first+left, ids+p.first+p.count,
                             [&](uint32_t x, uint32_t y) { return centre[size_t(x)*3+axis]<centre[size_t(y)*3+axis]; });
            stack.push_back({p.first+left, p.count-left}); stack.push_back({p.first, left}); // the left part first
        }
    }
    for(uint32_t t=0;t<m.triangles;++t) for(int k=0;k<3;++k) out.indices[size_t(t)*3+k]=m.indices[size_t(order[t])*3+k];
    return out;
}

struct Options {
    const char* mesh=nullptr; uint32_t groups=32, members=16, frames=60, maxTris=128, maxVerts=256, poses=3, width=1920, height=1080, traces=20;
    bool direct=true, psoFlag=true, bounds=false, rt=true; uint32_t spatial=1, camera=0; float spread=14.f;
};

double median(std::vector<double> v) { if(v.empty()) return 0; std::sort(v.begin(), v.end()); return v[v.size()/2]; }
double least(const std::vector<double>& v) { return v.empty() ? 0 : *std::min_element(v.begin(), v.end()); }

} // namespace

int main(int argc, char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        const std::string a=argv[i];
        auto next=[&]() { if(i+1>=argc) throw std::runtime_error("missing value for "+a); return uint32_t(std::strtoul(argv[++i], nullptr, 10)); };
        if(a=="--groups") o.groups=next(); else if(a=="--members") o.members=next(); else if(a=="--frames") o.frames=next();
        else if(a=="--maxtris") o.maxTris=next(); else if(a=="--maxverts") o.maxVerts=next(); else if(a=="--traces") o.traces=next();
        else if(a=="--direct") o.direct=next()!=0; else if(a=="--pso-flag") o.psoFlag=next()!=0; else if(a=="--bounds") o.bounds=next()!=0; else if(a=="--spatial") o.spatial=next(); else if(a=="--rt") o.rt=next()!=0; else if(a=="--camera") o.camera=next(); else if(a=="--spread") o.spread=float(next());
        else o.mesh=argv[i];
    }
    if(!o.mesh) { std::printf("usage: megageo_cluster_bench <mesh.plnt> [--groups N] [--members N] [--frames N] [--maxtris N] [--maxverts N] [--direct 0|1] [--pso-flag 0|1] [--bounds 0|1]\n"); return 2; }
    try {
        const Mesh mesh=load(o.mesh);
        std::vector<uint8_t> local; std::vector<uint32_t> source;
        // The clusters are cut from the mesh with its triangles in an order of space (--spatial 1) or in its own order.
        std::vector<uint32_t> order(mesh.triangles); std::iota(order.begin(), order.end(), 0u);
        const Mesh cmesh=o.spatial ? spatial(mesh, o.maxTris, order, o.spatial==2) : mesh;
        const std::vector<Cluster> clusters=cut(cmesh, o.maxTris, o.maxVerts, local, source);
        const uint32_t C=uint32_t(clusters.size()), M=o.members, G=o.groups, V=mesh.vertices, T=mesh.triangles, S=uint32_t(mesh.subTris.size());
        const uint32_t VC=uint32_t(source.size()); // the cluster layout's vertices (shared ones listed once a cluster)
        uint32_t widest=0; for(const auto& c:clusters) widest=std::max(widest, c.verts);
        double boxSum=0;
        for(const auto& c:clusters) {
            float lo[3]{1e30f, 1e30f, 1e30f}, hi[3]{-1e30f, -1e30f, -1e30f};
            for(uint32_t j=c.base;j<c.base+c.verts;++j) for(int a=0;a<3;++a) { lo[a]=std::min(lo[a], mesh.positions[size_t(source[j])*3+a]); hi[a]=std::max(hi[a], mesh.positions[size_t(source[j])*3+a]); }
            boxSum+=std::sqrt(double(hi[0]-lo[0])*(hi[0]-lo[0])+double(hi[1]-lo[1])*(hi[1]-lo[1])+double(hi[2]-lo[2])*(hi[2]-lo[2]));
        }
        const double meshDiagonal=std::sqrt(double(mesh.hi[0]-mesh.lo[0])*(mesh.hi[0]-mesh.lo[0])+double(mesh.hi[1]-mesh.lo[1])*(mesh.hi[1]-mesh.lo[1])+double(mesh.hi[2]-mesh.lo[2])*(mesh.hi[2]-mesh.lo[2]));
        std::printf("mesh %s: %u vertices, %u triangles in %u submeshes, %.2f x %.2f x %.2f m\n", o.mesh, V, T, S,
                    mesh.hi[0]-mesh.lo[0], mesh.hi[1]-mesh.lo[1], mesh.hi[2]-mesh.lo[2]);
        std::printf("clusters (at most %u triangles and %u vertices, runs of the mesh's own triangle order): %u, %.1f triangles and %.1f vertices each (at most %u); "
                    "the cluster layout lists %u vertices for the mesh's %u (%+.1f %%)\n",
                    o.maxTris, o.maxVerts, C, double(T)/C, double(VC)/C, widest, VC, V, 100.0*(double(VC)/V-1.0));
        std::printf("   cut %s; a cluster's box: a diagonal of %.2f m on average, the mesh's %.2f m\n", o.spatial==2 ? "from an order of space (least surface area splits)" : o.spatial ? "from an order of space (middle splits)" : "from the mesh's own triangle order",
                    boxSum/C, meshDiagonal);
        std::printf("groups %u of %u members: %u plants, %.2f M triangles, %u stamped clusters a frame\n", G, M, G*M, double(G)*M*T/1e6, G*M*C);

        // ---- The device ----
        Gpu g;
        ComPtr<IDXGIFactory6> factory; check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
        ComPtr<IDXGIAdapter1> adapter;
        check(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)), "EnumAdapterByGpuPreference");
        DXGI_ADAPTER_DESC1 ad{}; adapter->GetDesc1(&ad);
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&g.device)), "D3D12CreateDevice");
        std::printf("adapter: %ls (vendor 0x%04X)\n", ad.Description, ad.VendorId);
        nv(NvAPI_Initialize(), "NvAPI_Initialize");
        NvU32 driver=0; NvAPI_ShortString branch{}; NvAPI_SYS_GetDriverAndBranchVersion(&driver, branch);
        NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAPS caps=NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_NONE;
        nv(NvAPI_D3D12_GetRaytracingCaps(g.device.Get(), NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS, &caps, sizeof(caps)), "GetRaytracingCaps");
        std::printf("driver %u.%02u (%s), cluster operations caps 0x%X\n", driver/100, driver%100, branch, unsigned(caps));
        if(!(caps&NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_STANDARD)) throw std::runtime_error("no cluster operations on this device");
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(g.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g.queue)), "CreateCommandQueue");
        check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.allocator)), "CreateCommandAllocator");
        check(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.allocator.Get(), nullptr, IID_PPV_ARGS(&g.list)), "CreateCommandList");
        check(g.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g.fence)), "CreateFence");
        g.event=CreateEventW(nullptr, FALSE, FALSE, nullptr);
        D3D12_QUERY_HEAP_DESC qhd{}; qhd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qhd.Count=g.queryCount;
        check(g.device->CreateQueryHeap(&qhd, IID_PPV_ARGS(&g.queries)), "CreateQueryHeap");
        uint64_t freq=0; check(g.queue->GetTimestampFrequency(&freq), "GetTimestampFrequency"); g.ticksPerMs=double(freq)/1000.0;
        Buf stampsRb=make(g, 8ull*g.queryCount, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        uint32_t stampAt=0;
        auto stamp=[&]() { g.list->EndQuery(g.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, stampAt); return stampAt++; };
        auto resolve=[&]() { if(stampAt) g.list->ResolveQueryData(g.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, stampAt, stampsRb.r.Get(), 0); };
        auto span=[&](uint32_t a, uint32_t b) { const auto* t=static_cast<const uint64_t*>(stampsRb.mapped); return double(t[b]-t[a])/g.ticksPerMs; };

        // ---- The groups: members turned, scaled and placed in their 16 m cell; poses bent by a sway ----
        const float height=std::max(0.5f, mesh.hi[1]-mesh.lo[1]);
        const uint32_t gridW=uint32_t(std::ceil(std::sqrt(double(G))));
        struct Member { float yaw, scale, x, z, phase, dirX, dirZ; };
        std::vector<Member> members(size_t(G)*M);
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> u01(0.f, 1.f);
        for(auto& mb_:members) {
            mb_.yaw=u01(rng)*6.2831853f; mb_.scale=0.8f+0.4f*u01(rng); mb_.x=8.f+o.spread*(u01(rng)-0.5f); mb_.z=8.f+o.spread*(u01(rng)-0.5f);
            mb_.phase=u01(rng)*6.2831853f; const float d=u01(rng)*6.2831853f; mb_.dirX=std::cos(d); mb_.dirZ=std::sin(d);
        }
        const uint64_t groupVertices=uint64_t(M)*V, groupBytes=groupVertices*12;
        const uint64_t clusterGroupVertices=uint64_t(M)*VC, clusterGroupBytes=clusterGroupVertices*12;
        auto pose=[&](uint32_t frame, std::vector<float>& out) {
            out.resize(size_t(G)*groupVertices*3);
            const float t=float(frame)*0.9f;
            for(uint32_t gi=0;gi<G;++gi) for(uint32_t mi=0;mi<M;++mi) {
                const Member& mm=members[size_t(gi)*M+mi];
                const float c=std::cos(mm.yaw), s=std::sin(mm.yaw);
                float* dst=&out[(size_t(gi)*groupVertices+size_t(mi)*V)*3];
                for(uint32_t v=0;v<V;++v) {
                    const float* p=&mesh.positions[size_t(v)*3];
                    const float h=std::max(0.f, (p[1]-mesh.lo[1])/height);
                    const float bend=0.05f*height*h*h*std::sin(t+mm.phase+0.5f*p[0]);
                    const float x=(c*p[0]-s*p[2])*mm.scale, z=(s*p[0]+c*p[2])*mm.scale, y=p[1]*mm.scale;
                    dst[v*3+0]=x+mm.x+bend*mm.dirX; dst[v*3+1]=y; dst[v*3+2]=z+mm.z+bend*mm.dirZ;
                }
            }
        };
        // The same poses in the cluster layout (each listed vertex its source vertex's posed position): what the pose pass
        // would write with one more table, a source vertex a listed vertex.
        std::vector<Buf> poses, clusterPoses;
        {
            std::vector<float> p, q;
            for(uint32_t k=0;k<o.poses;++k) {
                pose(k, p); poses.push_back(upload(g, p.data(), p.size()*4, D3D12_RESOURCE_STATE_COPY_SOURCE));
                q.resize(size_t(G)*clusterGroupVertices*3);
                for(uint32_t gm=0;gm<G*M;++gm) for(uint32_t j=0;j<VC;++j) for(int a=0;a<3;++a)
                    q[(size_t(gm)*VC+j)*3+a]=p[(size_t(gm)*V+source[j])*3+a];
                clusterPoses.push_back(upload(g, q.data(), q.size()*4, D3D12_RESOURCE_STATE_COPY_SOURCE));
            }
        }
        Buf live=make(g, uint64_t(G)*groupBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        Buf clusterLive=make(g, uint64_t(G)*clusterGroupBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        auto set_pose=[&](uint32_t k) {
            to(g, live, D3D12_RESOURCE_STATE_COPY_DEST); to(g, clusterLive, D3D12_RESOURCE_STATE_COPY_DEST);
            g.list->CopyBufferRegion(live.r.Get(), 0, poses[k%o.poses].r.Get(), 0, live.size);
            g.list->CopyBufferRegion(clusterLive.r.Get(), 0, clusterPoses[k%o.poses].r.Get(), 0, clusterLive.size);
            to(g, live, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); to(g, clusterLive, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };
        // A group's merged indices: member by member, submesh by submesh (as pt_sway step 4 writes them), the same for every group.
        std::vector<uint32_t> groupIndices; groupIndices.reserve(size_t(M)*T*3);
        for(uint32_t mi=0;mi<M;++mi) for(uint32_t i=0;i<T*3;++i) groupIndices.push_back(mesh.indices[i]+mi*V);
        Buf indices=upload(g, groupIndices.data(), groupIndices.size()*4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ================= Plain: one bottom level a group, built, compacted, refitted =================
        const auto plainFlags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE|
                              D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
        auto plain_geometry=[&](uint32_t gi, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geos) {
            geos.assign(size_t(M)*S, D3D12_RAYTRACING_GEOMETRY_DESC{});
            for(uint32_t mi=0;mi<M;++mi) for(uint32_t s=0;s<S;++s) {
                auto& d=geos[size_t(mi)*S+s];
                d.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; d.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; // leaves: tested, not opaque
                auto& t=d.Triangles;
                t.VertexBuffer.StartAddress=live.va()+uint64_t(gi)*groupBytes; t.VertexBuffer.StrideInBytes=12;
                t.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT; t.VertexCount=UINT(groupVertices);
                t.IndexFormat=DXGI_FORMAT_R32_UINT; t.IndexCount=mesh.subTris[s]*3;
                t.IndexBuffer=indices.va()+(uint64_t(mi)*T+mesh.subFirstTri[s])*12;
            }
        };
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos; plain_geometry(0, geos);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS pin{};
        pin.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL; pin.Flags=plainFlags; pin.NumDescs=UINT(geos.size());
        pin.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; pin.pGeometryDescs=geos.data();
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pinfo{}; g.device->GetRaytracingAccelerationStructurePrebuildInfo(&pin, &pinfo);
        const uint64_t plainSlot=align(pinfo.ResultDataMaxSizeInBytes, 256), plainScratchSlot=align(std::max(pinfo.ScratchDataSizeInBytes, pinfo.UpdateScratchDataSizeInBytes), 256);
        Buf plainFull=make(g, plainSlot*G, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        Buf plainScratch=make(g, plainScratchSlot*G, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Buf compactSizes=make(g, 8ull*G, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        set_pose(0);
        stampAt=0; const uint32_t pb0=stamp();
        for(uint32_t gi=0;gi<G;++gi) {
            plain_geometry(gi, geos);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC d{}; d.Inputs=pin; d.Inputs.pGeometryDescs=geos.data();
            d.DestAccelerationStructureData=plainFull.va()+gi*plainSlot; d.ScratchAccelerationStructureData=plainScratch.va()+gi*plainScratchSlot;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC post{}; post.DestBuffer=compactSizes.va()+8ull*gi;
            post.InfoType=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
            g.list->BuildRaytracingAccelerationStructure(&d, 1, &post);
        }
        uav(g); const uint32_t pb1=stamp(); resolve(); submit(g);
        const double plainBuildMs=span(pb0, pb1);
        const std::vector<uint64_t> compacted=read<uint64_t>(g, compactSizes, 8ull*G);
        std::vector<uint64_t> compactAt(G); uint64_t compactTotal=0;
        for(uint32_t gi=0;gi<G;++gi) { compactAt[gi]=compactTotal; compactTotal+=align(compacted[gi], 256); }
        Buf plain=make(g, compactTotal, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        for(uint32_t gi=0;gi<G;++gi)
            g.list->CopyRaytracingAccelerationStructure(plain.va()+compactAt[gi], plainFull.va()+gi*plainSlot, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
        uav(g); submit(g);
        plainFull=Buf{};
        std::printf("\nPLAIN (one bottom level a group, %u geometries each): reserved before compaction %.2f MB a group (%.1f MB in all), compacted %.2f MB a group (%.1f MB in all, %.1f bytes a triangle); "
                    "scratch %.2f MB a group to build, %.2f MB to refit; built in %.3f ms\n",
                    M*S, mb(plainSlot), mb(plainSlot*G), mb(compactTotal/G), mb(compactTotal), double(compactTotal)/(double(G)*M*T),
                    mb(pinfo.ScratchDataSizeInBytes), mb(pinfo.UpdateScratchDataSizeInBytes), plainBuildMs);
        const uint64_t refitScratchSlot=align(pinfo.UpdateScratchDataSizeInBytes, 256);

        // ================= Clusters =================
        const uint32_t clusterCount=G*M*C;
        // The cluster templates of the mesh: local 8-bit indices, the mesh's own positions to shape them.
        Buf localBuf=upload(g, local.data(), local.size(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        std::vector<float> clusterRest(size_t(VC)*3);
        for(uint32_t j=0;j<VC;++j) for(int a=0;a<3;++a) clusterRest[size_t(j)*3+a]=mesh.positions[size_t(source[j])*3+a];
        Buf meshPositions=upload(g, clusterRest.data(), clusterRest.size()*4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // Bounds a template's stamps never leave (optional): the whole field, as a group's posed copy may lie anywhere in it.
        float boundsData[8]={-50.f, -50.f, -50.f, 50.f, 50.f, 50.f, 0, 0};
        boundsData[0]=-2.f*height; boundsData[1]=-2.f*height; boundsData[2]=-2.f*height; boundsData[3]=16.f+2.f*height; boundsData[4]=3.f*height; boundsData[5]=16.f+2.f*height;
        Buf boundsBuf=upload(g, boundsData, sizeof(boundsData), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const uint32_t maxGeometry=M*C-1;
        auto triangles_desc=[&](uint32_t args, uint64_t tris, uint64_t verts) {
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUT_TRIANGLES_DESC t{};
            t.vertexFormat=uint32_t(DXGI_FORMAT_R32G32B32_FLOAT); t.maxGeometryIndexValue=maxGeometry; t.maxUniqueGeometryCountPerArg=1;
            t.maxTriangleCountPerArg=o.maxTris; t.maxVertexCountPerArg=256; t.maxTotalTriangleCount=uint32_t(tris); t.maxTotalVertexCount=uint32_t(verts);
            t.minPositionTruncateBitCount=0; (void)args;
            return t;
        };
        auto requirements=[&](const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& in) {
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO info{};
            NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS p{};
            p.version=NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER; p.pInput=&in; p.pInfo=&info;
            nv(NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo(g.device.Get(), &p), "cluster requirements");
            return info;
        };
        auto execute=[&](const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC& d, const char* what) {
            NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS p{};
            p.version=NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER; p.pDesc=&d;
            nv(NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(g.list.Get(), &p), what);
        };
        const uint64_t meshSpanSum=VC;

        // 1. Templates, one a cluster, built once (implicit destinations: the driver packs them and says where and how large).
        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS tin{};
        tin.maxArgCount=C; tin.flags=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
        tin.type=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES;
        tin.mode=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
        tin.trianglesDesc=triangles_desc(C, T, meshSpanSum);
        const auto tinfo=requirements(tin);
        std::vector<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS> targs(C);
        for(uint32_t c=0;c<C;++c) {
            const Cluster& k=clusters[c]; auto& a=targs[c]; a={};
            a.clusterId=c; a.clusterFlags=0; a.triangleCount=k.tris; a.vertexCount=k.verts; a.positionTruncateBitCount=0;
            a.indexFormat=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_8BIT; a.opacityMicromapIndexFormat=0;
            a.baseGeometryIndexAndFlags=c; // its own record (member * C + c once stamped); no flags: tested, as leaves are
            a.indexBufferStride=0; a.vertexBufferStride=12; a.geometryIndexAndFlagsBufferStride=0; a.opacityMicromapIndexBufferStride=0;
            a.indexBuffer=localBuf.va()+k.localAt; a.vertexBuffer=meshPositions.va()+uint64_t(k.base)*12;
            a.geometryIndexAndFlagsBuffer=0; a.opacityMicromapArray=0; a.opacityMicromapIndexBuffer=0;
            a.instantiationBoundingBoxLimit=o.bounds ? boundsBuf.va() : 0;
        }
        Buf targsBuf=upload(g, targs.data(), targs.size()*sizeof(targs[0]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Buf templates=make(g, tinfo.resultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        Buf templateVas=make(g, 8ull*C, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Buf templateSizes=make(g, 4ull*C, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        uint64_t scratchBytes=std::max<uint64_t>(tinfo.scratchDataSizeInBytes, 256);
        Buf scratch=make(g, scratchBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        {
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC d{};
            d.inputs=tin; d.batchResultData=templates.va(); d.batchScratchData=scratch.va();
            d.destinationAddressArray={templateVas.va(), 8}; d.resultSizeArray={templateSizes.va(), 4};
            d.indirectArgArray={targsBuf.va(), sizeof(targs[0])}; d.indirectArgCount=0;
            stampAt=0; const uint32_t a=stamp();
            execute(d, "build the cluster templates");
            uav(g); const uint32_t b=stamp(); resolve(); submit(g);
            const auto sizes=read<uint32_t>(g, templateSizes, 4ull*C);
            const uint64_t sum=std::accumulate(sizes.begin(), sizes.end(), uint64_t(0));
            std::printf("\nCLUSTERS: %u templates built in %.3f ms: %.3f MB in all (%.0f bytes each; the driver asked for %.3f MB, scratch %.3f MB)\n",
                        C, span(a, b), mb(sum), double(sum)/C, mb(tinfo.resultDataMaxSizeInBytes), mb(tinfo.scratchDataSizeInBytes));
        }
        const std::vector<uint64_t> templateAt=read<uint64_t>(g, templateVas, 8ull*C);

        // 2. Each template's largest stamp, whatever the vertices (GET_SIZES with no vertices): the room each stamped cluster gets.
        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS iin{};
        iin.flags=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
        iin.type=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES;
        std::vector<uint32_t> roomOf(C);
        {
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS sin=iin;
            sin.maxArgCount=C; sin.mode=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_GET_SIZES;
            sin.trianglesDesc=triangles_desc(C, T, meshSpanSum);
            const auto sinfo=requirements(sin);
            std::vector<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS> sargs(C);
            for(uint32_t c=0;c<C;++c) { sargs[c]={}; sargs[c].clusterIdOffset=0; sargs[c].geometryIndexOffset=0; sargs[c].clusterTemplate=templateAt[c]; sargs[c].vertexBuffer={0, 12}; }
            Buf sargsBuf=upload(g, sargs.data(), sargs.size()*sizeof(sargs[0]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Buf sizes=make(g, 4ull*C, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if(sinfo.scratchDataSizeInBytes>scratchBytes) { scratchBytes=sinfo.scratchDataSizeInBytes; scratch=make(g, scratchBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); }
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC d{};
            d.inputs=sin; d.batchScratchData=scratch.va(); d.resultSizeArray={sizes.va(), 4}; d.indirectArgArray={sargsBuf.va(), sizeof(sargs[0])};
            execute(d, "the stamps' largest sizes"); uav(g); submit(g);
            roomOf=read<uint32_t>(g, sizes, 4ull*C);
        }
        std::vector<uint64_t> clasAt(clusterCount); uint64_t clasRoom=0;
        for(uint32_t gi=0;gi<G;++gi) for(uint32_t mi=0;mi<M;++mi) for(uint32_t c=0;c<C;++c) {
            clasAt[(size_t(gi)*M+mi)*C+c]=clasRoom; clasRoom+=align(roomOf[c], NVAPI_D3D12_RAYTRACING_CLAS_BYTE_ALIGNMENT);
        }
        Buf clas=make(g, clasRoom, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        std::vector<uint64_t> clasVa(clusterCount); for(size_t i=0;i<clusterCount;++i) clasVa[i]=clas.va()+clasAt[i];
        // 3. Every frame: every member's clusters stamped from the templates with its posed vertices (explicit destinations).
        iin.maxArgCount=clusterCount; iin.mode=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS;
        iin.trianglesDesc=triangles_desc(clusterCount, uint64_t(G)*M*T, uint64_t(G)*M*meshSpanSum);
        const auto iinfo=requirements(iin);
        std::vector<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS> iargs(clusterCount);
        for(uint32_t gi=0;gi<G;++gi) for(uint32_t mi=0;mi<M;++mi) for(uint32_t c=0;c<C;++c) {
            auto& a=iargs[(size_t(gi)*M+mi)*C+c]; a={};
            a.clusterIdOffset=mi*C; a.geometryIndexOffset=mi*C; a.clusterTemplate=templateAt[c];
            a.vertexBuffer={clusterLive.va()+uint64_t(gi)*clusterGroupBytes+(uint64_t(mi)*VC+clusters[c].base)*12, 12};
        }
        Buf iargsBuf=upload(g, iargs.data(), iargs.size()*sizeof(iargs[0]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Buf clasDest=upload(g, clasVa.data(), clasVa.size()*8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        Buf clasSizes=make(g, 4ull*clusterCount, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // 4. Every frame: each group's bottom level from its clusters (explicit destinations, room from GET_SIZES).
        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS bin{};
        bin.maxArgCount=G; bin.flags=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
        bin.type=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS;
        bin.mode=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_GET_SIZES;
        bin.clasDesc.maxTotalClasCount=clusterCount; bin.clasDesc.maxClasCountPerArg=M*C;
        std::vector<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS> bargs(G);
        for(uint32_t gi=0;gi<G;++gi) { bargs[gi]={}; bargs[gi].clusterCount=M*C; bargs[gi].reserved=0; bargs[gi].clusterVAs=clasDest.va()+uint64_t(gi)*M*C*8; }
        Buf bargsBuf=upload(g, bargs.data(), bargs.size()*sizeof(bargs[0]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        std::vector<uint32_t> blasRoom;
        {
            const auto info=requirements(bin);
            if(info.scratchDataSizeInBytes>scratchBytes) { scratchBytes=info.scratchDataSizeInBytes; scratch=make(g, scratchBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); }
            Buf sizes=make(g, 4ull*G, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC d{};
            d.inputs=bin; d.batchScratchData=scratch.va(); d.resultSizeArray={sizes.va(), 4}; d.indirectArgArray={bargsBuf.va(), sizeof(bargs[0])};
            execute(d, "the cluster bottom levels' largest sizes"); uav(g); submit(g);
            blasRoom=read<uint32_t>(g, sizes, 4ull*G);
        }
        std::vector<uint64_t> blasVa(G); uint64_t blasTotal=0;
        for(uint32_t gi=0;gi<G;++gi) { blasVa[gi]=blasTotal; blasTotal+=align(blasRoom[gi], D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT); }
        Buf blas=make(g, blasTotal, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        for(auto& v:blasVa) v+=blas.va();
        Buf blasDest=upload(g, blasVa.data(), blasVa.size()*8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        Buf blasSizes=make(g, 4ull*G, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        bin.mode=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS;
        const auto binfo=requirements(bin);
        scratchBytes=std::max<uint64_t>({scratchBytes, iinfo.scratchDataSizeInBytes, binfo.scratchDataSizeInBytes});
        if(scratch.size<scratchBytes) scratch=make(g, scratchBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Buf scratch2=make(g, binfo.scratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto stamp_clusters=[&]() {
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC d{};
            d.inputs=iin; d.batchScratchData=scratch.va(); d.destinationAddressArray={clasDest.va(), 8}; d.resultSizeArray={clasSizes.va(), 4};
            d.indirectArgArray={iargsBuf.va(), sizeof(iargs[0])};
            execute(d, "stamp the clusters");
        };
        auto build_blas=[&]() {
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC d{};
            d.inputs=bin; d.batchScratchData=scratch2.va(); d.destinationAddressArray={blasDest.va(), 8}; d.resultSizeArray={blasSizes.va(), 4};
            d.indirectArgArray={bargsBuf.va(), sizeof(bargs[0])};
            execute(d, "build the bottom levels from clusters");
        };

        // 5. (For the comparison) the same clusters built from triangles each frame, no templates (implicit destinations).
        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS din{};
        din.maxArgCount=clusterCount; din.flags=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
        din.type=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES;
        din.mode=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
        din.trianglesDesc=triangles_desc(clusterCount, uint64_t(G)*M*T, uint64_t(G)*M*meshSpanSum);
        Buf dargsBuf, direct, directVas, directSizes, scratch3; NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO dinfo{};
        if(o.direct) {
            dinfo=requirements(din);
            std::vector<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS> dargs(clusterCount);
            for(uint32_t gi=0;gi<G;++gi) for(uint32_t mi=0;mi<M;++mi) for(uint32_t c=0;c<C;++c) {
                const Cluster& k=clusters[c]; auto& a=dargs[(size_t(gi)*M+mi)*C+c]; a={};
                a.clusterId=mi*C+c; a.triangleCount=k.tris; a.vertexCount=k.verts; a.positionTruncateBitCount=0;
                a.indexFormat=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_8BIT;
                a.baseGeometryIndexAndFlags=mi*C+c; a.vertexBufferStride=12;
                a.indexBuffer=localBuf.va()+k.localAt; a.vertexBuffer=clusterLive.va()+uint64_t(gi)*clusterGroupBytes+(uint64_t(mi)*VC+k.base)*12;
            }
            dargsBuf=upload(g, dargs.data(), dargs.size()*sizeof(dargs[0]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            direct=make(g, dinfo.resultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            directVas=make(g, 8ull*clusterCount, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            directSizes=make(g, 4ull*clusterCount, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scratch3=make(g, dinfo.scratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        // ================= Frames: the same poses for both =================
        std::vector<double> tRefit, tStamp, tBlas, tDirect;
        for(uint32_t f=0;f<o.frames;++f) {
            set_pose(f+1);
            uav(g);
            stampAt=0;
            const uint32_t r0=stamp();
            for(uint32_t gi=0;gi<G;++gi) {
                plain_geometry(gi, geos);
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC d{}; d.Inputs=pin; d.Inputs.pGeometryDescs=geos.data();
                d.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
                d.SourceAccelerationStructureData=d.DestAccelerationStructureData=plain.va()+compactAt[gi];
                d.ScratchAccelerationStructureData=plainScratch.va()+gi*refitScratchSlot;
                g.list->BuildRaytracingAccelerationStructure(&d, 0, nullptr);
            }
            uav(g); const uint32_t r1=stamp();
            stamp_clusters();
            uav(g); const uint32_t r2=stamp();
            build_blas();
            uav(g); const uint32_t r3=stamp();
            uint32_t r4=r3;
            if(o.direct) {
                NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC d{};
                d.inputs=din; d.batchResultData=direct.va(); d.batchScratchData=scratch3.va(); d.destinationAddressArray={directVas.va(), 8};
                d.resultSizeArray={directSizes.va(), 4}; d.indirectArgArray={dargsBuf.va(), sizeof(NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS)};
                execute(d, "build the clusters from triangles");
                uav(g); r4=stamp();
            }
            resolve(); submit(g);
            tRefit.push_back(span(r0, r1)); tStamp.push_back(span(r1, r2)); tBlas.push_back(span(r2, r3)); if(o.direct) tDirect.push_back(span(r3, r4));
        }
        const auto realClas=read<uint32_t>(g, clasSizes, 4ull*clusterCount);
        const auto realBlas=read<uint32_t>(g, blasSizes, 4ull*G);
        const uint64_t realClasSum=std::accumulate(realClas.begin(), realClas.end(), uint64_t(0));
        const uint64_t realBlasSum=std::accumulate(realBlas.begin(), realBlas.end(), uint64_t(0));
        const uint64_t roomSum=std::accumulate(roomOf.begin(), roomOf.end(), uint64_t(0));
        std::printf("stamped clusters: room (the driver's largest stamp of each template) %.1f MB in all (%.0f bytes a cluster, %.1f bytes a triangle); REAL %.1f MB (%.0f bytes a cluster, %.1f bytes a triangle)\n",
                    mb(clasRoom), double(clasRoom)/clusterCount, double(clasRoom)/(double(G)*M*T), mb(realClasSum), double(realClasSum)/clusterCount, double(realClasSum)/(double(G)*M*T));
        std::printf("   (a template's largest stamp, the mean %.0f bytes; the driver's bound for the whole call, implicit destinations: %.1f MB, explicit: %.0f bytes an object; scratch %.2f MB)\n",
                    double(roomSum)/C, mb(iinfo.resultDataMaxSizeInBytes*(iin.mode==NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS ? 1 : 0)),
                    double(iinfo.resultDataMaxSizeInBytes), mb(iinfo.scratchDataSizeInBytes));
        std::printf("bottom levels from clusters: room %.2f MB (%.1f KB a group), REAL %.2f MB (%.1f KB a group); scratch %.2f MB\n",
                    mb(blasTotal), double(blasTotal)/G/1024.0, mb(realBlasSum), double(realBlasSum)/G/1024.0, mb(binfo.scratchDataSizeInBytes));
        if(o.direct) {
            const auto realDirect=read<uint32_t>(g, directSizes, 4ull*clusterCount);
            const uint64_t s=std::accumulate(realDirect.begin(), realDirect.end(), uint64_t(0));
            std::printf("clusters built from triangles (no templates): REAL %.1f MB (%.1f bytes a triangle); the driver's bound for the call %.1f MB, scratch %.2f MB\n",
                        mb(s), double(s)/(double(G)*M*T), mb(dinfo.resultDataMaxSizeInBytes), mb(dinfo.scratchDataSizeInBytes));
        }
        std::printf("\nMEMORY of the structures (the posed copies are the same either way): plain compacted %.1f MB; clusters as reserved %.1f MB (stamps %.1f + bottom levels %.1f + templates once) [real use %.1f MB]\n",
                    mb(compactTotal), mb(clasRoom+blasTotal), mb(clasRoom), mb(blasTotal), mb(realClasSum+realBlasSum));
        std::printf("TIME a frame over %u frames (median / least): plain refit of %u groups %.3f / %.3f ms; clusters stamped %.3f / %.3f ms + bottom levels %.3f / %.3f ms = %.3f ms",
                    o.frames, G, median(tRefit), least(tRefit), median(tStamp), least(tStamp), median(tBlas), least(tBlas), median(tStamp)+median(tBlas));
        if(o.direct) std::printf("; clusters from triangles %.3f / %.3f ms (+ bottom levels)", median(tDirect), least(tDirect));
        std::printf("\n");

        // ================= Rays: the same through both =================
        auto tlas_of=[&](const std::vector<uint64_t>& blasAddress, Buf& out) {
            std::vector<D3D12_RAYTRACING_INSTANCE_DESC> inst(G);
            for(uint32_t gi=0;gi<G;++gi) {
                auto& d=inst[gi]; d={};
                d.Transform[0][0]=d.Transform[1][1]=d.Transform[2][2]=1.f;
                d.Transform[0][3]=float(gi%gridW)*16.f; d.Transform[2][3]=float(gi/gridW)*16.f;
                d.InstanceID=gi; d.InstanceMask=0xFF; d.AccelerationStructure=blasAddress[gi];
            }
            Buf instBuf=upload(g, inst.data(), inst.size()*sizeof(inst[0]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{}; in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE; in.NumDescs=G; in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; in.InstanceDescs=instBuf.va();
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{}; g.device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
            out=make(g, info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            Buf s=make(g, info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC d{}; d.Inputs=in; d.DestAccelerationStructureData=out.va(); d.ScratchAccelerationStructureData=s.va();
            g.list->BuildRaytracingAccelerationStructure(&d, 0, nullptr); uav(g); submit(g);
        };
        std::vector<uint64_t> plainAddress(G); for(uint32_t gi=0;gi<G;++gi) plainAddress[gi]=plain.va()+compactAt[gi];
        Buf plainTop, clusterTop, directTop; tlas_of(plainAddress, plainTop); tlas_of(blasVa, clusterTop);
        // (The clusters built from triangles, in bottom levels of their own, to time rays through them too.)
        Buf directBlas;
        if(o.direct) {
            std::vector<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS> dbargs(G);
            for(uint32_t gi=0;gi<G;++gi) { dbargs[gi]={}; dbargs[gi].clusterCount=M*C; dbargs[gi].clusterVAs=directVas.va()+uint64_t(gi)*M*C*8; }
            Buf dbargsBuf=upload(g, dbargs.data(), dbargs.size()*sizeof(dbargs[0]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            directBlas=make(g, blasTotal, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            std::vector<uint64_t> dva(G); for(uint32_t gi=0;gi<G;++gi) dva[gi]=directBlas.va()+(blasVa[gi]-blas.va());
            Buf dDest=upload(g, dva.data(), dva.size()*8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC d{};
            d.inputs=bin; d.batchScratchData=scratch2.va(); d.destinationAddressArray={dDest.va(), 8}; d.resultSizeArray={blasSizes.va(), 4};
            d.indirectArgArray={dbargsBuf.va(), sizeof(dbargs[0])};
            execute(d, "build the bottom levels from the clusters built from triangles"); uav(g); submit(g);
            tlas_of(dva, directTop);
        }

        if(o.psoFlag) {
            NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS p{}; p.version=NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;
            p.flags=NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT;
            nv(NvAPI_D3D12_SetCreatePipelineStateOptions(g.device.Get(), &p), "SetCreatePipelineStateOptions");
        }
        ComPtr<ID3D12RootSignature> root; check(g.device->CreateRootSignature(0, g_main, sizeof(g_main), IID_PPV_ARGS(&root)), "CreateRootSignature");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root.Get(); pd.CS={g_main, sizeof(g_main)};
        ComPtr<ID3D12PipelineState> pso; check(g.device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)), "CreateComputePipelineState");
        const uint32_t W=o.width, H=o.height, rays=W*H*2;
        Buf hitsPlain=make(g, 16ull*rays, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Buf hitsCluster=make(g, 16ull*rays, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Buf hitsDirect=make(g, 16ull*rays, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float field=float(gridW)*16.f;
        struct Cam { float pos[3]; uint32_t w; float fwd[3]; uint32_t h; float right[3]; uint32_t mode; float up[3]; uint32_t seed; } cam{};
        {
            // camera 0: from a corner, nearly level, across the field; 1: from above one side, looking down into it, so
            // that nearly every camera ray reaches the plants' layer.
            float p[3]={-4.f, 0.45f*height+1.5f, -4.f}, target[3]={field*0.6f, 0.3f*height, field*0.6f};
            if(o.camera==1) { p[0]=field*0.5f; p[1]=height+0.25f*field; p[2]=-0.1f*field; target[0]=field*0.5f; target[1]=0.f; target[2]=field*0.55f; }
            float f[3]={target[0]-p[0], target[1]-p[1], target[2]-p[2]}; const float fl=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]); for(float& x:f) x/=fl;
            float r[3]={f[2], 0.f, -f[0]}; const float rl=std::sqrt(r[0]*r[0]+r[2]*r[2]); r[0]/=rl; r[2]/=rl;
            const float u[3]={r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0]};
            std::memcpy(cam.pos, p, 12); std::memcpy(cam.fwd, f, 12); std::memcpy(cam.right, r, 12); std::memcpy(cam.up, u, 12);
            if(cam.up[1]<0) for(float& x:cam.up) x=-x;
            cam.w=W; cam.h=H; cam.seed=7;
        }
        auto trace=[&](Buf& top, Buf& out, uint32_t mode) {
            cam.mode=mode;
            g.list->SetComputeRootSignature(root.Get()); g.list->SetPipelineState(pso.Get());
            g.list->SetComputeRoot32BitConstants(0, 16, &cam, 0);
            g.list->SetComputeRootShaderResourceView(1, top.va()); g.list->SetComputeRootUnorderedAccessView(2, out.va());
            g.list->Dispatch((W+7)/8, (H+7)/8, 1);
            uav(g);
        };
        for(uint32_t mode=0;mode<2;++mode) {
            std::vector<double> tp, tc, td;
            for(uint32_t i=0;i<o.traces;++i) {
                stampAt=0;
                const uint32_t a=stamp(); trace(plainTop, hitsPlain, mode); const uint32_t b=stamp(); trace(clusterTop, hitsCluster, mode); const uint32_t c=stamp();
                trace(clusterTop, hitsCluster, mode); const uint32_t d=stamp(); trace(plainTop, hitsPlain, mode); const uint32_t e=stamp();
                uint32_t f=e, h=e;
                if(o.direct) { trace(directTop, hitsDirect, mode); f=stamp(); trace(directTop, hitsDirect, mode); h=stamp(); }
                resolve(); submit(g);
                tp.push_back(span(a, b)); tc.push_back(span(b, c)); tc.push_back(span(c, d)); tp.push_back(span(d, e));
                if(o.direct) { td.push_back(span(e, f)); td.push_back(span(f, h)); }
            }
            if(o.direct) std::printf("\n(rays through the clusters built from triangles, no templates: %.3f / %.3f ms, %+.1f %% against plain)", median(td), least(td), 100.0*(median(td)/median(tp)-1.0));
            const auto hp=read<uint32_t>(g, hitsPlain, 16ull*rays), hc=read<uint32_t>(g, hitsCluster, 16ull*rays);
            uint64_t hitsP=0, hitsC=0, same=0, sameT=0, both=0, firstHalf=0; double worstT=0;
            for(uint32_t r=0;r<rays;++r) {
                const uint32_t* p=&hp[size_t(r)*4]; const uint32_t* c=&hc[size_t(r)*4];
                const bool ph=p[0]!=0xFFFFFFFFu, ch=c[0]!=0xFFFFFFFFu;
                hitsP+=ph; hitsC+=ch;
                if(!ph || !ch) { if(ph==ch) { ++same; if(r<W*H) ++firstHalf; } continue; }
                ++both;
                // plain: geometry = member * S + submesh, primitive within the submesh; clusters: geometry = member * C + cluster, primitive within it
                const uint32_t pm=p[1]/S, ps=p[1]%S, cm=c[1]/C, cc=c[1]%C;
                const uint64_t ptri=uint64_t(p[0])*M*T+uint64_t(pm)*T+mesh.subFirstTri[ps]+p[2];
                const uint64_t ctri=cc<C && clusters[cc].firstTri+c[2]<T ? uint64_t(c[0])*M*T+uint64_t(cm)*T+order[clusters[cc].firstTri+c[2]] : ~0ull;
                float tp_, tc_; std::memcpy(&tp_, &p[3], 4); std::memcpy(&tc_, &c[3], 4);
                if(ptri==ctri) { ++same; if(r<W*H) ++firstHalf; }
                const double dt=std::fabs(double(tp_)-double(tc_))/std::max(1e-6, double(tp_));
                if(dt<1e-5) ++sameT; worstT=std::max(worstT, dt);
            }
            std::printf("\nRAYS (%s), %ux%u camera rays and one incoherent ray from each hit, %u rounds, the card's time (median / least): plain %.3f / %.3f ms, clusters %.3f / %.3f ms (%+.1f %% at the median)\n",
                        mode==0 ? "every triangle solid" : "every triangle a candidate committed in the loop", W, H, o.traces,
                        median(tp), least(tp), median(tc), least(tc), 100.0*(median(tc)/median(tp)-1.0));
            std::printf("   hits: plain %llu, clusters %llu of %u rays; the same triangle (or both missed) %llu (%.4f %%; camera rays %llu of %u); both hit %llu, the same distance to 1e-5 %llu, worst %.2e\n",
                        (unsigned long long)hitsP, (unsigned long long)hitsC, rays, (unsigned long long)same, 100.0*double(same)/rays, (unsigned long long)firstHalf, W*H,
                        (unsigned long long)both, (unsigned long long)sameT, worstT);
        }
        // ---- The same rays from a ray generation shader with TraceRay (the tracer's big passes trace so) ----
        if(o.rt) {
            ComPtr<ID3D12StateObject> so; ComPtr<ID3D12StateObjectProperties> props;
            D3D12_DXIL_LIBRARY_DESC lib{}; lib.DXILLibrary={g_rt, sizeof(g_rt)};
            D3D12_GLOBAL_ROOT_SIGNATURE grs{}; grs.pGlobalRootSignature=root.Get();
            D3D12_STATE_SUBOBJECT subs[2]={{D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &lib}, {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &grs}};
            D3D12_STATE_OBJECT_DESC sd{}; sd.Type=D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE; sd.NumSubobjects=2; sd.pSubobjects=subs;
            check(g.device->CreateStateObject(&sd, IID_PPV_ARGS(&so)), "CreateStateObject");
            check(so.As(&props), "state object properties");
            const uint32_t rec=D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT; // one record a table, each 64 bytes apart
            std::vector<uint8_t> table(rec*3, 0);
            const wchar_t* names[3]={L"RayGen", L"Miss", L"group"};
            for(int i=0;i<3;++i) { const void* id=props->GetShaderIdentifier(names[i]); if(!id) throw std::runtime_error("a shader identifier is missing"); std::memcpy(&table[size_t(i)*rec], id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES); }
            Buf tableBuf=upload(g, table.data(), table.size(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            auto trace_rt=[&](Buf& top, Buf& out, uint32_t mode) {
                cam.mode=mode;
                g.list->SetComputeRootSignature(root.Get()); g.list->SetPipelineState1(so.Get());
                g.list->SetComputeRoot32BitConstants(0, 16, &cam, 0);
                g.list->SetComputeRootShaderResourceView(1, top.va()); g.list->SetComputeRootUnorderedAccessView(2, out.va());
                D3D12_DISPATCH_RAYS_DESC d{};
                d.RayGenerationShaderRecord={tableBuf.va(), rec};
                d.MissShaderTable={tableBuf.va()+rec, rec, rec}; d.HitGroupTable={tableBuf.va()+2*rec, rec, rec};
                d.Width=W; d.Height=H; d.Depth=1;
                g.list->DispatchRays(&d);
                uav(g);
            };
            for(uint32_t mode=0;mode<2;++mode) {
                std::vector<double> tp, tc;
                for(uint32_t i=0;i<o.traces;++i) {
                    stampAt=0;
                    const uint32_t a=stamp(); trace_rt(plainTop, hitsPlain, mode); const uint32_t b=stamp(); trace_rt(clusterTop, hitsCluster, mode); const uint32_t c=stamp();
                    trace_rt(clusterTop, hitsCluster, mode); const uint32_t d=stamp(); trace_rt(plainTop, hitsPlain, mode); const uint32_t e=stamp();
                    resolve(); submit(g);
                    tp.push_back(span(a, b)); tc.push_back(span(b, c)); tc.push_back(span(c, d)); tp.push_back(span(d, e));
                }
                const auto hp=read<uint32_t>(g, hitsPlain, 16ull*rays), hc=read<uint32_t>(g, hitsCluster, 16ull*rays);
                uint64_t same=0;
                for(uint32_t r=0;r<rays;++r) {
                    const uint32_t* pp=&hp[size_t(r)*4]; const uint32_t* cc_=&hc[size_t(r)*4];
                    const bool ph=pp[0]!=0xFFFFFFFFu, ch=cc_[0]!=0xFFFFFFFFu;
                    if(!ph || !ch) { same+=ph==ch; continue; }
                    const uint32_t pm=pp[1]/S, ps=pp[1]%S, cm=cc_[1]/C, cc=cc_[1]%C;
                    const uint64_t ptri=uint64_t(pp[0])*M*T+uint64_t(pm)*T+mesh.subFirstTri[ps]+pp[2];
                    const uint64_t ctri=cc<C && clusters[cc].firstTri+cc_[2]<T ? uint64_t(cc_[0])*M*T+uint64_t(cm)*T+order[clusters[cc].firstTri+cc_[2]] : ~0ull;
                    same+=ptri==ctri;
                }
                uint64_t hitCount=0, cameraHits=0; for(uint32_t r=0;r<rays;++r) if(hp[size_t(r)*4]!=0xFFFFFFFFu) { ++hitCount; if(r<W*H) ++cameraHits; }
                std::printf("\nRAYS FROM A RAY GENERATION SHADER (TraceRay; %s): plain %.3f / %.3f ms, clusters %.3f / %.3f ms (%+.1f %% at the median); the same triangle %.4f %% of %u rays; "
                            "%.1f %% of camera rays and %.1f %% of all rays hit a plant\n",
                            mode==0 ? "every triangle solid" : "every triangle a candidate, accepted by the any-hit shader",
                            median(tp), least(tp), median(tc), least(tc), 100.0*(median(tc)/median(tp)-1.0), 100.0*double(same)/rays, rays,
                            100.0*double(cameraHits)/(W*H), 100.0*double(hitCount)/rays);
            }
        }
        std::printf("\ndone\n");
        return 0;
    } catch(const std::exception& e) {
        std::printf("FAILED: %s\n", e.what());
        return 1;
    }
}
